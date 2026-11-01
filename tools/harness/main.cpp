// Tests the helper, the protocol, the interface and the rendering without the game, in a 32-bit process.
//
//   harness <helper.exe> <shell url> <output directory>
//
// The coordinates match ui/shell.html at 1280x720.
// With a DXVK d3d9.dll next to the executable, it also tests that path.
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "AddressTypo.h"
#include "BrowserClient.h"
#include "EyeFinderApi.h"
#include "KeyConfig.h"
#include "Locations.h"
#include "MailStore.h"
#include "OverlayRenderer.h"
#include "PagerRenderer.h"

namespace {

constexpr int gWidth = 1280;
constexpr int gHeight = 720;

// Interface
constexpr POINT kIconBin = { 72, 72 };
constexpr POINT kIconInternet = { 72, 194 };
constexpr POINT kIconSecond = { 72, 317 };  // The session's own browser, or else Mail.
constexpr POINT kIconLogOff = { 72, 439 };  // In a normal session.
constexpr POINT kBack = { 186, 51 };
constexpr POINT kForward = { 226, 51 };
constexpr POINT kAddress = { 640, 51 };
constexpr POINT kTab1 = { 240, 87 };
constexpr POINT kTab2 = { 430, 87 };
constexpr POINT kNewTabWithOneTab = { 373, 87 };
constexpr POINT kMinimize = { 1056, 15 };
constexpr POINT kRestore = { 60, 680 };
constexpr POINT kMailButton = { 346, 51 };
constexpr POINT kPublicSiteAd = { 640, 442 };
constexpr POINT kNetMode = { 1070, 51 };
constexpr POINT kConnect = { 545, 437 };
constexpr POINT kStayLocal = { 735, 437 };

// mail.eyefinder.info
constexpr POINT kFirstMailRow = { 300, 190 };
constexpr POINT kZoomIn = { 1096, 703 };
constexpr POINT kZoomOut = { 1020, 703 };
constexpr POINT kReplyGood = { 700, 655 };

// tests.html: the CLEO button
constexpr POINT kRunCleo = { 290, 372 };
constexpr POINT kDeleteMail = { 1072, 175 };

// tests.html, from the corner of the content area
constexpr POINT kClickMe = { 258, 204 };
constexpr POINT kTextField = { 368, 260 };
constexpr POINT kDropdown = { 286, 316 };
constexpr POINT kDropdownThird = { 286, 400 };

// Colors expected in the captures.
constexpr uint32_t kDesktopColor = 0x027676;
constexpr uint32_t kDropdownHighlight = 0x1967D2;
constexpr uint32_t kTitleBarColor = 0x073DA5;

struct Stats {
	int uploads = 0;
	int draws = 0;
	double uploadMs = 0.0;
	double uploadMaxMs = 0.0;
};

BrowserClient gClient;
OverlayRenderer gRenderer;
PagerRenderer gPager;
MailStore gStore;
DWORD gLastFrame = 0;
D3DPRESENT_PARAMETERS gParameters{};
int gDeviceLosses = 0;
int gDeviceLossesAtLastShot = 0;
IDirect3DDevice9* gDevice = nullptr;
Stats gStats;
std::string gOutput;
float gCursorX = 40.0f;
float gCursorY = 40.0f;
int gFailures = 0;
std::vector<uint32_t> gLastShot;

// Checks a pixel of the last capture: a passing test isn't enough if what's drawn is wrong.
void ExpectPixel(const char* what, POINT point, uint32_t expected) {
	const uint32_t actual = gLastShot.empty() ? 0 : gLastShot[size_t(point.y) * gWidth + point.x] & 0xFFFFFF;
	if (actual != expected) {
		printf("FAIL: %s: pixel %06X, expected %06X\n", what, actual, expected);
		++gFailures;
	}
}

void ExpectIconDrawn(const char* what, POINT center) {
	const uint32_t background = gLastShot.empty() ? 0 : gLastShot[size_t(center.y - 30) * gWidth + center.x - 36] & 0xFFFFFF;
	const uint32_t actual = gLastShot.empty() ? 0 : gLastShot[size_t(center.y) * gWidth + center.x] & 0xFFFFFF;
	if (actual == background) {
		printf("FAIL: the %s icon isn't drawn\n", what);
		++gFailures;
	}
}

// Returns false while it can't draw. It does what the plugin does: release the texture and ask for a frame.
bool DeviceReady() {
	const HRESULT state = gDevice->TestCooperativeLevel();
	if (state == D3D_OK) return true;

	if (state == D3DERR_DEVICENOTRESET) {
		gRenderer.Release();
		if (SUCCEEDED(gDevice->Reset(&gParameters))) {
			++gDeviceLosses;
			printf("device lost and recovered\n");
			gClient.Send({ ef::EventType::Invalidate });
			return true;
		}
	}
	return false;
}

void RenderFrame() {
	if (!DeviceReady()) {
		Sleep(50);
		return;
	}

	if (const uint8_t* pixels = gClient.AcquireFrame()) {
		if (gRenderer.Upload(gDevice, pixels, gClient.Width(), gClient.Height(), gClient.Stride())) {
			++gStats.uploads;
			gStats.uploadMs += gRenderer.LastUploadMs();
			gStats.uploadMaxMs = (std::max)(gStats.uploadMaxMs, gRenderer.LastUploadMs());
		}
	}

	gDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);
	gDevice->BeginScene();
	gRenderer.Draw(gDevice, float(gWidth), float(gHeight), gCursorX, gCursorY, gClient.Cursor());
	const DWORD now = GetTickCount();
	gPager.Advance(gLastFrame ? now - gLastFrame : 0);
	gLastFrame = now;
	gPager.Draw(gDevice, float(gWidth), float(gHeight));
	gDevice->EndScene();
	gDevice->Present(nullptr, nullptr, nullptr, nullptr);
	++gStats.draws;
}

void Run(int milliseconds) {
	const DWORD start = GetTickCount();
	while (GetTickCount() - start < DWORD(milliseconds)) {
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
		RenderFrame();
		Sleep(1);
	}
}

// Saves what's really in the back buffer, not what the helper sent.
void Shot(const char* name) {
	// If the device was lost, it must first be recovered and drawn again.
	for (int attempt = 0; attempt < 100 && gDevice->TestCooperativeLevel() != D3D_OK; ++attempt) Run(50);
	if (gDeviceLosses > gDeviceLossesAtLastShot) {
		gDeviceLossesAtLastShot = gDeviceLosses;
		Run(500);
	}

	IDirect3DSurface9* target = nullptr;
	IDirect3DSurface9* copy = nullptr;
	bool ok = false;

	if (SUCCEEDED(gDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &target)) &&
		SUCCEEDED(gDevice->CreateOffscreenPlainSurface(gWidth, gHeight, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
		SUCCEEDED(gDevice->GetRenderTargetData(target, copy))) {
		D3DLOCKED_RECT locked;
		if (SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
			std::ofstream file(gOutput + "\\" + name + ".raw", std::ios::binary);
			gLastShot.resize(size_t(gWidth) * gHeight);
			for (int y = 0; y < gHeight; ++y) {
				file.write(static_cast<const char*>(locked.pBits) + y * locked.Pitch, gWidth * 4);
				std::memcpy(&gLastShot[size_t(y) * gWidth], static_cast<const char*>(locked.pBits) + y * locked.Pitch, gWidth * 4);
			}
			// The Clear background is magenta: if it's still there, the interface wasn't drawn.
			const uint32_t center = *reinterpret_cast<const uint32_t*>(static_cast<const char*>(locked.pBits) + (gHeight / 2) * locked.Pitch + (gWidth / 2) * 4);
			ok = file.good() && (center & 0xFFFFFF) != 0xFF00FF;
			copy->UnlockRect();
		}
	}
	if (copy) copy->Release();
	if (target) target->Release();

	if (!ok) {
		printf("FAIL: capture %s\n", name);
		++gFailures;
	}
}

void Click(POINT point) {
	gCursorX = float(point.x);
	gCursorY = float(point.y);
	gClient.Send({ ef::EventType::MouseMove, point.x, point.y });
	Run(120);
	gClient.Send({ ef::EventType::MouseDown, point.x, point.y, 0, 0, ef::kModLeftMouse });
	gClient.Send({ ef::EventType::MouseUp, point.x, point.y, 0, 0, 0 });
	Run(250);
}

void TypeText(const char* text) {
	for (const char* c = text; *c; ++c) {
		gClient.Send({ ef::EventType::Char, 0, 0, *c, 0, 0 });
		Run(15);
	}
}

// What UpdateMouse does when the click comes from the pad's A button.
void PadClick(POINT point) {
	gCursorX = float(point.x);
	gCursorY = float(point.y);
	gClient.Send({ ef::EventType::MouseMove, point.x, point.y });
	Run(120);
	gClient.Send({ ef::EventType::MouseDown, point.x, point.y, 0, 1, ef::kModLeftMouse });
	gClient.Send({ ef::EventType::MouseUp, point.x, point.y, 0, 0, 0 });
	Run(500);
}

void PadKey(ef::PadKeyboardAction action) {
	gClient.Send({ ef::EventType::PadKeyboard, 0, 0, static_cast<int32_t>(action) });
	Run(150);
}

void Control(int key) {
	gClient.Send({ ef::EventType::KeyDown, 0, 0, key, 0, ef::kModControl });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, key, 0, ef::kModControl });
	Run(400);
}

void GoTo(const char* address, int wait) {
	Click(kAddress);
	TypeText(address);
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
	gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
	Run(wait);
}

void Expect(const char* what, bool ok) {
	if (!ok) {
		printf("FAIL: %s\n", what);
		++gFailures;
	}
}

// What the plugin does every frame with the inbox operations.
void SyncMail() {
	ef::MailOp operation;
	bool changed = false;
	while (gClient.TakeMailOp(operation)) {
		changed = true;
		std::string key, script;
		if (operation.kind == ef::MailOpKind::MarkRead) gStore.MarkRead(operation.id);
		else if (operation.kind == ef::MailOpKind::Remove) gStore.Remove(operation.id);
		else gStore.Answer(operation.id, operation.kind == ef::MailOpKind::ReplyGood ? 1 : 2, key, script);
	}
	if (changed) gClient.Send({ ef::EventType::MailChanged, 0, 0, gStore.UnreadCount() });

	std::string valueKey, valueText;
	while (gClient.TakeValueOp(valueKey, valueText)) gStore.SetValue(valueKey, valueText);
	gStore.FlushSession();
}

void RunWithMail(int milliseconds) {
	const DWORD start = GetTickCount();
	while (GetTickCount() - start < DWORD(milliseconds)) {
		Run(50);
		SyncMail();
	}
}

void TestApi() {
	// With the real name, as another mod finds it.
	Expect("API: not installed before loading", !eyefinder::IsInstalled() && eyefinder::SendEmail("a", "b", "c") == 0);

	HMODULE module = LoadLibraryW(L"EyeFinder92SA.asi");
	Expect("API: load the module", module != nullptr);
	if (!module) return;

	using Pending = int(__cdecl*)();
	using LastEquals = int(__cdecl*)(const char*, const char*, const char*, int);
	const auto pending = reinterpret_cast<Pending>(GetProcAddress(module, "EF_Test_PendingCount"));
	const auto lastEquals = reinterpret_cast<LastEquals>(GetProcAddress(module, "EF_Test_LastEquals"));

	Expect("API: version", eyefinder::GetApiVersion() == EF_API_VERSION);
	Expect("API: send email", eyefinder::SendEmail("Sweet", "Hi", "Grove Street", EF_NOTIFY_PAGER) == 1);
	Expect("API: email arrived intact", lastEquals("Sweet", "Hi", "Grove Street", EF_NOTIFY_PAGER) == 1);
	Expect("API: null fields are accepted", eyefinder::SendEmail(nullptr, nullptr, "body only") == 1);
	Expect("API: empty email refused", eyefinder::SendEmail(nullptr, nullptr, nullptr) == 0);
	Expect("API: notify", eyefinder::Notify("Text") == 1);
	Expect("API: empty notify refused", eyefinder::Notify(nullptr) == 0);
	Expect("API: page", eyefinder::Page("Cesar", "Text") == 1);
	Expect("API: reply email", eyefinder::SendEmailWithReplies("A", "B", "C", EF_NOTIFY_NONE, "key", "Good", "Bad") == 1);
	Expect("API: reply email needs a key", eyefinder::SendEmailWithReplies("A", "B", "C", EF_NOTIFY_NONE, nullptr, "Good", "Bad") == 0);
	Expect("API: reply email needs both answers", eyefinder::SendEmailWithReplies("A", "B", "C", EF_NOTIFY_NONE, "key", "Good", nullptr) == 0);
	Expect("API: unknown reply", eyefinder::GetReply("key") == EF_REPLY_NONE && eyefinder::GetReply(nullptr) == EF_REPLY_NONE);
	Expect("API: run script", eyefinder::RunCleoScript("x.cs") == 1);
	Expect("API: empty script refused", eyefinder::RunCleoScript(nullptr) == 0);
	Expect("API: queue count", pending() == 6);
	Expect("API: set value", eyefinder::SetValue("mod.text", "hello") == 1 && eyefinder::SetValueInt("mod.number", -42) == 1);
	char buffer[16];
	Expect("API: get value", eyefinder::GetValue("mod.text", buffer, sizeof(buffer)) == 1 && strcmp(buffer, "hello") == 0);
	Expect("API: get int", eyefinder::GetValueInt("mod.number", 7) == -42);
	Expect("API: missing value", eyefinder::GetValue("mod.none", buffer, sizeof(buffer)) == 0 && eyefinder::GetValueInt("mod.none", 7) == 7);
	Expect("API: text isn't a number", eyefinder::GetValueInt("mod.text", 7) == 7);
	Expect("API: bad key refused", eyefinder::SetValue("bad key!", "x") == 0 && eyefinder::SetValue(nullptr, "x") == 0);
	Expect("API: unread count", eyefinder::GetUnreadCount() == 0);

	// A full queue refuses, instead of growing forever.
	for (int i = 0; i < 400; ++i) eyefinder::Notify("flood");
	Expect("API: queue is limited", pending() == 256);
	Expect("API: refuses when full", eyefinder::Notify("flood") == 0);

	FreeLibrary(module);
}

void TestMailFiles() {
	const std::filesystem::path folder = std::filesystem::path(gOutput) / L"save data";
	MailStore store;
	store.Add("From \xC3\xB1", "Subject \"quoted\"", "Body\nwith lines", "Day 1, 10:00");
	store.Add("Bad UTF-8 \xFF", "Subject", "Body", "Day 2, 11:00");
	store.MarkRead(1);
	Expect("files: save", store.SaveTo(folder / L"GTASAsf1.json"));

	// One saved game doesn't touch the other.
	MailStore other;
	other.Add("Other", "Other", "Other", "Day 9, 09:00");
	Expect("files: save second slot", other.SaveTo(folder / L"GTASAsf2.json"));

	MailStore loaded;
	Expect("files: load", loaded.LoadFrom(folder / L"GTASAsf1.json"));
	Expect("files: mails kept", loaded.Mails().size() == 2 && loaded.UnreadCount() == 1);
	Expect("files: text kept", loaded.Mails()[0].from == "From \xC3\xB1" && loaded.Mails()[0].body == "Body\nwith lines");
	Expect("files: new ids don't collide", loaded.Add("x", "x", "x", "x") == 3);

	MailStore memory;
	for (int i = 1; i <= 12; ++i) memory.AddPagerEntry("Sender", "Message " + std::to_string(i), "Day 1, 10:00");
	Expect("pager memory: keeps the last 10", memory.PagerEntries().size() == 10 && memory.PagerEntries().back().text == "Message 12" && memory.PagerEntries().front().text == "Message 3");
	Expect("pager memory: save", memory.SaveTo(folder / L"GTASAsf6.json"));
	MailStore recalled;
	Expect("pager memory: load", recalled.LoadFrom(folder / L"GTASAsf6.json") && recalled.PagerEntries().size() == 10 && recalled.PagerEntries().back().text == "Message 12");
	memory.Clear();
	Expect("pager memory: new game starts empty", memory.PagerEntries().empty());

	Expect("keys: names", ParseKeyName("F6") == VK_F6 && ParseKeyName("pageup") == VK_PRIOR && ParseKeyName(" M ") == 'M' && ParseKeyName("7") == '7' && ParseKeyName("Numpad3") == VK_NUMPAD3 && ParseKeyName("None") == 0);
	Expect("keys: unknown names", ParseKeyName("F13") < 0 && ParseKeyName("banana") < 0 && ParseKeyName("") < 0);
	{
		const std::wstring iniPath = (folder / L"keys.ini").wstring();
		std::string problems;
		const KeyConfig defaults = LoadKeyConfig(iniPath, problems);
		Expect("keys: creates the file", std::filesystem::exists(iniPath) && problems.empty() && defaults.pagerOpen == VK_F6);
		const KeyConfig fromFile = LoadKeyConfig(iniPath, problems);
		Expect("keys: reads its own file", problems.empty() && fromFile.browser == VK_F8 && fromFile.pagerOlder == VK_PRIOR && fromFile.pagerNewer == VK_NEXT);
		{
			std::ofstream custom(iniPath, std::ios::binary | std::ios::trunc);
			custom << "[Keys]\r\nPagerOpen=F7\r\nPagerOlder=banana\r\nPagerNewer=None\r\n";
		}
		problems.clear();
		const KeyConfig edited = LoadKeyConfig(iniPath, problems);
		Expect("keys: edits apply, and a typo keeps the default", edited.pagerOpen == VK_F7 && edited.pagerOlder == VK_PRIOR && edited.pagerNewer == 0 && !problems.empty());
	}

	MailStore replying;
	replying.AddWithReplies("a", "b", "c", "d", "key1", "Good", "Bad", "g.cs", "b.cs");
	std::string replyKey, replyScript;
	Expect("files: answer", replying.Answer(1, 2, replyKey, replyScript) && replyKey == "key1" && replyScript == "b.cs");
	Expect("files: save replies", replying.SaveTo(folder / L"GTASAsf4.json"));
	MailStore replied;
	replying.SetValue("mod.level", "5");
	Expect("files: save values", replying.SaveTo(folder / L"GTASAsf4.json"));
	Expect("files: load replies", replied.LoadFrom(folder / L"GTASAsf4.json") && replied.Mails()[0].answer == 2 && replied.Mails()[0].badText == "Bad" && replied.Replies().at("key1") == 2);
	Expect("files: values kept", replied.Values().at("mod.level") == "5");

	MailStore second;
	Expect("files: load second slot", second.LoadFrom(folder / L"GTASAsf2.json") && second.Mails().size() == 1 && second.Mails()[0].from == "Other");

	MailStore missing;
	Expect("files: a save without mod data is fine", !missing.LoadFrom(folder / L"GTASAsf8.json") && missing.Mails().empty());

	{
		std::ofstream damaged(folder / L"GTASAsf3.json", std::ios::binary);
		damaged << "{ not json";
	}
	MailStore bad;
	Expect("files: damaged file refused", !bad.LoadFrom(folder / L"GTASAsf3.json") && bad.Mails().empty());
	Expect("files: damaged file set aside", std::filesystem::exists(folder / L"GTASAsf3.json.damaged"));

	Expect("text: accents", ToPlainAscii("Ca\xC3\xB1on \xC3\xA1\xC3\xA9\xC3\xAD") == "Canon aei");
	Expect("text: game codes removed", ToPlainAscii("~r~red") == "rred");
	Expect("text: other characters", ToPlainAscii("\xE2\x82\xAC" "5") == "?5");
}

std::string StoredValue(const char* key) {
	const auto found = gStore.Values().find(key);
	return found == gStore.Values().end() ? std::string() : found->second;
}

void TestKeyboard() {
	using Action = ef::PadKeyboardAction;

	// keyboard-tests.html, from the corner of the content area (168, 102).
	constexpr POINT kUser = { 268, 127 };
	constexpr POINT kPass = { 268, 167 };
	constexpr POINT kNotes = { 268, 207 };
	constexpr POINT kRich = { 268, 247 };
	constexpr POINT kNumber = { 268, 287 };
	constexpr POINT kLocked = { 268, 327 };
	constexpr POINT kEmptyPage = { 800, 500 };

	GoTo("www.eyefinder.info/keyboard-tests.html", 1200);

	Click(kUser);
	Expect("keyboard: a mouse click doesn't open it", !gClient.KeyboardOpen());

	PadClick(kEmptyPage);
	Expect("keyboard: a pad click outside a field doesn't open it", !gClient.KeyboardOpen());
	PadClick(kLocked);
	Expect("keyboard: a read-only field doesn't open it", !gClient.KeyboardOpen());

	PadClick(kUser);
	Expect("keyboard: a pad click on a field opens it", gClient.KeyboardOpen());
	Shot("05b_keyboard");

	// The first selected key is q.
	PadKey(Action::Press);
	PadKey(Action::Press);
	Run(200);
	Shot("05c_keyboard_typed");
	PadKey(Action::Close);
	RunWithMail(300);
	Expect("keyboard: Close closes it", !gClient.KeyboardOpen());
	Expect("keyboard: Close changes nothing", StoredValue("keyboard.user").empty());

	PadClick(kUser);
	Expect("keyboard: opens again", gClient.KeyboardOpen());
	PadKey(Action::Press);
	PadKey(Action::Right);
	PadKey(Action::Press);
	PadKey(Action::Shift);
	PadKey(Action::Right);
	PadKey(Action::Press);
	PadKey(Action::Space);
	PadKey(Action::Press);
	PadKey(Action::Backspace);
	RunWithMail(300);
	Expect("keyboard: nothing reaches the page before Go", StoredValue("keyboard.user").empty());
	PadKey(Action::Go);
	RunWithMail(500);
	Expect("keyboard: Go closes it", !gClient.KeyboardOpen());
	Expect("keyboard: Go writes the text", StoredValue("keyboard.user") == "qwE ");
	Expect("keyboard: Go doesn't submit while another field is empty", StoredValue("keyboard.submits").empty());
	Shot("05d_keyboard_applied");

	// The field already has text: typing continues at the end, and now the form is complete.
	PadClick(kPass);
	PadKey(Action::Press);
	Shot("05e_keyboard_password");
	PadKey(Action::Go);
	RunWithMail(500);
	Expect("keyboard: password written", StoredValue("keyboard.pass") == "q");
	Expect("keyboard: Go submits a complete form", StoredValue("keyboard.submits") == "1");

	// Multiple lines: Enter is a new line, and Go submits nothing.
	PadClick(kNotes);
	Expect("keyboard: opens on a textarea", gClient.KeyboardOpen());
	TypeText("a");
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
	gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
	Run(100);
	TypeText("b");
	Run(100);
	Shot("05f_keyboard_multiline");
	Expect("keyboard: Enter stays open in a textarea", gClient.KeyboardOpen());
	PadKey(Action::Go);
	RunWithMail(500);
	Expect("keyboard: textarea written", StoredValue("keyboard.notes") == "a|b");
	Expect("keyboard: Go doesn't submit from a textarea", StoredValue("keyboard.submits") == "1");

	PadClick(kRich);
	Expect("keyboard: opens on editable content", gClient.KeyboardOpen());
	TypeText("rich");
	PadKey(Action::Go);
	RunWithMail(500);
	Expect("keyboard: editable content written", StoredValue("keyboard.rich") == "rich");

	PadClick(kNumber);
	PadKey(Action::Press);  // A q isn't valid in a number.
	Expect("keyboard: a number field refuses letters", gClient.KeyboardOpen());
	TypeText("42");
	PadKey(Action::Go);
	RunWithMail(500);
	Expect("keyboard: number written", StoredValue("keyboard.number") == "42");

	// A mouse click outside the keyboard leaves without changing anything.
	PadClick(kUser);
	Expect("keyboard: opens for the outside click", gClient.KeyboardOpen());
	PadKey(Action::Press);
	Click({ 800, 200 });  // Above the keyboard.
	RunWithMail(300);
	Expect("keyboard: clicking outside closes it", !gClient.KeyboardOpen());
	Expect("keyboard: clicking outside changes nothing", StoredValue("keyboard.user") == "qwE ");

	// The address bar: Close leaves the address, and Go navigates.
	PadClick(kAddress);
	Expect("keyboard: opens on the address bar", gClient.KeyboardOpen());
	Shot("05g_keyboard_address");
	PadKey(Action::Press);
	PadKey(Action::Close);
	Run(300);
	Expect("keyboard: Close leaves the address bar", !gClient.KeyboardOpen());
	Shot("05h_keyboard_address_closed");

	PadClick(kAddress);
	TypeText("www.eyefinder.info/tests.html");
	Run(100);
	PadKey(Action::Go);
	Run(1500);
	Expect("keyboard: Go on the address bar closes it", !gClient.KeyboardOpen());
	Shot("05i_keyboard_navigated");

	// After navigating, focus and clicks still work: the next test uses them.
	PadClick(kTextField);
	Expect("keyboard: opens on the new page", gClient.KeyboardOpen());
	PadKey(Action::Close);
	Run(200);
}

void TestLocations(const std::filesystem::path& modFolder) {
	const std::filesystem::path folder = std::filesystem::path(gOutput) / L"locations";
	std::error_code error;
	std::filesystem::create_directories(folder, error);
	const std::wstring path = (folder / L"locations.json").wstring();
	const auto write = [&path](const char* text) {
		std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
		file << text;
	};

	std::string problems;
	const auto shipped = LoadLocations((modFolder / L"locations.json").wstring(), problems);
	Expect("locations: the shipped file has no problems", problems.empty());
	const auto hasType = [&shipped](ef::SessionType type) {
		return std::any_of(shipped.begin(), shipped.end(), [type](const ComputerLocation& location) { return location.type == type; });
	};
	Expect("locations: the shipped file has every type", hasType(ef::SessionType::Normal) && hasType(ef::SessionType::Police) && hasType(ef::SessionType::DarkWeb));

	problems.clear();
	const auto missing = LoadLocations((folder / L"none.json").wstring(), problems);
	Expect("locations: a missing file keeps the cyber cafe", missing.size() == 1 && missing[0].name == "Cyber cafe" && !problems.empty());

	write("{ not json");
	problems.clear();
	Expect("locations: broken JSON keeps the cyber cafe", LoadLocations(path, problems).size() == 1 && !problems.empty());

	write(R"({ "locations": [
		{ "name": "A", "type": "normal", "position": [10, 20, 30], "radius": 2, "pricePerMinute": 7 },
		{ "name": "B", "type": "pd", "position": [10, 20, 30], "radius": 2, "interior": 6, "pricePerMinute": 99 },
		{ "name": "Bad type", "type": "banana", "position": [0, 0, 0] },
		{ "name": "Bad position", "type": "normal", "position": [0, 0] },
		{ "name": "Bad radius", "type": "normal", "position": [0, 0, 0], "radius": "big" }
	] })");
	problems.clear();
	const auto list = LoadLocations(path, problems);
	Expect("locations: bad entries are skipped and explained", list.size() == 2 && problems.find("entry 3") != std::string::npos && problems.find("entry 4") != std::string::npos && problems.find("entry 5") != std::string::npos);
	Expect("locations: only normal computers charge", list.size() == 2 && list[0].pricePerMinute == 7 && list[1].pricePerMinute == 0);

	Expect("locations: found inside the radius", list.size() == 2 && FindComputer(list, 11, 20, 30, 0) == &list[0]);
	Expect("locations: not found outside it", FindComputer(list, 13, 20, 30, 0) == nullptr);
	write(R"({ "locations": [ { "type": "pd", "position": [1, 1, 1], "radius": 2, "interior": 6 } ] })");
	const auto inside = LoadLocations(path, problems);
	Expect("locations: an interior must match", FindComputer(inside, 1, 1, 1, 6) == &inside[0] && FindComputer(inside, 1, 1, 1, 0) == nullptr);

	Expect("session cost: started minutes", SessionCost(0, 10) == 10 && SessionCost(60, 10) == 10 && SessionCost(61, 10) == 20 && SessionCost(125, 10) == 30);

	Expect("CEF: the build's runtime is accepted", BrowserRuntimeProblem((modFolder / L"browser").wstring()).empty() || BrowserRuntimeProblem((modFolder / L"Release").wstring()).empty());
	Expect("CEF: a folder without it is refused", !BrowserRuntimeProblem(folder.wstring()).empty());

	// All the files are there, but libcef.dll isn't the build the helper needs.
	const std::filesystem::path wrong = folder / L"wrong cef";
	std::filesystem::create_directories(wrong / L"locales", error);
	for (const wchar_t* name : { L"EyeFinderBrowser.exe", L"libcef.dll", L"chrome_elf.dll", L"icudtl.dat", L"v8_context_snapshot.bin", L"resources.pak", L"chrome_100_percent.pak", L"locales\\en-US.pak" }) {
		std::ofstream file(wrong / name, std::ios::binary);
		file << "x";
	}
	Expect("CEF: another version is refused", BrowserRuntimeProblem(wrong.wstring()).rfind("Wrong CEF version", 0) == 0);
}

void TestTypos() {
	const std::string name = "eyefinder";
	const auto check = [](const std::string& word, const char* what) {
		for (const std::string& host : { "www." + word + ".info", word + ".info" }) {
			if (!ef::IsEyeFinderTypo(host)) {
				printf("FAIL: typo not recognized, %s: %s\n", what, host.c_str());
				++gFailures;
			}
		}
	};

	// Every letter: missing, wrong, doubled, with a stray one added, and swapped with its neighbor.
	for (size_t i = 0; i < name.size(); ++i) {
		check(name.substr(0, i) + name.substr(i + 1), "missing letter");
		std::string wrong = name;
		wrong[i] = wrong[i] == 'x' ? 'q' : 'x';
		check(wrong, "wrong letter");
		check(name.substr(0, i) + name[i] + name.substr(i), "doubled letter");
		check(name.substr(0, i) + 'x' + name.substr(i), "extra letter");
		if (i + 1 < name.size() && name[i] != name[i + 1]) {
			std::string swapped = name;
			std::swap(swapped[i], swapped[i + 1]);
			check(swapped, "swapped letters");
		}
	}

	for (const char* host : { "eyefinder.info", "ww.eyefinder.info", "wwww.eyefinder.info", "www.eyefinder.inf", "www.eyefinder.ifno", "www.eyefinder.com", "eyefinder.net", "eyefindr.org", "WWW.EYEFNDER.INFO", "www.efinder.info" }) {
		Expect((std::string("typo: ") + host).c_str(), ef::IsEyeFinderTypo(host));
	}
	for (const char* host : { "www.eyefinder.info", "WWW.EyeFinder.Info", "mail.eyefinder.info", "www.binco.sa", "www.google.com", "eyeliner.com", "www.finder.com", "hidden.onion", "localhost", "" }) {
		Expect((std::string("not a typo: ") + host).c_str(), !ef::IsEyeFinderTypo(host));
	}
}

void TestSessions() {
	constexpr POINT kInsideTimer = { 1270, 14 };
	constexpr POINT kPageTop = { 640, 130 };
	constexpr uint32_t kPoliceHeader = 0x000000;
	constexpr uint32_t kPoliceDesktop = 0x12306B;
	constexpr uint32_t kDarkDesktop = 0x101014;
	constexpr uint32_t kDarkPage = 0x050505;

	const auto start = [](ef::SessionType type, int price) {
		gClient.Send({ ef::EventType::Session, 0, 0, static_cast<int32_t>(type), price });
		Run(500);
	};
	const auto pageColor = [] { return gLastShot[size_t(kPageTop.y) * gWidth + kPageTop.x] & 0xFFFFFF; };
	// The dark web's page has moving rain and text, but the refused page is white.
	const auto onDarkWebPage = [&] {
		const uint32_t color = pageColor();
		return ((color >> 16) & 0xFF) < 150 && (color & 0xFF) < 150;
	};
	const auto stored = [](const char* key) {
		SyncMail();
		return StoredValue(key);
	};

	// The dial-up popup: it hides the page, and then the page appears with the keyboard still its own.
	{
		constexpr POINT kDialTitle = { 800, 310 };
		gClient.Send({ ef::EventType::ModemEffect, 0, 0, 2 });
		Run(100);
		GoTo("www.binco.sa", 350);
		Shot("19a_dial_up");
		ExpectPixel("dial-up: the popup shows while entering a page", kDialTitle, 0x000080);
		Run(5000);
		Shot("19b_dial_up_done");
		Expect("dial-up: the page appears afterwards", (gLastShot[size_t(kDialTitle.y) * gWidth + kDialTitle.x] & 0xFFFFFF) != 0x000080);
		gClient.Send({ ef::EventType::ModemEffect, 0, 0, 0 });
		Run(100);
	}

	// A normal session: a timer, and neither restricted site.
	start(ef::SessionType::Normal, 10);
	gClient.Send({ ef::EventType::SessionTime, 0, 0, 3725, 630 });
	Run(300);
	Shot("20a_normal_session");
	ExpectPixel("session: the timer shows in a normal session", kInsideTimer, 0x000000);
	GoTo("network.sapd.sa", 1000);
	Shot("20b_police_refused");
	Expect("session: the police site is refused from a normal computer", pageColor() != kPoliceHeader);
	GoTo("hidden.onion", 1000);
	Shot("20c_dark_web_refused");
	Expect("session: the dark web is refused from a normal computer", !onDarkWebPage());
	GoTo("sasquatchleaks.onion", 1000);
	Shot("20c1_hidden_site_refused");
	Expect("hidden folder: its sites are refused from a normal computer", !onDarkWebPage());

	// Search on a normal computer: public sites only, and never the hidden list.
	GoTo("www.eyefinder.info/search-tests.html", 1500);
	RunWithMail(800);
	Expect("search: a normal computer finds a public site", stored("search.normal.byName").find("www.binco.sa") != std::string::npos);
	Expect("search: something that doesn't exist finds nothing", stored("search.normal.byText") == "none");
	Expect("search: a normal computer can't search the hidden folder", stored("search.normal.hidden") == "refused" && stored("search.normal.hiddenAll") == "refused");
	Expect("search: hidden sites don't appear in a normal search", stored("search.normal.leak") == "none");
	Expect("search: unlisted sites don't appear in a search", stored("search.normal.unlisted") == "none");
	Expect("search: other sites can't set off the police alarm", stored("search.normal.alarm") == "refused" && !gClient.TakeAlarm());

	// The home page's search, from the page and from the address bar.
	GoTo("binco clothes", 1500);
	Shot("20c1a_home_search_from_address_bar");
	GoTo("www.eyefinder.info/?q=zzzznothing", 1200);
	Shot("20c1b_home_search_no_results");

	// Toggling to the real internet and searching asks first, because the browser is in local mode.
	constexpr POINT kHomeRealToggle = { 678, 287 };
	constexpr POINT kHomeQuery = { 595, 258 };
	constexpr POINT kBesideAlert = { 200, 600 };  // On the page, which the warning hides and shades.
	GoTo("www.eyefinder.info", 1200);
	Click(kHomeRealToggle);
	Run(200);
	Shot("20c1c_home_real_toggle");

	// Choosing another search engine.
	constexpr POINT kHomeEngine = { 795, 289 };
	constexpr POINT kHomeEngineGoogle = { 795, 327 };
	Click(kHomeEngine);
	Run(300);
	Shot("20c1c2_home_engine_list");
	Click(kHomeEngineGoogle);
	Run(300);
	Click(kHomeQuery);
	TypeText("grove street");
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
	gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
	Run(800);
	Shot("20c1d_home_real_search_asks");
	Expect("search: a real search in local mode asks first", (gLastShot[size_t(kBesideAlert.y) * gWidth + kBesideAlert.x] & 0xFFFFFF) != 0xFFFFFF);
	Click(kStayLocal);
	Run(400);
	Shot("20c1e_home_stayed_local");
	ExpectPixel("search: refusing keeps the page", kBesideAlert, 0xFFFFFF);

	// The chosen engine is remembered on a new visit.
	GoTo("www.eyefinder.info", 1200);
	Click(kHomeRealToggle);
	Run(200);
	Shot("20c1f_home_engine_remembered");

	// The recycle bin opens over the browser, and the tab comes back when it closes.
	constexpr POINT kBinTitle = { 800, 133 };
	constexpr POINT kBinClose = { 987, 133 };
	constexpr POINT kBinFirstFile = { 400, 170 };
	constexpr POINT kBinPreview = { 800, 300 };
	constexpr uint32_t kBinTitleColor = 0x000080;
	Click(kIconBin);
	Run(300);
	Shot("20c2_bin_empty");
	ExpectPixel("recycle bin: it opens over the browser", kBinTitle, kBinTitleColor);
	Click(kBinClose);
	Run(300);
	Shot("20c3_bin_closed");
	Expect("recycle bin: it closes", (gLastShot[size_t(kBinTitle.y) * gWidth + kBinTitle.x] & 0xFFFFFF) != kBinTitleColor);

	// The easter egg: a typo leads to a parked page, and answering "No" opens the dark web.
	constexpr POINT kParkedNo = { 278, 308 };
	constexpr POINT kParkedQuestion = { 300, 262 };
	const auto parked = [&](const char* address, const char* shot) {
		GoTo(address, 1000);
		Shot(shot);
		Expect("easter egg: a misspelled address reaches the parked page", !onDarkWebPage() && (gLastShot[size_t(kParkedQuestion.y) * gWidth + 180] & 0xFFFFFF) == 0xFFFFFF);
	};
	parked("eyefinder.com", "20c3a_easter_egg_alias");
	parked("www.yefinder.info", "20c3a2_easter_egg_first_letter");
	parked("www.eyefinde.info", "20c3a3_easter_egg_last_letter");
	parked("www.eyefindeer.info", "20c3a4_easter_egg_extra_letter");
	parked("www.eyeifnder.info", "20c3a5_easter_egg_swapped");
	parked("www.eyefnder.info", "20c3b_easter_egg_parked");
	Click(kParkedNo);
	Run(1200);
	Shot("20c3c_easter_egg_reply");
	Run(2200);
	Shot("20c3d_easter_egg_dark_web");
	Expect("easter egg: No opens the dark web from a normal computer", onDarkWebPage());
	GoTo("sasquatchleaks.onion", 1000);
	Shot("20c3e_easter_egg_hidden_site");
	Expect("easter egg: hidden sites open while it lasts", onDarkWebPage());

	// It ends with the session, and takes the tabs with it.
	start(ef::SessionType::Normal, 10);
	Shot("20c3f_easter_egg_closed");
	ExpectPixel("easter egg: the next session closes its tabs", { 640, 360 }, kDesktopColor);
	Click(kIconInternet);
	Run(1000);
	GoTo("sasquatchleaks.onion", 1000);
	Shot("20c3g_easter_egg_gone");
	Expect("easter egg: the next session can't reach the dark web", !onDarkWebPage());

	// A police computer: its own desktop and browser, and no timer. Tabs don't come along.
	start(ef::SessionType::Police, 0);
	Shot("20d_police_desktop");
	ExpectPixel("session: the police desktop has no timer", kInsideTimer, kPoliceDesktop);
	ExpectIconDrawn("police", { 72, 301 });

	// A police computer's recycle bin has files, and they can be read.
	Click(kIconBin);
	Run(300);
	Shot("20d2_police_bin");
	Click(kBinFirstFile);
	Run(300);
	Shot("20d3_police_bin_file");
	{
		// The first file's row turns dark, and the preview gains text somewhere.
		const uint32_t row = gLastShot[size_t(kBinFirstFile.y) * gWidth + 570] & 0xFFFFFF;
		Expect("recycle bin: a file can be selected", row == kBinTitleColor);
		bool hasText = false;
		for (int x = kBinPreview.x - 200; x < kBinPreview.x + 150 && !hasText; ++x) {
			for (int y = 165; y < 250 && !hasText; ++y) hasText = (gLastShot[size_t(y) * gWidth + x] & 0xFFFFFF) != 0xFFFFFF;
		}
		Expect("recycle bin: a file shows its text", hasText);
	}
	Click(kBinClose);
	Run(300);
	Click(kIconSecond);
	Run(1200);
	Shot("20e_police_site");
	ExpectPixel("session: the police icon opens its site", kPageTop, kPoliceHeader);

	// The police site: a search waits a moment, and then lists who it found.
	{
		const auto pressEnter = [] {
			gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
			gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
			gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
		};
		Shot("20e0_police_home");
		GoTo("network.sapd.sa/#records", 700);
		TypeText("john");
		Run(300);
		Shot("20e1_police_search");
		pressEnter();
		Run(300);
		Shot("20e2_police_looking");
		Run(2000);
		Shot("20e3_police_results");
		GoTo("network.sapd.sa/#records/tenpenny", 700);
		Shot("20e4_police_sealed");
		GoTo("network.sapd.sa/#wanted", 700);
		Shot("20e5_police_wanted");
		GoTo("network.sapd.sa/#dispatch", 700);
		Shot("20e6_police_dispatch");
		GoTo("network.sapd.sa/#evidence", 700);
		Shot("20e7_police_evidence");
		GoTo("network.sapd.sa/#bulletins", 700);
		Shot("20e8_police_bulletins");

		// C.R.A.S.H.: any password fails, and sets off the alarm that the plugin turns into stars.
		GoTo("network.sapd.sa/#crash", 700);
		Shot("20e9_police_crash");
		Expect("police: nothing happens before trying a password", !gClient.TakeAlarm());
		TypeText("letmein");
		pressEnter();
		Run(1200);
		Shot("20e10_police_crash_denied");
		Expect("police: a failed password sets off the alarm", gClient.TakeAlarm());
		Expect("police: the alarm is only taken once", !gClient.TakeAlarm());
		GoTo("network.sapd.sa/#home", 700);
	}

	GoTo("hidden.onion", 1000);
	Shot("20f_dark_web_refused_from_police");
	Expect("session: the dark web is refused from a police computer", !onDarkWebPage());

	start(ef::SessionType::DarkWeb, 0);
	Shot("20g_dark_web_desktop");
	ExpectPixel("session: the dark web desktop has no timer", kInsideTimer, kDarkDesktop);
	ExpectIconDrawn("dark web", { 72, 301 });
	Click(kIconSecond);
	Run(2500);
	Shot("20h_dark_web_intro");
	Expect("session: the dark web icon opens its site", onDarkWebPage());
	Run(4000);
	Shot("20h1_dark_web_intro_connecting");
	Run(4500);
	Shot("20h1b_dark_web_intro_humor");
	Run(10000);
	Shot("20h2_dark_web_main");

	// The console searches the hidden folder.
	constexpr POINT kDarkConsole = { 640, 400 };
	Click(kDarkConsole);
	TypeText("bigfoot");
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
	gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
	Run(600);
	TypeText("nothing like this");
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
	gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
	Run(600);
	Shot("20h3_dark_web_search");

	// The intro only plays once for each use of a computer: reloading doesn't repeat it.
	gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_F5 });
	gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_F5 });
	Run(1200);
	Shot("20h4_dark_web_reloaded");

	GoTo("sasquatchleaks.onion", 1000);
	Shot("20h5_hidden_site");
	Expect("hidden folder: its sites open on a dark web computer", onDarkWebPage());

	GoTo("www.eyefinder.info/search-tests.html", 1500);
	RunWithMail(800);
	Expect("search: a dark web computer finds a hidden site", stored("search.dark_web.hidden") == "sasquatchleaks.onion");
	Expect("search: the hidden list has every sample", stored("search.dark_web.hiddenAll") == "2");
	Expect("search: public search still leaves hidden sites out", stored("search.dark_web.leak") == "none");

	// A search typed in the address bar on a hidden page stays on the dark web.
	GoTo("hidden.onion", 1000);
	GoTo("ghost cars", 1500);
	Shot("20h6_dark_web_address_search");

	// Back on a normal computer, what was reachable before no longer is.
	start(ef::SessionType::Normal, 0);
	Shot("20i_back_to_normal");
	ExpectPixel("session: a free computer has no timer", kInsideTimer, kDesktopColor);
	Click(kIconInternet);
	Run(1000);
	GoTo("hidden.onion", 1000);
	Shot("20j_dark_web_refused_again");
	Expect("session: leaving a computer takes its access away", pageColor() != kDarkPage);
}

void Measure(const char* name, int milliseconds, bool scroll) {
	gStats = {};
	const uint32_t paints = gClient.PaintCount();
	const DWORD start = GetTickCount();
	int direction = -120;
	int ticks = 0;
	while (GetTickCount() - start < DWORD(milliseconds)) {
		if (scroll) {
			gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, direction, 0 });
			if (++ticks % 30 == 0) direction = -direction;
		}
		Run(16);
	}
	printf("%-14s draws %.1f/s | browser %.1f frames/s | uploads %.1f/s | upload avg %.2f ms, max %.2f ms\n", name,
		gStats.draws * 1000.0 / milliseconds, (gClient.PaintCount() - paints) * 1000.0 / milliseconds,
		gStats.uploads * 1000.0 / milliseconds, gStats.uploads ? gStats.uploadMs / gStats.uploads : 0.0, gStats.uploadMaxMs);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
	if (argc != 4) {
		puts("usage: harness <helper.exe> <shell url> <output directory>");
		return 2;
	}

	const std::wstring helper = argv[1];
	const std::wstring url = argv[2];
	char outputBuffer[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, outputBuffer, MAX_PATH, nullptr, nullptr);
	gOutput = outputBuffer;

	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = DefWindowProcW;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = L"EyeFinderHarness";
	RegisterClassW(&windowClass);
	// Off screen, so as not to get in the way.
	HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, windowClass.lpszClassName, L"harness", WS_POPUP, -gWidth - 200, 0, gWidth, gHeight,
		nullptr, nullptr, windowClass.hInstance, nullptr);
	ShowWindow(window, SW_SHOWNOACTIVATE);

	IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
	if (!d3d) {
		puts("FAIL: Direct3DCreate9");
		return 1;
	}

	gParameters.Windowed = TRUE;
	gParameters.SwapEffect = D3DSWAPEFFECT_COPY;
	gParameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	gParameters.BackBufferWidth = gWidth;
	gParameters.BackBufferHeight = gHeight;
	gParameters.hDeviceWindow = window;
	gParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &gParameters, &gDevice))) {
		puts("FAIL: CreateDevice");
		return 1;
	}

	std::string error;
	const DWORD launch = GetTickCount();
	gStore.SetSessionPath(std::filesystem::path(argv[3]) / L"session.json");
	gStore.Add("EyeFinder Staff", "Welcome to EyeMail", "Welcome!\n\nVisit https://www.eyefinder.info/ for more.", "Day 1, 08:00");
	gStore.Add("Sweet", "Get back to Grove Street", "Yo CJ, we need you back here.", "Day 2, 19:30");
	gStore.Add("<b>Script Kiddie</b>", "<img src=x onerror=alert(1)>", "<script>document.title='hacked'</script> Plain text only.", "Day 3, 02:15");
	gStore.FlushSession();

	// A site that search must not find. It has to exist before the helper starts.
	const std::filesystem::path unlistedSite = std::filesystem::path(helper).parent_path().parent_path() / L"sites" / L"unlistedsearchtest.example";
	{
		std::error_code unlistedError;
		std::filesystem::create_directories(unlistedSite, unlistedError);
		std::ofstream(unlistedSite / L"site.json", std::ios::binary) << "{ \"title\": \"unlistedsearchtest\", \"listed\": false }";
		std::ofstream(unlistedSite / L"index.html", std::ios::binary) << "<body>unlistedsearchtest</body>";
	}

	const std::wstring mailPath = std::wstring(argv[3]) + L"\\session.json";
	if (!gClient.Start(helper, url, mailPath, gWidth, gHeight, error)) {
		printf("FAIL: %s\n", error.c_str());
		return 1;
	}
	gClient.Send({ ef::EventType::Show });
	gClient.Send({ ef::EventType::ModemEffect, 0, 0, 0 });
	gClient.Send({ ef::EventType::MailChanged, 0, 0, gStore.UnreadCount() });

	{
		ef::GameStateData state{};
		state.inGame = 1;
		state.money = 12345;
		state.health = 87.5f;
		state.wantedLevel = 3;
		state.day = 42;
		strcpy_s(state.zone, "Ganton");
		strcpy_s(state.lastMission, "SWEET_1");
		gClient.PublishGameState(state);
	}

	while (!gRenderer.HasFrame()) {
		if (gClient.HasExited()) {
			puts("FAIL: the helper exited during startup");
			return 1;
		}
		if (GetTickCount() - launch > 30000) {
			puts("FAIL: no frame within 30 s");
			return 1;
		}
		Run(10);
	}
	printf("first frame after %lu ms\n", GetTickCount() - launch);
	Run(800);
	Shot("01_desktop");
	ExpectPixel("desktop", { 1200, 400 }, kDesktopColor);
	// A missing image leaves the desktop's color in its place, and nothing else would notice.
	ExpectIconDrawn("Recycle Bin", { 72, 57 });
	ExpectIconDrawn("Internet", { 72, 179 });
	ExpectIconDrawn("Mail", { 72, 301 });
	ExpectIconDrawn("Log Off", { 72, 423 });

	// Desktop -> browser with its home page.
	Click(kIconInternet);
	Run(1200);
	Shot("02_home");

	// Each visit shuffles the sites and ads.
	{
		const std::vector<uint32_t> first = gLastShot;
		bool changed = false;
		for (int attempt = 0; attempt < 8 && !changed; ++attempt) {
			gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_F5 });
			gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_F5 });
			Run(1000);
			Shot("02b_home_shuffled");
			changed = gLastShot != first;
		}
		Expect("home page: the order changes between visits", changed);
	}

	// A public site, with an ad from the shared folder.
	GoTo("www.binco.sa", 1200);
	Shot("02c_public_site_with_ad");
	Click(kPublicSiteAd);
	Run(1200);
	Shot("02d_after_clicking_ad");
	Expect("ad: clicking leaves for another site", (gLastShot[size_t(150) * gWidth + 300] & 0xFFFFFF) != 0x1A4F9C);

	// Extra addresses to check by hand: EF_HARNESS_VISIT=a;b;c
	if (const char* visits = getenv("EF_HARNESS_VISIT")) {
		std::string list = visits;
		int number = 0;
		size_t start = 0;
		while (start < list.size()) {
			size_t end = list.find(';', start);
			if (end == std::string::npos) end = list.size();
			const std::string address = list.substr(start, end - start);
			start = end + 1;

			++number;
			GoTo(address.c_str(), 1800);
			Shot(("visit_" + std::to_string(number) + "_top").c_str());
			gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, -100000, 0 });
			Run(800);
			Shot(("visit_" + std::to_string(number) + "_bottom").c_str());
		}
	}

	// Address bar -> local site, with input on the page.
	GoTo("www.eyefinder.info/tests.html", 1200);
	Click(kClickMe);
	Run(600);
	Click(kClickMe);
	Click(kTextField);
	TypeText("grove street");
	Run(300);
	Shot("03_input");
	Click(kDropdown);
	Run(300);
	Shot("04_dropdown");
	ExpectPixel("open dropdown", { 340, 351 }, kDropdownHighlight);
	Click(kDropdownThird);
	Run(300);
	Shot("05_picked");

	// A hand over links and buttons, and an arrow elsewhere. Pages show an arrow over buttons by default.
	{
		const auto cursorAt = [](POINT point) {
			gCursorX = float(point.x);
			gCursorY = float(point.y);
			gClient.Send({ ef::EventType::MouseMove, point.x, point.y });
			Run(250);
			return gClient.Cursor();
		};
		Expect("cursor: a hand over a page's button", cursorAt(kClickMe) == ef::CursorKind::Hand);
		Shot("05a_hand_cursor");
		Expect("cursor: a hand over the toolbar", cursorAt(kMailButton) == ef::CursorKind::Hand);
		Expect("cursor: an arrow over empty page", cursorAt({ 1000, 240 }) == ef::CursorKind::Arrow);
		Expect("cursor: a text cursor over a field", cursorAt(kTextField) == ef::CursorKind::IBeam);
	}

	TestKeyboard();

	Measure("idle", 3000, false);
	Measure("scrolling", 5000, true);

	// Second tab -> real internet, without losing the first.
	Click(kNewTabWithOneTab);
	Run(1000);

	// It starts in local mode: a real address asks, and refusing doesn't load it.
	const char* const kRealSite = "https://en.wikipedia.org/wiki/Grand_Theft_Auto:_San_Andreas";
	GoTo(kRealSite, 800);
	Shot("05b_internet_alert");
	Click(kStayLocal);
	Run(1500);
	Shot("05c_stayed_local");
	GoTo(kRealSite, 800);
	Click(kConnect);
	Run(5000);
	Shot("06_second_tab_real_site");
	Measure("real site", 5000, true);

	Click(kTab1);
	Run(500);
	Shot("07_back_on_first_tab");
	Click(kTab2);
	Run(500);
	Shot("08_back_on_second_tab");

	// Back and forward, in tab 2: Wikipedia <- home.
	Click(kBack);
	Run(1000);
	Shot("09_after_back");
	Click(kForward);
	Run(2500);
	Shot("10_after_forward");

	// Shortcuts.
	Control('T');
	Run(800);
	Shot("11_ctrl_t");
	Control('W');
	Run(400);
	Shot("12_ctrl_w");

	// Worst case: a page that repaints nonstop, in its own tab.
	Click(kTab1);
	GoTo("www.eyefinder.info/tests.html?animate", 1000);
	Measure("animated page", 5000, false);
	Shot("12a_internet_mode_network_reached");

	// Going back to local doesn't ask, and takes tabs off real sites. Going back to the internet asks again.
	Click(kTab2);
	Run(300);
	Click(kNetMode);
	Run(1500);
	Shot("12b_back_to_local");
	Click(kNetMode);
	Run(500);
	Shot("12c_asks_every_time");
	Click(kStayLocal);
	Run(400);
	Click(kTab1);
	GoTo("www.eyefinder.info/tests.html", 3000);
	Shot("12d_local_page_network_blocked");

	// Minimizing and restoring keep the tabs.
	Click(kMinimize);
	Run(400);
	Shot("13_minimized");
	ExpectPixel("minimized window", { 640, 360 }, kDesktopColor);
	Click(kRestore);
	Run(500);
	Shot("14_restored");
	ExpectPixel("restored window", { 600, 15 }, kTitleBarColor);

	// Mail: open, read, delete and receive a new one with the page open.
	gClient.Send({ ef::EventType::Session, 0, 0, static_cast<int32_t>(ef::SessionType::Normal), 0 });
	Run(100);
	Click(kMailButton);
	Run(1700);
	Shot("13a_mail_logon");
	ExpectPixel("mail: the logon screen shows first", { 200, 600 }, 0x008080);
	ExpectPixel("mail: the logon screen hides the inbox", { 188, 168 }, 0x008080);
	Run(5000);
	Shot("13b_mail_after_logon");
	Expect("mail: the logon screen goes away", (gLastShot[size_t(600) * gWidth + 200] & 0xFFFFFF) != 0x008080);
	RunWithMail(1800);
	Shot("15_mail_list");
	Click(kFirstMailRow);
	RunWithMail(800);
	Shot("16_mail_read");
	Expect("mail: reading marks as read", gStore.UnreadCount() == 2);
	Click(kDeleteMail);
	RunWithMail(800);
	Expect("mail: delete", gStore.Mails().size() == 2);
	gStore.Add("Big Smoke", "Dinner", "Two number 9s.", "Day 4, 12:00");
	gStore.FlushSession();
	gClient.Send({ ef::EventType::MailChanged, 0, 0, gStore.UnreadCount() });
	RunWithMail(2200);
	Shot("17_mail_new_arrived");

	// Replies: the new email has two, and only one choice can be made.
	const uint32_t replyMail = gStore.AddWithReplies("Ryder", "You in?", "Meet me at the spot.", "Day 5, 21:00",
		"harness_key", "I'm in, homie.", "Forget it, fool.", "good.cs", "");
	gStore.FlushSession();
	gClient.Send({ ef::EventType::MailChanged, 0, 0, gStore.UnreadCount() });
	RunWithMail(2200);
	Click(kFirstMailRow);
	RunWithMail(800);
	Shot("17b_mail_replies");
	Click(kReplyGood);
	Shot("17c_cj_typing");
	Expect("replies: not sent while typing", gStore.Replies().at("harness_key") == 0);
	RunWithMail(2500);
	Shot("17c2_mail_answered");
	{
		const Mail* answered = nullptr;
		for (const Mail& mail : gStore.Mails()) {
			if (mail.id == replyMail) answered = &mail;
		}
		Expect("replies: the good answer was recorded", answered && answered->answer == 1 && answered->read);
		std::string key, script;
		Expect("replies: only one answer", !gStore.Answer(replyMail, 2, key, script));
		Expect("replies: script kept", answered && answered->goodScript == "good.cs");
		Expect("replies: survive deleting", gStore.Remove(replyMail) && gStore.KnowsReplyKey("harness_key") && gStore.Replies().at("harness_key") == 1);
	}

	// A button on a local site requests a script, and only one from its folder.
	GoTo("www.eyefinder.info/tests.html", 1500);
	Click(kRunCleo);
	Run(700);
	Shot("17d_cleo_button");
	{
		std::string siteScript;
		Expect("web button: script requested", gClient.TakeSiteScript(siteScript) && siteScript == "www.eyefinder.info/hello");
	}

	// What the FAQ promises: a new file in an existing site loads without restarting, and F5 picks up changes.
	{
		const std::filesystem::path page = std::filesystem::path(helper).parent_path().parent_path() / L"system" / L"sites" / L"www.eyefinder.info" / L"f5test.html";
		const auto writePage = [&page](const char* color) {
			std::ofstream file(page, std::ios::binary | std::ios::trunc);
			file << "<body style='margin:0;background:" << color << "'></body>";
		};
		writePage("#ff0000");
		GoTo("www.eyefinder.info/f5test.html", 1000);
		Shot("17e_new_file");
		ExpectPixel("new file loads without restarting", { 640, 400 }, 0xFF0000);
		writePage("#00ff00");
		gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_F5 });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_F5 });
		Run(1000);
		Shot("17f_after_f5");
		ExpectPixel("F5 picks up the edited file", { 640, 400 }, 0x00FF00);
		std::error_code removeError;
		std::filesystem::remove(page, removeError);
		GoTo("www.eyefinder.info/tests.html", 800);
		Shot("17g_media_formats");
	}

	// Values and game state, in both directions.
	gStore.SetValue("tests.pong", "hello from the game");
	RunWithMail(1800);
	Expect("values: the page's value reached the game", gStore.Values().count("tests.ping") && gStore.Values().at("tests.ping") == "hello from the page");
	Shot("17h_state_and_values");

	// Zoom: the buttons and the keyboard.
	{
		const std::vector<uint32_t> before = gLastShot;
		Click(kZoomIn);
		Click(kZoomIn);
		Run(500);
		Shot("17i_zoomed_in");
		Expect("zoom: the buttons change the page", gLastShot != before);
		gClient.Send({ ef::EventType::KeyDown, 0, 0, '0', 0, ef::kModControl });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, '0', 0, ef::kModControl });
		Run(500);
		gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_OEM_MINUS, 0, ef::kModControl });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_OEM_MINUS, 0, ef::kModControl });
		gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_OEM_MINUS, 0, ef::kModControl });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_OEM_MINUS, 0, ef::kModControl });
		Run(500);
		Shot("17j_zoomed_out");
		gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, 120, ef::kModControl });
		gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, 120, ef::kModControl });
		Run(500);
		Shot("17k_ctrl_wheel_back_to_100");
	}

	// The pager, over the page. It has to finish by itself.
	gPager.Show("Big Smoke", "I'll have two number 9s, a number 9 large, a number 6 with extra dip, a number 7, two number 45s");
	PagerRenderer::PlayBeep();
	Run(1600);
	Shot("18_pager");
	Run(2800);
	Shot("19_pager_next_page");
	const DWORD pagerStart = GetTickCount();
	while (gPager.IsBusy() && GetTickCount() - pagerStart < 30000) Run(100);
	Expect("pager: finishes by itself", !gPager.IsBusy());

	gPager.Show("", "Ca\xC3\xB1on");
	Run(1600);
	Shot("20_pager_no_sender");
	while (gPager.IsBusy() && GetTickCount() - pagerStart < 60000) Run(100);

	// The memory: it rises, changes message without rising again, keeps paging by itself, and is saved.
	gPager.ShowFromMemory("Cesar", "Meet me at the garage in Little Mexico. Bring the car and come alone, ese.", "D568 13:51 1/2");
	Run(600);
	Shot("21_pager_memory");
	Run(2800);
	Shot("21b_pager_memory_next_page");
	gPager.ShowFromMemory("Sweet", "Get back here.", "D568 12:07 2/2");
	Run(100);
	Shot("21c_pager_memory_older");
	gPager.Close();
	Run(500);
	Expect("pager: closing puts it away", !gPager.IsBusy());

	// Hiding and showing (pause menu, F8) produce a new frame.
	gClient.Send({ ef::EventType::Hide });
	Run(300);
	uint32_t paints = gClient.PaintCount();
	gClient.Send({ ef::EventType::Show });
	Run(700);
	const uint32_t afterShow = gClient.PaintCount() - paints;
	printf("frames after hide and show: %u\n", afterShow);
	if (afterShow == 0) {
		puts("FAIL: no frame after show");
		++gFailures;
	}

	TestSessions();

	// Log Off must ask to return to the game.
	if (gClient.TakeCloseRequest()) {
		puts("FAIL: close was requested too early");
		++gFailures;
	}
	Click(kMinimize);
	Run(300);
	Click(kIconLogOff);
	Run(300);
	if (!gClient.TakeCloseRequest()) {
		puts("FAIL: Log Off did not request a close");
		++gFailures;
	}

	TestMailFiles();
	TestApi();
	TestLocations(std::filesystem::path(helper).parent_path().parent_path());
	TestTypos();

	const DWORD stopStart = GetTickCount();
	gClient.Stop();
	printf("helper stopped in %lu ms\n", GetTickCount() - stopStart);

	std::error_code cleanupError;
	std::filesystem::remove_all(unlistedSite, cleanupError);

	gRenderer.Release();
	gDevice->Release();
	d3d->Release();

	puts(gFailures ? "FAIL" : "PASS");
	return gFailures ? 1 : 0;
}
