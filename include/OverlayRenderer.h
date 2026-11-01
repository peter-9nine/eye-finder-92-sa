#pragma once
// Draws the browser with Direct3D 9. It doesn't depend on the game, so the harness uses it too.
#include <cstdint>
#include <d3d9.h>

#include "ipc.h"

class OverlayRenderer {
public:
	~OverlayRenderer() { Release(); }

	// Copies a BGRA frame to the texture. Returns false if Direct3D refuses it.
	bool Upload(IDirect3DDevice9* device, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride);

	// Draws full screen and leaves the device state as it was.
	// blackness (0 to 1) darkens the result for fades.
	void Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight, float cursorX, float cursorY, ef::CursorKind cursor, float blackness = 0.0f);

	// Must be called when the device is lost, because the texture is in D3DPOOL_DEFAULT.
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
