#include "stdafx.h"
#include "bodycam_pip_adapter.h"

extern float g_fov;
extern int g_svp_world_cam_fx;

namespace
{
void ReadPipRuntime(Bodycam::PipRuntimeState& runtime)
{
	auto& viewport = Device.m_SecondViewport;
	runtime.true_pip_enabled = Device.true_pip_on;
	runtime.geometry_ready = viewport.svp_lens_r > EPS || viewport.dbg_eyepiece_r > EPS;
	runtime.viewport_active = viewport.IsSVPActive();
	runtime.world_camera_effects = !!g_svp_world_cam_fx;
	runtime.main_fov = g_fov;
}

struct PipRuntimeRegistration
{
	PipRuntimeRegistration()
	{
		Bodycam::SetPipRuntimeProvider(&ReadPipRuntime);
	}

	~PipRuntimeRegistration()
	{
		Bodycam::SetPipRuntimeProvider(nullptr);
	}
} g_pip_runtime_registration;
} // namespace
