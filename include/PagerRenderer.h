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

	// Desde la memoria: sin vibrar. Si ya está en pantalla, cambia de mensaje sin volver a subir.
	void ShowFromMemory(const std::string& sender, const std::string& text, const std::string& label);
	void Close();
	void Advance(unsigned int milliseconds);
	void Draw(IDirect3DDevice9* device, float screenWidth, float screenHeight);

	bool IsBusy() const { return busy_; }

	// Tres pitidos, en un hilo de Windows para no detener el juego.
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
