#pragma once
// Starts the helper and talks to it. It doesn't depend on the game, so the harness uses it too.
#include <cstdint>
#include <string>
#include <windows.h>

#include "ipc.h"

// Players add CEF themselves. Returns what's wrong with the helper's folder, or nothing if it can run.
std::string BrowserRuntimeProblem(const std::wstring& folder);

class BrowserClient {
public:
	~BrowserClient() { Stop(); }

	bool Start(const std::wstring& helperPath, const std::wstring& url, const std::wstring& mailPath, uint32_t width, uint32_t height, std::string& error);
	void Stop();

	bool IsRunning() const { return process_ != nullptr; }
	bool HasExited() const;
	bool IsReady() const;
	void Send(const ef::Event& event);

	// Returns the latest frame if there's a new one. It stays valid until the next call.
	const uint8_t* AcquireFrame();

	uint32_t Width() const { return header_ ? header_->width : 0; }
	uint32_t Height() const { return header_ ? header_->height : 0; }
	uint32_t Stride() const { return header_ ? header_->stride : 0; }
	uint32_t PaintCount() const { return header_ ? header_->paintCount.load(std::memory_order_relaxed) : 0; }
	ef::CursorKind Cursor() const;
	bool TakeCloseRequest();
	bool TakeAlarm();
	bool KeyboardOpen() const;
	bool TakeMailOp(ef::MailOp& operation);
	bool TakeSiteScript(std::string& name);
	bool TakeValueOp(std::string& key, std::string& value);
	void PublishGameState(const ef::GameStateData& state);

private:
	HANDLE mapping_ = nullptr;
	HANDLE wake_ = nullptr;
	HANDLE process_ = nullptr;
	HANDLE job_ = nullptr;
	ef::Header* header_ = nullptr;
	uint32_t readerSlot_ = 2;
};
