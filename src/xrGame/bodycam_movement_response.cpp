#include "stdafx.h"
#include "bodycam_movement_response.h"
#include "bodycam_settings.h"
#include "actor_defs.h"

#include <algorithm>
#include <cmath>

namespace Bodycam
{
using namespace ACTOR_DEFS;

namespace
{
// The character controller's top speed is the control acceleration over this
// (CPHMovementControl::Calculate).
constexpr float kAccelPerSpeed = 10.f;
constexpr float kEpsilon = 1.0e-5f;
// Seconds. The shortest time to target, and the longest frame that is integrated.
constexpr float kMinResponseTime = 0.01f;
constexpr float kMaxResponseStep = 0.05f;

// Share of the remaining distance covered this frame.
float Response(float seconds, float dt)
{
	return 1.f - std::exp(-std::min(dt, kMaxResponseStep) / std::max(seconds, kMinResponseTime));
}

float XZMagnitude(const Fvector& value)
{
	return std::sqrt(value.x * value.x + value.z * value.z);
}

// 0 for the same planar direction, 1 for a reversal.
float DirectionChange(const Fvector& from, const Fvector& to)
{
	const float from_mag = XZMagnitude(from);
	const float to_mag = XZMagnitude(to);
	if (from_mag <= kEpsilon || to_mag <= kEpsilon)
		return 0.f;

	const float dot = std::clamp((from.x * to.x + from.z * to.z) / (from_mag * to_mag), -1.f, 1.f);
	return (1.f - dot) * 0.5f;
}

bool ShouldBypass(const MovementInertiaSettings& settings, const MovementInertiaInput& input)
{
	if (!settings.enable)
		return true;
	if (settings.disable_ads && input.ads)
		return true;
	if (input.move_flags & (mcCrouch | mcClimb | mcJump | mcFall | mcLanding | mcLanding2))
		return true;
	return false;
}
} // namespace

MovementInertiaOutput UpdateMovementInertia(
	const MovementInertiaSettings& settings, MovementInertiaState& state, const MovementInertiaInput& input)
{
	const float target_speed = XZMagnitude(input.target_accel);
	MovementInertiaOutput output;
	output.target_speed = target_speed / kAccelPerSpeed;

	if (ShouldBypass(settings, input))
	{
		state = {};
		output.accel.set(input.target_accel);
		output.filtered_speed = output.target_speed;
		output.speed_fraction = output.target_speed > kEpsilon ? 1.f : 0.f;
		return output;
	}

	if (!state.initialized)
	{
		state.initialized = true;
		state.accel.set(input.target_accel);
	}

	const float current_speed = XZMagnitude(state.accel);
	const bool stopping = target_speed <= kEpsilon;
	const bool slowing_down = target_speed + kEpsilon < current_speed;
	const bool sprinting = !!(input.move_flags & mcSprint);
	const float turn = DirectionChange(state.accel, input.target_accel);

	float response_time =
		stopping || slowing_down ? settings.decel_time : settings.accel_time / (sprinting ? settings.sprint_speed_scale : 1.f);
	response_time *= 1.f + settings.turn_response * turn;
	if (stopping)
		response_time *= settings.stop_response;

	const float blend = Response(response_time, input.dt);
	state.accel.x += (input.target_accel.x - state.accel.x) * blend;
	state.accel.y = input.target_accel.y;
	state.accel.z += (input.target_accel.z - state.accel.z) * blend;

	// The speed never exceeds what the keys ask for: slowing to a lower speed
	// is immediate, and only a full stop eases out.
	const float new_speed = XZMagnitude(state.accel);
	if (!stopping && new_speed > target_speed)
	{
		const float scale = target_speed / new_speed;
		state.accel.x *= scale;
		state.accel.z *= scale;
	}
	else if (stopping && new_speed < kEpsilon)
	{
		state.accel.x = 0.f;
		state.accel.z = 0.f;
	}

	output.accel.set(state.accel);
	output.filtered_speed = XZMagnitude(state.accel) / kAccelPerSpeed;
	output.speed_fraction = output.target_speed > kEpsilon ? output.filtered_speed / output.target_speed : 0.f;
	return output;
}
} // namespace Bodycam
