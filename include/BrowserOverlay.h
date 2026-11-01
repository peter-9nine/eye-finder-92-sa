#pragma once
#include "pch.h"
#include "BrowserClient.h"
#include "Locations.h"
#include "OverlayRenderer.h"
#include "PadInput.h"

#include <chrono>
#include <functional>

// Connects the browser to the game: opening and closing, input, controls, pause menu and drawing.
class BrowserOverlay {
public:
	void InstallWindowHook();

	void SetPad(const PadInput& pad) { pad_ = &pad; }
	void SetLocations(std::vector<ComputerLocation> locations) { locations_ = std::move(locations); }
	void SetToggleKey(unsigned int key) { toggleKey_ = key; }
	// Takes (key, is a repeat) and returns true if the key must not reach anything else.
	void SetKeyFilter(std::function<bool(unsigned int, bool)> filter) { keyFilter_ = std::move(filter); }
	void RemoveWindowHook();

	void Update();
	void Draw();
	void Restart();
	void OnDeviceLost();
	void OnDeviceReset();
	void Shutdown();

	// The game must not recenter the cursor while the browser is using it.
	bool UsesCursor() const;
	bool IsOpen() const { return phase_ != Phase::Closed; }
	bool KeyboardOpen() const { return client_.KeyboardOpen(); }
	// The text about what the last session cost, once.
	bool TakeSessionReceipt(std::string& text);

	void SetUnreadMail(int unread);
	bool TakeMailOp(ef::MailOp& operation) { return client_.TakeMailOp(operation); }
	bool TakeSiteScript(std::string& name) { return client_.TakeSiteScript(name); }
	bool TakeValueOp(std::string& key, std::string& value) { return client_.TakeValueOp(key, value); }
	void PublishGameState(const ef::GameStateData& state) { client_.PublishGameState(state); }

private:
	// Closed -> game fades to black -> open -> browser fades to black -> closed.
	enum class Phase { Closed, FadingGameOut, Open, FadingBrowserOut };

	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

	void BeginOpening(const ComputerLocation& computer);
	void OpenFromDebugKey();
	void FinishOpening();
	void BeginClosing();
	void FinishClosing();
	void ReleaseGame();
	void SyncPauseMenu();
	void CheckForComputers();
	void SendSession();
	void UpdateSessionClock();
	void ChargeSession();
	bool EnsureHelper();
	void UpdateMouse();
	void UpdatePad();
	void UpdatePadKeyboard();
	void MoveCursorWithPad(float seconds);
	void ScrollWithPad(float seconds);
	void SendPadShortcut(int32_t key, uint32_t modifiers);
	void ForwardKey(ef::EventType type, WPARAM wParam, LPARAM lParam);
	void LogStats();
	float Blackness() const;

	BrowserClient client_;
	OverlayRenderer renderer_;

	HWND window_ = nullptr;
	WNDPROC originalProc_ = nullptr;
	unsigned int toggleKey_ = VK_F8;
	std::function<bool(unsigned int, bool)> keyFilter_;

	Phase phase_ = Phase::Closed;
	DWORD phaseStart_ = 0;
	bool togglePending_ = false;
	bool suspendedByMenu_ = false;
	unsigned short savedControls_ = 0;

	int mouseX_ = 0;
	int mouseY_ = 0;
	bool haveMouse_ = false;
	uint32_t buttons_ = 0;
	int unreadMail_ = 0;

	std::vector<ComputerLocation> locations_;
	const ComputerLocation* nearbyComputer_ = nullptr;
	std::string lastPrompt_;
	bool debugKeyPressed_ = false;
	uint32_t debugKeyModifiers_ = 0;
	std::string startProblem_;
	ef::SessionType sessionType_ = ef::SessionType::Normal;
	int sessionPrice_ = 0;
	DWORD sessionMs_ = 0;
	DWORD sessionLastTick_ = 0;
	int sessionSecondsSent_ = -1;
	std::string sessionReceipt_;

	const PadInput* pad_ = nullptr;
	uint32_t padIgnored_ = 0;  // Buttons already held on arrival: they don't count until released.
	std::chrono::steady_clock::time_point padLastUpdate_{};
	float padCursorCarryX_ = 0.0f;
	float padCursorCarryY_ = 0.0f;
	float padWheelCarryX_ = 0.0f;
	float padWheelCarryY_ = 0.0f;
	DWORD padRepeatAt_[2] = {};
	bool padKeyboardOpen_ = false;
	uint32_t padKeyboardHeld_ = 0;
	DWORD padKeyboardRepeatAt_[12] = {};

	// Prototype statistics.
	DWORD statsStart_ = 0;
	uint32_t statsPaintsStart_ = 0;
	uint32_t statsUploads_ = 0;
	uint32_t statsDraws_ = 0;
	double statsUploadMs_ = 0.0;
	double statsUploadMaxMs_ = 0.0;
};
