#include "pch.h"
#include "BrowserOverlay.h"
#include "Paths.h"

#include <cmath>

namespace {

BrowserOverlay* gOverlay = nullptr;

constexpr DWORD kGameFadeMs = 1100;
constexpr DWORD kBrowserFadeInMs = 400;
constexpr DWORD kBrowserFadeOutMs = 350;

constexpr float kPadCursorScreensPerSecond = 1.2f;  // Screen heights, with the stick all the way over.
constexpr float kPadWheelPerSecond = 2400.0f;       // One wheel notch is 120.
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

std::string ComputerPrompt(const ComputerLocation& computer, bool padInHands) {
	const std::string press = std::string("Press ~h~") + (padInHands ? "L1" : "TAB") + "~s~ to ";
	switch (computer.type) {
	case ef::SessionType::Police:
		return press + "log in to the police network.";
	case ef::SessionType::DarkWeb:
		return press + "use this computer.";
	default:
		break;
	}
	if (computer.pricePerMinute <= 0) return press + "use the internet.";
	return press + "use the internet. $" + std::to_string(computer.pricePerMinute) + " per minute.";
}

// What Windows would send for that key. Chromium gets the physical code from here.
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

	// Update may not run again until the window is back in front, so this can't wait for it.
	if (message == WM_ACTIVATEAPP && !wParam) self.Suspend();

	// The pause menu belongs to the game: while it's open, touch nothing.
	if (!PauseMenuActive()) {
		if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && self.keyFilter_ &&
			self.keyFilter_(static_cast<unsigned int>(wParam), (lParam & (1 << 30)) != 0)) {
			return 0;
		}

		if (message == WM_KEYDOWN && wParam == self.toggleKey_) {
			if (!(lParam & (1 << 30))) {
				self.togglePending_ = true;
				self.debugKeyPressed_ = true;
				self.debugKeyModifiers_ = KeyboardModifiers();
			}
			if (self.phase_ != Phase::Closed) return 0;
		}

		if (self.phase_ != Phase::Closed) {
			switch (message) {
			case WM_KEYDOWN:
				// Escape opens the pause menu, as always.
				if (wParam == VK_ESCAPE) break;
				self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
				return 0;
			case WM_SYSKEYDOWN:
				if (wParam == VK_F4 || wParam == VK_TAB) break;  // Alt+F4 and Alt+Tab keep working.
				self.ForwardKey(ef::EventType::KeyDown, wParam, lParam);
				return 0;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				// The game gets it too: otherwise a key held on opening would stick.
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

	const std::wstring folder = PluginDirectory() + L"\\browser";
	const std::string problem = BrowserRuntimeProblem(folder);
	if (!problem.empty()) {
		Log("Can't start the browser: " + problem);
		startProblem_ = "EyeFinder needs CEF. Run Install CEF.bat in its mod folder.";
		return false;
	}

	std::string error;
	const std::wstring url = FileUrl(PluginDirectory() + L"\\system\\ui\\shell.html");
	if (!client_.Start(folder + L"\\EyeFinderBrowser.exe", url, SessionSavePath(), width, height, error)) {
		Log("Could not start the browser helper: " + error);
		startProblem_ = "EyeFinder: the browser could not start. See EyeFinder92SA.log.";
		return false;
	}

	Log("Browser helper started at " + std::to_string(width) + "x" + std::to_string(height));
	client_.Send({ ef::EventType::MailChanged, 0, 0, unreadMail_ });
	// A new helper, after a resolution change, must keep the session it replaces.
	SendSession();
	return true;
}

void BrowserOverlay::SendSession() {
	const int seconds = static_cast<int>(sessionMs_ / 1000);
	client_.Send({ ef::EventType::Session, 0, 0, static_cast<int32_t>(sessionType_), sessionPrice_ });
	client_.Send({ ef::EventType::SessionTime, 0, 0, seconds, sessionPrice_ > 0 ? SessionCost(seconds, sessionPrice_) : 0 });
	sessionSecondsSent_ = seconds;
}

// Only counts while the browser is really open: not during the fades, the pause menu or with another window in front.
void BrowserOverlay::UpdateSessionClock() {
	const DWORD now = GetTickCount();
	sessionMs_ += (std::min<DWORD>)(now - sessionLastTick_, 1000);
	sessionLastTick_ = now;

	const int seconds = static_cast<int>(sessionMs_ / 1000);
	if (seconds == sessionSecondsSent_ || sessionPrice_ <= 0) return;
	sessionSecondsSent_ = seconds;
	client_.Send({ ef::EventType::SessionTime, 0, 0, seconds, SessionCost(seconds, sessionPrice_) });
}

// The police site's C.R.A.S.H. password was tried. The stars come now, so the police are on the way
// while the player is still at the computer, and the browser closes a moment later.
void BrowserOverlay::RaiseAlarm() {
	constexpr int kAlarmStars = 3;
	constexpr DWORD kAlarmCloseMs = 5000;

	if (alarmCloseAt_ != 0) return;

	CWanted* wanted = FindPlayerWanted(-1);
	if (wanted && wanted->m_nWantedLevel < kAlarmStars) wanted->SetWantedLevel(kAlarmStars);

	alarmCloseAt_ = (std::max<DWORD>)(GetTickCount() + kAlarmCloseMs, 1);
	Log("C.R.A.S.H. password failed: 3 stars, and the browser closes in 5 seconds");
}

void BrowserOverlay::ChargeSession() {
	if (sessionPrice_ <= 0) return;

	const int seconds = static_cast<int>(sessionMs_ / 1000);
	const int cost = SessionCost(seconds, sessionPrice_);
	int& money = CWorld::Players[0].m_nMoney;
	// A player short of money pays what they have, instead of going into debt.
	const int paid = (std::min)(cost, (std::max)(money, 0));
	money -= paid;

	char text[128];
	snprintf(text, sizeof(text), "Internet session: %d:%02d. You paid $%d%s", seconds / 60, seconds % 60, paid, paid < cost ? ", all you had." : ".");
	sessionReceipt_ = text;
	Log(text);
}

bool BrowserOverlay::TakeSessionReceipt(std::string& text) {
	if (sessionReceipt_.empty()) return false;
	text = std::move(sessionReceipt_);
	sessionReceipt_.clear();
	return true;
}

// For testing: opens anywhere. Shift and Ctrl pick the police and dark web sessions, even at
// another kind of computer. Otherwise it's the computer here, or a normal paid session.
void BrowserOverlay::OpenFromDebugKey() {
	ComputerLocation debug;
	debug.name = "Debug";
	if (debugKeyModifiers_ & ef::kModShift) debug.type = ef::SessionType::Police;
	else if (debugKeyModifiers_ & ef::kModControl) debug.type = ef::SessionType::DarkWeb;
	else if (nearbyComputer_) {
		BeginOpening(*nearbyComputer_);
		return;
	}
	else debug.pricePerMinute = 10;

	Log("Opening a debug session");
	BeginOpening(debug);
}

void BrowserOverlay::BeginOpening(const ComputerLocation& computer) {
	sessionType_ = computer.type;
	sessionPrice_ = sessionType_ == ef::SessionType::Normal ? computer.pricePerMinute : 0;
	sessionMs_ = 0;

	// Start the helper now: the fade gives it time to get the page ready.
	if (!EnsureHelper()) {
		CHud::SetHelpMessage(startProblem_.c_str(), true, false, false);
		return;
	}
	SendSession();

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

	// The game is visible again behind the browser, ready for when it closes.
	TheCamera.Fade(0.5f, 1);

	haveMouse_ = false;
	buttons_ = 0;
	// A is sprint: running up to the computer must not click when it opens.
	padIgnored_ = pad_->Held();
	phase_ = Phase::Open;
	phaseStart_ = GetTickCount();
	sessionLastTick_ = phaseStart_;
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
	client_.Send({ ef::EventType::EndSession });
	client_.Send({ ef::EventType::Hide });
	ChargeSession();
	ReleaseGame();

	TheCamera.SetFadeColour(0, 0, 0);
	TheCamera.Fade(0.0f, 0);
	TheCamera.ProcessFade();
	TheCamera.Fade(0.6f, 1);
}

void BrowserOverlay::ReleaseGame() {
	phase_ = Phase::Closed;
	alarmCloseAt_ = 0;
	suspended_ = false;

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

void BrowserOverlay::Suspend() {
	if (suspended_ || (phase_ != Phase::Open && phase_ != Phase::FadingBrowserOut)) return;
	suspended_ = true;
	client_.Send({ ef::EventType::Hide });
}

// With the pause menu or another window in front, nobody is using the browser: pages stop and,
// on return, everything is where it was.
void BrowserOverlay::SyncPresence() {
	if (phase_ != Phase::Open && phase_ != Phase::FadingBrowserOut) return;

	const bool away = PauseMenuActive() || GetForegroundWindow() != window_;
	if (away == suspended_) return;

	if (away) {
		Suspend();
		return;
	}
	suspended_ = false;
	sessionLastTick_ = GetTickCount();

	// The resolution may have changed in the menu.
	if (!EnsureHelper()) {
		ReleaseGame();
		CHud::SetHelpMessage(startProblem_.c_str(), true, false, false);
		return;
	}

	haveMouse_ = false;
	buttons_ = 0;
	// The button that left the menu is still held, and must not click the page.
	padIgnored_ = pad_->Held();
	client_.Send({ ef::EventType::Show });
}

void BrowserOverlay::CheckForComputers() {
	nearbyComputer_ = nullptr;

	CPed* player = FindPlayerPed(-1);
	if (!player) return;

	const CVector position = player->GetPosition();
	nearbyComputer_ = FindComputer(locations_, position.x, position.y, position.z, player->m_nAreaCode);
	if (!nearbyComputer_) return;

	// Set again if the player changes between the pad and the keyboard, to name the right button.
	const std::string prompt = ComputerPrompt(*nearbyComputer_, pad_->InHands());
	if (!CHud::HelpMessageDisplayed() || prompt != lastPrompt_) {
		CHud::SetHelpMessage(prompt.c_str(), true, false, false);
		lastPrompt_ = prompt;
	}

	if (pad_->Pressed(kPadLeftShoulder)) togglePending_ = true;

	CPad* pad = CPad::GetPad(0);
	if (pad->NewKeyState.tab && !pad->OldKeyState.tab) togglePending_ = true;
}

void BrowserOverlay::Update() {
	SyncPresence();
	if (PauseMenuActive()) {
		togglePending_ = false;
		debugKeyPressed_ = false;
		return;
	}

	if (phase_ == Phase::Closed) CheckForComputers();

	const bool toggle = togglePending_;
	togglePending_ = false;
	const bool debugKey = debugKeyPressed_;
	debugKeyPressed_ = false;

	switch (phase_) {
	case Phase::Closed:
		if (debugKey) OpenFromDebugKey();
		else if (toggle && nearbyComputer_) BeginOpening(*nearbyComputer_);
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

	// If the helper dies, the game goes on: go back to it and tell the player.
	if (client_.HasExited()) {
		Log("The browser helper exited unexpectedly");
		client_.Stop();
		renderer_.Release();
		ReleaseGame();
		CHud::SetHelpMessage("EyeFinder: the browser stopped. See EyeFinder92SA.log.", true, false, false);
		return;
	}

	if (client_.TakeAlarm()) RaiseAlarm();
	const bool alarmExpired = alarmCloseAt_ != 0 && static_cast<int32_t>(GetTickCount() - alarmCloseAt_) >= 0;

	if (toggle || client_.TakeCloseRequest() || alarmExpired) {
		alarmCloseAt_ = 0;
		BeginClosing();
		return;
	}

	if (!suspended_) {
		// The pad goes first: it moves the Windows cursor, and UpdateMouse reads that as if it were the mouse.
		UpdatePad();
		UpdateMouse();
		UpdateSessionClock();
	}
	LogStats();
}

void BrowserOverlay::UpdateMouse() {
	POINT point;
	RECT client;
	if (!GetCursorPos(&point) || !ScreenToClient(window_, &point) || !GetClientRect(window_, &client)) return;
	if (client.right <= 0 || client.bottom <= 0) return;

	// The window can be a different size from the game's buffer.
	const int x = std::clamp<int>(point.x * RsGlobal.maximumWidth / client.right, 0, RsGlobal.maximumWidth - 1);
	const int y = std::clamp<int>(point.y * RsGlobal.maximumHeight / client.bottom, 0, RsGlobal.maximumHeight - 1);
	const bool inside = point.x >= 0 && point.y >= 0 && point.x < client.right && point.y < client.bottom;

	uint32_t buttons = 0;
	if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= ef::kModLeftMouse;
	if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= ef::kModMiddleMouse;
	if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= ef::kModRightMouse;
	// With the on-screen keyboard open, A presses its keys. The mouse still clicks.
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
		if (now && !inside) continue;  // A click outside the window isn't ours.

		buttons_ = now ? (buttons_ | entry.flag) : (buttons_ & ~entry.flag);
		// The helper only opens the on-screen keyboard if the click came from the pad.
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

	// Speed grows with the square: slow for aiming, and fast for crossing the screen.
	const float speed = std::hypot(stickX, stickY) * client.bottom * kPadCursorScreensPerSecond * seconds;
	padCursorCarryX_ += stickX * speed;
	padCursorCarryY_ -= stickY * speed;

	const int moveX = static_cast<int>(padCursorCarryX_);
	const int moveY = static_cast<int>(padCursorCarryY_);
	if (moveX == 0 && moveY == 0) return;
	padCursorCarryX_ -= moveX;
	padCursorCarryY_ -= moveY;

	// In a window, the cursor must not wander onto another program, where A doesn't reach.
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

	// To Chromium, a positive delta scrolls the page up or to the left.
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
	// After a stall, or the first time, the cursor must not jump.
	const float seconds = (std::min)(std::chrono::duration<float>(now - padLastUpdate_).count(), 0.1f);
	padLastUpdate_ = now;

	// The button that opens or closes the keyboard is still held: it must not press a key or click underneath.
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

	// These are the same shortcuts as the keyboard's, which the helper already handles.
	if (pressed(kPadB)) SendPadShortcut(VK_LEFT, ef::kModAlt);
	if (pressed(kPadY)) SendPadShortcut(VK_RIGHT, ef::kModAlt);
	if (pressed(kPadX)) SendPadShortcut(VK_F5, 0);
	if (pressed(kPadLeftShoulder)) SendPadShortcut(VK_TAB, ef::kModControl | ef::kModShift);
	if (pressed(kPadRightShoulder)) SendPadShortcut(VK_TAB, ef::kModControl);

	// The arrows are for dropdowns and pages, and repeat as on a keyboard.
	// Left and right belong to the pager.
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

	// The stick works like the D-pad, in the direction it's tilted most.
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
	client_.Send({ ef::EventType::EndSession });
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
	// The texture is recreated with the next frame, so ask for one.
	if (client_.IsRunning()) client_.Send({ ef::EventType::Invalidate });
}

void BrowserOverlay::Shutdown() {
	client_.Stop();
	renderer_.Release();
	RemoveWindowHook();
}
