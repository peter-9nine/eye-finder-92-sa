#include "pch.h"
#include "BrowserOverlay.h"
#include "Paths.h"

#include <cmath>

namespace {

BrowserOverlay* gOverlay = nullptr;

constexpr DWORD kGameFadeMs = 1100;
constexpr DWORD kBrowserFadeInMs = 400;
constexpr DWORD kBrowserFadeOutMs = 350;

// El ordenador del cibercafé.
const CVector kCyberCafePosition = { 1170.4697f, -1655.1893f, 1014.3158f };
constexpr float kCyberCafeRadius = 3.0f;

constexpr float kPadCursorScreensPerSecond = 1.2f;  // Altos de pantalla, con el stick a tope.
constexpr float kPadWheelPerSecond = 2400.0f;       // Una muesca de la rueda son 120.
constexpr DWORD kPadRepeatDelayMs = 400;
constexpr DWORD kPadRepeatIntervalMs = 70;

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

// Lo que enviaría Windows con esa tecla. Chromium saca de aquí el código físico.
int32_t PadKeyParam(int32_t key, bool repeat) {
	uint32_t param = 1u | (MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC) << 16);
	if (key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT) param |= 1u << 24;
	if (repeat) param |= 1u << 30;
	return static_cast<int32_t>(param);
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
		if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self.keyFilter_ &&
			self.keyFilter_(static_cast<unsigned int>(wParam), (lParam & (1 << 30)) != 0)) {
			return 0;
		}

		if (message == WM_KEYDOWN && wParam == self.toggleKey_) {
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
	// A es esprintar: si venía corriendo, no debe hacer clic al abrirse.
	padIgnored_ = pad_->Held();
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
	// El botón con el que se salió del menú sigue pulsado: no debe hacer clic en la página.
	padIgnored_ = pad_->Held();
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

	if (GetForegroundWindow() == window_) {
		// Primero el mando: mueve el cursor de Windows, y UpdateMouse lo lee como si fuera el ratón.
		UpdatePad();
		UpdateMouse();
	}
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
	// Con el teclado en pantalla abierto, A pulsa sus teclas. El ratón sigue haciendo clic.
	const bool mouseLeft = (buttons & ef::kModLeftMouse) != 0;
	if (!padKeyboardOpen_ && (pad_->Held() & ~padIgnored_ & kPadA)) buttons |= ef::kModLeftMouse;

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
		// El helper abre el teclado en pantalla solo si el clic viene del mando.
		const int32_t fromPad = now && entry.button == 0 && !mouseLeft ? 1 : 0;
		client_.Send({ now ? ef::EventType::MouseDown : ef::EventType::MouseUp, x, y, entry.button, fromPad, modifiers | buttons_ });
	}

	CPad* pad = CPad::GetPad(0);
	const int wheel = (pad->NewMouseControllerState.wheelUp ? 120 : 0) - (pad->NewMouseControllerState.wheelDown ? 120 : 0);
	if (wheel != 0) client_.Send({ ef::EventType::Wheel, x, y, 0, wheel, modifiers | buttons_ });
}

void BrowserOverlay::MoveCursorWithPad(float seconds) {
	const float stickX = pad_->LeftX();
	const float stickY = pad_->LeftY();
	if (stickX == 0.0f && stickY == 0.0f) {
		padCursorCarryX_ = padCursorCarryY_ = 0.0f;
		return;
	}

	POINT point;
	RECT client;
	if (!GetCursorPos(&point) || !ScreenToClient(window_, &point) || !GetClientRect(window_, &client)) return;
	if (client.right <= 0 || client.bottom <= 0) return;

	// La velocidad crece con el cuadrado: despacio para apuntar, y rápido para cruzar la pantalla.
	const float speed = std::hypot(stickX, stickY) * client.bottom * kPadCursorScreensPerSecond * seconds;
	padCursorCarryX_ += stickX * speed;
	padCursorCarryY_ -= stickY * speed;

	const int moveX = static_cast<int>(padCursorCarryX_);
	const int moveY = static_cast<int>(padCursorCarryY_);
	if (moveX == 0 && moveY == 0) return;
	padCursorCarryX_ -= moveX;
	padCursorCarryY_ -= moveY;

	// En una ventana, el cursor no debe irse a otro programa donde A no llega.
	point.x = std::clamp<LONG>(point.x + moveX, 0, client.right - 1);
	point.y = std::clamp<LONG>(point.y + moveY, 0, client.bottom - 1);
	if (ClientToScreen(window_, &point)) SetCursorPos(point.x, point.y);
}

void BrowserOverlay::ScrollWithPad(float seconds) {
	if (!haveMouse_) return;

	const float stickX = pad_->RightX();
	const float stickY = pad_->RightY();
	if (stickX == 0.0f && stickY == 0.0f) {
		padWheelCarryX_ = padWheelCarryY_ = 0.0f;
		return;
	}

	// Para Chromium, un delta positivo sube la página o la lleva a la izquierda.
	const float speed = std::hypot(stickX, stickY) * kPadWheelPerSecond * seconds;
	padWheelCarryX_ -= stickX * speed;
	padWheelCarryY_ += stickY * speed;

	const int wheelX = static_cast<int>(padWheelCarryX_);
	const int wheelY = static_cast<int>(padWheelCarryY_);
	if (wheelX == 0 && wheelY == 0) return;
	padWheelCarryX_ -= wheelX;
	padWheelCarryY_ -= wheelY;

	client_.Send({ ef::EventType::Wheel, mouseX_, mouseY_, wheelX, wheelY, KeyboardModifiers() | buttons_ });
}

void BrowserOverlay::SendPadShortcut(int32_t key, uint32_t modifiers) {
	client_.Send({ ef::EventType::KeyDown, 0, 0, key, PadKeyParam(key, false), modifiers | buttons_ });
	client_.Send({ ef::EventType::KeyUp, 0, 0, key, PadKeyParam(key, false), modifiers | buttons_ });
}

void BrowserOverlay::UpdatePad() {
	padIgnored_ &= pad_->Held();

	const auto now = std::chrono::steady_clock::now();
	// Tras un parón, o la primera vez, el cursor no debe dar un salto.
	const float seconds = (std::min)(std::chrono::duration<float>(now - padLastUpdate_).count(), 0.1f);
	padLastUpdate_ = now;

	// El botón que abre o cierra el teclado sigue pulsado: no debe pulsar una tecla ni hacer clic debajo.
	const bool keyboardOpen = client_.KeyboardOpen();
	if (keyboardOpen != padKeyboardOpen_) {
		padKeyboardOpen_ = keyboardOpen;
		padIgnored_ = pad_->Held();
		padKeyboardHeld_ = 0;
	}
	if (keyboardOpen) {
		UpdatePadKeyboard();
		return;
	}

	MoveCursorWithPad(seconds);
	ScrollWithPad(seconds);

	const auto pressed = [this](PadButton button) { return pad_->Pressed(button) && !(padIgnored_ & button); };

	// Son los mismos atajos que los del teclado, que ya maneja el helper.
	if (pressed(kPadB)) SendPadShortcut(VK_LEFT, ef::kModAlt);
	if (pressed(kPadY)) SendPadShortcut(VK_RIGHT, ef::kModAlt);
	if (pressed(kPadX)) SendPadShortcut(VK_F5, 0);
	if (pressed(kPadLeftShoulder)) SendPadShortcut(VK_TAB, ef::kModControl | ef::kModShift);
	if (pressed(kPadRightShoulder)) SendPadShortcut(VK_TAB, ef::kModControl);

	// Las flechas sirven para las listas desplegables y las páginas, y se repiten como en un teclado.
	// Izquierda y derecha son del busca.
	static constexpr struct { PadButton button; int32_t key; } kArrows[] = {
		{ kPadDPadUp, VK_UP }, { kPadDPadDown, VK_DOWN },
	};
	const DWORD tick = GetTickCount();
	for (size_t i = 0; i < std::size(kArrows); ++i) {
		const PadButton button = kArrows[i].button;
		const int32_t key = kArrows[i].key;

		if (pressed(button)) {
			client_.Send({ ef::EventType::KeyDown, 0, 0, key, PadKeyParam(key, false), buttons_ });
			padRepeatAt_[i] = tick + kPadRepeatDelayMs;
		}
		else if (pad_->Released(button)) {
			client_.Send({ ef::EventType::KeyUp, 0, 0, key, PadKeyParam(key, false), buttons_ });
		}
		else if (pad_->Held(button) && !(padIgnored_ & button) && static_cast<int32_t>(tick - padRepeatAt_[i]) >= 0) {
			client_.Send({ ef::EventType::KeyDown, 0, 0, key, PadKeyParam(key, true), buttons_ });
			padRepeatAt_[i] = tick + kPadRepeatIntervalMs;
		}
	}
}

void BrowserOverlay::UpdatePadKeyboard() {
	const auto held = [this](PadButton button) { return (pad_->Held() & ~padIgnored_ & button) != 0; };

	// El stick funciona como la cruceta, en la dirección donde esté más inclinado.
	const float stickX = pad_->LeftX();
	const float stickY = pad_->LeftY();
	const bool horizontal = std::abs(stickX) >= std::abs(stickY);
	constexpr float kStickThreshold = 0.5f;

	using Action = ef::PadKeyboardAction;
	const struct { bool down; Action action; bool repeats; } inputs[] = {
		{ held(kPadDPadUp) || (!horizontal && stickY > kStickThreshold), Action::Up, true },
		{ held(kPadDPadDown) || (!horizontal && stickY < -kStickThreshold), Action::Down, true },
		{ held(kPadDPadLeft) || (horizontal && stickX < -kStickThreshold), Action::Left, true },
		{ held(kPadDPadRight) || (horizontal && stickX > kStickThreshold), Action::Right, true },
		{ held(kPadA), Action::Press, false },
		{ held(kPadB), Action::Close, false },
		{ held(kPadX), Action::Backspace, true },
		{ held(kPadY), Action::Space, false },
		{ held(kPadLeftShoulder), Action::CaretLeft, true },
		{ held(kPadRightShoulder), Action::CaretRight, true },
		{ held(kPadLeftThumb), Action::Shift, false },
		{ held(kPadSelect), Action::Go, false },
	};
	static_assert(std::size(inputs) == std::size(padKeyboardRepeatAt_));

	const DWORD tick = GetTickCount();
	for (size_t i = 0; i < std::size(inputs); ++i) {
		const uint32_t bit = 1u << i;
		if (!inputs[i].down) {
			padKeyboardHeld_ &= ~bit;
			continue;
		}

		if (!(padKeyboardHeld_ & bit)) {
			padKeyboardHeld_ |= bit;
			padKeyboardRepeatAt_[i] = tick + kPadRepeatDelayMs;
		}
		else if (!inputs[i].repeats || static_cast<int32_t>(tick - padKeyboardRepeatAt_[i]) < 0) {
			continue;
		}
		else {
			padKeyboardRepeatAt_[i] = tick + kPadRepeatIntervalMs + 40;
		}
		client_.Send({ ef::EventType::PadKeyboard, 0, 0, static_cast<int32_t>(inputs[i].action) });
	}
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
