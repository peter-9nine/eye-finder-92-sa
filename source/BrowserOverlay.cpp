#include "pch.h"
#include "BrowserOverlay.h"
#include "Paths.h"

namespace {

BrowserOverlay* gOverlay = nullptr;

constexpr UINT kToggleKey = VK_F8;
constexpr DWORD kGameFadeMs = 1100;
constexpr DWORD kBrowserFadeInMs = 400;
constexpr DWORD kBrowserFadeOutMs = 350;

// El ordenador del cibercafé.
const CVector kCyberCafePosition = { 1170.4697f, -1655.1893f, 1014.3158f };
constexpr float kCyberCafeRadius = 3.0f;

uint32_t KeyboardModifiers() {
	uint32_t modifiers = 0;
	if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= ef::kModShift;
	if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= ef::kModControl;
	if (GetKeyState(VK_MENU) & 0x8000) modifiers |= ef::kModAlt;
	return modifiers;
}

bool PauseMenuActive() {
	return FrontEndMenuManager.m_bMenuActive;
}

}  // namespace

void BrowserOverlay::InstallWindowHook() {
	if (originalProc_ || !RsGlobal.ps || !RsGlobal.ps->window) return;

	gOverlay = this;
	window_ = RsGlobal.ps->window;
	originalProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BrowserOverlay::WindowProc)));
	Log(originalProc_ ? "Game window hooked. Press F8 in-game to open the browser." : "Could not hook the game window");
}

void BrowserOverlay::RemoveWindowHook() {
	if (!originalProc_) return;

	SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProc_));
	originalProc_ = nullptr;
	gOverlay = nullptr;
}

bool BrowserOverlay::UsesCursor() const {
	return (phase_ == Phase::Open || phase_ == Phase::FadingBrowserOut) && !PauseMenuActive();
}

LRESULT CALLBACK BrowserOverlay::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	BrowserOverlay& self = *gOverlay;

	// El menú de pausa es del juego: mientras esté abierto, no tocamos nada.
	if (!PauseMenuActive()) {
		if (message == WM_KEYDOWN && wParam == kToggleKey) {
			if (!(lParam & (1 << 30))) self.togglePending_ = true;
			if (self.phase_ != Phase::Closed) return 0;
		}

		if (self.phase_ != Phase::Closed) {
			switch (message) {
			case WM_KEYDOWN:
				// Escape abre el menú de pausa, como siempre.
				if (wParam == VK_ESCAPE) break;
				self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
				return 0;
			case WM_SYSKEYDOWN:
				if (wParam == VK_F4 || wParam == VK_TAB) break;  // Alt+F4 y Alt+Tab siguen funcionando.
				self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
				return 0;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				// El juego también lo recibe: si no, una tecla pulsada al abrir se quedaría atascada.
				if (wParam != VK_ESCAPE) self.ForwardKey(ef::EventType::KeyUp, wParam, lParam);
				break;
			case WM_CHAR:
			case WM_SYSCHAR:
				if (wParam != VK_ESCAPE) self.ForwardKey(ef::EventType::Char, wParam, lParam);
				return 0;
			default:
				break;
			}
		}
	}

	return CallWindowProcA(self.originalProc_, window, message, wParam, lParam);
}

void BrowserOverlay::ForwardKey(ef::EventType type, WPARAM wParam, LPARAM lParam) {
	if (phase_ != Phase::Open) return;

	ef::Event event{ type };
	event.a = static_cast<int32_t>(wParam);
	event.b = static_cast<int32_t>(lParam);
	event.modifiers = KeyboardModifiers() | buttons_;
	client_.Send(event);
}

bool BrowserOverlay::EnsureHelper() {
	const uint32_t width = static_cast<uint32_t>(RsGlobal.maximumWidth);
	const uint32_t height = static_cast<uint32_t>(RsGlobal.maximumHeight);

	if (client_.IsRunning() && !client_.HasExited() && client_.Width() == width && client_.Height() == height) return true;

	renderer_.Release();

	std::string error;
	const std::wstring helper = PluginDirectory() + L"\\browser\\EyeFinderBrowser.exe";
	const std::wstring url = FileUrl(PluginDirectory() + L"\\browser\\ui\\shell.html");
	if (!client_.Start(helper, url, SessionSavePath(), width, height, error)) {
		Log("Could not start the browser helper: " + error);
		return false;
	}

	Log("Browser helper started at " + std::to_string(width) + "x" + std::to_string(height));
	client_.Send({ ef::EventType::MailChanged, 0, 0, unreadMail_ });
	return true;
}

void BrowserOverlay::BeginOpening() {
	// Arrancamos el helper ya: el fundido le da tiempo a tener la página lista.
	if (!EnsureHelper()) {
		CHud::SetHelpMessage("EyeFinder: the browser could not start. See EyeFinder92SA.log.", true, false, false);
		return;
	}

	CPad* pad = CPad::GetPad(0);
	savedControls_ = pad->DisablePlayerControls;
	pad->DisablePlayerControls |= ~0x80;

	TheCamera.SetFadeColour(0, 0, 0);
	TheCamera.Fade(1.0f, 0);

	phase_ = Phase::FadingGameOut;
	phaseStart_ = GetTickCount();
}

void BrowserOverlay::FinishOpening() {
	CTheScripts::bDisplayHud = 0;
	CHud::bScriptDontDisplayRadar = 1;

	// El juego vuelve a verse detrás del navegador, listo para cuando se cierre.
	TheCamera.Fade(0.5f, 1);

	haveMouse_ = false;
	buttons_ = 0;
	phase_ = Phase::Open;
	phaseStart_ = GetTickCount();
	client_.Send({ ef::EventType::Show });

	statsStart_ = GetTickCount();
	statsPaintsStart_ = client_.PaintCount();
	statsUploads_ = statsDraws_ = 0;
	statsUploadMs_ = statsUploadMaxMs_ = 0.0;
}

void BrowserOverlay::BeginClosing() {
	phase_ = Phase::FadingBrowserOut;
	phaseStart_ = GetTickCount();
}

void BrowserOverlay::FinishClosing() {
	client_.Send({ ef::EventType::Hide });
	ReleaseGame();

	TheCamera.SetFadeColour(0, 0, 0);
	TheCamera.Fade(0.0f, 0);
	TheCamera.ProcessFade();
	TheCamera.Fade(0.6f, 1);
}

void BrowserOverlay::ReleaseGame() {
	phase_ = Phase::Closed;
	suspendedByMenu_ = false;

	CPad::GetPad(0)->DisablePlayerControls = savedControls_;
	CTheScripts::bDisplayHud = 1;
	CHud::bScriptDontDisplayRadar = 0;
}

float BrowserOverlay::Blackness() const {
	const float elapsed = static_cast<float>(GetTickCount() - phaseStart_);
	if (phase_ == Phase::Open) return std::clamp(1.0f - elapsed / kBrowserFadeInMs, 0.0f, 1.0f);
	if (phase_ == Phase::FadingBrowserOut) return std::clamp(elapsed / kBrowserFadeOutMs, 0.0f, 1.0f);
	return 0.0f;
}

// El menú de pausa cubre el navegador: las páginas callan y, al volver, todo sigue donde estaba.
void BrowserOverlay::SyncPauseMenu() {
	if (phase_ != Phase::Open && phase_ != Phase::FadingBrowserOut) return;

	const bool menu = PauseMenuActive();
	if (menu == suspendedByMenu_) return;
	suspendedByMenu_ = menu;

	if (menu) {
		client_.Send({ ef::EventType::Hide });
		return;
	}

	// En el menú se puede haber cambiado la resolución.
	if (!EnsureHelper()) {
		ReleaseGame();
		CHud::SetHelpMessage("EyeFinder: the browser could not start. See EyeFinder92SA.log.", true, false, false);
		return;
	}

	haveMouse_ = false;
	buttons_ = 0;
	client_.Send({ ef::EventType::Show });
}

void BrowserOverlay::CheckForCyberCafe() {
	CPed* player = FindPlayerPed(-1);
	if (!player) return;

	const CVector position = player->GetPosition();
	const float dx = position.x - kCyberCafePosition.x;
	const float dy = position.y - kCyberCafePosition.y;
	const float dz = position.z - kCyberCafePosition.z;
	if (dx * dx + dy * dy + dz * dz >= kCyberCafeRadius * kCyberCafeRadius) return;

	if (!CHud::HelpMessageDisplayed()) CHud::SetHelpMessage("Press ~h~TAB~s~ To Open Browser", true, false, false);

	CPad* pad = CPad::GetPad(0);
	if (pad->NewKeyState.tab && !pad->OldKeyState.tab) togglePending_ = true;
}

void BrowserOverlay::Update() {
	SyncPauseMenu();
	if (PauseMenuActive()) {
		togglePending_ = false;
		return;
	}

	if (phase_ == Phase::Closed) CheckForCyberCafe();

	const bool toggle = togglePending_;
	togglePending_ = false;

	switch (phase_) {
	case Phase::Closed:
		if (toggle) BeginOpening();
		return;

	case Phase::FadingGameOut:
		if (GetTickCount() - phaseStart_ >= kGameFadeMs) FinishOpening();
		return;

	case Phase::FadingBrowserOut:
		if (GetTickCount() - phaseStart_ >= kBrowserFadeOutMs) FinishClosing();
		return;

	case Phase::Open:
		break;
	}

	// Si el helper muere, el juego sigue: volvemos a él y avisamos.
	if (client_.HasExited()) {
		Log("The browser helper exited unexpectedly");
		client_.Stop();
		renderer_.Release();
		ReleaseGame();
		CHud::SetHelpMessage("EyeFinder: the browser stopped. See EyeFinder92SA.log.", true, false, false);
		return;
	}

	if (toggle || client_.TakeCloseRequest()) {
		BeginClosing();
		return;
	}

	if (GetForegroundWindow() == window_) UpdateMouse();
	LogStats();
}

void BrowserOverlay::UpdateMouse() {
	POINT point;
	RECT client;
	if (!GetCursorPos(&point) || !ScreenToClient(window_, &point) || !GetClientRect(window_, &client)) return;
	if (client.right <= 0 || client.bottom <= 0) return;

	// La ventana puede tener otro tamaño que el búfer del juego.
	const int x = std::clamp<int>(point.x * RsGlobal.maximumWidth / client.right, 0, RsGlobal.maximumWidth - 1);
	const int y = std::clamp<int>(point.y * RsGlobal.maximumHeight / client.bottom, 0, RsGlobal.maximumHeight - 1);
	const bool inside = point.x >= 0 && point.y >= 0 && point.x < client.right && point.y < client.bottom;

	uint32_t buttons = 0;
	if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= ef::kModLeftMouse;
	if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= ef::kModMiddleMouse;
	if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= ef::kModRightMouse;

	const uint32_t modifiers = KeyboardModifiers();

	if (!haveMouse_ || x != mouseX_ || y != mouseY_) {
		mouseX_ = x;
		mouseY_ = y;
		haveMouse_ = true;
		client_.Send({ ef::EventType::MouseMove, x, y, 0, 0, modifiers | buttons_ });
	}

	static constexpr struct { uint32_t flag; int32_t button; } kButtons[] = {
		{ ef::kModLeftMouse, 0 }, { ef::kModMiddleMouse, 1 }, { ef::kModRightMouse, 2 },
	};
	for (const auto& entry : kButtons) {
		const bool now = (buttons & entry.flag) != 0;
		const bool before = (buttons_ & entry.flag) != 0;
		if (now == before) continue;
		if (now && !inside) continue;  // Un clic fuera de la ventana no es nuestro.

		buttons_ = now ? (buttons_ | entry.flag) : (buttons_ & ~entry.flag);
		client_.Send({ now ? ef::EventType::MouseDown : ef::EventType::MouseUp, x, y, entry.button, 0, modifiers | buttons_ });
	}

	CPad* pad = CPad::GetPad(0);
	const int wheel = (pad->NewMouseControllerState.wheelUp ? 120 : 0) - (pad->NewMouseControllerState.wheelDown ? 120 : 0);
	if (wheel != 0) client_.Send({ ef::EventType::Wheel, x, y, 0, wheel, modifiers | buttons_ });
}

void BrowserOverlay::Draw() {
	if (phase_ != Phase::Open && phase_ != Phase::FadingBrowserOut) return;
	if (PauseMenuActive()) return;

	auto* device = reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
	if (!device) return;

	if (const uint8_t* pixels = client_.AcquireFrame()) {
		if (renderer_.Upload(device, pixels, client_.Width(), client_.Height(), client_.Stride())) {
			++statsUploads_;
			statsUploadMs_ += renderer_.LastUploadMs();
			statsUploadMaxMs_ = std::max(statsUploadMaxMs_, renderer_.LastUploadMs());
		}
	}

	++statsDraws_;
	renderer_.Draw(device, static_cast<float>(RsGlobal.maximumWidth), static_cast<float>(RsGlobal.maximumHeight),
		static_cast<float>(mouseX_), static_cast<float>(mouseY_), client_.Cursor(), Blackness());
}

void BrowserOverlay::LogStats() {
	const DWORD elapsed = GetTickCount() - statsStart_;
	if (elapsed < 5000) return;

	const double seconds = elapsed / 1000.0;
	const uint32_t paints = client_.PaintCount() - statsPaintsStart_;
	char line[256];
	snprintf(line, sizeof(line), "game %.1f fps | browser %.1f frames/s | uploaded %.1f/s | upload avg %.2f ms, max %.2f ms",
		statsDraws_ / seconds, paints / seconds, statsUploads_ / seconds,
		statsUploads_ ? statsUploadMs_ / statsUploads_ : 0.0, statsUploadMaxMs_);
	Log(line);

	statsStart_ = GetTickCount();
	statsPaintsStart_ = client_.PaintCount();
	statsUploads_ = statsDraws_ = 0;
	statsUploadMs_ = statsUploadMaxMs_ = 0.0;
}

void BrowserOverlay::Restart() {
	if (phase_ == Phase::Closed) return;
	client_.Send({ ef::EventType::Hide });
	ReleaseGame();
}

void BrowserOverlay::SetUnreadMail(int unread) {
	unreadMail_ = unread;
	if (client_.IsRunning()) client_.Send({ ef::EventType::MailChanged, 0, 0, unread });
}

void BrowserOverlay::OnDeviceLost() {
	renderer_.Release();
}

void BrowserOverlay::OnDeviceReset() {
	// La textura se recrea con el siguiente fotograma, así que pedimos uno.
	if (client_.IsRunning()) client_.Send({ ef::EventType::Invalidate });
}

void BrowserOverlay::Shutdown() {
	client_.Stop();
	renderer_.Release();
	RemoveWindowHook();
}
