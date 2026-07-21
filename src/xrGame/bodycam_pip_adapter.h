#pragma once

namespace Bodycam
{
struct PipInput
{
	bool weapon_zoomed = false;
	bool primary_sight = false;
	float requested_fov = 0.f;
};

struct PipView
{
	bool active = false;
	bool freeze_world_pose = false;
	float main_fov = 0.f;
};

struct PipRuntimeState
{
	bool true_pip_enabled = false;
	bool geometry_ready = false;
	bool viewport_active = false;
	bool world_camera_effects = false;
	float main_fov = 0.f;
};

using PipRuntimeProvider = void (*)(PipRuntimeState& state);

void SetPipRuntimeProvider(PipRuntimeProvider provider);

class PipAdapter
{
public:
	PipView Update(const PipInput& input);
	PipView Update(const PipInput& input, const PipRuntimeState& runtime);
	void Reset();

private:
	bool m_session_active = false;
};
} // namespace Bodycam
