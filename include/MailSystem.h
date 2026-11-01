#pragma once
#include "pch.h"
#include "BrowserOverlay.h"
#include "KeyConfig.h"
#include "MailStore.h"
#include "PagerRenderer.h"

#include <deque>

// Receives what other mods send, keeps the current game's inbox and shows the notifications.
class MailSystem {
public:
	void Initialize(BrowserOverlay& overlay, const PadInput& pad);

	void Update();
	void Draw();

	// Saved games: the game has just started, saved or loaded one.
	void OnNewGame();
	void OnGameSaved();
	void OnGameLoaded();

private:
	struct PagerMessage {
		std::string sender;
		std::string text;
	};

	void HandleApiRequests();
	void HandleBrowserOperations();
	void RunCleoScript(const std::string& path);
	void SyncReplies();
	bool HandleKey(unsigned int key, bool repeat);
	void UpdatePad();
	void TogglePagerMemory();
	void StepPagerMemory(bool older);
	void ShowMemoryEntry();
	void PublishGameState();
	void ShowNextText();
	void ShowNextPage();
	void Deliver(std::string from, std::string subject, std::string body);
	void PublishUnreadCount();
	std::filesystem::path SaveFileFor(const char* gameSaveFile) const;

	BrowserOverlay* overlay_ = nullptr;
	const PadInput* pad_ = nullptr;
	MailStore store_;
	PagerRenderer pager_;
	std::filesystem::path saveFolder_;

	bool needsWelcome_ = true;
	int newMailToAnnounce_ = 0;
	std::deque<std::string> texts_;
	std::deque<PagerMessage> pages_;
	KeyConfig keys_;
	bool memoryOpen_ = false;
	size_t memoryIndex_ = 0;  // 0 is the latest message.
	DWORD padHoldStart_ = 0;  // When the pager button started being held, or 0 if it doesn't count.
	DWORD lastDraw_ = 0;
	DWORD lastTextShown_ = 0;
	DWORD lastStatePublished_ = 0;
	DWORD lastFlush_ = 0;
	bool stateSampleLogged_ = false;
	DWORD stateSampleTime_ = 0;
};
