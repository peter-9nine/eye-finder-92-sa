#include "pch.h"
#include "BrowserOverlay.h"
#include "Paths.h"

namespace {

BrowserOverlay* gOverlay = nullptr;

constexpr UINT kToggleKey = VK_F8;

uint32_t KeyboardModifiers() {
	uint32_t modifiers = 0;
	if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= ef::kModShift;
	if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= ef::kModControl;
	if (GetKeyState(VK_MENU) & 0x8000) modifiers |= ef::kModAlt;
	return modifiers;
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

LRESULT CALLBACK BrowserOverlay::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	BrowserOverlay& self = *gOverlay;

	if (message == WM_KEYDOWN && wParam == kToggleKey) {
		if (!(lParam & (1 << 30))) self.togglePending_ = true;
		if (self.open_) return 0;
	}

	if (self.open_) {
		switch (message) {
		case WM_KEYDOWN:
			self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
			return 0;
		case WM_SYSKEYDOWN:
			if (wParam == VK_F4 || wParam == VK_TAB) break;  // Alt+F4 y Alt+Tab siguen funcionando.
			self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
			return 0;
		case WM_KEYUP:
		case WM_SYSKEYUP:
			// El juego también lo recibe: si no, una tecla pulsada al abrir se quedaría atascada.
			self.ForwardKey(ef::EventType::KeyUp, wParam, lParam);
			break;
		case WM_CHAR:
		case WM_SYSCHAR:
			self.ForwardKey(ef::EventType::Char, wParam, lParam);
			return 0;
		default:
			break;
		}
	}

	return CallWindowProcA(self.originalProc_, window, message, wParam, lParam);
}

void BrowserOverlay::ForwardKey(ef::EventType type, WPARAM wParam, LPARAM lParam) {
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
	const std::wstring url = FileUrl(PluginDirectory() + L"\\browser\\ui\\start.html");
	if (!client_.Start(helper, url, width, height, error)) {
		Log("Could not start the browser helper: " + error);
		return false;
	}

	Log("Browser helper started at " + std::to_string(width) + "x" + std::to_string(height));
	return true;
}

void BrowserOverlay::Open() {
	if (!EnsureHelper()) {
		CHud::SetHelpMessage("EyeFinder: the browser could not start. See EyeFinder92SA.log.", true, false, false);
		return;
	}

	CPad* pad = CPad::GetPad(0);
	savedControls_ = pad->DisablePlayerControls;
	pad->DisablePlayerControls |= ~0x80;
	CTheScripts::bDisplayHud = 0;
	CHud::bScriptDontDisplayRadar = 1;

	haveMouse_ = false;
	buttons_ = 0;
	open_ = true;
	client_.Send({ ef::EventType::Show });

	statsStart_ = GetTickCount();
	statsPaintsStart_ = client_.PaintCount();
	statsUploads_ = statsDraws_ = 0;
	statsUploadMs_ = statsUploadMaxMs_ = 0.0;
}

void BrowserOverlay::Close() {
	open_ = false;
	client_.Send({ ef::EventType::Hide });

	CPad* pad = CPad::GetPad(0);
	pad->DisablePlayerControls = savedControls_;
	CTheScripts::bDisplayHud = 1;
	CHud::bScriptDontDisplayRadar = 0;
}

void BrowserOverlay::Update() {
	if (togglePending_) {
		togglePending_ = false;
		if (open_) Close();
		else if (!FrontEndMenuManager.m_bMenuActive) Open();
	}

	if (!open_) return;

	// Si el helper muere, el juego sigue: cerramos y avisamos.
	if (client_.HasExited()) {
		Log("The browser helper exited unexpectedly");
		Close();
		client_.Stop();
		renderer_.Release();
		CHud::SetHelpMessage("EyeFinder: the browser stopped. See EyeFinder92SA.log.", true, false, false);
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
	if (!open_) return;

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
		static_cast<float>(mouseX_), static_cast<float>(mouseY_), client_.Cursor());
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
	if (open_) Close();
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
