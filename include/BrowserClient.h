#pragma once
// Lanza el helper y habla con él. No depende del juego, así que el arnés también lo usa.
#include <cstdint>
#include <string>
#include <windows.h>

#include "ipc.h"

class BrowserClient {
public:
	~BrowserClient() { Stop(); }

	bool Start(const std::wstring& helperPath, const std::wstring& url, const std::wstring& mailPath, uint32_t width, uint32_t height, std::string& error);
	void Stop();

	bool IsRunning() const { return process_ != nullptr; }
	bool HasExited() const;
	bool IsReady() const;
	void Send(const ef::Event& event);

	// Devuelve el fotograma más reciente si hay uno nuevo. Sigue siendo válido hasta la siguiente llamada.
	const uint8_t* AcquireFrame();

	uint32_t Width() const { return header_ ? header_->width : 0; }
	uint32_t Height() const { return header_ ? header_->height : 0; }
	uint32_t Stride() const { return header_ ? header_->stride : 0; }
	uint32_t PaintCount() const { return header_ ? header_->paintCount.load(std::memory_order_relaxed) : 0; }
	ef::CursorKind Cursor() const;
	bool TakeCloseRequest();
	bool TakeMailOp(ef::MailOp& operation);
	bool TakeSiteScript(std::string& name);

private:
	HANDLE mapping_ = nullptr;
	HANDLE wake_ = nullptr;
	HANDLE process_ = nullptr;
	HANDLE job_ = nullptr;
	ef::Header* header_ = nullptr;
	uint32_t readerSlot_ = 2;
};
