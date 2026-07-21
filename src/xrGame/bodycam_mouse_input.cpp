#if !defined(BODYCAM_STANDALONE)
#	include "stdafx.h"
#endif
#include "bodycam_mouse_input.h"

#include <cmath>

namespace Bodycam
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kReconcileTolerance = 1.0e-5f;

float NormalizeAngle(float value)
{
	while (value > kPi)
		value -= 2.f * kPi;
	while (value < -kPi)
		value += 2.f * kPi;
	return value;
}

float AngleDifference(float target, float current)
{
	return NormalizeAngle(target - current);
}
} // namespace

void ResetMouseAim(MouseAimState& state, float yaw, float pitch)
{
	state = MouseAimState();
	state.yaw = NormalizeAngle(yaw);
	state.pitch = NormalizeAngle(pitch);
	state.initialized = true;
}

void AddMouseAimDelta(MouseAimState& state, float yaw_delta, float pitch_delta)
{
	state.pending_yaw += yaw_delta;
	state.pending_pitch += pitch_delta;
}

MouseAimOutput ResolveMouseAim(MouseAimState& state, float target_yaw, float target_pitch)
{
	if (!state.initialized)
		ResetMouseAim(state, target_yaw, target_pitch);

	state.yaw = NormalizeAngle(state.yaw + state.pending_yaw);
	state.pitch = NormalizeAngle(state.pitch + state.pending_pitch);

	// Recoil, script cameras, pitch clamps and other engine-side changes remain authoritative.
	if (std::fabs(AngleDifference(target_yaw, state.yaw)) > kReconcileTolerance)
		state.yaw = NormalizeAngle(target_yaw);
	if (std::fabs(AngleDifference(target_pitch, state.pitch)) > kReconcileTolerance)
		state.pitch = NormalizeAngle(target_pitch);

	MouseAimOutput output;
	output.yaw = state.yaw;
	output.pitch = state.pitch;
	output.available = true;

	state.pending_yaw = 0.f;
	state.pending_pitch = 0.f;
	return output;
}
} // namespace Bodycam
