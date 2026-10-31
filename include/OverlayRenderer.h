#pragma once
// Dibuja el navegador con Direct3D 9. No depende del juego, así que el arnés también lo usa.
#include <cstdint>
#include <d3d9.h>

#include "ipc.h"

class OverlayRenderer {
public:
	~OverlayRenderer() { Release(); }

	// Copia un fotograma BGRA a la textura. Devuelve false si Direct3D lo rechaza.
	bool Upload(IDirect3DDevice9* device, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride);

	// Dibuja a pantalla completa y deja el estado del dispositivo como estaba.
	// blackness (0 a 1) oscurece el resultado para los fundidos.
	void Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight, float cursorX, float cursorY, ef::CursorKind cursor, float blackness = 0.0f);

	// Hay que llamarlo cuando se pierde el dispositivo: la textura está en D3DPOOL_DEFAULT.
	void Release();

	bool HasFrame() const { return hasFrame_; }
	double LastUploadMs() const { return lastUploadMs_; }

private:
	IDirect3DTexture9* texture_ = nullptr;
	uint32_t width_ = 0;
	uint32_t height_ = 0;
	bool hasFrame_ = false;
	double lastUploadMs_ = 0.0;
};
