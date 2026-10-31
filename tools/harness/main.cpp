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
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "BrowserClient.h"
#include "OverlayRenderer.h"

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

void RenderFrame() {
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

	D3DPRESENT_PARAMETERS parameters{};
	parameters.Windowed = TRUE;
	parameters.SwapEffect = D3DSWAPEFFECT_COPY;
	parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
	parameters.BackBufferWidth = gWidth;
	parameters.BackBufferHeight = gHeight;
	parameters.hDeviceWindow = window;
	parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	if (FAILED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &parameters, &gDevice))) {
		puts("FAIL: CreateDevice");
		return 1;
	}

	std::string error;
	const DWORD launch = GetTickCount();
	if (!gClient.Start(helper, url, gWidth, gHeight, error)) {
		printf("FAIL: %s\n", error.c_str());
		return 1;
	}
	gClient.Send({ ef::EventType::Show });

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
	GoTo("https://en.wikipedia.org/wiki/Grand_Theft_Auto:_San_Andreas", 5000);
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

	// Minimizar y restaurar conservan las pestañas.
	Click(kMinimize);
	Run(400);
	Shot("13_minimized");
	ExpectPixel("minimized window", { 640, 360 }, kDesktopColor);
	Click(kRestore);
	Run(500);
	Shot("14_restored");
	ExpectPixel("restored window", { 600, 15 }, kTitleBarColor);

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

	const DWORD stopStart = GetTickCount();
	gClient.Stop();
	printf("helper stopped in %lu ms\n", GetTickCount() - stopStart);

	gRenderer.Release();
	gDevice->Release();
	d3d->Release();

	puts(gFailures ? "FAIL" : "PASS");
	return gFailures ? 1 : 0;
}
