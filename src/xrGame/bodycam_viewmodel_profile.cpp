#include "bodycam_viewmodel_profile.h"
#include "bodycam_smoothing.h"

#include <algorithm>
#include <cmath>

namespace Bodycam
{
namespace
{
// Limits on what a script may ask for.
constexpr float kMinimumBlendSpeed = 0.01f;
constexpr float kMaximumBlendSpeed = 30.f;
constexpr float kMaximumPositionOffset = 0.5f; // metres
constexpr float kMaximumRotationOffset = 45.f; // degrees
constexpr float kActiveEpsilon = 0.00001f;

float Sanitize(float value, float limit)
{
	return std::isfinite(value) ? std::clamp(value, -limit, limit) : 0.f;
}

SVec3 Sanitize(const SVec3& value, float limit)
{
	return { Sanitize(value.x, limit), Sanitize(value.y, limit), Sanitize(value.z, limit) };
}

float SanitizeBlendSpeed(float value)
{
	if (!std::isfinite(value))
		return kDefaultViewmodelBlendSpeed;
	return std::clamp(value, kMinimumBlendSpeed, kMaximumBlendSpeed);
}

bool NearlyZero(const SVec3& value)
{
	return std::fabs(value.x) < kActiveEpsilon && std::fabs(value.y) < kActiveEpsilon && std::fabs(value.z) < kActiveEpsilon;
}
} // namespace

void SetViewmodelProfile(ViewmodelProfileState& state, const SVec3& pos, const SVec3& rot, float blend_speed)
{
	state.target_pos = Sanitize(pos, kMaximumPositionOffset);
	state.target_rot = Sanitize(rot, kMaximumRotationOffset);
	state.blend_speed = SanitizeBlendSpeed(blend_speed);
	state.enabled = true;
}

void ClearViewmodelProfile(ViewmodelProfileState& state, float blend_speed)
{
	state.target_pos = {};
	state.target_rot = {};
	state.blend_speed = SanitizeBlendSpeed(blend_speed);
	state.enabled = false;
}

ViewmodelProfileOutput UpdateViewmodelProfile(ViewmodelProfileState& state, float dt, float ads_blend)
{
	SmoothVector(state.current_pos, state.target_pos, state.blend_speed, dt);
	SmoothVector(state.current_rot, state.target_rot, state.blend_speed, dt);

	ViewmodelProfileOutput output;
	output.pos = state.current_pos;
	output.rot = state.current_rot;
	output.pos.Mul(1.f - ads_blend);
	output.rot.Mul(1.f - ads_blend);
	output.active = !NearlyZero(output.pos) || !NearlyZero(output.rot);
	return output;
}

bool ViewmodelProfileActive(const ViewmodelProfileState& state)
{
	return state.enabled || !NearlyZero(state.current_pos) || !NearlyZero(state.current_rot);
}
} // namespace Bodycam
