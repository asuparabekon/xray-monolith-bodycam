#include "bodycam_viewmodel_profile.h"

#include <algorithm>
#include <cmath>

namespace Bodycam
{
namespace
{
constexpr float kMinimumBlendSpeed = 0.01f;
constexpr float kMaximumBlendSpeed = 30.f;
constexpr float kDefaultBlendSpeed = 8.f;
constexpr float kMaximumPositionOffset = 0.5f;
constexpr float kMaximumRotationOffset = 45.f;
constexpr float kActiveEpsilon = 0.00001f;

float Clamp01(float value)
{
	return std::max(0.f, std::min(value, 1.f));
}

float Sanitize(float value, float limit)
{
	return std::isfinite(value) ? std::max(-limit, std::min(value, limit)) : 0.f;
}

SVec3 Sanitize(const SVec3& value, float limit)
{
	return { Sanitize(value.x, limit), Sanitize(value.y, limit), Sanitize(value.z, limit) };
}

float SanitizeBlendSpeed(float value)
{
	if (!std::isfinite(value))
		return kDefaultBlendSpeed;
	return std::max(kMinimumBlendSpeed, std::min(value, kMaximumBlendSpeed));
}

bool NearlyZero(const SVec3& value)
{
	return std::abs(value.x) < kActiveEpsilon &&
		std::abs(value.y) < kActiveEpsilon &&
		std::abs(value.z) < kActiveEpsilon;
}

void Approach(SVec3& current, const SVec3& target, float amount)
{
	current.x += (target.x - current.x) * amount;
	current.y += (target.y - current.y) * amount;
	current.z += (target.z - current.z) * amount;
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
	const float frame_time = std::isfinite(dt) ? std::max(dt, 0.f) : 0.f;
	const float response = 1.f - std::exp(-state.blend_speed * frame_time);
	Approach(state.current_pos, state.target_pos, response);
	Approach(state.current_rot, state.target_rot, response);

	ViewmodelProfileOutput output;
	const float hip_weight = 1.f - Clamp01(ads_blend);
	output.pos = state.current_pos;
	output.rot = state.current_rot;
	output.pos.x *= hip_weight;
	output.pos.y *= hip_weight;
	output.pos.z *= hip_weight;
	output.rot.x *= hip_weight;
	output.rot.y *= hip_weight;
	output.rot.z *= hip_weight;
	output.active = !NearlyZero(output.pos) || !NearlyZero(output.rot);
	return output;
}

bool ViewmodelProfileActive(const ViewmodelProfileState& state)
{
	return state.enabled || !NearlyZero(state.current_pos) || !NearlyZero(state.current_rot);
}
} // namespace Bodycam
