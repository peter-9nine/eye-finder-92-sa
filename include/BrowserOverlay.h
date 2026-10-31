#pragma once
#include "pch.h"
#include "BrowserClient.h"
#include "OverlayRenderer.h"

// Conecta el navegador con el juego: abrir y cerrar, entrada, controles y dibujo.
class BrowserOverlay {
public:
	void InstallWindowHook();
	void RemoveWindowHook();

	void Update();
	void Draw();
	void Restart();
	void OnDeviceLost();
	void OnDeviceReset();
	void Shutdown();

	bool IsOpen() const { return open_; }

private:
	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

	void Open();
	void Close();
	bool EnsureHelper();
	void UpdateMouse();
	void ForwardKey(ef::EventType type, WPARAM wParam, LPARAM lParam);
	void LogStats();

	BrowserClient client_;
	OverlayRenderer renderer_;

	HWND window_ = nullptr;
	WNDPROC originalProc_ = nullptr;

	bool open_ = false;
	bool togglePending_ = false;
	unsigned short savedControls_ = 0;

	int mouseX_ = 0;
	int mouseY_ = 0;
	bool haveMouse_ = false;
	uint32_t buttons_ = 0;

	// Estadísticas del prototipo.
	DWORD statsStart_ = 0;
	uint32_t statsPaintsStart_ = 0;
	uint32_t statsUploads_ = 0;
	uint32_t statsDraws_ = 0;
	double statsUploadMs_ = 0.0;
	double statsUploadMaxMs_ = 0.0;
};
