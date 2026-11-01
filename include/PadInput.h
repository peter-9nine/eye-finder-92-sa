#pragma once
#include "pch.h"

class IGInputPad;

// Los botones del mando que usa el mod, cada uno con su campo de CControllerState.
enum PadButton : uint32_t {
	kPadDPadUp = 1u << 0,
	kPadDPadDown = 1u << 1,
	kPadDPadLeft = 1u << 2,
	kPadDPadRight = 1u << 3,
	kPadLeftShoulder = 1u << 4,
	kPadRightShoulder = 1u << 5,
	kPadA = 1u << 6,
	kPadB = 1u << 7,
	kPadX = 1u << 8,
	kPadY = 1u << 9,
};

// El mando. GInput solo dice si el jugador lo está usando, y el estado se lee de CPad::GetPad(0).
// Hay que preguntárselo a GInput porque el ratón también llena esos campos: un clic llegaría como un botón.
class PadInput {
public:
	// No se puede llamar desde DllMain: GInput podría no estar listo.
	void Initialize();
	void Shutdown();

	// Una vez por fotograma, antes que quien lo use.
	void Update();

	uint32_t Held() const { return held_; }
	bool Held(PadButton button) const { return (held_ & button) != 0; }
	bool Pressed(PadButton button) const { return (held_ & button) && !(previous_ & button); }
	bool Released(PadButton button) const { return !(held_ & button) && (previous_ & button); }

	// De -1 a 1. Positivo es derecha y arriba.
	float LeftX() const { return leftX_; }
	float LeftY() const { return leftY_; }
	float RightX() const { return rightX_; }
	float RightY() const { return rightY_; }

private:
	IGInputPad* ginput_ = nullptr;

	uint32_t held_ = 0;
	uint32_t previous_ = 0;
	float leftX_ = 0.0f;
	float leftY_ = 0.0f;
	float rightX_ = 0.0f;
	float rightY_ = 0.0f;
};
