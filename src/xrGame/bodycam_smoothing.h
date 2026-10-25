#pragma once
// Smoothing steps shared by the Classic simulation, the camera rig and the
// viewmodel profile. Internal to those; not part of their interfaces.
#include "bodycam_simulation.h"
#include <algorithm>
#include <cmath>

namespace Bodycam
{
constexpr float kEpsilon = 1.0e-6f;
// Smoothing integrates at most this much time per frame, so a long frame eases instead of snapping.
constexpr float kMaxSmoothingStep = 0.033f;
// A follow with zero damping would never move; this is the slowest it gets.
constexpr float kMinFollowDamping = 0.01f;

// Share of the remaining distance covered this frame at the given rate (1/s).
inline float BlendFactor(float rate, float dt)
{
	return 1.f - std::exp(-rate * std::min(dt, kMaxSmoothingStep));
}

inline float SmoothFloat(float current, float target, float rate, float dt)
{
	return current + (target - current) * BlendFactor(rate, dt);
}

inline float SmoothAngle(float current, float target, float rate, float dt)
{
	return NormalizeAngle(current + AngleDifference(target, current) * BlendFactor(rate, dt));
}

inline void SmoothVector(SVec3& current, const SVec3& target, float rate, float dt)
{
	current.x = SmoothFloat(current.x, target.x, rate, dt);
	current.y = SmoothFloat(current.y, target.y, rate, dt);
	current.z = SmoothFloat(current.z, target.z, rate, dt);
}

// The follow settings are a speed and a damping; their product is the rate.
inline float FollowRate(float speed, float damping)
{
	return speed * std::max(damping, kMinFollowDamping);
}

inline float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

inline void ClampVector(SVec3& value, float limit)
{
	value.x = std::clamp(value.x, -limit, limit);
	value.y = std::clamp(value.y, -limit, limit);
	value.z = std::clamp(value.z, -limit, limit);
}
} // namespace Bodycam
