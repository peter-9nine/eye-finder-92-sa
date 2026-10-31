// Helper de 64 bits: ejecuta Chromium sin ventana y publica los fotogramas en memoria compartida.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "ipc.h"

namespace {

std::wstring gStartUrl;
ef::Header* gHeader = nullptr;
HANDLE gBrowserReady = nullptr;
HANDLE gBrowserClosed = nullptr;
HANDLE gQuit = nullptr;

class Client : public CefClient,
               public CefRenderHandler,
               public CefLifeSpanHandler,
               public CefDisplayHandler,
               public CefLoadHandler,
               public CefContextMenuHandler,
               public CefJSDialogHandler {
public:
	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
	CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }

	CefRefPtr<CefBrowser> Browser() const { return browser_; }

	// --- Render ---
	void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
		rect = CefRect(0, 0, gHeader->width, gHeader->height);
	}

	bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override {
		info.device_scale_factor = 1.0f;
		return true;
	}

	void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override {
		popupShown_ = show;
		if (!show) popup_.clear();
		browser->GetHost()->Invalidate(PET_VIEW);
	}

	void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override {
		popupRect_ = rect;
	}

	void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList&, const void* buffer, int width, int height) override {
		const auto stride = gHeader->stride;
		const size_t size = size_t(stride) * gHeader->height;

		if (type == PET_VIEW) {
			if (static_cast<uint32_t>(width) != gHeader->width || static_cast<uint32_t>(height) != gHeader->height) return;

			auto* target = ef::SlotPixels(gHeader, writerSlot_);
			std::memcpy(target, buffer, size);

			if (popupShown_) {
				view_.assign(static_cast<const uint8_t*>(buffer), static_cast<const uint8_t*>(buffer) + size);
				BlitPopup(target);
			}
			Publish();
			return;
		}

		// PET_POPUP: las listas desplegables se pintan aparte y hay que componerlas sobre la página.
		popupWidth_ = width;
		popupHeight_ = height;
		const auto* source = static_cast<const uint8_t*>(buffer);
		popup_.assign(source, source + size_t(width) * height * 4);

		if (view_.size() != size) {
			browser->GetHost()->Invalidate(PET_VIEW);
			return;
		}

		auto* target = ef::SlotPixels(gHeader, writerSlot_);
		std::memcpy(target, view_.data(), size);
		BlitPopup(target);
		Publish();
	}

	// --- Ciclo de vida ---
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
		browser_ = browser;
		gHeader->state.store(static_cast<uint32_t>(ef::HelperState::Ready), std::memory_order_release);
		SetEvent(gBrowserReady);
	}

	void OnBeforeClose(CefRefPtr<CefBrowser>) override {
		browser_ = nullptr;
		SetEvent(gBrowserClosed);
	}

	// No hay ventanas emergentes: se abren en la misma vista.
	bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int, const CefString& targetUrl, const CefString&,
		WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
		CefRefPtr<CefDictionaryValue>&, bool*) override {
		if (!targetUrl.empty()) browser->GetMainFrame()->LoadURL(targetUrl);
		return true;
	}

	// --- Pantalla ---
	bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle, cef_cursor_type_t type, const CefCursorInfo&) override {
		ef::CursorKind kind = ef::CursorKind::Arrow;
		if (type == CT_HAND) kind = ef::CursorKind::Hand;
		else if (type == CT_IBEAM) kind = ef::CursorKind::IBeam;
		gHeader->cursor.store(static_cast<uint32_t>(kind), std::memory_order_relaxed);
		return true;
	}

	// --- Errores de carga ---
	void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& text, const CefString& url) override {
		if (code == ERR_ABORTED || !frame->IsMain()) return;
		std::string html = "data:text/html,<body style='font:18px sans-serif;margin:40px'><h2>This page could not be loaded</h2><p>";
		html += text.ToString() + " (" + std::to_string(static_cast<int>(code)) + ")</p></body>";
		frame->LoadURL(html);
		(void)url;
	}

	// --- Sin menú contextual ni diálogos nativos de momento: no hay ventana donde mostrarlos ---
	void OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams>, CefRefPtr<CefMenuModel> model) override {
		model->Clear();
	}

	bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType, const CefString&, const CefString&,
		CefRefPtr<CefJSDialogCallback>, bool& suppress) override {
		suppress = true;
		return false;
	}

private:
	void BlitPopup(uint8_t* target) {
		if (popup_.empty()) return;

		const int viewWidth = static_cast<int>(gHeader->width);
		const int viewHeight = static_cast<int>(gHeader->height);
		const int left = std::max(popupRect_.x, 0);
		const int top = std::max(popupRect_.y, 0);
		const int right = std::min(popupRect_.x + popupWidth_, viewWidth);
		const int bottom = std::min(popupRect_.y + popupHeight_, viewHeight);

		for (int y = top; y < bottom; ++y) {
			const uint8_t* source = popup_.data() + size_t(y - popupRect_.y) * popupWidth_ * 4 + size_t(left - popupRect_.x) * 4;
			uint8_t* destination = target + size_t(y) * gHeader->stride + size_t(left) * 4;
			if (right > left) std::memcpy(destination, source, size_t(right - left) * 4);
		}
	}

	void Publish() {
		writerSlot_ = gHeader->ready.exchange(writerSlot_ | ef::kNewFrame, std::memory_order_acq_rel) & ~ef::kNewFrame;
		gHeader->paintCount.fetch_add(1, std::memory_order_relaxed);
	}

	CefRefPtr<CefBrowser> browser_;
	uint32_t writerSlot_ = 0;
	bool popupShown_ = false;
	CefRect popupRect_;
	int popupWidth_ = 0;
	int popupHeight_ = 0;
	std::vector<uint8_t> popup_;
	std::vector<uint8_t> view_;

	IMPLEMENT_REFCOUNTING(Client);
};

CefRefPtr<Client> gClient;

class App : public CefApp, public CefBrowserProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override {
		if (processType.empty()) {
			// Prototipo: todo por software, para entregar los fotogramas a la CPU de forma predecible.
			commandLine->AppendSwitch("disable-gpu");
			commandLine->AppendSwitch("disable-gpu-compositing");
		}
	}

	void OnContextInitialized() override {
		CefWindowInfo window;
		window.SetAsWindowless(nullptr);

		CefBrowserSettings settings;
		settings.windowless_frame_rate = 60;
		settings.background_color = CefColorSetARGB(255, 255, 255, 255);

		gClient = new Client();
		CefBrowserHost::CreateBrowser(window, gClient, gStartUrl, settings, nullptr, nullptr);
	}

private:
	IMPLEMENT_REFCOUNTING(App);
};

void HandleEvent(const ef::Event& event, CefRefPtr<CefBrowser> browser) {
	static DWORD lastClickTime = 0;
	static int lastClickX = 0;
	static int lastClickY = 0;
	static int lastClickButton = -1;
	static int clickCount = 1;

	CefRefPtr<CefBrowserHost> host = browser->GetHost();

	CefMouseEvent mouse;
	mouse.x = event.x;
	mouse.y = event.y;
	mouse.modifiers = event.modifiers;

	const auto button = event.a == 1 ? MBT_MIDDLE : (event.a == 2 ? MBT_RIGHT : MBT_LEFT);

	switch (event.type) {
	case ef::EventType::MouseMove:
		host->SendMouseMoveEvent(mouse, false);
		break;

	case ef::EventType::MouseDown: {
		const DWORD now = GetTickCount();
		const bool repeat = event.a == lastClickButton && now - lastClickTime < GetDoubleClickTime() &&
			abs(event.x - lastClickX) <= 4 && abs(event.y - lastClickY) <= 4;
		clickCount = repeat ? clickCount + 1 : 1;
		lastClickTime = now;
		lastClickX = event.x;
		lastClickY = event.y;
		lastClickButton = event.a;
		host->SendMouseClickEvent(mouse, button, false, clickCount);
		break;
	}

	case ef::EventType::MouseUp:
		host->SendMouseClickEvent(mouse, button, true, clickCount);
		break;

	case ef::EventType::Wheel:
		host->SendMouseWheelEvent(mouse, event.a, event.b);
		break;

	case ef::EventType::KeyDown:
		if (event.a == VK_F5) {
			browser->Reload();
			break;
		}
		if ((event.modifiers & ef::kModAlt) && event.a == VK_LEFT) {
			browser->GoBack();
			break;
		}
		if ((event.modifiers & ef::kModAlt) && event.a == VK_RIGHT) {
			browser->GoForward();
			break;
		}
		[[fallthrough]];
	case ef::EventType::KeyUp:
	case ef::EventType::Char: {
		CefKeyEvent key;
		key.type = event.type == ef::EventType::KeyDown ? KEYEVENT_RAWKEYDOWN
			: event.type == ef::EventType::KeyUp ? KEYEVENT_KEYUP : KEYEVENT_CHAR;
		key.windows_key_code = event.a;
		key.native_key_code = event.b;
		key.modifiers = event.modifiers;
		key.is_system_key = (event.modifiers & ef::kModAlt) != 0;
		host->SendKeyEvent(key);
		break;
	}

	case ef::EventType::Show:
		host->WasHidden(false);
		host->SetFocus(true);
		host->Invalidate(PET_VIEW);
		break;

	case ef::EventType::Hide:
		host->SetFocus(false);
		host->WasHidden(true);
		break;

	case ef::EventType::Invalidate:
		host->Invalidate(PET_VIEW);
		break;

	case ef::EventType::Quit:
		break;
	}
}

void PumpEvents(HANDLE wake, HANDLE parent) {
	// Esperamos al navegador sin perder de vista al juego.
	HANDLE startWaits[] = { gBrowserReady, parent };
	if (WaitForMultipleObjects(parent ? 2 : 1, startWaits, FALSE, INFINITE) != WAIT_OBJECT_0) {
		SetEvent(gQuit);
		return;
	}

	HANDLE waits[] = { wake, parent };
	for (;;) {
		const DWORD result = WaitForMultipleObjects(parent ? 2 : 1, waits, FALSE, INFINITE);
		if (result != WAIT_OBJECT_0) {
			SetEvent(gQuit);
			return;
		}

		uint32_t read = gHeader->ringRead.load(std::memory_order_relaxed);
		const uint32_t write = gHeader->ringWrite.load(std::memory_order_acquire);
		CefRefPtr<CefBrowser> browser = gClient->Browser();

		while (read != write) {
			const ef::Event event = gHeader->ring[read % ef::kRingSize];
			++read;
			gHeader->ringRead.store(read, std::memory_order_release);

			if (event.type == ef::EventType::Quit) {
				SetEvent(gQuit);
				return;
			}
			if (browser) HandleEvent(event, browser);
		}
	}
}

std::wstring CachePath() {
	wchar_t buffer[MAX_PATH]{};
	GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
	return std::wstring(buffer) + L"\\EyeFinder92SA";
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
	CefMainArgs args(instance);
	CefRefPtr<App> app(new App());

	// Los subprocesos de Chromium (renderer, utilidades) terminan aquí.
	const int subprocess = CefExecuteProcess(args, app, nullptr);
	if (subprocess >= 0) return subprocess;

	CefRefPtr<CefCommandLine> commandLine = CefCommandLine::CreateCommandLine();
	commandLine->InitFromString(GetCommandLineW());
	const std::wstring section = commandLine->GetSwitchValue("ef-shm").ToWString();
	const std::wstring parentId = commandLine->GetSwitchValue("ef-parent").ToWString();
	gStartUrl = commandLine->GetSwitchValue("ef-url").ToWString();
	if (section.empty() || gStartUrl.empty()) return 2;

	HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, section.c_str());
	if (!mapping) return 3;

	gHeader = static_cast<ef::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if (!gHeader || gHeader->magic != ef::kMagic || gHeader->protocol != ef::kProtocol) return 4;

	HANDLE wake = OpenEventW(EVENT_ALL_ACCESS, FALSE, (section + L"_wake").c_str());
	if (!wake) return 5;

	HANDLE parent = parentId.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(std::stoul(parentId)));

	gBrowserReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	gBrowserClosed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	gQuit = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	const std::wstring cache = CachePath();

	CefSettings settings;
	settings.no_sandbox = true;
	settings.windowless_rendering_enabled = true;
	settings.multi_threaded_message_loop = true;
	settings.log_severity = LOGSEVERITY_WARNING;
	CefString(&settings.root_cache_path).FromWString(cache);
	CefString(&settings.cache_path).FromWString(cache + L"\\profile");
	CefString(&settings.log_file).FromWString(cache + L"\\helper.log");

	if (!CefInitialize(args, settings, app, nullptr)) {
		gHeader->state.store(static_cast<uint32_t>(ef::HelperState::Failed), std::memory_order_release);
		return 6;
	}

	std::thread pump(PumpEvents, wake, parent);
	WaitForSingleObject(gQuit, INFINITE);
	pump.join();

	if (gClient && gClient->Browser()) {
		gClient->Browser()->GetHost()->CloseBrowser(true);
		WaitForSingleObject(gBrowserClosed, 3000);
	}
	gClient = nullptr;
	CefShutdown();
	return 0;
}
