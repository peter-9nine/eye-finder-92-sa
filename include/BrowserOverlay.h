#pragma once
#include "pch.h"
#include "BrowserClient.h"
#include "OverlayRenderer.h"

// Conecta el navegador con el juego: abrir y cerrar, entrada, controles, menú de pausa y dibujo.
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

	// El juego no debe recentrar el cursor mientras el navegador lo usa.
	bool UsesCursor() const;
	bool IsOpen() const { return phase_ != Phase::Closed; }

	void SetUnreadMail(int unread);
	bool TakeMailOp(ef::MailOp& operation) { return client_.TakeMailOp(operation); }

private:
	// Cerrado -> fundido a negro del juego -> abierto -> fundido a negro del navegador -> cerrado.
	enum class Phase { Closed, FadingGameOut, Open, FadingBrowserOut };

	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

	void BeginOpening();
	void FinishOpening();
	void BeginClosing();
	void FinishClosing();
	void ReleaseGame();
	void SyncPauseMenu();
	void CheckForCyberCafe();
	bool EnsureHelper();
	void UpdateMouse();
	void ForwardKey(ef::EventType type, WPARAM wParam, LPARAM lParam);
	void LogStats();
	float Blackness() const;

	BrowserClient client_;
	OverlayRenderer renderer_;

	HWND window_ = nullptr;
	WNDPROC originalProc_ = nullptr;

	Phase phase_ = Phase::Closed;
	DWORD phaseStart_ = 0;
	bool togglePending_ = false;
	bool suspendedByMenu_ = false;
	unsigned short savedControls_ = 0;

	int mouseX_ = 0;
	int mouseY_ = 0;
	bool haveMouse_ = false;
	uint32_t buttons_ = 0;
	int unreadMail_ = 0;

	// Estadísticas del prototipo.
	DWORD statsStart_ = 0;
	uint32_t statsPaintsStart_ = 0;
	uint32_t statsUploads_ = 0;
	uint32_t statsDraws_ = 0;
	double statsUploadMs_ = 0.0;
	double statsUploadMaxMs_ = 0.0;
};
