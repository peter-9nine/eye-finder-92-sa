// Prueba el helper, el protocolo, la interfaz y el renderizado sin el juego, en un proceso de 32 bits.
//
//   harness <helper.exe> <shell url> <output directory>
//
// Las coordenadas corresponden a ui/shell.html a 1280x720.
// Con un d3d9.dll de DXVK junto al ejecutable, también prueba esa ruta.
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

#include "BrowserClient.h"
#include "EyeFinderApi.h"
#include "MailStore.h"
#include "OverlayRenderer.h"
#include "PagerRenderer.h"

namespace {

constexpr int gWidth = 1280;
constexpr int gHeight = 720;

// Interfaz
constexpr POINT kIconInternet = { 433, 320 };
constexpr POINT kIconLogOff = { 831, 526 };
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
constexpr POINT kFirstMailRow = { 500, 187 };
constexpr POINT kReplyGood = { 410, 655 };

// tests.html: el botón de CLEO
constexpr POINT kRunCleo = { 290, 372 };
constexpr POINT kDeleteMail = { 1058, 391 };

// tests.html, a partir de la esquina del hueco de contenido
constexpr POINT kClickMe = { 258, 204 };
constexpr POINT kTextField = { 368, 260 };
constexpr POINT kDropdown = { 286, 316 };
constexpr POINT kDropdownThird = { 286, 400 };

// Colores esperados en las capturas.
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

// Comprueba un píxel de la última captura: que pase la prueba no basta si lo dibujado está mal.
void ExpectPixel(const char* what, POINT point, uint32_t expected) {
	const uint32_t actual = gLastShot.empty() ? 0 : gLastShot[size_t(point.y) * gWidth + point.x] & 0xFFFFFF;
	if (actual != expected) {
		printf("FAIL: %s: pixel %06X, expected %06X\n", what, actual, expected);
		++gFailures;
	}
}

// Devuelve false mientras no se pueda dibujar. Es lo que hace el plugin: soltar la textura y pedir un fotograma.
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

// Guarda lo que hay realmente en el búfer trasero, no lo que envió el helper.
void Shot(const char* name) {
	// Si se ha perdido el dispositivo, primero hay que recuperarlo y volver a dibujar.
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
			// El fondo de Clear es magenta: si sigue ahí, la interfaz no se ha dibujado.
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

// Lo que el plugin hace en cada fotograma con las operaciones de la bandeja.
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
	// Con el nombre real, como lo encuentra otro mod.
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
	Expect("API: unread count", eyefinder::GetUnreadCount() == 0);

	// Una cola llena rechaza, en lugar de crecer sin fin.
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

	// Una partida distinta no toca la otra.
	MailStore other;
	other.Add("Other", "Other", "Other", "Day 9, 09:00");
	Expect("files: save second slot", other.SaveTo(folder / L"GTASAsf2.json"));

	MailStore loaded;
	Expect("files: load", loaded.LoadFrom(folder / L"GTASAsf1.json"));
	Expect("files: mails kept", loaded.Mails().size() == 2 && loaded.UnreadCount() == 1);
	Expect("files: text kept", loaded.Mails()[0].from == "From \xC3\xB1" && loaded.Mails()[0].body == "Body\nwith lines");
	Expect("files: new ids don't collide", loaded.Add("x", "x", "x", "x") == 3);

	MailStore replying;
	replying.AddWithReplies("a", "b", "c", "d", "key1", "Good", "Bad", "g.cs", "b.cs");
	std::string replyKey, replyScript;
	Expect("files: answer", replying.Answer(1, 2, replyKey, replyScript) && replyKey == "key1" && replyScript == "b.cs");
	Expect("files: save replies", replying.SaveTo(folder / L"GTASAsf4.json"));
	MailStore replied;
	Expect("files: load replies", replied.LoadFrom(folder / L"GTASAsf4.json") && replied.Mails()[0].answer == 2 && replied.Mails()[0].badText == "Bad" && replied.Replies().at("key1") == 2);

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
	// Fuera de la pantalla para no molestar.
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

	const std::wstring mailPath = std::wstring(argv[3]) + L"\\session.json";
	if (!gClient.Start(helper, url, mailPath, gWidth, gHeight, error)) {
		printf("FAIL: %s\n", error.c_str());
		return 1;
	}
	gClient.Send({ ef::EventType::Show });
	gClient.Send({ ef::EventType::MailChanged, 0, 0, gStore.UnreadCount() });

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

	// Escritorio -> navegador con su página de inicio.
	Click(kIconInternet);
	Run(1200);
	Shot("02_home");

	// Cada visita baraja los sitios y los anuncios.
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

	// Un sitio público, con un anuncio de la carpeta compartida.
	GoTo("www.binco.sa", 1200);
	Shot("02c_public_site_with_ad");
	Click(kPublicSiteAd);
	Run(1200);
	Shot("02d_after_clicking_ad");
	Expect("ad: clicking leaves for another site", (gLastShot[size_t(150) * gWidth + 300] & 0xFFFFFF) != 0x1A4F9C);

	// Direcciones extra para revisar a mano: EF_HARNESS_VISIT=a;b;c
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

	// Barra de direcciones -> sitio local, con entrada en la página.
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

	Measure("idle", 3000, false);
	Measure("scrolling", 5000, true);

	// Segunda pestaña -> internet real, sin perder la primera.
	Click(kNewTabWithOneTab);
	Run(1000);

	// Empieza en local: una dirección real pregunta, y negarse no la carga.
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

	// Atrás y adelante, en la pestaña 2: Wikipedia <- inicio.
	Click(kBack);
	Run(1000);
	Shot("09_after_back");
	Click(kForward);
	Run(2500);
	Shot("10_after_forward");

	// Atajos.
	Control('T');
	Run(800);
	Shot("11_ctrl_t");
	Control('W');
	Run(400);
	Shot("12_ctrl_w");

	// Peor caso: una página que se repinta sin parar, en su pestaña.
	Click(kTab1);
	GoTo("www.eyefinder.info/tests.html?animate", 1000);
	Measure("animated page", 5000, false);
	Shot("12a_internet_mode_network_reached");

	// Volver a local no pregunta y saca a las pestañas de las webs reales. Volver a internet pregunta otra vez.
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

	// Minimizar y restaurar conservan las pestañas.
	Click(kMinimize);
	Run(400);
	Shot("13_minimized");
	ExpectPixel("minimized window", { 640, 360 }, kDesktopColor);
	Click(kRestore);
	Run(500);
	Shot("14_restored");
	ExpectPixel("restored window", { 600, 15 }, kTitleBarColor);

	// Correo: abrir, leer, borrar y recibir uno nuevo con la página abierta.
	Click(kMailButton);
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

	// Respuestas: el correo nuevo trae dos, y solo se puede elegir una vez.
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

	// Un botón en una web local pide un script, y solo el de su carpeta.
	GoTo("www.eyefinder.info/tests.html", 1500);
	Click(kRunCleo);
	Run(700);
	Shot("17d_cleo_button");
	{
		std::string siteScript;
		Expect("web button: script requested", gClient.TakeSiteScript(siteScript) && siteScript == "www.eyefinder.info/hello");
	}

	// Lo que promete la FAQ: un archivo nuevo en un sitio existente carga sin reiniciar, y F5 recoge los cambios.
	{
		const std::filesystem::path page = std::filesystem::path(helper).parent_path() / L"sites" / L"www.eyefinder.info" / L"f5test.html";
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

	// El busca, sobre la página. Tiene que acabar solo.
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

	// Ocultar y mostrar (menú de pausa, F8) producen un fotograma nuevo.
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

	// Log Off debe pedir la vuelta al juego.
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

	const DWORD stopStart = GetTickCount();
	gClient.Stop();
	printf("helper stopped in %lu ms\n", GetTickCount() - stopStart);

	gRenderer.Release();
	gDevice->Release();
	d3d->Release();

	puts(gFailures ? "FAIL" : "PASS");
	return gFailures ? 1 : 0;
}
