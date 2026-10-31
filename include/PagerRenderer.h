#pragma once
// El busca de los 90: una pantalla de matriz de puntos dibujada con Direct3D 9.
// No depende del juego, así que el arnés también la usa.
#include <d3d9.h>

#include <string>
#include <vector>

// El juego y la pantalla del busca solo tienen ASCII: quita acentos y lo que no se pueda mostrar.
std::string ToPlainAscii(const std::string& utf8);

class PagerRenderer {
public:
	void Show(const std::string& sender, const std::string& text);
	void Advance(unsigned int milliseconds);
	void Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight);

	bool IsBusy() const { return busy_; }

	// Tres pitidos, en un hilo de Windows para no detener el juego.
	static void PlayBeep();

private:
	std::string header_;
	std::vector<std::string> rows_;
	unsigned int elapsed_ = 0;
	unsigned int holdTime_ = 0;
	bool busy_ = false;
};
