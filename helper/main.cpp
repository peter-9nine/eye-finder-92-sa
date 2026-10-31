// Helper de 64 bits: ejecuta Chromium sin ventana y publica los fotogramas en memoria compartida.
//
// Hay una vista de interfaz a pantalla completa (ui/shell.html: escritorio y ventana del navegador)
// y una vista por pestaña. La pestaña activa se compone sobre el hueco que le indica la interfaz.
// Todo el estado de este archivo vive en el hilo UI de CEF, salvo la cola de eventos.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "include/base/cef_callback.h"
#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/cef_command_line.h"
#include "include/base/cef_logging.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_scheme.h"
#include "include/cef_resource_request_handler.h"
#include "include/wrapper/cef_byte_read_handler.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_message_router.h"
#include "include/wrapper/cef_stream_resource_handler.h"
#include "ipc.h"

namespace {

namespace fs = std::filesystem;

std::string gShellUrl;
std::wstring gMailPath;
int gUnreadMail = 0;
constexpr char kMailDomain[] = "mail.eyefinder.info";
constexpr char kHomeUrl[] = "https://www.eyefinder.info/";

// El navegador arranca solo con los sitios locales. El internet real solo se activa desde la interfaz,
// y esta pide confirmación cada vez. Se consulta desde varios hilos.
std::atomic<bool> gInternetEnabled{ false };
std::set<std::string> gLocalDomains;  // Solo se escribe al arrancar, antes de crear navegadores.

bool IsAllowedUrl(const std::string& url) {
	if (gInternetEnabled.load(std::memory_order_relaxed)) return true;

	CefURLParts parts;
	if (!CefParseURL(url, parts)) return false;

	std::string scheme = CefString(&parts.scheme).ToString();
	for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	// data:, blob: y about: no salen a la red.
	if (scheme != "http" && scheme != "https" && scheme != "ws" && scheme != "wss" && scheme != "ftp") return true;

	std::string host = CefString(&parts.host).ToString();
	for (char& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return gLocalDomains.count(host) != 0;
}
ef::Header* gHeader = nullptr;
HANDLE gShellReady = nullptr;
HANDLE gAllClosed = nullptr;
HANDLE gQuit = nullptr;

class View;
CefRefPtr<View> gShell;
std::map<int, CefRefPtr<View>> gTabs;
int gActiveTab = -1;
CefRect gContentRect;
bool gContentVisible = false;
bool gOverlayVisible = true;
int gOpenBrowsers = 0;
bool gClosingAll = false;

View* gHover = nullptr;
View* gCaptured = nullptr;
View* gKeyboard = nullptr;

uint32_t gWriterSlot = 0;

void ComposeAndPublish();
void SetKeyboardTarget(View* view);
void PublishCursor();
void CallShell(const std::string& function, CefRefPtr<CefValue> argument);
void AskForInternet(const std::string& url, bool newTab);

CefRefPtr<CefValue> StringValue(const std::string& text) {
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetString(text);
	return value;
}

// --- Sitios locales: sites/<dominio>/ se sirve como si fuera ese dominio ---
class TextHolder : public CefBaseRefCounted {
public:
	explicit TextHolder(std::string text) : text(std::move(text)) {}
	std::string text;

	IMPLEMENT_REFCOUNTING(TextHolder);
};

CefRefPtr<CefResourceHandler> JsonResponse(int status, std::string text) {
	CefRefPtr<TextHolder> holder = new TextHolder(std::move(text));
	CefRefPtr<CefReadHandler> reader = new CefByteReadHandler(reinterpret_cast<const unsigned char*>(holder->text.data()), holder->text.size(), holder);

	CefResponse::HeaderMap headers;
	headers.insert({ "Cache-Control", "no-store" });
	return new CefStreamResourceHandler(status, status == 200 ? "OK" : "Error", "application/json", headers, CefStreamReader::CreateForHandler(reader));
}

// La bandeja del correo, solo para su propio sitio. Las demás webs no pueden ni leerla ni tocarla:
// la cabecera propia obliga al navegador a pedir permiso (CORS), y aquí nunca se concede.
CefRefPtr<CefResourceHandler> HandleMailApi(const std::string& path, CefRefPtr<CefRequest> request) {
	if (request->GetHeaderByName("X-EyeFinder").empty()) return JsonResponse(403, "{}");

	if (path == "/api/inbox") {
		std::ifstream file(fs::path(gMailPath), std::ios::binary);
		if (!file.is_open()) return JsonResponse(503, "{}");
		std::ostringstream contents;
		contents << file.rdbuf();
		return JsonResponse(200, contents.str());
	}

	if (request->GetMethod().ToString() != "POST") return JsonResponse(405, "{}");

	ef::MailOpKind kind;
	std::string idText;
	if (path.rfind("/api/reply/", 0) == 0) {
		// /api/reply/<id>/<1 buena o 2 mala>
		const size_t slash = path.rfind('/');
		const std::string answer = path.substr(slash + 1);
		if (answer != "1" && answer != "2") return JsonResponse(400, "{}");
		kind = answer == "1" ? ef::MailOpKind::ReplyGood : ef::MailOpKind::ReplyBad;
		idText = path.substr(11, slash - 11);
	}
	else if (path.rfind("/api/read/", 0) == 0) {
		kind = ef::MailOpKind::MarkRead;
		idText = path.substr(10);
	}
	else if (path.rfind("/api/delete/", 0) == 0) {
		kind = ef::MailOpKind::Remove;
		idText = path.substr(12);
	}
	else {
		return JsonResponse(404, "{}");
	}

	if (idText.empty() || idText.size() > 9 || idText.find_first_not_of("0123456789") != std::string::npos) return JsonResponse(400, "{}");

	const uint32_t write = gHeader->mailOpWrite.load(std::memory_order_relaxed);
	if (write - gHeader->mailOpRead.load(std::memory_order_acquire) >= ef::kMailOpRingSize) return JsonResponse(503, "{}");
	gHeader->mailOps[write % ef::kMailOpRingSize] = { kind, static_cast<uint32_t>(std::stoul(idText)) };
	gHeader->mailOpWrite.store(write + 1, std::memory_order_release);
	return JsonResponse(200, "{}");
}

fs::path ExecutableDirectory() {
	wchar_t buffer[MAX_PATH]{};
	GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	return fs::path(buffer).parent_path();
}

// Dos sitios para los sitios: browser\\sites es del mod y se sustituye al actualizarlo. La carpeta
// sites junto al .asi es pública: quien quiera añade ahí lo suyo y nada la toca.
fs::path SystemSitesFolder() { return ExecutableDirectory() / L"sites"; }
fs::path PublicSitesFolder() { return ExecutableDirectory().parent_path() / L"sites"; }
fs::path SystemAdsFolder() { return ExecutableDirectory() / L"ads"; }
fs::path PublicAdsFolder() { return ExecutableDirectory().parent_path() / L"ads"; }

std::string ReadTextFile(const fs::path& path) {
	std::ifstream file(path, std::ios::binary);
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}

// Un JSON roto de un sitio o un anuncio nunca debe romper la lista: se trata como si no estuviera.
CefRefPtr<CefDictionaryValue> ReadJsonObject(const fs::path& path) {
	std::error_code error;
	if (!fs::is_regular_file(path, error)) return nullptr;
	CefRefPtr<CefValue> value = CefParseJSON(ReadTextFile(path), JSON_PARSER_ALLOW_TRAILING_COMMAS);
	return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
}

std::string StringField(CefRefPtr<CefDictionaryValue> object, const char* name) {
	return object && object->HasKey(name) && object->GetType(name) == VTYPE_STRING ? object->GetString(name).ToString() : std::string();
}

std::string ListAsJson(const char* name, CefRefPtr<CefListValue> list) {
	CefRefPtr<CefDictionaryValue> root = CefDictionaryValue::Create();
	root->SetList(name, list);
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(root);
	return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string BuildSitesJson() {
	CefRefPtr<CefListValue> list = CefListValue::Create();
	size_t index = 0;

	std::error_code error;
	for (const auto& entry : fs::directory_iterator(PublicSitesFolder(), error)) {
		if (!entry.is_directory()) continue;

		const std::string domain = entry.path().filename().string();
		if (domain.find('.') == std::string::npos) continue;

		// Un sitio del mod con el mismo nombre tiene prioridad, así que este no está disponible.
		std::error_code systemError;
		if (fs::exists(SystemSitesFolder() / entry.path().filename(), systemError)) continue;

		CefRefPtr<CefDictionaryValue> details = ReadJsonObject(entry.path() / L"site.json");
		if (details && details->HasKey("listed") && details->GetType("listed") == VTYPE_BOOL && !details->GetBool("listed")) continue;

		const std::string title = StringField(details, "title");
		CefRefPtr<CefDictionaryValue> site = CefDictionaryValue::Create();
		site->SetString("domain", domain);
		site->SetString("url", "https://" + domain + "/");
		site->SetString("title", title.empty() ? domain : title);
		site->SetString("description", StringField(details, "description"));
		list->SetDictionary(index++, site);
	}
	return ListAsJson("sites", list);
}

bool IsSimpleFileName(const std::string& name) {
	return !name.empty() && name.size() <= 128 && name != "." && name != ".." &&
		name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") == std::string::npos;
}

std::string BuildAdsJson() {
	CefRefPtr<CefListValue> list = CefListValue::Create();
	size_t index = 0;

	for (const fs::path& folder : { SystemAdsFolder(), PublicAdsFolder() }) {
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(folder, error)) {
			if (!entry.is_directory()) continue;

			const std::string id = entry.path().filename().string();
			CefRefPtr<CefDictionaryValue> details = ReadJsonObject(entry.path() / L"ad.json");
			const std::string image = StringField(details, "image");
			if (!IsSimpleFileName(id) || !IsSimpleFileName(image)) continue;

			// Un enlace solo puede ser una dirección web: nada de javascript:.
			std::string link = StringField(details, "link");
			if (link.rfind("http://", 0) != 0 && link.rfind("https://", 0) != 0) link.clear();

			CefRefPtr<CefDictionaryValue> ad = CefDictionaryValue::Create();
			ad->SetString("id", id);
			ad->SetString("image", "/_game/ad/" + id + "/" + image);
			ad->SetString("link", link);
			ad->SetString("kind", StringField(details, "kind"));
			ad->SetString("text", StringField(details, "text"));
			list->SetDictionary(index++, ad);
		}
	}
	return ListAsJson("ads", list);
}

CefRefPtr<CefResourceHandler> ServeFile(const fs::path& file, const std::string& mime) {
	CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(file.wstring());
	if (!stream) return JsonResponse(404, "{}");
	return new CefStreamResourceHandler(mime, stream);
}

// "<carpeta del anuncio>/<archivo>", sin poder salir de las carpetas de anuncios.
CefRefPtr<CefResourceHandler> ServeAdImage(const std::string& path) {
	const size_t slash = path.find('/');
	if (slash == std::string::npos) return JsonResponse(404, "{}");

	const std::string id = path.substr(0, slash);
	const std::string name = path.substr(slash + 1);
	if (!IsSimpleFileName(id) || !IsSimpleFileName(name)) return JsonResponse(404, "{}");

	for (const fs::path& folder : { SystemAdsFolder(), PublicAdsFolder() }) {
		const fs::path file = folder / id / name;
		std::error_code error;
		if (!fs::is_regular_file(file, error)) continue;

		std::string extension = file.extension().string();
		if (!extension.empty()) extension.erase(0, 1);
		std::string mime = CefGetMimeType(extension).ToString();
		if (mime.empty()) mime = "application/octet-stream";
		return ServeFile(file, mime);
	}
	return JsonResponse(404, "{}");
}

// Lo que un sitio local puede pedirle al juego. Las webs reales no llegan aquí: estos dominios solo
// se sirven desde el disco, y la cabecera propia impide que otro origen haga la petición.
CefRefPtr<CefResourceHandler> HandleGameApi(const std::string& domain, const fs::path& root, const std::string& path, CefRefPtr<CefRequest> request) {
	// Información pública: la lista de sitios y los anuncios se leen en cada petición.
	if (path == "/_game/sa.js") return ServeFile(ExecutableDirectory() / L"ui" / L"sa.js", "text/javascript");
	if (path == "/_game/sites.json") return JsonResponse(200, BuildSitesJson());
	if (path == "/_game/ads.json") return JsonResponse(200, BuildAdsJson());
	if (path.rfind("/_game/ad/", 0) == 0) return ServeAdImage(path.substr(10));

	if (request->GetHeaderByName("X-EyeFinder").empty()) return JsonResponse(403, "{}");

	if (path.rfind("/_game/script/", 0) == 0 && request->GetMethod().ToString() == "POST") {
		// Solo nombres simples y solo de la carpeta scripts del propio sitio.
		const std::string name = path.substr(14);
		const bool valid = !name.empty() && name.size() <= 64 &&
			name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
		std::error_code error;
		if (!valid || !fs::is_regular_file(root / L"scripts" / (name + ".efs"), error)) return JsonResponse(404, "{}");

		const std::string entry = domain + "/" + name;
		if (entry.size() >= ef::kSiteScriptNameSize) return JsonResponse(400, "{}");

		const uint32_t write = gHeader->siteScriptWrite.load(std::memory_order_relaxed);
		if (write - gHeader->siteScriptRead.load(std::memory_order_acquire) >= ef::kSiteScriptRingSize) return JsonResponse(503, "{}");
		char* slot = gHeader->siteScripts[write % ef::kSiteScriptRingSize];
		std::memset(slot, 0, ef::kSiteScriptNameSize);
		std::memcpy(slot, entry.data(), entry.size());
		gHeader->siteScriptWrite.store(write + 1, std::memory_order_release);
		return JsonResponse(200, "{}");
	}

	return JsonResponse(404, "{}");
}

class SiteHandlerFactory : public CefSchemeHandlerFactory {
public:
	SiteHandlerFactory(fs::path root, bool isMailSite) : root_(std::move(root)), isMailSite_(isMailSite) {}

	CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&, CefRefPtr<CefRequest> request) override {
		CefURLParts parts;
		if (!CefParseURL(request->GetURL(), parts)) return nullptr;

		// Un sitio no puede salir de su carpeta.
		const std::string escaped = CefString(&parts.path).ToString();
		if (isMailSite_ && escaped.rfind("/api/", 0) == 0) return HandleMailApi(escaped, request);
		if (escaped.rfind("/_game/", 0) == 0) return HandleGameApi(root_.filename().string(), root_, escaped, request);

		const std::wstring decoded = CefURIDecode(escaped, true, UU_SPACES).ToWString();
		fs::path relative = fs::path(decoded).relative_path().lexically_normal();
		for (const auto& piece : relative) {
			if (piece == L"..") return NotFound();
		}

		fs::path file = root_ / relative;
		std::error_code error;
		if (relative.empty() || relative == L"." || fs::is_directory(file, error)) file /= L"index.html";
		if (!fs::is_regular_file(file, error)) return NotFound();

		CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForFile(file.wstring());
		if (!stream) return NotFound();

		std::string extension = file.extension().string();
		if (!extension.empty()) extension.erase(0, 1);
		std::string mime = CefGetMimeType(extension).ToString();
		if (mime.empty()) mime = "application/octet-stream";
		return new CefStreamResourceHandler(mime, stream);
	}

private:
	static CefRefPtr<CefResourceHandler> NotFound() {
		static const char kPage[] = "<body style='font:18px Tahoma,sans-serif;margin:40px'><h2>404</h2><p>This page does not exist.</p></body>";
		CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForData(const_cast<char*>(kPage), sizeof(kPage) - 1);
		return new CefStreamResourceHandler(404, "Not Found", "text/html", CefResponse::HeaderMap(), stream);
	}

	fs::path root_;
	const bool isMailSite_;

	IMPLEMENT_REFCOUNTING(SiteHandlerFactory);
};

// --- Órdenes de la interfaz ---
void OpenTab(int id, const std::string& url);
void CloseTab(int id);
void ActivateTab(int id);
void SetLayout(int x, int y, int width, int height, bool visible);

class ShellQueryHandler : public CefMessageRouterBrowserSide::Handler {
public:
	bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request, bool, CefRefPtr<Callback> callback) override;
};

class View : public CefClient,
             public CefRenderHandler,
             public CefLifeSpanHandler,
             public CefDisplayHandler,
             public CefLoadHandler,
             public CefRequestHandler,
             public CefResourceRequestHandler,
             public CefContextMenuHandler,
             public CefJSDialogHandler {
public:
	explicit View(int tabId) : tabId_(tabId) {
		if (IsShell()) {
			router_ = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
			router_->AddHandler(&queryHandler_, false);
		}
	}

	bool IsShell() const { return tabId_ < 0; }
	int TabId() const { return tabId_; }
	CefRefPtr<CefBrowser> Browser() const { return browser_; }
	CefRefPtr<CefBrowserHost> Host() const { return browser_ ? browser_->GetHost() : nullptr; }
	ef::CursorKind Cursor() const { return cursor_; }

	const uint8_t* Pixels() const {
		if (composite_.empty()) return base_.empty() ? nullptr : base_.data();
		return composite_.data();
	}
	int PixelWidth() const { return baseWidth_; }
	int PixelHeight() const { return baseHeight_; }

	CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
	CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
	CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
	CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override { return this; }
	CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }

	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
		return router_ && router_->OnProcessMessageReceived(browser, frame, source, message);
	}

	// --- Render ---
	void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override {
		if (IsShell()) rect = CefRect(0, 0, gHeader->width, gHeader->height);
		else rect = CefRect(0, 0, std::max(gContentRect.width, 1), std::max(gContentRect.height, 1));
	}

	bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override {
		info.device_scale_factor = 1.0f;
		return true;
	}

	void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override {
		popupShown_ = show;
		if (!show) {
			popup_.clear();
			composite_.clear();
			Changed();
		}
	}

	void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override { popupRect_ = rect; }

	void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&, const void* buffer, int width, int height) override {
		const auto* source = static_cast<const uint8_t*>(buffer);
		if (type == PET_VIEW) {
			base_.assign(source, source + size_t(width) * height * 4);
			baseWidth_ = width;
			baseHeight_ = height;
		}
		else {
			popup_.assign(source, source + size_t(width) * height * 4);
			popupWidth_ = width;
			popupHeight_ = height;
		}
		RebuildComposite();
		Changed();
	}

	// --- Ciclo de vida ---
	void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
		browser_ = browser;
		++gOpenBrowsers;

		if (IsShell()) {
			gHeader->state.store(static_cast<uint32_t>(ef::HelperState::Ready), std::memory_order_release);
			SetEvent(gShellReady);
			return;
		}

		const bool visible = gOverlayVisible && tabId_ == gActiveTab;
		browser->GetHost()->WasHidden(!visible);
		browser->GetHost()->SetAudioMuted(!gOverlayVisible);
		if (visible) SetKeyboardTarget(this);
	}

	void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
		if (router_) router_->OnBeforeClose(browser);
		browser_ = nullptr;

		if (gHover == this) gHover = nullptr;
		if (gCaptured == this) gCaptured = nullptr;
		if (gKeyboard == this) gKeyboard = nullptr;

		if (--gOpenBrowsers == 0 && gClosingAll) SetEvent(gAllClosed);
	}

	// Las ventanas emergentes se abren como pestañas nuevas.
	bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int, const CefString& targetUrl, const CefString&,
		CefLifeSpanHandler::WindowOpenDisposition, bool, const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
		CefRefPtr<CefDictionaryValue>&, bool*) override {
		if (!IsShell() && !targetUrl.empty()) {
			const std::string url = targetUrl.ToString();
			if (IsAllowedUrl(url)) CallShell("openTab", StringValue(url));
			else AskForInternet(url, true);
		}
		return true;
	}

	// --- Navegación ---
	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool) override {
		const std::string url = request->GetURL().ToString();

		if (IsShell()) {
			// La interfaz tiene acceso a las órdenes del helper: nunca puede salir de su página.
			if (url.rfind(gShellUrl, 0) != 0) return true;
			router_->OnBeforeBrowse(browser, frame);
			return false;
		}

		// Sin sandbox, una página no debe poder abrir archivos del disco.
		if (url.rfind("file:", 0) == 0) return true;

		if (!IsAllowedUrl(url)) {
			// Solo se pregunta si lo ha pedido el jugador: una página no puede insistir sola.
			if (frame->IsMain() && userGesture) AskForInternet(url, false);
			return true;
		}
		return false;
	}

	CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool, const CefString&, bool&) override {
		return IsShell() ? nullptr : this;
	}

	// Cada recurso, no solo las páginas: un sitio local no debe poder cargar nada de internet.
	CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) override {
		return IsAllowedUrl(request->GetURL().ToString()) ? RV_CONTINUE : RV_CANCEL;
	}

	void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
		if (router_) router_->OnRenderProcessTerminated(browser);
	}

	// --- Estado que muestra la interfaz ---
	void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, const CefString& url) override {
		if (!frame->IsMain() || IsShell()) return;
		url_ = url.ToString();
		SendState();
	}

	void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) override {
		if (IsShell()) return;
		title_ = title.ToString();
		SendState();
	}

	void OnStatusMessage(CefRefPtr<CefBrowser>, const CefString& text) override {
		if (IsShell() || tabId_ != gActiveTab) return;
		CallShell("onStatus", StringValue(text.ToString()));
	}

	void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool loading, bool back, bool forward) override {
		if (IsShell()) return;
		loading_ = loading;
		canBack_ = back;
		canForward_ = forward;
		SendState();
	}

	bool OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle, cef_cursor_type_t type, const CefCursorInfo&) override {
		cursor_ = type == CT_HAND ? ef::CursorKind::Hand : type == CT_IBEAM ? ef::CursorKind::IBeam : ef::CursorKind::Arrow;
		PublishCursor();
		return true;
	}

	void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override {
		if (IsShell() && frame->IsMain()) {
			CefRefPtr<CefValue> value = CefValue::Create();
			value->SetInt(gUnreadMail);
			CallShell("onMail", value);
		}
	}

	void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& text, const CefString& url) override {
		if (code == ERR_ABORTED || !frame->IsMain() || IsShell()) return;
		const std::string html = "<body style='font:18px Tahoma,sans-serif;margin:40px'><h2>This page could not be loaded</h2><p>" +
			text.ToString() + " (" + std::to_string(static_cast<int>(code)) + ")</p></body>";
		frame->LoadURL(CefURIEncode("data:text/html," + html, false));
		url_ = url.ToString();
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
	void Changed() {
		if (IsShell() || tabId_ == gActiveTab) ComposeAndPublish();
	}

	void RebuildComposite() {
		if (!popupShown_ || popup_.empty() || base_.empty()) {
			composite_.clear();
			return;
		}

		composite_ = base_;
		const int left = std::max(popupRect_.x, 0);
		const int top = std::max(popupRect_.y, 0);
		const int right = std::min(popupRect_.x + popupWidth_, baseWidth_);
		const int bottom = std::min(popupRect_.y + popupHeight_, baseHeight_);
		if (right <= left) return;

		for (int y = top; y < bottom; ++y) {
			const uint8_t* source = popup_.data() + (size_t(y - popupRect_.y) * popupWidth_ + (left - popupRect_.x)) * 4;
			uint8_t* destination = composite_.data() + (size_t(y) * baseWidth_ + left) * 4;
			std::memcpy(destination, source, size_t(right - left) * 4);
		}
	}

	void SendState() {
		CefRefPtr<CefDictionaryValue> state = CefDictionaryValue::Create();
		state->SetInt("id", tabId_);
		state->SetString("url", url_);
		state->SetString("title", title_);
		state->SetBool("loading", loading_);
		state->SetBool("back", canBack_);
		state->SetBool("forward", canForward_);
		CefRefPtr<CefValue> value = CefValue::Create();
		value->SetDictionary(state);
		CallShell("onTab", value);
	}

	const int tabId_;
	CefRefPtr<CefBrowser> browser_;
	CefRefPtr<CefMessageRouterBrowserSide> router_;
	ShellQueryHandler queryHandler_;

	std::vector<uint8_t> base_;
	std::vector<uint8_t> popup_;
	std::vector<uint8_t> composite_;
	int baseWidth_ = 0;
	int baseHeight_ = 0;
	bool popupShown_ = false;
	CefRect popupRect_;
	int popupWidth_ = 0;
	int popupHeight_ = 0;

	ef::CursorKind cursor_ = ef::CursorKind::Arrow;
	std::string url_;
	std::string title_;
	bool loading_ = false;
	bool canBack_ = false;
	bool canForward_ = false;

	IMPLEMENT_REFCOUNTING(View);
};

void CallShell(const std::string& function, CefRefPtr<CefValue> argument) {
	if (!gShell || !gShell->Browser()) return;
	const std::string json = CefWriteJSON(argument, JSON_WRITER_DEFAULT).ToString();
	gShell->Browser()->GetMainFrame()->ExecuteJavaScript("ef." + function + "(" + json + ")", gShellUrl, 0);
}

// La segunda barrera, a nivel de red: en modo local, todo sale por un proxy que no existe. Hace falta
// porque los WebSockets no pasan por OnBeforeResourceLoad. Los sitios locales no usan la red, así que
// siguen funcionando. <-loopback> incluye también a localhost, que Chromium excluye de los proxies.
void ApplyNetworkPolicy(bool internet) {
	CefRefPtr<CefDictionaryValue> proxy = CefDictionaryValue::Create();
	if (internet) {
		proxy->SetString("mode", "direct");
	}
	else {
		proxy->SetString("mode", "fixed_servers");
		proxy->SetString("server", "127.0.0.1:1");
		proxy->SetString("bypass_list", "<-loopback>");
	}

	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(proxy);

	CefString error;
	CefRefPtr<CefRequestContext> context = CefRequestContext::GetGlobalContext();
	if (!context->SetPreference("proxy", value, error)) {
		LOG(ERROR) << "Could not set the network policy: " << error.ToString();
	}

	// WebRTC puede enviar UDP saltandose el proxy, y con el revelar la IP real de quien juega.
	CefRefPtr<CefValue> webrtc = CefValue::Create();
	webrtc->SetString(internet ? "default_public_interface_only" : "disable_non_proxied_udp");
	if (!context->SetPreference("webrtc.ip_handling_policy", webrtc, error)) {
		LOG(ERROR) << "Could not set the WebRTC policy: " << error.ToString();
	}
}

void AskForInternet(const std::string& url, bool newTab) {
	CefRefPtr<CefDictionaryValue> details = CefDictionaryValue::Create();
	details->SetString("url", url);
	details->SetBool("newTab", newTab);
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(details);
	CallShell("onBlocked", value);
}

View* ActiveTabView() {
	auto found = gTabs.find(gActiveTab);
	return found == gTabs.end() ? nullptr : found->second.get();
}

void ComposeAndPublish() {
	if (!gShell || !gShell->Pixels()) return;

	const int viewWidth = static_cast<int>(gHeader->width);
	const int viewHeight = static_cast<int>(gHeader->height);
	if (gShell->PixelWidth() != viewWidth || gShell->PixelHeight() != viewHeight) return;

	uint8_t* target = ef::SlotPixels(gHeader, gWriterSlot);
	std::memcpy(target, gShell->Pixels(), size_t(gHeader->stride) * gHeader->height);

	View* tab = gContentVisible ? ActiveTabView() : nullptr;
	if (tab && tab->Pixels()) {
		const int left = std::max(gContentRect.x, 0);
		const int top = std::max(gContentRect.y, 0);
		const int right = std::min({ gContentRect.x + gContentRect.width, gContentRect.x + tab->PixelWidth(), viewWidth });
		const int bottom = std::min({ gContentRect.y + gContentRect.height, gContentRect.y + tab->PixelHeight(), viewHeight });

		if (right > left) {
			for (int y = top; y < bottom; ++y) {
				const uint8_t* source = tab->Pixels() + (size_t(y - gContentRect.y) * tab->PixelWidth() + (left - gContentRect.x)) * 4;
				std::memcpy(target + (size_t(y) * viewWidth + left) * 4, source, size_t(right - left) * 4);
			}
		}
	}

	gWriterSlot = gHeader->ready.exchange(gWriterSlot | ef::kNewFrame, std::memory_order_acq_rel) & ~ef::kNewFrame;
	gHeader->paintCount.fetch_add(1, std::memory_order_relaxed);
}

void PublishCursor() {
	const ef::CursorKind kind = gHover ? gHover->Cursor() : ef::CursorKind::Arrow;
	gHeader->cursor.store(static_cast<uint32_t>(kind), std::memory_order_relaxed);
}

void SetKeyboardTarget(View* view) {
	if (gKeyboard == view) return;
	if (gKeyboard && gKeyboard->Host()) gKeyboard->Host()->SetFocus(false);
	gKeyboard = view;
	if (gKeyboard && gKeyboard->Host()) gKeyboard->Host()->SetFocus(true);
}

// La pestaña activa solo es visible con la ventana abierta y el navegador en pantalla.
void ApplyTabVisibility() {
	for (auto& [id, tab] : gTabs) {
		if (!tab->Host()) continue;
		tab->Host()->WasHidden(!(gOverlayVisible && gContentVisible && id == gActiveTab));
	}
}

void OpenTab(int id, const std::string& url) {
	CefRefPtr<View> view = new View(id);
	gTabs[id] = view;

	CefWindowInfo window;
	window.SetAsWindowless(nullptr);

	CefBrowserSettings settings;
	settings.windowless_frame_rate = 60;
	settings.background_color = CefColorSetARGB(255, 255, 255, 255);

	CefBrowserHost::CreateBrowser(window, view, url, settings, nullptr, nullptr);
}

void CloseTab(int id) {
	auto found = gTabs.find(id);
	if (found == gTabs.end()) return;

	CefRefPtr<View> view = found->second;
	gTabs.erase(found);
	if (gActiveTab == id) gActiveTab = -1;
	if (view->Host()) view->Host()->CloseBrowser(true);
	ComposeAndPublish();
}

void ActivateTab(int id) {
	gActiveTab = id;
	ApplyTabVisibility();

	if (View* tab = ActiveTabView()) {
		if (tab->Host()) {
			tab->Host()->Invalidate(PET_VIEW);
			SetKeyboardTarget(tab);
		}
	}
	ComposeAndPublish();
}

void SetLayout(int x, int y, int width, int height, bool visible) {
	const bool resized = width != gContentRect.width || height != gContentRect.height;
	gContentRect = CefRect(x, y, width, height);
	gContentVisible = visible;

	if (resized) {
		for (auto& [id, tab] : gTabs) {
			if (tab->Host()) tab->Host()->WasResized();
		}
	}
	ApplyTabVisibility();
	if (!visible) SetKeyboardTarget(gShell.get());
	ComposeAndPublish();
}

bool ShellQueryHandler::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request, bool, CefRefPtr<Callback> callback) {
	// Solo la interfaz, nunca una página web, puede dar órdenes.
	if (!gShell || !gShell->Browser() || !browser->IsSame(gShell->Browser()) || !frame->IsMain() ||
		frame->GetURL().ToString().rfind(gShellUrl, 0) != 0) {
		return false;
	}

	CefRefPtr<CefValue> parsed = CefParseJSON(request, JSON_PARSER_RFC);
	if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return false;
	CefRefPtr<CefDictionaryValue> message = parsed->GetDictionary();
	const std::string command = message->GetString("cmd").ToString();

	View* active = ActiveTabView();
	CefRefPtr<CefBrowser> page = active ? active->Browser() : nullptr;

	if (command == "layout") {
		SetLayout(message->GetInt("x"), message->GetInt("y"), message->GetInt("w"), message->GetInt("h"), message->GetBool("visible"));
	}
	else if (command == "openTab") {
		OpenTab(message->GetInt("id"), message->GetString("url").ToString());
	}
	else if (command == "closeTab") {
		CloseTab(message->GetInt("id"));
	}
	else if (command == "activateTab") {
		ActivateTab(message->GetInt("id"));
	}
	else if (command == "navigate") {
		const std::string url = message->GetString("url").ToString();
		if (!IsAllowedUrl(url)) {
			AskForInternet(url, false);
		}
		else if (page) {
			page->GetMainFrame()->LoadURL(url);
			SetKeyboardTarget(active);
		}
	}
	else if (command == "back") {
		if (page) page->GoBack();
	}
	else if (command == "forward") {
		if (page) page->GoForward();
	}
	else if (command == "reload") {
		if (page) page->Reload();
	}
	else if (command == "stop") {
		if (page) page->StopLoad();
	}
	else if (command == "setInternet") {
		const bool enabled = message->GetBool("enabled");
		gInternetEnabled.store(enabled, std::memory_order_relaxed);
		ApplyNetworkPolicy(enabled);

		// Al volver a local, ninguna pestaña se queda en una web real ni con una conexión abierta.
		if (!enabled) {
			for (auto& [id, tab] : gTabs) {
				CefRefPtr<CefBrowser> tabBrowser = tab->Browser();
				if (!tabBrowser) continue;
				if (IsAllowedUrl(tabBrowser->GetMainFrame()->GetURL().ToString())) tabBrowser->Reload();
				else tabBrowser->GetMainFrame()->LoadURL(kHomeUrl);
			}
		}
	}
	else if (command == "logOff") {
		gHeader->closeRequest.store(1, std::memory_order_release);
	}
	else {
		return false;
	}

	callback->Success("");
	return true;
}

void CreateShell() {
	CefWindowInfo window;
	window.SetAsWindowless(nullptr);

	CefBrowserSettings settings;
	settings.windowless_frame_rate = 60;
	settings.background_color = CefColorSetARGB(255, 0, 0, 0);

	gShell = new View(-1);
	CefBrowserHost::CreateBrowser(window, gShell, gShellUrl, settings, nullptr, nullptr);
}

class App : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override {
		if (processType.empty()) {
			// Prototipo: todo por software, para entregar los fotogramas a la CPU de forma predecible.
			commandLine->AppendSwitch("disable-gpu");
			commandLine->AppendSwitch("disable-gpu-compositing");
		}
	}

	void OnContextInitialized() override {
		RegisterSites();
		ApplyNetworkPolicy(false);
		CreateShell();
	}

	// --- Proceso de renderizado ---
	void OnWebKitInitialized() override {
		router_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
	}

	void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
		// window.cefQuery solo existe en la interfaz, que se carga desde el disco. Las webs no lo ven.
		if (router_ && frame->GetURL().ToString().rfind("file:", 0) == 0) router_->OnContextCreated(browser, frame, context);
	}

	void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
		if (router_) router_->OnContextReleased(browser, frame, context);
	}

	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
		return router_ && router_->OnProcessMessageReceived(browser, frame, source, message);
	}

private:
	static void RegisterSites() {
		// Los del mod van los últimos: si hay dos con el mismo nombre, mandan ellos.
		RegisterSitesIn(PublicSitesFolder(), false);
		RegisterSitesIn(SystemSitesFolder(), true);
	}

	static void RegisterSitesIn(const fs::path& sites, bool isSystem) {
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(sites, error)) {
			if (!entry.is_directory()) continue;
			const CefString domain = entry.path().filename().wstring();
			std::string lowered = domain.ToString();
			for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			gLocalDomains.insert(lowered);

			// El correo solo es del sitio del mod: uno público con ese nombre no tendría acceso.
			CefRefPtr<SiteHandlerFactory> factory = new SiteHandlerFactory(entry.path(), isSystem && lowered == kMailDomain);
			CefRegisterSchemeHandlerFactory("https", lowered, factory);
			CefRegisterSchemeHandlerFactory("http", lowered, factory);
		}
	}

	CefRefPtr<CefMessageRouterRendererSide> router_;

	IMPLEMENT_REFCOUNTING(App);
};

// --- Entrada ---
View* HitTest(int x, int y) {
	if (gContentVisible && ActiveTabView() && x >= gContentRect.x && y >= gContentRect.y &&
		x < gContentRect.x + gContentRect.width && y < gContentRect.y + gContentRect.height) {
		return ActiveTabView();
	}
	return gShell.get();
}

CefMouseEvent MouseFor(View* view, const ef::Event& event) {
	CefMouseEvent mouse;
	mouse.x = view->IsShell() ? event.x : event.x - gContentRect.x;
	mouse.y = view->IsShell() ? event.y : event.y - gContentRect.y;
	mouse.modifiers = event.modifiers;
	return mouse;
}

void UpdateHover(View* target, const ef::Event& event) {
	if (gHover == target) return;
	if (gHover && gHover->Host()) gHover->Host()->SendMouseMoveEvent(MouseFor(gHover, event), true);
	gHover = target;
	PublishCursor();
}

// Devuelve true si el atajo era del navegador y no debe llegar a la página.
bool HandleShortcut(const ef::Event& event) {
	View* tab = ActiveTabView();
	CefRefPtr<CefBrowser> page = tab ? tab->Browser() : nullptr;
	const bool control = (event.modifiers & ef::kModControl) != 0;
	const bool shift = (event.modifiers & ef::kModShift) != 0;
	const bool alt = (event.modifiers & ef::kModAlt) != 0;

	if (event.a == VK_F5) {
		if (page && gContentVisible) page->Reload();
		return true;
	}
	if (alt && event.a == VK_LEFT) {
		if (page && gContentVisible) page->GoBack();
		return true;
	}
	if (alt && event.a == VK_RIGHT) {
		if (page && gContentVisible) page->GoForward();
		return true;
	}

	if (!control) return false;
	const char* action = nullptr;
	switch (event.a) {
	case 'T': action = "newTab"; break;
	case 'W': action = "closeTab"; break;
	case 'L': action = "focusAddress"; break;
	case VK_TAB: action = shift ? "previousTab" : "nextTab"; break;
	default: return false;
	}
	CallShell("shortcut", StringValue(action));
	if (event.a == 'L') SetKeyboardTarget(gShell.get());
	return true;
}

void HandleEvent(const ef::Event& event) {
	static DWORD lastClickTime = 0;
	static int lastClickX = 0;
	static int lastClickY = 0;
	static int lastClickButton = -1;
	static int clickCount = 1;

	const auto button = event.a == 1 ? MBT_MIDDLE : (event.a == 2 ? MBT_RIGHT : MBT_LEFT);

	switch (event.type) {
	case ef::EventType::MouseMove: {
		View* target = gCaptured ? gCaptured : HitTest(event.x, event.y);
		UpdateHover(target, event);
		if (target && target->Host()) target->Host()->SendMouseMoveEvent(MouseFor(target, event), false);
		break;
	}

	case ef::EventType::MouseDown: {
		View* target = HitTest(event.x, event.y);
		UpdateHover(target, event);
		gCaptured = target;
		SetKeyboardTarget(target);
		// Al navegar, Chromium puede cambiar el widget interno y este nace sin foco: sin él no se abren las listas.
		if (target && target->Host()) target->Host()->SetFocus(true);

		const DWORD now = GetTickCount();
		const bool repeat = event.a == lastClickButton && now - lastClickTime < GetDoubleClickTime() &&
			abs(event.x - lastClickX) <= 4 && abs(event.y - lastClickY) <= 4;
		clickCount = repeat ? clickCount + 1 : 1;
		lastClickTime = now;
		lastClickX = event.x;
		lastClickY = event.y;
		lastClickButton = event.a;

		if (target && target->Host()) target->Host()->SendMouseClickEvent(MouseFor(target, event), button, false, clickCount);
		break;
	}

	case ef::EventType::MouseUp: {
		View* target = gCaptured ? gCaptured : HitTest(event.x, event.y);
		gCaptured = nullptr;
		if (target && target->Host()) target->Host()->SendMouseClickEvent(MouseFor(target, event), button, true, clickCount);
		break;
	}

	case ef::EventType::Wheel: {
		View* target = HitTest(event.x, event.y);
		if (target && target->Host()) target->Host()->SendMouseWheelEvent(MouseFor(target, event), event.a, event.b);
		break;
	}

	case ef::EventType::KeyDown:
		if (HandleShortcut(event)) break;
		[[fallthrough]];
	case ef::EventType::KeyUp:
	case ef::EventType::Char: {
		if (!gKeyboard || !gKeyboard->Host()) break;
		CefKeyEvent key;
		key.type = event.type == ef::EventType::KeyDown ? KEYEVENT_RAWKEYDOWN
			: event.type == ef::EventType::KeyUp ? KEYEVENT_KEYUP : KEYEVENT_CHAR;
		key.windows_key_code = event.a;
		key.native_key_code = event.b;
		key.modifiers = event.modifiers;
		key.is_system_key = (event.modifiers & ef::kModAlt) != 0;
		gKeyboard->Host()->SendKeyEvent(key);
		break;
	}

	case ef::EventType::Show:
		gOverlayVisible = true;
		if (gShell->Host()) {
			gShell->Host()->WasHidden(false);
			gShell->Host()->Invalidate(PET_VIEW);
		}
		for (auto& [id, tab] : gTabs) {
			if (tab->Host()) tab->Host()->SetAudioMuted(false);
		}
		ApplyTabVisibility();
		if (gKeyboard && gKeyboard->Host()) gKeyboard->Host()->SetFocus(true);
		else SetKeyboardTarget(gShell.get());
		if (View* tab = ActiveTabView()) {
			if (tab->Host()) tab->Host()->Invalidate(PET_VIEW);
		}
		ComposeAndPublish();
		break;

	case ef::EventType::Hide:
		// Con el juego delante, las páginas no deben seguir sonando.
		gOverlayVisible = false;
		if (gKeyboard && gKeyboard->Host()) gKeyboard->Host()->SetFocus(false);
		if (gShell->Host()) gShell->Host()->WasHidden(true);
		for (auto& [id, tab] : gTabs) {
			if (tab->Host()) tab->Host()->SetAudioMuted(true);
		}
		ApplyTabVisibility();
		break;

	case ef::EventType::Invalidate:
		ComposeAndPublish();
		break;

	case ef::EventType::MailChanged: {
		gUnreadMail = event.a;
		CefRefPtr<CefValue> value = CefValue::Create();
		value->SetInt(gUnreadMail);
		CallShell("onMail", value);
		break;
	}

	case ef::EventType::Quit:
		break;
	}
}

void CloseEverything() {
	gClosingAll = true;
	if (gOpenBrowsers == 0) {
		SetEvent(gAllClosed);
		return;
	}
	for (auto& [id, tab] : gTabs) {
		if (tab->Host()) tab->Host()->CloseBrowser(true);
	}
	if (gShell && gShell->Host()) gShell->Host()->CloseBrowser(true);
}

void PumpEvents(HANDLE wake, HANDLE parent) {
	// Esperamos a la interfaz sin perder de vista al juego.
	HANDLE startWaits[] = { gShellReady, parent };
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

		while (read != write) {
			const ef::Event event = gHeader->ring[read % ef::kRingSize];
			++read;
			gHeader->ringRead.store(read, std::memory_order_release);

			if (event.type == ef::EventType::Quit) {
				SetEvent(gQuit);
				return;
			}
			CefPostTask(TID_UI, base::BindOnce(&HandleEvent, event));
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
	gShellUrl = commandLine->GetSwitchValue("ef-url").ToString();
	gMailPath = commandLine->GetSwitchValue("ef-mail").ToWString();
	if (section.empty() || gShellUrl.empty()) return 2;

	HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, section.c_str());
	if (!mapping) return 3;

	gHeader = static_cast<ef::Header*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));
	if (!gHeader || gHeader->magic != ef::kMagic || gHeader->protocol != ef::kProtocol) return 4;

	HANDLE wake = OpenEventW(EVENT_ALL_ACCESS, FALSE, (section + L"_wake").c_str());
	if (!wake) return 5;

	HANDLE parent = parentId.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(std::stoul(parentId)));

	gShellReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	gAllClosed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
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

	CefPostTask(TID_UI, base::BindOnce(&CloseEverything));
	WaitForSingleObject(gAllClosed, 3000);

	CefShutdown();
	return 0;
}
