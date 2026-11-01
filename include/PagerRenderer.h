#pragma once
// The 90s pager: a dot matrix screen drawn with Direct3D 9.
// It doesn't depend on the game, so the harness uses it too.
#include <d3d9.h>

#include <string>
#include <vector>

// The game and the pager's screen only have ASCII: removes accents and whatever can't be shown.
std::string ToPlainAscii(const std::string& utf8);

class PagerRenderer {
public:
	void Show(const std::string& sender, const std::string& text);

	// From the memory: no vibration. If it's already on screen, it changes message without rising again.
	void ShowFromMemory(const std::string& sender, const std::string& text, const std::string& label);
	void Close();
	void Advance(unsigned int milliseconds);
	void Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight);

	bool IsBusy() const { return busy_; }

	// Three beeps, on a Windows thread so as not to stop the game.
	static void PlayBeep();

private:
	void SetMessage(const std::string& sender, const std::string& text);

	std::string header_;
	std::string label_;
	bool isNew_ = true;
	std::vector<std::string> rows_;
	unsigned int elapsed_ = 0;
	unsigned int holdTime_ = 0;
	bool busy_ = false;
};
