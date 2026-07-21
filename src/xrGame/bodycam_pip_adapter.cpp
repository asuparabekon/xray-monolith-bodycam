#include "bodycam_pip_adapter.h"

// This file deliberately stays independent of X-Ray headers for the off-game adapter test.

namespace Bodycam
{
namespace
{
PipRuntimeProvider& RuntimeProvider()
{
	static PipRuntimeProvider provider = nullptr;
	return provider;
}
} // namespace

void SetPipRuntimeProvider(PipRuntimeProvider provider)
{
	RuntimeProvider() = provider;
}

PipView PipAdapter::Update(const PipInput& input)
{
	PipRuntimeState runtime;
	if (const PipRuntimeProvider provider = RuntimeProvider())
		provider(runtime);

	return Update(input, runtime);
}

PipView PipAdapter::Update(const PipInput& input, const PipRuntimeState& runtime)
{
	const bool eligible = runtime.true_pip_enabled && input.weapon_zoomed && input.primary_sight;
	if (!eligible)
		m_session_active = false;
	else if (runtime.geometry_ready || runtime.viewport_active)
		m_session_active = true;

	PipView view;
	view.active = m_session_active;
	view.freeze_world_pose = m_session_active && !runtime.world_camera_effects;
	view.main_fov = m_session_active ? runtime.main_fov : input.requested_fov;
	return view;
}

void PipAdapter::Reset()
{
	m_session_active = false;
}
} // namespace Bodycam
