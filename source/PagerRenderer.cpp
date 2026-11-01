#include "PagerRenderer.h"

#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "winmm.lib")

namespace {

constexpr int kColumns = 18;
constexpr int kBodyRows = 2;
constexpr unsigned int kSlideMs = 260;
constexpr unsigned int kPageMs = 2800;
constexpr unsigned int kMinimumHoldMs = 6000;
constexpr unsigned int kVibrateMs = 1100;

const D3DCOLOR kBodyColor = D3DCOLOR_XRGB(38, 38, 42);
const D3DCOLOR kBodyLight = D3DCOLOR_XRGB(72, 72, 78);
const D3DCOLOR kBodyDark = D3DCOLOR_XRGB(16, 16, 18);
const D3DCOLOR kScreenColor = D3DCOLOR_XRGB(142, 168, 92);
const D3DCOLOR kScreenBezel = D3DCOLOR_XRGB(10, 12, 8);
const D3DCOLOR kDotColor = D3DCOLOR_XRGB(30, 42, 24);
const D3DCOLOR kBrandColor = D3DCOLOR_XRGB(205, 205, 205);
const D3DCOLOR kLedOn = D3DCOLOR_XRGB(255, 50, 40);
const D3DCOLOR kLedOff = D3DCOLOR_XRGB(70, 18, 16);
const D3DCOLOR kButtonColor = D3DCOLOR_XRGB(96, 96, 104);

// Classic 5x7 font, ASCII 32 to 95. Each byte is a column, with the low bit at the top.
const uint8_t kFont[64][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x5F, 0x00, 0x00 }, { 0x00, 0x07, 0x00, 0x07, 0x00 }, { 0x14, 0x7F, 0x14, 0x7F, 0x14 },
	{ 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, { 0x23, 0x13, 0x08, 0x64, 0x62 }, { 0x36, 0x49, 0x55, 0x22, 0x50 }, { 0x00, 0x05, 0x03, 0x00, 0x00 },
	{ 0x00, 0x1C, 0x22, 0x41, 0x00 }, { 0x00, 0x41, 0x22, 0x1C, 0x00 }, { 0x14, 0x08, 0x3E, 0x08, 0x14 }, { 0x08, 0x08, 0x3E, 0x08, 0x08 },
	{ 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x08, 0x08, 0x08, 0x08, 0x08 }, { 0x00, 0x60, 0x60, 0x00, 0x00 }, { 0x20, 0x10, 0x08, 0x04, 0x02 },
	{ 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 }, { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
	{ 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 }, { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
	{ 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }, { 0x00, 0x36, 0x36, 0x00, 0x00 }, { 0x00, 0x56, 0x36, 0x00, 0x00 },
	{ 0x08, 0x14, 0x22, 0x41, 0x00 }, { 0x14, 0x14, 0x14, 0x14, 0x14 }, { 0x00, 0x41, 0x22, 0x14, 0x08 }, { 0x02, 0x01, 0x51, 0x09, 0x06 },
	{ 0x32, 0x49, 0x79, 0x41, 0x3E }, { 0x7E, 0x11, 0x11, 0x11, 0x7E }, { 0x7F, 0x49, 0x49, 0x49, 0x36 }, { 0x3E, 0x41, 0x41, 0x41, 0x22 },
	{ 0x7F, 0x41, 0x41, 0x22, 0x1C }, { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 }, { 0x3E, 0x41, 0x49, 0x49, 0x7A },
	{ 0x7F, 0x08, 0x08, 0x08, 0x7F }, { 0x00, 0x41, 0x7F, 0x41, 0x00 }, { 0x20, 0x40, 0x41, 0x3F, 0x01 }, { 0x7F, 0x08, 0x14, 0x22, 0x41 },
	{ 0x7F, 0x40, 0x40, 0x40, 0x40 }, { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F }, { 0x3E, 0x41, 0x41, 0x41, 0x3E },
	{ 0x7F, 0x09, 0x09, 0x09, 0x06 }, { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 }, { 0x46, 0x49, 0x49, 0x49, 0x31 },
	{ 0x01, 0x01, 0x7F, 0x01, 0x01 }, { 0x3F, 0x40, 0x40, 0x40, 0x3F }, { 0x1F, 0x20, 0x40, 0x20, 0x1F }, { 0x3F, 0x40, 0x38, 0x40, 0x3F },
	{ 0x63, 0x14, 0x08, 0x14, 0x63 }, { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 }, { 0x00, 0x7F, 0x41, 0x41, 0x00 },
	{ 0x02, 0x04, 0x08, 0x10, 0x20 }, { 0x00, 0x41, 0x41, 0x7F, 0x00 }, { 0x04, 0x02, 0x01, 0x02, 0x04 }, { 0x40, 0x40, 0x40, 0x40, 0x40 },
};

struct Vertex {
	float x, y, z, rhw;
	D3DCOLOR color;
};

// Everything is collected and drawn at once, because there are thousands of dots.
class Batch {
public:
	void Rect(float left, float top, float right, float bottom, D3DCOLOR color) {
		const Vertex a{ left, top, 0.0f, 1.0f, color };
		const Vertex b{ right, top, 0.0f, 1.0f, color };
		const Vertex c{ left, bottom, 0.0f, 1.0f, color };
		const Vertex d{ right, bottom, 0.0f, 1.0f, color };
		vertices_.insert(vertices_.end(), { a, b, c, b, d, c });
	}

	// A rectangle with clipped corners, so that it doesn't look like a box.
	void Rounded(float left, float top, float right, float bottom, float cut, D3DCOLOR color) {
		Rect(left + cut, top, right - cut, bottom, color);
		Rect(left, top + cut, left + cut, bottom - cut, color);
		Rect(right - cut, top + cut, right, bottom - cut, color);
		const float half = cut * 0.5f;
		Rect(left + half, top + half, left + cut, top + cut, color);
		Rect(right - cut, top + half, right - half, top + cut, color);
		Rect(left + half, bottom - cut, left + cut, bottom - half, color);
		Rect(right - cut, bottom - cut, right - half, bottom - half, color);
	}

	void Text(const std::string& text, float x, float y, float dot, D3DCOLOR color) {
		for (const char character : text) {
			const auto index = static_cast<unsigned char>(character) - 32;
			if (index >= 0 && index < 64) {
				for (int column = 0; column < 5; ++column) {
					for (int row = 0; row < 7; ++row) {
						if (kFont[index][column] & (1 << row)) {
							// A gap between dots: that's what makes it look like an LCD screen.
							const float gap = dot >= 4.0f ? 1.0f : 0.0f;
							const float dotX = x + column * dot;
							const float dotY = y + row * dot;
							Rect(dotX, dotY, dotX + dot - gap, dotY + dot - gap, color);
						}
					}
				}
			}
			x += 6.0f * dot;
		}
	}

	void Flush(IDirect3DDevice9* device) const {
		if (!vertices_.empty()) {
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices_.size() / 3), vertices_.data(), sizeof(Vertex));
		}
	}

private:
	std::vector<Vertex> vertices_;
};

std::string ToUpper(std::string text) {
	for (char& character : text) {
		if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
	}
	return text;
}

std::vector<std::string> WrapWords(const std::string& text, size_t width) {
	std::vector<std::string> rows;
	std::string row;

	size_t position = 0;
	while (position < text.size()) {
		size_t end = text.find_first_of(" \n", position);
		if (end == std::string::npos) end = text.size();
		std::string word = text.substr(position, end - position);
		const bool newline = end < text.size() && text[end] == '\n';
		position = end + 1;

		// A word longer than the screen gets split.
		while (word.size() > width) {
			if (!row.empty()) {
				rows.push_back(row);
				row.clear();
			}
			rows.push_back(word.substr(0, width));
			word.erase(0, width);
		}

		if (!row.empty() && row.size() + 1 + word.size() > width) {
			rows.push_back(row);
			row.clear();
		}
		if (!word.empty()) {
			if (!row.empty()) row += ' ';
			row += word;
		}
		if (newline && !row.empty()) {
			rows.push_back(row);
			row.clear();
		}
	}
	if (!row.empty()) rows.push_back(row);
	return rows;
}

std::vector<uint8_t> BuildBeepWave() {
	constexpr uint32_t kSampleRate = 22050;
	constexpr int kBeeps = 3;
	constexpr uint32_t kOnSamples = kSampleRate * 90 / 1000;
	constexpr uint32_t kOffSamples = kSampleRate * 70 / 1000;
	constexpr uint32_t kPeriod = kSampleRate / 2800;
	constexpr uint32_t kSamples = (kOnSamples + kOffSamples) * kBeeps;

	std::vector<uint8_t> wave(44 + kSamples, 128);
	const auto put32 = [&wave](size_t at, uint32_t value) { std::memcpy(wave.data() + at, &value, 4); };
	const auto put16 = [&wave](size_t at, uint16_t value) { std::memcpy(wave.data() + at, &value, 2); };

	std::memcpy(wave.data(), "RIFF", 4);
	put32(4, 36 + kSamples);
	std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
	put32(16, 16);
	put16(20, 1);
	put16(22, 1);
	put32(24, kSampleRate);
	put32(28, kSampleRate);
	put16(32, 1);
	put16(34, 8);
	std::memcpy(wave.data() + 36, "data", 4);
	put32(40, kSamples);

	// A quiet square wave: a tiny speaker.
	for (uint32_t sample = 0; sample < kSamples; ++sample) {
		if (sample % (kOnSamples + kOffSamples) < kOnSamples) {
			wave[44 + sample] = (sample % kPeriod) < kPeriod / 2 ? 158 : 98;
		}
	}
	return wave;
}

}  // namespace

std::string ToPlainAscii(const std::string& utf8) {
	std::string result;
	result.reserve(utf8.size());

	for (size_t i = 0; i < utf8.size(); ++i) {
		const auto byte = static_cast<unsigned char>(utf8[i]);

		if (byte == '\n') {
			result += '\n';
		}
		else if (byte == '\t' || byte == '\r') {
			result += ' ';
		}
		else if (byte < 0x80) {
			// "~" starts the game's format codes: another mod must not be able to use them by accident.
			if (byte >= 0x20 && byte != '~') result += static_cast<char>(byte);
		}
		else if ((byte == 0xC3) && i + 1 < utf8.size()) {
			// Accented Latin letters: the accent is removed.
			static const char kLatin[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
			const auto next = static_cast<unsigned char>(utf8[++i]);
			result += (next >= 0x80 && next < 0xC0) ? kLatin[next - 0x80] : '?';
		}
		else {
			// Any other character becomes a single "?".
			int extra = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
			while (extra-- > 0 && i + 1 < utf8.size()) ++i;
			result += '?';
		}
	}
	return result;
}

void PagerRenderer::Show(const std::string& sender, const std::string& text) {
	SetMessage(sender, text);
	label_.clear();
	isNew_ = true;
	elapsed_ = 0;
	busy_ = true;
}

void PagerRenderer::ShowFromMemory(const std::string& sender, const std::string& text, const std::string& label) {
	const bool alreadyUp = busy_ && elapsed_ >= kSlideMs && elapsed_ <= kSlideMs + holdTime_;
	SetMessage(sender, text);
	label_ = ToUpper(label);
	isNew_ = false;
	elapsed_ = alreadyUp ? kSlideMs : 0;
	busy_ = true;
}

void PagerRenderer::Close() {
	if (busy_ && elapsed_ < kSlideMs + holdTime_) elapsed_ = kSlideMs + holdTime_;
}

void PagerRenderer::SetMessage(const std::string& sender, const std::string& text) {
	const std::string name = ToUpper(ToPlainAscii(sender));
	header_ = name.empty() ? "NEW MESSAGE" : ("FROM " + name).substr(0, kColumns);
	rows_ = WrapWords(ToUpper(ToPlainAscii(text)), kColumns);

	const unsigned int pages = static_cast<unsigned int>((rows_.size() + kBodyRows - 1) / kBodyRows);
	holdTime_ = (std::max)(kMinimumHoldMs, pages * kPageMs);
}

void PagerRenderer::Advance(unsigned int milliseconds) {
	if (!busy_) return;
	elapsed_ += milliseconds;
	if (elapsed_ >= kSlideMs + holdTime_ + kSlideMs) busy_ = false;
}

void PagerRenderer::Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight) {
	if (!busy_) return;

	// Whole dots, so that the text stays sharp at any resolution.
	const float dot = (std::max)(2.0f, std::floor(screenHeight / 240.0f));
	const float unit = dot / 3.0f;

	const float screenInnerWidth = kColumns * 6.0f * dot;
	const float screenInnerHeight = (kBodyRows + 1) * 8.0f * dot + 3.0f * dot;
	const float pad = 8.0f * unit;
	const float screenWide = screenInnerWidth + pad * 2.0f;
	const float screenTall = screenInnerHeight + pad * 2.0f;

	const float width = screenWide + 28.0f * unit;
	const float height = screenTall + 58.0f * unit;

	// Rises from below, stays and goes back down. The radar is on the left.
	float visibility = 1.0f;
	if (elapsed_ < kSlideMs) visibility = float(elapsed_) / kSlideMs;
	else if (elapsed_ > kSlideMs + holdTime_) visibility = 1.0f - float(elapsed_ - kSlideMs - holdTime_) / kSlideMs;
	visibility = 1.0f - (1.0f - visibility) * (1.0f - visibility);

	const float margin = 24.0f * unit;
	float x = std::floor(screenWidth - width - margin);
	const float y = std::floor(screenHeight - (height + margin) * visibility);

	// Vibrates on receiving, as it would on a belt.
	if (isNew_ && elapsed_ > kSlideMs && elapsed_ < kSlideMs + kVibrateMs) {
		x += ((elapsed_ / 30) % 2 == 0) ? 2.0f * unit : -2.0f * unit;
	}

	Batch batch;
	const float cut = 8.0f * unit;

	batch.Rounded(x - 2 * unit, y - 2 * unit, x + width + 2 * unit, y + height + 2 * unit, cut, kBodyDark);
	batch.Rounded(x, y, x + width, y + height, cut, kBodyLight);
	batch.Rounded(x + 2 * unit, y + 3 * unit, x + width - unit, y + height - unit, cut, kBodyColor);

	// Brand and alert light.
	batch.Text("WHIZ", x + 16 * unit, y + 11 * unit, (std::max)(1.0f, std::floor(dot * 0.67f)), kBrandColor);
	const bool ledOn = isNew_ && (elapsed_ / 350) % 2 == 0;
	const float ledRight = x + width - 16 * unit;
	batch.Rounded(ledRight - 14 * unit, y + 12 * unit, ledRight, y + 22 * unit, 3 * unit, ledOn ? kLedOn : kLedOff);

	// Which message in the memory this is.
	if (!label_.empty()) {
		const float labelDot = (std::max)(1.0f, std::floor(dot * 0.67f));
		const float labelWidth = label_.size() * 6.0f * labelDot;
		batch.Text(label_, std::floor(ledRight - 24 * unit - labelWidth), y + 11 * unit, labelDot, kBrandColor);
	}

	// Screen
	const float screenLeft = x + 14 * unit;
	const float screenTop = y + 30 * unit;
	batch.Rect(screenLeft - 3 * unit, screenTop - 3 * unit, screenLeft + screenWide + 3 * unit, screenTop + screenTall + 3 * unit, kScreenBezel);
	batch.Rect(screenLeft, screenTop, screenLeft + screenWide, screenTop + screenTall, kScreenColor);

	const float textLeft = std::floor(screenLeft + pad);
	const float textTop = std::floor(screenTop + pad);
	batch.Text(header_, textLeft, textTop, dot, kDotColor);
	batch.Rect(textLeft, textTop + 8.0f * dot, textLeft + screenInnerWidth - dot, textTop + 8.0f * dot + (std::max)(1.0f, dot * 0.5f), kDotColor);

	const size_t page = rows_.empty() ? 0 : (elapsed_ < kSlideMs ? 0 : ((elapsed_ - kSlideMs) / kPageMs) % ((rows_.size() + kBodyRows - 1) / kBodyRows));
	for (size_t row = 0; row < kBodyRows; ++row) {
		const size_t index = page * kBodyRows + row;
		if (index < rows_.size()) {
			batch.Text(rows_[index], textLeft, textTop + (2.0f * dot) + (row + 1) * 8.0f * dot + dot, dot, kDotColor);
		}
	}

	// Buttons
	const float buttonTop = screenTop + screenTall + 10 * unit;
	for (int button = 0; button < 3; ++button) {
		const float left = x + width * 0.5f + (button - 1.5f) * 44.0f * unit + 2.0f * unit;
		batch.Rounded(left, buttonTop, left + 40 * unit, buttonTop + 9 * unit, 3 * unit, kButtonColor);
	}

	// RenderWare keeps its own copy of the state, so everything must be left as it was.
	IDirect3DStateBlock9* saved = nullptr;
	if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) return;

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
	device->SetTexture(0, nullptr);
	device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

	batch.Flush(device);

	saved->Apply();
	saved->Release();
}

void PagerRenderer::PlayBeep() {
	// Windows keeps reading this buffer while it plays, so it must last forever.
	static const std::vector<uint8_t> wave = BuildBeepWave();
	PlaySoundA(reinterpret_cast<LPCSTR>(wave.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
