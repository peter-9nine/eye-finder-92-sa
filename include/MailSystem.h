#pragma once
#include "pch.h"
#include "BrowserOverlay.h"
#include "KeyConfig.h"
#include "MailStore.h"
#include "PagerRenderer.h"

#include <deque>

// Recibe lo que envían otros mods, lleva la bandeja de la partida en curso y muestra los avisos.
class MailSystem {
public:
	void Initialize(BrowserOverlay& overlay);

	void Update();
	void Draw();

	// Partidas: el juego acaba de empezar, guardar o cargar una.
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
	void ShowMemoryEntry();
	void PublishGameState();
	void ShowNextText();
	void ShowNextPage();
	void Deliver(std::string from, std::string subject, std::string body);
	void PublishUnreadCount();
	std::filesystem::path SaveFileFor(const char* gameSaveFile) const;

	BrowserOverlay* overlay_ = nullptr;
	MailStore store_;
	PagerRenderer pager_;
	std::filesystem::path saveFolder_;

	bool needsWelcome_ = true;
	int newMailToAnnounce_ = 0;
	std::deque<std::string> texts_;
	std::deque<PagerMessage> pages_;
	KeyConfig keys_;
	bool memoryOpen_ = false;
	size_t memoryIndex_ = 0;  // 0 es el mensaje más reciente.
	DWORD lastDraw_ = 0;
	DWORD lastTextShown_ = 0;
	DWORD lastStatePublished_ = 0;
	DWORD lastFlush_ = 0;
	bool stateSampleLogged_ = false;
	DWORD stateSampleTime_ = 0;
};
