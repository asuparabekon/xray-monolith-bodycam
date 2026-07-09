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
constexpr float kVelocityScale = 10.f;
constexpr float kEpsilon = 1.0e-5f;

float Clamp(float value, float min_value, float max_value)
{
	return value < min_value ? min_value : (value > max_value ? max_value : value);
}

float Response(float seconds, float dt)
{
	seconds = std::max(seconds, 0.01f);
	dt = Clamp(dt, 0.f, 0.05f);
	return Clamp(1.f - std::exp(-dt / seconds), 0.f, 1.f);
}

float XZMagnitude(const Fvector& value)
{
	return std::sqrt(value.x * value.x + value.z * value.z);
}

float DotXZ(const Fvector& a, const Fvector& b)
{
	return a.x * b.x + a.z * b.z;
}

float DirectionChange(const Fvector& from, const Fvector& to)
{
	const float from_mag = XZMagnitude(from);
	const float to_mag = XZMagnitude(to);
	if (from_mag <= kEpsilon || to_mag <= kEpsilon)
		return 0.f;

	const float dot = Clamp(DotXZ(from, to) / (from_mag * to_mag), -1.f, 1.f);
	return Clamp((1.f - dot) * 0.5f, 0.f, 1.f);
}

bool ShouldBypass(const MovementResponseSettings& settings, const MovementResponseInput& input)
{
	if (!settings.enable)
		return true;
	if (settings.ads_disable && input.ads)
		return true;
	if (input.move_flags & (mcCrouch | mcClimb | mcJump | mcFall | mcLanding | mcLanding2))
		return true;
	return false;
}
} // namespace

void ResetMovementResponse(MovementResponseState& state)
{
	state.initialized = false;
	state.accel.set(0.f, 0.f, 0.f);
	state.target_speed = 0.f;
	state.actual_speed = 0.f;
	state.speed_fraction = 0.f;
}

MovementResponseOutput UpdateMovementResponse(const MovementResponseSettings& settings, MovementResponseState& state, const MovementResponseInput& input)
{
	MovementResponseOutput output;
	output.accel.set(input.target_accel);
	output.target_speed = XZMagnitude(input.target_accel) / kVelocityScale;

	if (ShouldBypass(settings, input))
	{
		ResetMovementResponse(state);
		output.actual_speed = output.target_speed;
		output.speed_fraction = output.target_speed > kEpsilon ? 1.f : 0.f;
		return output;
	}

	if (!state.initialized)
	{
		state.initialized = true;
		state.accel.set(input.target_accel);
	}

	const float current_speed = XZMagnitude(state.accel);
	const float target_speed = XZMagnitude(input.target_accel);
	const bool stopping = target_speed <= kEpsilon;
	const bool slowing_down = target_speed + kEpsilon < current_speed;
	const bool sprinting = !!(input.move_flags & mcSprint);
	const float sprint_mult = sprinting ? std::max(settings.sprint_mult, 0.01f) : 1.f;
	const float turn = DirectionChange(state.accel, input.target_accel);

	float response_time = stopping || slowing_down ? settings.decel_time : settings.accel_time / sprint_mult;
	response_time *= 1.f + Clamp(settings.turn_response, 0.f, 1.f) * turn;
	if (stopping)
		response_time *= std::max(settings.stop_response, 0.01f);

	const float blend = Response(response_time, input.dt);
	state.accel.x += (input.target_accel.x - state.accel.x) * blend;
	state.accel.y = input.target_accel.y;
	state.accel.z += (input.target_accel.z - state.accel.z) * blend;

	const float new_speed = XZMagnitude(state.accel);
	if (target_speed > kEpsilon && new_speed > target_speed)
	{
		const float scale = target_speed / new_speed;
		state.accel.x *= scale;
		state.accel.z *= scale;
	}
	else if (target_speed <= kEpsilon && new_speed < kEpsilon)
	{
		state.accel.x = 0.f;
		state.accel.z = 0.f;
	}

	state.target_speed = target_speed / kVelocityScale;
	state.actual_speed = XZMagnitude(state.accel) / kVelocityScale;
	state.speed_fraction = state.target_speed > kEpsilon ? Clamp(state.actual_speed / state.target_speed, 0.f, 1.25f) : 0.f;

	output.accel.set(state.accel);
	output.target_speed = state.target_speed;
	output.actual_speed = state.actual_speed;
	output.speed_fraction = state.speed_fraction;
	output.active = true;
	return output;
}
} // namespace Bodycam
