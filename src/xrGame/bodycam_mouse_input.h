#pragma once

namespace Bodycam
{
struct MouseAimState
{
	float yaw = 0.f;
	float pitch = 0.f;
	float pending_yaw = 0.f;
	float pending_pitch = 0.f;
	bool initialized = false;
};

struct MouseAimOutput
{
	float yaw = 0.f;
	float pitch = 0.f;
	bool available = false;
};

void ResetMouseAim(MouseAimState& state, float yaw, float pitch);
void AddMouseAimDelta(MouseAimState& state, float yaw_delta, float pitch_delta);
void RebaseMouseAim(MouseAimState& state, float yaw_delta, float pitch_delta);
MouseAimOutput ResolveMouseAim(MouseAimState& state, float target_yaw, float target_pitch);
} // namespace Bodycam
