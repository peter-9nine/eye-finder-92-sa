#include "pch.h"
#include "PadInput.h"
#include "GInputAPI.h"
#include "Paths.h"

#include <cmath>

namespace {

// GInput already applies the player's deadzone: this only stops the cursor drifting on a tiny remainder.
constexpr float kStickDeadzone = 0.08f;

// In CPad, the axes go from -128 to 127 and up is negative.
void ReadStick(short rawX, short rawY, float& x, float& y) {
	const float fx = std::clamp(rawX / 128.0f, -1.0f, 1.0f);
	const float fy = std::clamp(-rawY / 128.0f, -1.0f, 1.0f);
	const float length = std::hypot(fx, fy);
	if (length <= kStickDeadzone) {
		x = y = 0.0f;
		return;
	}

	// No jump on leaving the deadzone.
	const float scaled = (std::min)((length - kStickDeadzone) / (1.0f - kStickDeadzone), 1.0f);
	x = fx / length * scaled;
	y = fy / length * scaled;
}

}  // namespace

void PadInput::Initialize() {
	IGInputPad* pad = nullptr;
	if (!GInput_Load(&pad)) {
		Log("GInput not found: no controller support");
		return;
	}

	ginput_ = pad;

	char line[64];
	snprintf(line, sizeof(line), "GInput version %08X found: controller enabled", static_cast<unsigned>(pad->GetVersion()));
	Log(line);
}

void PadInput::Shutdown() {
	if (!ginput_) return;
	ginput_ = nullptr;
	GInput_Release();
}

void PadInput::Update() {
	previous_ = held_;
	held_ = 0;
	leftX_ = leftY_ = rightX_ = rightY_ = 0.0f;

	inHands_ = ginput_ && ginput_->HasPadInHands();
	if (!inHands_) return;

	// The game updates NewState even with the player's controls disabled.
	const CControllerState& state = CPad::GetPad(0)->NewState;

	if (state.DPadUp) held_ |= kPadDPadUp;
	if (state.DPadDown) held_ |= kPadDPadDown;
	if (state.DPadLeft) held_ |= kPadDPadLeft;
	if (state.DPadRight) held_ |= kPadDPadRight;
	if (state.LeftShoulder1) held_ |= kPadLeftShoulder;
	if (state.RightShoulder1) held_ |= kPadRightShoulder;
	if (state.ButtonCross) held_ |= kPadA;
	if (state.ButtonCircle) held_ |= kPadB;
	if (state.ButtonSquare) held_ |= kPadX;
	if (state.ButtonTriangle) held_ |= kPadY;
	if (state.Select) held_ |= kPadSelect;
	if (state.ShockButtonL) held_ |= kPadLeftThumb;

	ReadStick(state.LeftStickX, state.LeftStickY, leftX_, leftY_);
	ReadStick(state.RightStickX, state.RightStickY, rightX_, rightY_);
}
