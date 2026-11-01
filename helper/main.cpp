// 64-bit helper: runs Chromium without a window and publishes its frames in shared memory.
//
// There's one full-screen interface view (ui/shell.html: the desktop and browser window)
// and one view per tab. The active tab is composited into the space the interface reports.
// All the state in this file lives on CEF's UI thread, except the event ring.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
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

// The browser starts with local sites only. The real internet is only enabled from the interface,
// which asks for confirmation every time. It's read from several threads.
std::atomic<bool> gInternetEnabled{ false };
std::set<std::string> gLocalDomains;  // Only written at startup, before any browser exists.

// The kind of computer being used. Some sites can only be reached from one kind, and they're
// served on another thread, so it's atomic. The rest is only for the interface's timer.
std::atomic<int32_t> gSessionType{ static_cast<int32_t>(ef::SessionType::Normal) };
std::atomic<int64_t> gSessionId{ 0 };  // Changes each time a computer is used, so that pages can notice.

// An easter egg: a misspelled EyeFinder address offers a way onto the dark web from any computer,
// until that computer's session ends. Only that site can open it.
constexpr char kBackdoorSite[] = "www.eyefnder.info";
std::atomic<bool> gBackdoorOpen{ false };
bool gCloseTabsOnSession = false;

bool SessionReaches(int requiredSession) {
	if (gSessionType.load(std::memory_order_relaxed) == requiredSession) return true;
	return requiredSession == static_cast<int>(ef::SessionType::DarkWeb) && gBackdoorOpen.load(std::memory_order_relaxed);
}
int gSessionPrice = 0;
int gSessionSeconds = 0;
int gSessionCost = 0;

bool IsAllowedUrl(const std::string& url) {
	if (gInternetEnabled.load(std::memory_order_relaxed)) return true;

	CefURLParts parts;
	if (!CefParseURL(url, parts)) return false;

	std::string scheme = CefString(&parts.scheme).ToString();
	for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	// data:, blob: and about: don't go out to the network.
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

// On-screen keyboard: the interface draws it, and it's composited over the tab here.
bool gKeyboardOpen = false;
CefRect gKeyboardRect;
View* gKeyboardReturn = nullptr;       // Who gets the physical keyboard back when it closes.
CefRefPtr<CefFrame> gKeyboardFrame;    // The frame holding the field being edited.
bool gFieldQueryPending = false;
bool gFieldApplyPending = false;

constexpr char kMsgQueryField[] = "ef.queryField";
constexpr char kMsgFieldInfo[] = "ef.fieldInfo";
constexpr char kMsgApplyField[] = "ef.applyField";
constexpr char kMsgFieldApplied[] = "ef.fieldApplied";

void OnFieldInfo(CefRefPtr<CefFrame> frame, const std::string& json);
void OnFieldApplied(CefRefPtr<CefBrowser> browser, bool submit);

void ComposeAndPublish();
void SetKeyboardTarget(View* view);
void PublishCursor();
void CallShell(const std::string& function, CefRefPtr<CefValue> argument);
void AskForInternet(const std::string& url, bool newTab);
void SendSessionToShell();

CefRefPtr<CefValue> StringValue(const std::string& text) {
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetString(text);
	return value;
}

// --- Local sites: sites/<domain>/ is served as if it were that domain ---
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

// The inbox, for its own site only. Other sites can neither read nor change it:
// the custom header makes the browser ask for permission (CORS), which is never granted here.
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
		// /api/reply/<id>/<1 good or 2 bad>
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

// The helper and CEF are in browser. Nothing else is, so that players can add CEF by themselves.
// Two places for sites: system\sites belongs to the mod and is replaced when it updates. The sites
// folder next to the .asi is public: anyone can add to it, and nothing else touches it.
fs::path ModDirectory() { return ExecutableDirectory().parent_path(); }
fs::path SystemDirectory() { return ModDirectory() / L"system"; }
fs::path SystemSitesFolder() { return SystemDirectory() / L"sites"; }
fs::path PublicSitesFolder() { return ModDirectory() / L"sites"; }
fs::path SystemAdsFolder() { return SystemDirectory() / L"ads"; }
fs::path PublicAdsFolder() { return ModDirectory() / L"ads"; }

// Sites inside a hidden folder can only be reached in a dark web session.
constexpr wchar_t kHiddenFolder[] = L"hidden";

std::string ReadTextFile(const fs::path& path) {
	std::ifstream file(path, std::ios::binary);
	std::ostringstream contents;
	contents << file.rdbuf();
	return contents.str();
}

// Broken JSON in a site or ad must never break the list: it's treated as if it weren't there.
CefRefPtr<CefDictionaryValue> ReadJsonObject(const fs::path& path) {
	std::error_code error;
	if (!fs::is_regular_file(path, error)) return nullptr;
	CefRefPtr<CefValue> value = CefParseJSON(ReadTextFile(path), JSON_PARSER_ALLOW_TRAILING_COMMAS);
	return value && value->GetType() == VTYPE_DICTIONARY ? value->GetDictionary() : nullptr;
}

std::string StringField(CefRefPtr<CefDictionaryValue> object, const char* name) {
	return object && object->HasKey(name) && object->GetType(name) == VTYPE_STRING ? object->GetString(name).ToString() : std::string();
}

// The session a site needs, from "session" in its site.json, or -1 if any computer can reach it.
int RequiredSession(CefRefPtr<CefDictionaryValue> details) {
	const std::string session = StringField(details, "session");
	if (session == "pd") return static_cast<int>(ef::SessionType::Police);
	if (session == "dark_web") return static_cast<int>(ef::SessionType::DarkWeb);
	return -1;
}

std::string ListAsJson(const char* name, CefRefPtr<CefListValue> list) {
	CefRefPtr<CefDictionaryValue> root = CefDictionaryValue::Create();
	root->SetList(name, list);
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(root);
	return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string BuildGameStateJson() {
	ef::GameStateData state{};
	for (int attempt = 0; attempt < 64; ++attempt) {
		const uint32_t before = gHeader->gameStateSequence.load(std::memory_order_acquire);
		if (before % 2 != 0) continue;
		std::memcpy(&state, &gHeader->gameState, sizeof(state));
		std::atomic_thread_fence(std::memory_order_acquire);
		if (gHeader->gameStateSequence.load(std::memory_order_relaxed) == before) break;
	}
	state.zone[sizeof(state.zone) - 1] = '\0';
	state.lastMission[sizeof(state.lastMission) - 1] = '\0';

	CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
	result->SetBool("inGame", state.inGame != 0);
	result->SetInt("money", state.money);
	result->SetDouble("health", state.health);
	result->SetDouble("armor", state.armor);
	result->SetInt("wantedLevel", state.wantedLevel);
	result->SetInt("day", state.day);
	result->SetInt("hour", state.hour);
	result->SetInt("minute", state.minute);
	result->SetDouble("x", state.x);
	result->SetDouble("y", state.y);
	result->SetDouble("z", state.z);
	result->SetInt("interior", state.interior);
	result->SetBool("inVehicle", state.inVehicle != 0);
	result->SetBool("onMission", state.onMission != 0);
	result->SetInt("weather", state.weather);
	result->SetString("zone", state.zone);
	result->SetString("lastMission", state.lastMission);

	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(result);
	return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

// Only the values: the game's file also holds the mail, which isn't for every site.
std::string BuildValuesJson() {
	CefRefPtr<CefDictionaryValue> session = ReadJsonObject(fs::path(gMailPath));
	CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
	if (session && session->HasKey("values") && session->GetType("values") == VTYPE_DICTIONARY) {
		result->SetDictionary("values", session->GetDictionary("values"));
	}
	else {
		result->SetDictionary("values", CefDictionaryValue::Create());
	}

	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(result);
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

		// A mod site with the same name takes priority, so this one isn't available.
		std::error_code systemError;
		if (fs::exists(SystemSitesFolder() / entry.path().filename(), systemError)) continue;

		CefRefPtr<CefDictionaryValue> details = ReadJsonObject(entry.path() / L"site.json");
		if (details && details->HasKey("listed") && details->GetType("listed") == VTYPE_BOOL && !details->GetBool("listed")) continue;
		if (RequiredSession(details) >= 0) continue;  // The public list must not reveal these.

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

// The visible text of a page, for searching. Good enough for simple pages, and it's only a search hint.
std::string VisibleText(std::string html) {
	std::string lower = html;
	for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	std::string text;
	size_t position = 0;
	while (position < html.size()) {
		const size_t tag = html.find('<', position);
		text.append(html, position, tag == std::string::npos ? std::string::npos : tag - position);
		if (tag == std::string::npos) break;

		size_t end = html.find('>', tag);
		if (end == std::string::npos) break;

		for (const char* block : { "script", "style" }) {
			if (lower.compare(tag + 1, std::strlen(block), block) != 0) continue;
			const size_t close = lower.find(std::string("</") + block, tag);
			end = close == std::string::npos ? html.size() : lower.find('>', close);
			if (end == std::string::npos) end = html.size();
			break;
		}
		text += ' ';
		position = end == html.size() ? end : end + 1;
	}

	std::string collapsed;
	for (const char c : text) {
		const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
		if (space && (collapsed.empty() || collapsed.back() == ' ')) continue;
		collapsed += space ? ' ' : c;
	}

	// Cut without splitting a UTF-8 character.
	constexpr size_t kMaxText = 4000;
	if (collapsed.size() > kMaxText) {
		size_t cut = kMaxText;
		while (cut > 0 && (static_cast<unsigned char>(collapsed[cut]) & 0xC0) == 0x80) --cut;
		collapsed.resize(cut);
	}
	return collapsed;
}

CefRefPtr<CefDictionaryValue> SearchEntry(const fs::path& folder) {
	const std::string domain = folder.filename().string();
	CefRefPtr<CefDictionaryValue> details = ReadJsonObject(folder / L"site.json");
	const std::string title = StringField(details, "title");

	CefRefPtr<CefDictionaryValue> site = CefDictionaryValue::Create();
	site->SetString("domain", domain);
	site->SetString("url", "https://" + domain + "/");
	site->SetString("title", title.empty() ? domain : title);
	site->SetString("description", StringField(details, "description"));
	site->SetString("keywords", StringField(details, "keywords"));
	site->SetString("text", VisibleText(ReadTextFile(folder / L"index.html")));
	return site;
}

// Everything search can find in the sites folder. It leaves out what the home page leaves out: sites
// marked "listed": false, which promise to be reached only by typing their address, and restricted ones.
std::string BuildSearchJson() {
	CefRefPtr<CefListValue> list = CefListValue::Create();
	size_t index = 0;

	std::error_code error;
	for (const auto& entry : fs::directory_iterator(PublicSitesFolder(), error)) {
		if (!entry.is_directory() || entry.path().filename().string().find('.') == std::string::npos) continue;

		std::error_code systemError;
		if (fs::exists(SystemSitesFolder() / entry.path().filename(), systemError)) continue;
		CefRefPtr<CefDictionaryValue> details = ReadJsonObject(entry.path() / L"site.json");
		if (details && details->HasKey("listed") && details->GetType("listed") == VTYPE_BOOL && !details->GetBool("listed")) continue;
		if (RequiredSession(details) >= 0) continue;

		list->SetDictionary(index++, SearchEntry(entry.path()));
	}
	return ListAsJson("sites", list);
}

std::string BuildHiddenJson() {
	CefRefPtr<CefListValue> list = CefListValue::Create();
	size_t index = 0;
	std::set<std::string> seen;

	// The mod's go first: if two share a name, they win, as they do when they're served.
	for (const fs::path& folder : { SystemSitesFolder() / kHiddenFolder, PublicSitesFolder() / kHiddenFolder }) {
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(folder, error)) {
			if (!entry.is_directory() || !seen.insert(entry.path().filename().string()).second) continue;
			list->SetDictionary(index++, SearchEntry(entry.path()));
		}
	}
	return ListAsJson("sites", list);
}

std::string BuildBrowserJson() {
	const int type = gSessionType.load(std::memory_order_relaxed);
	CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
	result->SetBool("internet", gInternetEnabled.load(std::memory_order_relaxed));
	result->SetString("session", type == static_cast<int>(ef::SessionType::Police) ? "pd" : type == static_cast<int>(ef::SessionType::DarkWeb) ? "dark_web" : "normal");
	result->SetString("sessionId", std::to_string(gSessionId.load(std::memory_order_relaxed)));

	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(result);
	return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
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

			// A link can only be a web address: no javascript:.
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

// "<ad folder>/<file>", with no way out of the ad folders.
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

// What a local site can ask of the game. Real sites can't get here: these domains are only
// served from disk, and the custom header stops another origin from making the request.
CefRefPtr<CefResourceHandler> HandleGameApi(const std::string& domain, const fs::path& root, const std::string& path, CefRefPtr<CefRequest> request) {
	// Public information: the site list and ads are read on every request.
	if (path == "/_game/sa.js") return ServeFile(SystemDirectory() / L"ui" / L"sa.js", "text/javascript");
	if (path == "/_game/sites.json") return JsonResponse(200, BuildSitesJson());
	if (path == "/_game/ads.json") return JsonResponse(200, BuildAdsJson());
	if (path.rfind("/_game/ad/", 0) == 0) return ServeAdImage(path.substr(10));

	if (request->GetHeaderByName("X-EyeFinder").empty()) return JsonResponse(403, "{}");

	if (path == "/_game/state.json") return JsonResponse(200, BuildGameStateJson());
	if (path == "/_game/values.json") return JsonResponse(200, BuildValuesJson());
	if (path == "/_game/browser.json") return JsonResponse(200, BuildBrowserJson());
	if (path == "/_game/search.json") return JsonResponse(200, BuildSearchJson());
	if (path == "/_game/hidden.json") {
		// The list itself is a secret: it only exists on a dark web computer.
		if (!SessionReaches(static_cast<int>(ef::SessionType::DarkWeb))) return JsonResponse(403, "{}");
		return JsonResponse(200, BuildHiddenJson());
	}
	if (path == "/_game/backdoor" && domain == kBackdoorSite && request->GetMethod().ToString() == "POST") {
		gBackdoorOpen.store(true, std::memory_order_relaxed);
		return JsonResponse(200, "{}");
	}

	if (path.rfind("/_game/value/", 0) == 0 && request->GetMethod().ToString() == "POST") {
		const std::string key = path.substr(13);
		const std::string value = CefURIDecode(request->GetHeaderByName("X-EyeFinder-Value"), true,
			static_cast<cef_uri_unescape_rule_t>(UU_SPACES | UU_PATH_SEPARATORS | UU_URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS)).ToString();

		const bool validKey = !key.empty() && key.size() < ef::kValueKeySize &&
			key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos;
		if (!validKey || value.size() >= ef::kValueTextSize) return JsonResponse(400, "{}");

		const uint32_t write = gHeader->valueOpWrite.load(std::memory_order_relaxed);
		if (write - gHeader->valueOpRead.load(std::memory_order_acquire) >= ef::kValueOpRingSize) return JsonResponse(503, "{}");
		ef::ValueOp& entry = gHeader->valueOps[write % ef::kValueOpRingSize];
		std::memset(&entry, 0, sizeof(entry));
		std::memcpy(entry.key, key.data(), key.size());
		std::memcpy(entry.value, value.data(), value.size());
		gHeader->valueOpWrite.store(write + 1, std::memory_order_release);
		return JsonResponse(200, "{}");
	}

	if (path.rfind("/_game/script/", 0) == 0 && request->GetMethod().ToString() == "POST") {
		// Only simple names, and only from the site's own scripts folder.
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
	SiteHandlerFactory(fs::path root, bool isMailSite, int requiredSession) : root_(std::move(root)), isMailSite_(isMailSite), requiredSession_(requiredSession) {}

	CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, const CefString&, CefRefPtr<CefRequest> request) override {
		// Checked first and for every request, so that links, scripts and the game API are all covered.
		if (requiredSession_ >= 0 && !SessionReaches(requiredSession_)) return Forbidden();

		CefURLParts parts;
		if (!CefParseURL(request->GetURL(), parts)) return nullptr;

		// A site can't leave its folder.
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

	static CefRefPtr<CefResourceHandler> Forbidden() {
		static const char kPage[] = "<body style='font:18px Tahoma,sans-serif;margin:40px'><h2>Connection refused</h2><p>This network can't be reached from this computer.</p></body>";
		CefRefPtr<CefStreamReader> stream = CefStreamReader::CreateForData(const_cast<char*>(kPage), sizeof(kPage) - 1);
		return new CefStreamResourceHandler(403, "Forbidden", "text/html", CefResponse::HeaderMap(), stream);
	}

	fs::path root_;
	const bool isMailSite_;
	const int requiredSession_;

	IMPLEMENT_REFCOUNTING(SiteHandlerFactory);
};

// --- Commands from the interface ---
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
		const std::string name = message->GetName().ToString();
		if (!IsShell() && name == kMsgFieldInfo) {
			OnFieldInfo(frame, message->GetArgumentList()->GetString(0).ToString());
			return true;
		}
		if (!IsShell() && name == kMsgFieldApplied) {
			OnFieldApplied(browser, message->GetArgumentList()->GetBool(0));
			return true;
		}
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

	// --- Lifetime ---
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
		if (gKeyboardReturn == this) gKeyboardReturn = nullptr;

		if (--gOpenBrowsers == 0 && gClosingAll) SetEvent(gAllClosed);
	}

	// Popups open as new tabs.
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

	// --- Navigation ---
	bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool) override {
		const std::string url = request->GetURL().ToString();

		if (IsShell()) {
			// The interface can give the helper commands, so it can never leave its page.
			if (url.rfind(gShellUrl, 0) != 0) return true;
			router_->OnBeforeBrowse(browser, frame);
			return false;
		}

		// Without a sandbox, a page must not be able to open files from disk.
		if (url.rfind("file:", 0) == 0) return true;

		if (!IsAllowedUrl(url)) {
			// Only ask if the player requested it: a page can't keep insisting by itself.
			if (frame->IsMain() && userGesture) AskForInternet(url, false);
			return true;
		}
		return false;
	}

	CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>, bool, bool, const CefString&, bool&) override {
		return IsShell() ? nullptr : this;
	}

	// Every resource, not just pages: a local site must not be able to load anything from the internet.
	CefResourceRequestHandler::ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback>) override {
		return IsAllowedUrl(request->GetURL().ToString()) ? RV_CONTINUE : RV_CANCEL;
	}

	void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString&) override {
		if (router_) router_->OnRenderProcessTerminated(browser);
	}

	// --- State shown by the interface ---
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

	// Zoom belongs to the tab and survives navigation. Chromium stores it per domain, so it's applied again.
	void ApplyZoom() {
		if (Host()) Host()->SetZoomLevel(std::log(zoomPercent_ / 100.0) / std::log(1.2));
	}

	void ChangeZoom(int direction) {
		static const int kSteps[] = { 50, 67, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };
		if (direction == 0) {
			zoomPercent_ = 100;
		}
		else if (direction > 0) {
			for (const int step : kSteps) {
				if (step > zoomPercent_) {
					zoomPercent_ = step;
					break;
				}
			}
		}
		else {
			for (auto step = std::rbegin(kSteps); step != std::rend(kSteps); ++step) {
				if (*step < zoomPercent_) {
					zoomPercent_ = *step;
					break;
				}
			}
		}
		ApplyZoom();
		SendState();
	}

	void OnLoadEnd(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int) override {
		if (!IsShell() && frame->IsMain()) ApplyZoom();
		if (IsShell() && frame->IsMain()) {
			CefRefPtr<CefValue> value = CefValue::Create();
			value->SetInt(gUnreadMail);
			CallShell("onMail", value);
			SendSessionToShell();
		}
	}

	void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode code, const CefString& text, const CefString& url) override {
		if (code == ERR_ABORTED || !frame->IsMain() || IsShell()) return;
		const std::string html = "<body style='font:18px Tahoma,sans-serif;margin:40px'><h2>This page could not be loaded</h2><p>" +
			text.ToString() + " (" + std::to_string(static_cast<int>(code)) + ")</p></body>";
		frame->LoadURL(CefURIEncode("data:text/html," + html, false));
		url_ = url.ToString();
	}

	// --- No context menu or native dialogs for now: there's no window to show them in ---
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
		state->SetInt("zoom", zoomPercent_);
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
	int zoomPercent_ = 100;

	IMPLEMENT_REFCOUNTING(View);
};

void CallShell(const std::string& function, CefRefPtr<CefValue> argument) {
	if (!gShell || !gShell->Browser()) return;
	const std::string json = CefWriteJSON(argument, JSON_WRITER_DEFAULT).ToString();
	gShell->Browser()->GetMainFrame()->ExecuteJavaScript("ef." + function + "(" + json + ")", gShellUrl, 0);
}

void SendSessionToShell() {
	const int type = gSessionType.load(std::memory_order_relaxed);
	CefRefPtr<CefDictionaryValue> details = CefDictionaryValue::Create();
	details->SetString("type", type == static_cast<int>(ef::SessionType::Police) ? "pd" : type == static_cast<int>(ef::SessionType::DarkWeb) ? "dark_web" : "normal");
	details->SetInt("price", gSessionPrice);
	details->SetInt("seconds", gSessionSeconds);
	details->SetInt("cost", gSessionCost);
	details->SetBool("closeTabs", gCloseTabsOnSession);
	gCloseTabsOnSession = false;
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(details);
	CallShell("onSession", value);
}

// The second barrier, at network level: in local mode, everything goes through a proxy that doesn't exist.
// It's needed because WebSockets skip OnBeforeResourceLoad. Local sites don't use the network, so they
// keep working. <-loopback> also covers localhost, which Chromium excludes from proxies.
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

	// WebRTC can send UDP around the proxy, and reveal the player's real IP address that way.
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

	// The keyboard goes over the tab, which stays visible above it.
	if (gKeyboardOpen) {
		const int left = std::max(gKeyboardRect.x, 0);
		const int top = std::max(gKeyboardRect.y, 0);
		const int right = std::min(gKeyboardRect.x + gKeyboardRect.width, viewWidth);
		const int bottom = std::min(gKeyboardRect.y + gKeyboardRect.height, viewHeight);
		if (right > left) {
			for (int y = top; y < bottom; ++y) {
				const size_t offset = (size_t(y) * viewWidth + left) * 4;
				std::memcpy(target + offset, gShell->Pixels() + offset, size_t(right - left) * 4);
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

// The active tab is only visible with the window open and the browser on screen.
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
			tab->ApplyZoom();
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

// The page only changes on Go, and only the field the pad clicked.
void SetKeyboardOpen(bool open, const CefRect& rect) {
	if (open) {
		gKeyboardRect = rect;
		if (!gKeyboardOpen) {
			// The tab keeps its focus, so that the page doesn't see the field being left.
			gKeyboardReturn = gKeyboard == gShell.get() ? nullptr : gKeyboard;
			gKeyboard = gShell.get();
			if (gShell->Host()) gShell->Host()->SetFocus(true);
		}
	}
	else if (gKeyboardOpen) {
		if (gKeyboardReturn) SetKeyboardTarget(gKeyboardReturn);
		gKeyboardReturn = nullptr;
		gKeyboardFrame = nullptr;
	}

	gKeyboardOpen = open;
	gHeader->keyboardOpen.store(open ? 1 : 0, std::memory_order_release);
	ComposeAndPublish();
}

void ApplyKeyboardText(const std::string& text) {
	if (!gKeyboardFrame || !gKeyboardFrame->IsValid()) return;

	CefRefPtr<CefProcessMessage> request = CefProcessMessage::Create(kMsgApplyField);
	request->GetArgumentList()->SetString(0, text);
	gFieldApplyPending = true;
	gKeyboardFrame->SendProcessMessage(PID_RENDERER, request);
}

// Only answers a question of ours, after a pad click. The rest comes from the page, so it's only text.
void OnFieldInfo(CefRefPtr<CefFrame> frame, const std::string& json) {
	if (!gFieldQueryPending) return;
	gFieldQueryPending = false;
	if (gKeyboardOpen || json.empty()) return;

	CefRefPtr<CefValue> parsed = CefParseJSON(json, JSON_PARSER_RFC);
	if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
	CefRefPtr<CefDictionaryValue> field = parsed->GetDictionary();

	CefRefPtr<CefDictionaryValue> details = CefDictionaryValue::Create();
	details->SetString("type", StringField(field, "type"));
	details->SetString("text", StringField(field, "text"));
	details->SetString("label", StringField(field, "label"));
	details->SetInt("maxLength", field->HasKey("maxLength") && field->GetType("maxLength") == VTYPE_INT ? field->GetInt("maxLength") : 0);

	gKeyboardFrame = frame;
	CefRefPtr<CefValue> value = CefValue::Create();
	value->SetDictionary(details);
	CallShell("keyboardOpen", value);
}

void OnFieldApplied(CefRefPtr<CefBrowser> browser, bool submit) {
	if (!gFieldApplyPending) return;
	gFieldApplyPending = false;
	if (!submit || !browser->GetHost()) return;

	// A real Enter: the page treats it as if the player had pressed it.
	CefKeyEvent key;
	key.windows_key_code = VK_RETURN;
	key.type = KEYEVENT_RAWKEYDOWN;
	browser->GetHost()->SendKeyEvent(key);
	key.type = KEYEVENT_CHAR;
	key.windows_key_code = '\r';
	browser->GetHost()->SendKeyEvent(key);
	key.type = KEYEVENT_KEYUP;
	key.windows_key_code = VK_RETURN;
	browser->GetHost()->SendKeyEvent(key);
}

// Asked a moment after the click, so that Chromium has already moved the focus.
void QueryFocusedField(int tabId) {
	if (gKeyboardOpen) return;

	if (tabId < 0) {
		CallShell("padClicked", StringValue(""));
		return;
	}

	auto found = gTabs.find(tabId);
	CefRefPtr<CefBrowser> browser = found == gTabs.end() ? nullptr : found->second->Browser();
	if (!browser) return;

	CefRefPtr<CefFrame> frame = browser->GetFocusedFrame();
	if (!frame) frame = browser->GetMainFrame();
	if (!frame || !frame->IsValid()) return;

	gFieldQueryPending = true;
	frame->SendProcessMessage(PID_RENDERER, CefProcessMessage::Create(kMsgQueryField));
}

bool ShellQueryHandler::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t, const CefString& request, bool, CefRefPtr<Callback> callback) {
	// Only the interface, never a web page, can give commands.
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
	else if (command == "zoom") {
		if (active) active->ChangeZoom(message->GetInt("direction"));
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

		// On going back to local, no tab stays on a real site or keeps a connection open.
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
	else if (command == "keyboard") {
		const bool open = message->GetBool("open");
		SetKeyboardOpen(open, open ? CefRect(message->GetInt("x"), message->GetInt("y"), message->GetInt("w"), message->GetInt("h")) : CefRect());
	}
	else if (command == "keyboardApply") {
		ApplyKeyboardText(message->GetString("text").ToString());
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

// These run inside the page, in the render process, to read and write the focused field.
constexpr char kActiveFieldScript[] = R"js(
function efActiveField() {
  let element = document.activeElement;
  while (element && element.shadowRoot && element.shadowRoot.activeElement) element = element.shadowRoot.activeElement;
  if (!element || element.disabled || element.readOnly) return null;
  if (element.tagName === 'TEXTAREA') return element;
  if (element.tagName === 'INPUT') {
    return ['text', 'search', 'url', 'email', 'tel', 'password', 'number'].includes((element.type || 'text').toLowerCase()) ? element : null;
  }
  return element.isContentEditable ? element : null;
}
)js";

constexpr char kQueryFieldScript[] = R"js((function () {
  const element = efActiveField();
  if (!element) return '';
  const isInput = element.tagName === 'INPUT';
  const isEditable = !isInput && element.tagName !== 'TEXTAREA';
  return JSON.stringify({
    type: isInput ? element.type.toLowerCase() : 'multiline',
    text: isEditable ? element.innerText : element.value,
    label: (element.getAttribute('aria-label') || element.placeholder || '') + '',
    maxLength: element.maxLength > 0 ? element.maxLength : 0,
  });
})())js";

// insertText is like really typing: pages that listen for every change notice it too.
// Returns 'submit' if the field should get Enter: not a multi-line one, and not in a form
// that's still missing another field, like a username without its password.
constexpr char kApplyFieldScript[] = R"js((function (text) {
  const element = efActiveField();
  if (!element) return '';
  const isField = element.tagName === 'INPUT' || element.tagName === 'TEXTAREA';

  element.focus();
  if (isField) { try { element.select(); } catch (error) {} }
  else window.getSelection().selectAllChildren(element);

  try { document.execCommand(text === '' ? 'delete' : 'insertText', false, text); } catch (error) {}

  if (isField && element.value !== text) {
    const prototype = element.tagName === 'INPUT' ? HTMLInputElement.prototype : HTMLTextAreaElement.prototype;
    Object.getOwnPropertyDescriptor(prototype, 'value').set.call(element, text);
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
  }

  if (element.tagName !== 'INPUT') return '';
  if (element.form) {
    for (const other of element.form.elements) {
      const needsText = other !== element && other.tagName === 'INPUT' && !other.disabled && !other.readOnly &&
        ['text', 'search', 'url', 'email', 'tel', 'password', 'number'].includes(other.type) && other.offsetParent !== null;
      if (needsText && other.value === '') return '';
    }
  }
  return 'submit';
}))js";

class App : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
public:
	CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
	CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }

	void OnBeforeCommandLineProcessing(const CefString& processType, CefRefPtr<CefCommandLine> commandLine) override {
		if (processType.empty()) {
			// Prototype: everything in software, to hand frames to the CPU predictably.
			commandLine->AppendSwitch("disable-gpu");
			commandLine->AppendSwitch("disable-gpu-compositing");
		}
	}

	void OnContextInitialized() override {
		RegisterSites();
		ApplyNetworkPolicy(false);
		CreateShell();
	}

	// --- Render process ---
	void OnWebKitInitialized() override {
		router_ = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
	}

	void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
		// window.cefQuery only exists in the interface, which loads from disk. Websites can't see it.
		if (router_ && frame->GetURL().ToString().rfind("file:", 0) == 0) router_->OnContextCreated(browser, frame, context);
	}

	void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override {
		if (router_) router_->OnContextReleased(browser, frame, context);
	}

	bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
		const std::string name = message->GetName().ToString();

		if (name == kMsgQueryField) {
			CefRefPtr<CefProcessMessage> reply = CefProcessMessage::Create(kMsgFieldInfo);
			reply->GetArgumentList()->SetString(0, RunInFrame(frame, kQueryFieldScript));
			frame->SendProcessMessage(PID_BROWSER, reply);
			return true;
		}

		if (name == kMsgApplyField) {
			CefRefPtr<CefValue> text = CefValue::Create();
			text->SetString(message->GetArgumentList()->GetString(0));
			const std::string call = std::string(kApplyFieldScript) + "(" + CefWriteJSON(text, JSON_WRITER_DEFAULT).ToString() + ")";

			CefRefPtr<CefProcessMessage> reply = CefProcessMessage::Create(kMsgFieldApplied);
			reply->GetArgumentList()->SetBool(0, RunInFrame(frame, call) == "submit");
			frame->SendProcessMessage(PID_BROWSER, reply);
			return true;
		}

		return router_ && router_->OnProcessMessageReceived(browser, frame, source, message);
	}

private:
	// Everything goes inside a function, so that efActiveField doesn't stay in the page.
	static std::string RunInFrame(CefRefPtr<CefFrame> frame, const std::string& expression) {
		CefRefPtr<CefV8Context> context = frame->GetV8Context();
		CefRefPtr<CefV8Value> result;
		CefRefPtr<CefV8Exception> exception;
		const std::string code = std::string("(function () {") + kActiveFieldScript + "return " + expression + ";\n})()";
		if (!context || !context->Eval(code, CefString(), 0, result, exception) || !result || !result->IsString()) return {};
		return result->GetStringValue().ToString();
	}

	static void RegisterSites() {
		// The mod's go last: if two share a name, they win.
		RegisterSitesIn(PublicSitesFolder(), false);
		RegisterSitesIn(SystemSitesFolder(), true);
	}

	static void RegisterSitesIn(const fs::path& sites, bool isSystem) {
		std::error_code error;
		for (const auto& entry : fs::directory_iterator(sites, error)) {
			if (!entry.is_directory() || entry.path().filename() == kHiddenFolder) continue;
			RegisterSite(entry.path(), isSystem, RequiredSession(ReadJsonObject(entry.path() / L"site.json")));
		}

		std::error_code hiddenError;
		for (const auto& entry : fs::directory_iterator(sites / kHiddenFolder, hiddenError)) {
			if (entry.is_directory()) RegisterSite(entry.path(), false, static_cast<int>(ef::SessionType::DarkWeb));
		}
	}

	static void RegisterSite(const fs::path& folder, bool isSystem, int requiredSession) {
		// Other addresses for the same site, with and without www, for the mod's own sites only.
		if (isSystem) {
			CefRefPtr<CefDictionaryValue> details = ReadJsonObject(folder / L"site.json");
			if (details && details->HasKey("aliases") && details->GetType("aliases") == VTYPE_LIST) {
				CefRefPtr<CefListValue> aliases = details->GetList("aliases");
				CefRefPtr<SiteHandlerFactory> factory = new SiteHandlerFactory(folder, false, requiredSession);
				for (size_t i = 0; i < aliases->GetSize(); ++i) {
					const std::string alias = aliases->GetString(i).ToString();
					for (const std::string& host : { alias, "www." + alias }) {
						gLocalDomains.insert(host);
						CefRegisterSchemeHandlerFactory("https", host, factory);
						CefRegisterSchemeHandlerFactory("http", host, factory);
					}
				}
			}
		}

		{
			const CefString domain = folder.filename().wstring();
			std::string lowered = domain.ToString();
			for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			gLocalDomains.insert(lowered);

			// Mail belongs only to the mod's site: a public one with that name gets no access.
			CefRefPtr<SiteHandlerFactory> factory = new SiteHandlerFactory(folder, isSystem && lowered == kMailDomain, requiredSession);
			CefRegisterSchemeHandlerFactory("https", lowered, factory);
			CefRegisterSchemeHandlerFactory("http", lowered, factory);
		}
	}

	CefRefPtr<CefMessageRouterRendererSide> router_;

	IMPLEMENT_REFCOUNTING(App);
};

// --- Input ---
View* HitTest(int x, int y) {
	if (gKeyboardOpen && gKeyboardRect.Contains(x, y)) return gShell.get();
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

// Returns true if the shortcut was the browser's and must not reach the page.
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

	// Ctrl with +, - or 0, including those on the numeric keypad.
	if (tab && gContentVisible) {
		if (event.a == VK_OEM_PLUS || event.a == VK_ADD) {
			tab->ChangeZoom(1);
			return true;
		}
		if (event.a == VK_OEM_MINUS || event.a == VK_SUBTRACT) {
			tab->ChangeZoom(-1);
			return true;
		}
		if (event.a == '0' || event.a == VK_NUMPAD0) {
			tab->ChangeZoom(0);
			return true;
		}
	}

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
	static bool padClick = false;

	const auto button = event.a == 1 ? MBT_MIDDLE : (event.a == 2 ? MBT_RIGHT : MBT_LEFT);

	switch (event.type) {
	case ef::EventType::MouseMove: {
		View* target = gCaptured ? gCaptured : HitTest(event.x, event.y);
		UpdateHover(target, event);
		if (target && target->Host()) target->Host()->SendMouseMoveEvent(MouseFor(target, event), false);
		break;
	}

	case ef::EventType::MouseDown: {
		padClick = event.a == 0 && event.b == 1;
		// A click outside the on-screen keyboard leaves it, without changing anything.
		if (gKeyboardOpen && !gKeyboardRect.Contains(event.x, event.y)) CallShell("keyboardCancel", StringValue(""));

		View* target = HitTest(event.x, event.y);
		UpdateHover(target, event);
		gCaptured = target;
		SetKeyboardTarget(target);
		// Navigating can replace Chromium's internal widget, which starts unfocused, and dropdowns won't open without focus.
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

		if (padClick && event.a == 0 && target) CefPostDelayedTask(TID_UI, base::BindOnce(&QueryFocusedField, target->TabId()), 60);
		padClick = false;
		break;
	}

	case ef::EventType::Wheel: {
		View* target = HitTest(event.x, event.y);
		// Ctrl and the wheel over a page: zoom.
		if ((event.modifiers & ef::kModControl) && target && !target->IsShell()) {
			target->ChangeZoom(event.b > 0 ? 1 : -1);
			break;
		}
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
		// With the game in front, pages must not keep playing sound.
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

	case ef::EventType::PadKeyboard: {
		static const char* const kActions[] = { "up", "down", "left", "right", "press", "close", "backspace", "space", "caretLeft", "caretRight", "shift", "go" };
		if (event.a >= 0 && event.a < static_cast<int>(std::size(kActions))) CallShell("padKeyboard", StringValue(kActions[event.a]));
		break;
	}

	case ef::EventType::Session:
		gSessionType.store(event.a, std::memory_order_relaxed);
		gSessionId.store(static_cast<int64_t>(GetTickCount64()), std::memory_order_relaxed);
		// The way in doesn't outlast the session, and a tab left on the dark web mustn't stay open either.
		gCloseTabsOnSession = gBackdoorOpen.exchange(false, std::memory_order_relaxed);
		gSessionPrice = event.b;
		gSessionSeconds = 0;
		gSessionCost = 0;
		SendSessionToShell();
		break;

	case ef::EventType::SessionTime:
		gSessionSeconds = event.a;
		gSessionCost = event.b;
		SendSessionToShell();
		break;

	case ef::EventType::Quit:
		break;
	}
}

void CloseEverything() {
	gClosingAll = true;
	gKeyboardFrame = nullptr;
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
	// Wait for the interface without losing sight of the game.
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

	// Chromium's subprocesses (renderer, utilities) end up here.
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
