#include "stdafx.h"
#pragma hdrstop

#include "../xrRender/blenders/Blender_Recorder.h"
#include "../../xrEngine/xr_ioconsole.h"
#include "../../xrEngine/xr_ioc_cmd.h"
#include "r4_3dss_reticle.h"
#include "r4_reticle_vector.h"

// 3DSS reticle atlas settings. Each acts on top of what the atlas authors.
int ps_r__reticle_bloom_exclusion = 1;
float ps_r__reticle_brightness = 1.0f;
Fvector ps_r__reticle_glow_size = {3.f, 12.f, 40.f};
Fvector ps_r__reticle_glow_weight = {1.0f, 0.35f, 0.15f};
float ps_r__reticle_glow_exposure = 1.0f;
float ps_r__reticle_glow_lowlum = 0.2f;
float ps_r__reticle_glow_shoulder = 1.0f;
float ps_r__reticle_glow_halo = 0.55f;
float ps_r__reticle_glow_tint = 0.5f;
float ps_r__reticle_holo_level = 1.0f;
float ps_r__reticle_grain = 1.0f;
float ps_r__reticle_line_weight = 0.0f;
int ps_r__reticle_debug = 0;

float g_reticle_reflex_glow = 0.f;
float g_reticle_image_glow = 0.f;

bool reticle_glow_wanted()
{
	return ps_r__reticle_bloom_exclusion != 0;
}

void reticle_3dss_console_init()
{
	CMD4(CCC_Integer, "r__reticle_svg", &ps_r__reticle_svg, 0, 1); // r4_reticle_vector.h
	reticle_vector_console_init();
	CMD4(CCC_Integer, "r__reticle_msdf_bloom_exclusion", &ps_r__reticle_bloom_exclusion, 0, 1);
	CMD4(CCC_Float, "r__reticle_msdf_brightness", &ps_r__reticle_brightness, 0.0f, 8.0f);
	CMD4(CCC_Vector3, "r__reticle_msdf_glow_size", &ps_r__reticle_glow_size, Fvector().set(0.5f, 0.5f, 0.5f), Fvector().set(16.f, 32.f, 64.f));
	CMD4(CCC_Vector3, "r__reticle_msdf_glow_weight", &ps_r__reticle_glow_weight, Fvector().set(0.f, 0.f, 0.f), Fvector().set(4.f, 4.f, 4.f));
	CMD4(CCC_Float, "r__reticle_msdf_glow_shoulder", &ps_r__reticle_glow_shoulder, 0.0f, 8.0f);
	CMD4(CCC_Float, "r__reticle_msdf_glow_exposure", &ps_r__reticle_glow_exposure, 1.0f, 4.0f);
	CMD4(CCC_Float, "r__reticle_msdf_glow_lowlum", &ps_r__reticle_glow_lowlum, 0.01f, 1.0f);
	CMD4(CCC_Float, "r__reticle_msdf_glow_halo", &ps_r__reticle_glow_halo, 0.1f, 1.0f);
	CMD4(CCC_Float, "r__reticle_msdf_glow_tint", &ps_r__reticle_glow_tint, 0.0f, 1.0f);
	CMD4(CCC_Float, "r__reticle_msdf_holo_level", &ps_r__reticle_holo_level, 0.0f, 2.0f);
	CMD4(CCC_Float, "r__reticle_msdf_speckle", &ps_r__reticle_grain, 0.0f, 2.0f);
	CMD4(CCC_Float, "r__reticle_msdf_line_weight", &ps_r__reticle_line_weight, 0.0f, 1.0f);
	CMD4(CCC_Integer, "r__reticle_msdf_debug", &ps_r__reticle_debug, 0, 1);
}

// reticle_atlas_params: x holographic level, y brightness, z grain depth, w line weight
static class reticle_atlas_params_binder : public R_constant_setup
{
	void setup(R_constant* C) override
	{
		RCache.set_c(C, ps_r__reticle_holo_level, ps_r__reticle_brightness, ps_r__reticle_grain,
			ps_r__reticle_line_weight);
	}
} binder_reticle_atlas_params;

// reticle_atlas_control: x 1 = the glow has its own targets, y 1 = unusable atlases show magenta, w 1 = params are live
static class reticle_atlas_control_binder : public R_constant_setup
{
	void setup(R_constant* C) override
	{
		RCache.set_c(C, reticle_glow_wanted() ? 1.f : 0.f, ps_r__reticle_debug ? 1.f : 0.f, 0.f, 1.f);
	}
} binder_reticle_atlas_control;

// reticle_holo_eye: the physical eye in the current view space, w = 1. A second viewport pass sees the main
// view's eye, so holographic grain follows the same eye in both images.
static class reticle_holo_eye_binder : public R_constant_setup
{
	Fvector eye = {0.f, 0.f, 0.f};
	void setup(R_constant* C) override
	{
		if (!Device.m_SecondViewport.IsSVPFrame())
			eye = Device.vCameraPosition;
		Fvector view;
		Device.mView.transform_tiny(view, eye);
		RCache.set_c(C, view.x, view.y, view.z, 1.f);
	}
} binder_reticle_holo_eye;

static class reticle_atlas_reflex_binder : public R_constant_setup
{
	void setup(R_constant* C) override
	{
		RCache.set_c(C, g_reticle_reflex_glow, g_reticle_image_glow, 0.f, 0.f);
	}
} binder_reticle_atlas_reflex;

void RegisterReticle3DSSConstants(CBlender_Compile& dst)
{
	dst.r_Constant("reticle_atlas_params", &binder_reticle_atlas_params);
	dst.r_Constant("reticle_atlas_control", &binder_reticle_atlas_control);
	dst.r_Constant("reticle_holo_eye", &binder_reticle_holo_eye);
	dst.r_Constant("reticle_atlas_reflex_glow", &binder_reticle_atlas_reflex);
}
