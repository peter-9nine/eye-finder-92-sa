#include "OverlayRenderer.h"

#include <cstring>

namespace {

struct TexturedVertex {
	float x, y, z, rhw, u, v;
};
constexpr DWORD kTexturedFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;

struct ColorVertex {
	float x, y, z, rhw;
	D3DCOLOR color;
};
constexpr DWORD kColorFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

void SetBaseState(IDirect3DDevice9* device) {
	device->SetVertexShader(nullptr);
	device->SetPixelShader(nullptr);
	device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

void DrawPage(IDirect3DDevice9* device, IDirect3DTexture9* texture, float width, float height) {
	device->SetTexture(0, texture);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
	device->SetFVF(kTexturedFvf);

	const TexturedVertex quad[4] = {
		{ -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f },
		{ width - 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.0f },
		{ -0.5f, height - 0.5f, 0.0f, 1.0f, 0.0f, 1.0f },
		{ width - 0.5f, height - 0.5f, 0.0f, 1.0f, 1.0f, 1.0f },
	};
	device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(TexturedVertex));
}

void PrepareColorDraw(IDirect3DDevice9* device) {
	device->SetTexture(0, nullptr);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->SetFVF(kColorFvf);
}

void DrawRect(IDirect3DDevice9* device, float left, float top, float right, float bottom, D3DCOLOR color) {
	const ColorVertex quad[4] = {
		{ left, top, 0.0f, 1.0f, color },
		{ right, top, 0.0f, 1.0f, color },
		{ left, bottom, 0.0f, 1.0f, color },
		{ right, bottom, 0.0f, 1.0f, color },
	};
	device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ColorVertex));
}

// Classic arrow: a fan of triangles from the tip.
void DrawArrow(IDirect3DDevice9* device, float x, float y, D3DCOLOR color) {
	const float points[][2] = { { 0, 0 }, { 0, 17 }, { 4, 13 }, { 7, 20 }, { 10, 19 }, { 7, 12 }, { 12, 12 } };
	ColorVertex fan[7];
	for (int i = 0; i < 7; ++i) {
		fan[i] = { x + points[i][0], y + points[i][1], 0.0f, 1.0f, color };
	}
	device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 5, fan, sizeof(ColorVertex));
}

// The pointing hand, one character per pixel: # is the outline, o the fill. The fingertip is the hot spot.
void DrawHand(IDirect3DDevice9* device, float x, float y) {
	static constexpr const char* kRows[] = {
		"     ##        ",
		"    #oo#       ",
		"    #oo#       ",
		"    #oo#       ",
		"    #oo###     ",
		"    #oo#oo###  ",
		"    #oo#oo#oo# ",
		" ## #oo#oo#oo##",
		"#oo##ooooooo#o#",
		"#ooo#oooooooooo#",
		" #ooooooooooooo#",
		"  #oooooooooooo#",
		"  #ooooooooooo# ",
		"   #oooooooooo# ",
		"    #oooooooo#  ",
		"    #oooooooo#  ",
		"    ##########  ",
	};
	constexpr float kHotSpotX = 6.0f;

	int row = 0;
	for (const char* pixels : kRows) {
		for (int column = 0; pixels[column]; ) {
			const char kind = pixels[column];
			int end = column + 1;
			while (pixels[end] == kind) ++end;
			if (kind != ' ') {
				const float left = x - kHotSpotX + column;
				DrawRect(device, left, y + row, left + (end - column), y + row + 1.0f, kind == '#' ? D3DCOLOR_XRGB(0, 0, 0) : D3DCOLOR_XRGB(255, 255, 255));
			}
			column = end;
		}
		++row;
	}
}

void DrawCursor(IDirect3DDevice9* device, float x, float y, ef::CursorKind cursor) {
	PrepareColorDraw(device);

	if (cursor == ef::CursorKind::Hand) {
		DrawHand(device, x, y);
		return;
	}

	if (cursor == ef::CursorKind::IBeam) {
		DrawRect(device, x - 2, y - 10, x + 3, y + 11, D3DCOLOR_XRGB(255, 255, 255));
		DrawRect(device, x - 1, y - 9, x + 2, y + 10, D3DCOLOR_XRGB(0, 0, 0));
		return;
	}

	// The outline is offset black copies, so it shows on any background.
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (dx != 0 || dy != 0) DrawArrow(device, x + dx, y + dy, D3DCOLOR_XRGB(0, 0, 0));
		}
	}
	DrawArrow(device, x, y, D3DCOLOR_XRGB(255, 255, 255));
}

}  // namespace

bool OverlayRenderer::Upload(IDirect3DDevice9* device, const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride) {
	if (!texture_ || width_ != width || height_ != height) {
		Release();
		if (FAILED(device->CreateTexture(width, height, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture_, nullptr))) {
			texture_ = nullptr;
			return false;
		}
		width_ = width;
		height_ = height;
	}

	LARGE_INTEGER frequency, start, end;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);

	D3DLOCKED_RECT locked;
	if (FAILED(texture_->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD))) return false;

	if (static_cast<uint32_t>(locked.Pitch) == stride) {
		std::memcpy(locked.pBits, pixels, size_t(stride) * height);
	}
	else {
		auto* destination = static_cast<uint8_t*>(locked.pBits);
		for (uint32_t row = 0; row < height; ++row) {
			std::memcpy(destination + size_t(row) * locked.Pitch, pixels + size_t(row) * stride, size_t(width) * 4);
		}
	}
	texture_->UnlockRect(0);

	QueryPerformanceCounter(&end);
	lastUploadMs_ = double(end.QuadPart - start.QuadPart) * 1000.0 / double(frequency.QuadPart);
	hasFrame_ = true;
	return true;
}

void OverlayRenderer::Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight, float cursorX, float cursorY, ef::CursorKind cursor, float blackness) {
	// RenderWare keeps its own copy of the state, so everything must be left as it was.
	IDirect3DStateBlock9* saved = nullptr;
	if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;

	SetBaseState(device);

	if (hasFrame_) {
		DrawPage(device, texture_, screenWidth, screenHeight);
	}
	else {
		// Background while the helper starts.
		PrepareColorDraw(device);
		DrawRect(device, 0, 0, screenWidth, screenHeight, D3DCOLOR_XRGB(0, 60, 120));
	}

	DrawCursor(device, cursorX, cursorY, cursor);

	if (blackness > 0.0f) {
		PrepareColorDraw(device);
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
		const auto alpha = static_cast<DWORD>((blackness > 1.0f ? 1.0f : blackness) * 255.0f);
		DrawRect(device, 0, 0, screenWidth, screenHeight, D3DCOLOR_ARGB(alpha, 0, 0, 0));
	}

	saved->Apply();
	saved->Release();
}

void OverlayRenderer::Release() {
	if (texture_) {
		texture_->Release();
		texture_ = nullptr;
	}
	hasFrame_ = false;
}
