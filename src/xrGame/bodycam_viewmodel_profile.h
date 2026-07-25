#pragma once

#include "bodycam_simulation.h"

namespace Bodycam
{
struct ViewmodelProfileState
{
	SVec3 target_pos;
	SVec3 target_rot;
	SVec3 current_pos;
	SVec3 current_rot;
	float blend_speed = 8.f;
	bool enabled = false;
};

struct ViewmodelProfileOutput
{
	SVec3 pos;
	SVec3 rot;
	bool active = false;
};

void SetViewmodelProfile(ViewmodelProfileState& state, const SVec3& pos, const SVec3& rot, float blend_speed);
void ClearViewmodelProfile(ViewmodelProfileState& state, float blend_speed);
ViewmodelProfileOutput UpdateViewmodelProfile(ViewmodelProfileState& state, float dt, float ads_blend);
bool ViewmodelProfileActive(const ViewmodelProfileState& state);
} // namespace Bodycam
