#pragma once
#include "pch.h"

class IGInputPad;

// The pad buttons the mod uses, each with its CControllerState field.
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
	kPadSelect = 1u << 10,
	kPadLeftThumb = 1u << 11,
};

// The pad. GInput only says whether the player is using it, and its state is read from CPad::GetPad(0).
// GInput has to be asked because the mouse also fills those fields, so a click could arrive as a button.
class PadInput {
public:
	// Can't be called from DllMain: GInput might not be ready.
	void Initialize();
	void Shutdown();

	// Once per frame, before anything that uses it.
	void Update();

	// Whether the player is using the pad right now, rather than the keyboard and mouse.
	bool InHands() const { return inHands_; }

	uint32_t Held() const { return held_; }
	bool Held(PadButton button) const { return (held_ & button) != 0; }
	bool Pressed(PadButton button) const { return (held_ & button) && !(previous_ & button); }
	bool Released(PadButton button) const { return !(held_ & button) && (previous_ & button); }

	// From -1 to 1. Positive is right and up.
	float LeftX() const { return leftX_; }
	float LeftY() const { return leftY_; }
	float RightX() const { return rightX_; }
	float RightY() const { return rightY_; }

private:
	IGInputPad* ginput_ = nullptr;
	bool inHands_ = false;

	uint32_t held_ = 0;
	uint32_t previous_ = 0;
	float leftX_ = 0.0f;
	float leftY_ = 0.0f;
	float rightX_ = 0.0f;
	float rightY_ = 0.0f;
};
