// Prueba el helper, el protocolo y el renderizado sin el juego, en un proceso de 32 bits.
//
//   harness <helper.exe> <url> <width> <height> <output directory>
//
// Con un d3d9.dll de DXVK junto al ejecutable, también prueba esa ruta.
#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "BrowserClient.h"
#include "OverlayRenderer.h"

namespace {

struct Stats {
	int uploads = 0;
	int draws = 0;
	double uploadMs = 0.0;
	double uploadMaxMs = 0.0;
};

BrowserClient gClient;
OverlayRenderer gRenderer;
IDirect3DDevice9* gDevice = nullptr;
int gWidth = 0;
int gHeight = 0;
float gCursorX = 40.0f;
float gCursorY = 40.0f;

void RenderFrame(Stats& stats) {
	if (const uint8_t* pixels = gClient.AcquireFrame()) {
		if (gRenderer.Upload(gDevice, pixels, gClient.Width(), gClient.Height(), gClient.Stride())) {
			++stats.uploads;
			stats.uploadMs += gRenderer.LastUploadMs();
			stats.uploadMaxMs = (std::max)(stats.uploadMaxMs, gRenderer.LastUploadMs());
		}
	}

	gDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 0, 255), 1.0f, 0);
	gDevice->BeginScene();
	gRenderer.Draw(gDevice, float(gWidth), float(gHeight), gCursorX, gCursorY, gClient.Cursor());
	gDevice->EndScene();
	gDevice->Present(nullptr, nullptr, nullptr, nullptr);
	++stats.draws;
}

void Run(int milliseconds, Stats& stats) {
	const DWORD start = GetTickCount();
	while (GetTickCount() - start < DWORD(milliseconds)) {
		MSG message;
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
		RenderFrame(stats);
		Sleep(1);
	}
}

// Guarda lo que hay realmente en el búfer trasero, no lo que envió el helper.
bool Screenshot(const std::string& path, uint32_t* centerPixel = nullptr) {
	IDirect3DSurface9* target = nullptr;
	IDirect3DSurface9* copy = nullptr;
	if (FAILED(gDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &target))) return false;

	bool ok = false;
	if (SUCCEEDED(gDevice->CreateOffscreenPlainSurface(gWidth, gHeight, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
		SUCCEEDED(gDevice->GetRenderTargetData(target, copy))) {
		D3DLOCKED_RECT locked;
		if (SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
			std::ofstream file(path, std::ios::binary);
			for (int y = 0; y < gHeight; ++y) {
				file.write(static_cast<const char*>(locked.pBits) + y * locked.Pitch, gWidth * 4);
			}
			if (centerPixel) {
				*centerPixel = *reinterpret_cast<const uint32_t*>(static_cast<const char*>(locked.pBits) + (gHeight / 2) * locked.Pitch + (gWidth / 2) * 4);
			}
			copy->UnlockRect();
			ok = file.good();
		}
	}
	if (copy) copy->Release();
	target->Release();
	return ok;
}

void Click(int x, int y, Stats& stats) {
	gCursorX = float(x);
	gCursorY = float(y);
	gClient.Send({ ef::EventType::MouseMove, x, y });
	Run(100, stats);
	gClient.Send({ ef::EventType::MouseDown, x, y, 0, 0, ef::kModLeftMouse });
	gClient.Send({ ef::EventType::MouseUp, x, y, 0, 0, 0 });
	Run(200, stats);
}

void TypeText(const char* text, Stats& stats) {
	for (const char* c = text; *c; ++c) {
		gClient.Send({ ef::EventType::Char, 0, 0, *c, 0, 0 });
		Run(20, stats);
	}
}

void Report(const char* name, const Stats& stats, int milliseconds, uint32_t paints) {
	printf("%-10s draws %.1f/s | browser %.1f frames/s | uploads %.1f/s | upload avg %.2f ms, max %.2f ms\n", name,
		stats.draws * 1000.0 / milliseconds, paints * 1000.0 / milliseconds, stats.uploads * 1000.0 / milliseconds,
		stats.uploads ? stats.uploadMs / stats.uploads : 0.0, stats.uploadMaxMs);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
	if (argc != 6) {
		puts("usage: harness <helper.exe> <url> <width> <height> <output directory>");
		return 2;
	}

	const std::wstring helper = argv[1];
	const std::wstring url = argv[2];
	gWidth = _wtoi(argv[3]);
	gHeight = _wtoi(argv[4]);
	char outputBuffer[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, argv[5], -1, outputBuffer, MAX_PATH, nullptr, nullptr);
	const std::string output = outputBuffer;

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

	Stats startup;
	while (!gRenderer.HasFrame()) {
		if (gClient.HasExited()) {
			puts("FAIL: the helper exited during startup");
			return 1;
		}
		if (GetTickCount() - launch > 30000) {
			puts("FAIL: no frame within 30 s");
			return 1;
		}
		Run(10, startup);
	}
	printf("first frame after %lu ms\n", GetTickCount() - launch);

	Run(2500, startup);

	uint32_t center = 0;
	if (!Screenshot(output + "\\1_loaded.raw", &center)) {
		puts("FAIL: could not read the back buffer");
		return 1;
	}
	// El fondo de Clear es magenta: si sigue ahí, la página no se ha dibujado.
	if ((center & 0xFFFFFF) == 0xFF00FF) {
		puts("FAIL: the page did not reach the back buffer");
		return 1;
	}

	// Entrada: coordenadas de ui/start.html con su disposición por defecto.
	Stats input;
	if (url.find(L"start.html") != std::wstring::npos && url.find(L"animate") == std::wstring::npos) {
		Click(90, 344, input);   // Botón "Click me"
		Run(600, input);         // Dos clics separados, no un doble clic.
		Click(90, 344, input);
		Click(200, 400, input);  // Campo de texto
		TypeText("grove street", input);
		Run(300, input);
		Screenshot(output + "\\2_input.raw");

		Click(118, 456, input);  // Lista desplegable: Chromium la pinta aparte.
		Run(400, input);
		Screenshot(output + "\\2_dropdown.raw");
		Click(118, 515, input);  // Tercera opción
		Run(400, input);
		Screenshot(output + "\\2_picked.raw");
	}

	// Medida 1: página en reposo.
	Stats idle;
	uint32_t paints = gClient.PaintCount();
	Run(3000, idle);
	Report("idle", idle, 3000, gClient.PaintCount() - paints);

	// Medida 2: desplazamiento continuo.
	Stats scroll;
	paints = gClient.PaintCount();
	const DWORD scrollStart = GetTickCount();
	int direction = -120;
	int ticks = 0;
	while (GetTickCount() - scrollStart < 5000) {
		gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, direction, 0 });
		if (++ticks % 30 == 0) direction = -direction;
		Run(16, scroll);
	}
	Report("scrolling", scroll, 5000, gClient.PaintCount() - paints);
	Screenshot(output + "\\3_scrolled.raw");

	// Medida 3: lo que haya en la página durante otros 5 s (con ?animate es el peor caso).
	Stats busy;
	paints = gClient.PaintCount();
	Run(5000, busy);
	Report("page", busy, 5000, gClient.PaintCount() - paints);

	// Internet real desde la barra de direcciones: teclado, Enter y red de extremo a extremo.
	if (url.find(L"start.html") != std::wstring::npos && url.find(L"animate") == std::wstring::npos) {
		gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, 100000, 0 });
		Run(600, busy);
		Click(660, 35, busy);
		gClient.Send({ ef::EventType::KeyDown, 0, 0, 'A', 0, ef::kModControl });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, 'A', 0, ef::kModControl });
		Run(100, busy);
		TypeText("https://en.wikipedia.org/wiki/Grand_Theft_Auto:_San_Andreas", busy);
		gClient.Send({ ef::EventType::KeyDown, 0, 0, VK_RETURN });
		gClient.Send({ ef::EventType::Char, 0, 0, '\r' });
		gClient.Send({ ef::EventType::KeyUp, 0, 0, VK_RETURN });
		Run(6000, busy);
		Screenshot(output + "\\4_real_site.raw");

		Stats realScroll;
		paints = gClient.PaintCount();
		const DWORD realStart = GetTickCount();
		int realTicks = 0;
		int realDirection = -120;
		while (GetTickCount() - realStart < 5000) {
			gClient.Send({ ef::EventType::Wheel, gWidth / 2, gHeight / 2, 0, realDirection, 0 });
			if (++realTicks % 30 == 0) realDirection = -realDirection;
			Run(16, realScroll);
		}
		Report("real site", realScroll, 5000, gClient.PaintCount() - paints);
		Screenshot(output + "\\5_real_site_scrolled.raw");
	}

	// Volver a mostrar debe producir un fotograma nuevo.
	gClient.Send({ ef::EventType::Hide });
	Run(300, busy);
	paints = gClient.PaintCount();
	gClient.Send({ ef::EventType::Show });
	Run(700, busy);
	printf("frames after hide and show: %u\n", gClient.PaintCount() - paints);

	const DWORD stopStart = GetTickCount();
	gClient.Stop();
	printf("helper stopped in %lu ms\n", GetTickCount() - stopStart);

	gRenderer.Release();
	gDevice->Release();
	d3d->Release();
	puts("PASS");
	return 0;
}
