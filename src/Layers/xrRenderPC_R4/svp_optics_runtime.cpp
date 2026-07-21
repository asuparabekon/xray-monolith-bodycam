#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "svp_optics_runtime.h"
#include "svp_physical_optics.h"
#include "svp_overlay_state.h"

extern int ps_r__svp_aperture;
extern int ps_r__svp_authored_optics;
extern int ps_r__svp_diag;
extern float ps_r__svp_eyebox;
extern int ps_r__svp_local_exposure;
extern int ps_r__svp_photo_model;
extern int ps_markswitch_current;
extern float g_pip_scope_magnification;
extern float g_pip_scope_max_mag;
extern float g_pip_scope_min_mag;
extern float ps_r__svp_exposure_bias;
extern float ps_r__svp_twilight;
extern float ps_s3ds_eye_relief_low_mm;
extern float ps_s3ds_eye_relief_high_mm;
extern float ps_s3ds_eye_tracking_accel_mm_s2;
extern float ps_s3ds_eye_tracking_limit_mm;
extern float ps_s3ds_eye_tracking_speed;
extern float ps_s3ds_exit_pupil_low_mm;
extern float ps_s3ds_exit_pupil_high_mm;
extern float ps_s3ds_objective_mm;
extern float ps_s3ds_pupil_field_low;
extern float ps_s3ds_pupil_field_high;
extern float ps_s3ds_transmission;
extern float ps_s3ds_tunneling_max;
extern float ps_s3ds_tunneling_min;
extern float ps_s3ds_tunneling_parallax;
extern float ps_s3ds_twilight_strength;
extern float ps_svp_dim_offset;
extern float ps_svp_dim_scale;
extern float ps_svp_exit_offset;
extern float ps_svp_exit_scale;
extern float ps_svp_tunnel_offset;
extern float ps_svp_tunnel_scale;
extern Fvector4 ps_dev_param_8;
extern Fvector4 ps_s3ds_param_1;
extern Fvector4 ps_s3ds_param_3;
extern Fvector4 ps_svp_dim_curve_high;
extern Fvector4 ps_svp_dim_curve_low;
extern Fvector4 ps_svp_exit_curve_high;
extern Fvector4 ps_svp_exit_curve_low;
extern Fvector4 ps_svp_tunnel_curve_high;
extern Fvector4 ps_svp_tunnel_curve_low;
extern Fvector4 scope_objective_lens_offset;

namespace SvpOpticsRuntime
{
namespace
{
SvpPhysicalOptics::MagnificationResponse MakeResponse(const Fvector4& low, const Fvector4& high)
{
	SvpPhysicalOptics::MagnificationResponse response;
	response.value[0] = low.x;
	response.value[1] = low.y;
	response.value[2] = low.z;
	response.value[3] = low.w;
	response.value[4] = high.x;
	response.value[5] = high.y;
	response.value[6] = high.z;
	response.value[7] = high.w;
	return response;
}

float ObjectiveDiameterMm()
{
	return Device.m_SecondViewport.svp_opt_obj_mm;
}

float InterpolateProfileEndpoints(float low, float high)
{
	return SvpPhysicalOptics::InterpolateMagnification(low, high, g_pip_scope_magnification,
		g_pip_scope_min_mag, g_pip_scope_max_mag);
}

float EyeReliefMm()
{
	return InterpolateProfileEndpoints(ps_s3ds_eye_relief_low_mm, ps_s3ds_eye_relief_high_mm);
}

float PupilFieldScale()
{
	return InterpolateProfileEndpoints(ps_s3ds_pupil_field_low, ps_s3ds_pupil_field_high);
}

float CalculateEyePupilMm(float& environment_brightness)
{
	environment_brightness = 0.f;
	if (!g_pGamePersistent)
		return 0.f;

	CEnvDescriptor& environment = *g_pGamePersistent->Environment().CurrentEnv;
	environment_brightness = 0.299f * environment.sun_color.x + 0.587f * environment.sun_color.y +
		0.114f * environment.sun_color.z + 0.5f * (0.299f * environment.hemi_color.x +
		0.587f * environment.hemi_color.y + 0.114f * environment.hemi_color.z);

	if (!ps_r__svp_photo_model)
		return 6.f - 3.5f * _min(environment_brightness / 0.25f, 1.f);

	const float luminance = 2500.f * environment_brightness * environment_brightness;
	float pupil_mm = 4.9f - 3.f * tanhf(0.4f * log10f(_max(luminance, 1e-4f)));
	clamp(pupil_mm, 2.f, 8.f);
	return pupil_mm;
}

float CalculateExitPupilMm(float objective_mm)
{
	if (g_pip_scope_magnification <= 0.01f)
		return 0.f;

	float low = ps_s3ds_exit_pupil_low_mm;
	float high = ps_s3ds_exit_pupil_high_mm;
	if (objective_mm > 0.01f)
	{
		if (low <= 0.01f && g_pip_scope_min_mag > 0.01f)
			low = objective_mm / g_pip_scope_min_mag;
		if (high <= 0.01f && g_pip_scope_max_mag > 0.01f)
			high = objective_mm / g_pip_scope_max_mag;
	}
	if (low > 0.01f || high > 0.01f)
	{
		if (low <= 0.01f)
			low = high;
		if (high <= 0.01f)
			high = low;
		return SvpPhysicalOptics::InterpolateReciprocalMagnification(low, high, g_pip_scope_magnification,
			g_pip_scope_min_mag, g_pip_scope_max_mag);
	}

	const float authored_exit = ps_s3ds_param_1.z > 0.01f ? ps_s3ds_param_1.z : 0.5f;
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : g_pip_scope_magnification;
	return authored_exit * Device.m_SecondViewport.eyepiece.radius * 2000.f *
		(minimum_mag / g_pip_scope_magnification);
}

float CalculateTwilightDimming(float pupil_mm, float environment_brightness)
{
	if (SvpOverlayActive(ps_s3ds_param_3.x, ps_markswitch_current) || g_pip_scope_magnification <= 0.01f || pupil_mm <= EPS)
		return 0.f;

	const float exit_pupil_mm = CalculateExitPupilMm(ObjectiveDiameterMm());
	const float pupil_ratio = _min(exit_pupil_mm / pupil_mm, 1.f);
	const float relative_brightness = ps_r__svp_photo_model ? pupil_ratio * pupil_ratio : pupil_ratio;
	const float twilight_strength = _min(ps_r__svp_twilight, 1.f) * clampr(ps_s3ds_twilight_strength, 0.f, 1.f);
	float dimming = clampr(ps_s3ds_transmission, 0.f, 1.f) *
		(1.f + (_max(relative_brightness, 0.6f) - 1.f) * twilight_strength);
	const float response = SvpPhysicalOptics::ApplyMagnificationResponse(
		MakeResponse(ps_svp_dim_curve_low, ps_svp_dim_curve_high), g_pip_scope_magnification,
		ps_svp_dim_scale, ps_svp_dim_offset);
	dimming = 1.f - clampr((1.f - dimming) * response, 0.f, 1.f);

	if (ps_r__svp_diag)
	{
		static u32 next_dump = 0;
		if (Device.dwTimeGlobal - next_dump > 1000)
		{
			next_dump = Device.dwTimeGlobal;
			PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", exit_pupil_mm,
				pupil_mm, environment_brightness, dimming);
		}
	}
	return dimming;
}

void UpdateEyeboxLimit(float pupil_mm)
{
	const float exit_pupil_mm = CalculateExitPupilMm(ObjectiveDiameterMm());
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && exit_pupil_mm > 0.01f &&
		g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
	{
		const float exit_radius = exit_pupil_mm * 0.0005f;
		const float pupil_radius = pupil_mm * 0.0005f;
		const float eye_relief = _max(EyeReliefMm() * 0.001f, 0.05f);
		Device.m_SecondViewport.svp_eyebox_rad = atanf((exit_radius + pupil_radius) / eye_relief);
		return;
	}

	Device.m_SecondViewport.svp_eyebox_rad = 0.f;
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
	{
		static u32 next_dump = 0;
		if (Device.dwTimeGlobal - next_dump > 1000)
		{
			next_dump = Device.dwTimeGlobal;
			PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f",
				ps_r__svp_authored_optics, scope_objective_lens_offset.w, ps_s3ds_objective_mm,
				g_pip_scope_magnification, pupil_mm);
		}
	}
}

SvpPhysicalOptics::EyeTrackingState LoadEyeTrackingState()
{
	const auto& viewport = Device.m_SecondViewport;
	SvpPhysicalOptics::EyeTrackingState state;
	state.offset = { viewport.svp_eye_tracking_offset.x, viewport.svp_eye_tracking_offset.y };
	state.velocity = { viewport.svp_eye_tracking_velocity.x, viewport.svp_eye_tracking_velocity.y };
	state.epoch = viewport.svp_eye_tracking_epoch;
	state.frame = viewport.svp_eye_tracking_frame;
	state.valid = viewport.svp_eye_tracking_valid;
	return state;
}

void StoreEyeTrackingState(const SvpPhysicalOptics::EyeTrackingState& state)
{
	auto& viewport = Device.m_SecondViewport;
	viewport.svp_eye_tracking_offset.set(state.offset.x, state.offset.y);
	viewport.svp_eye_tracking_velocity.set(state.velocity.x, state.velocity.y);
	viewport.svp_eye_tracking_epoch = state.epoch;
	viewport.svp_eye_tracking_frame = state.frame;
	viewport.svp_eye_tracking_valid = state.valid;
}

Fvector2 UpdateVirtualEye(const Fvector2& raw_offset_mm)
{
	auto& viewport = Device.m_SecondViewport;
	const SvpPhysicalOptics::Vec2 raw = { raw_offset_mm.x, raw_offset_mm.y };
	const SvpPhysicalOptics::Vec2 target = SvpPhysicalOptics::LimitEyeOffset(raw, ps_s3ds_eye_tracking_limit_mm);
	SvpPhysicalOptics::EyeTrackingState state = LoadEyeTrackingState();

	SvpPhysicalOptics::UpdateEyeTracking(state, target, viewport.svp_eye_tracking_suspended,
		viewport.svp_optic_epoch, Device.dwFrame, Device.fTimeDelta, ps_s3ds_eye_tracking_speed,
		ps_s3ds_eye_tracking_accel_mm_s2);

	StoreEyeTrackingState(state);
	viewport.svp_eye_residual.sub(raw_offset_mm, viewport.svp_eye_tracking_offset);
	return viewport.svp_eye_residual;
}

void BindScopeExposure(float pupil_mm, float environment_brightness)
{
	const bool nvg_on = ps_dev_param_8.x >= 1.f;
	const float dimming = CalculateTwilightDimming(pupil_mm, environment_brightness);
	const float magnification_blend = clampr((g_pip_scope_magnification - 1.f) / 7.f, 0.f, 1.f);
	const float snug = 1.3f + (0.55f - 1.3f) * magnification_blend;
	const float shadow_gain = Device.m_SecondViewport.svp_shadow_gain;
	const float gain = clampr(shadow_gain * 4.f, 0.f, 1.f);
	const float effective_depth = _max(ps_s3ds_param_1.z * (3.f + (snug - 3.f) * gain), 0.08f);
	const float swing_scale = 0.4f * effective_depth / _max(ps_s3ds_param_1.y, 0.05f);

	Device.m_SecondViewport.svp_mag = _max(g_pip_scope_magnification, 0.f);
	RCache.set_c("svp_exposure", (ps_r__svp_local_exposure && !nvg_on) ? powf(2.f, ps_r__svp_exposure_bias) : 0.f,
		nvg_on ? 0.f : dimming, Device.m_SecondViewport.svp_swing_x * shadow_gain * swing_scale,
		Device.m_SecondViewport.svp_swing_y * shadow_gain * swing_scale);
}

void BindAperture(float pupil_mm)
{
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : g_pip_scope_magnification;
	const float maximum_mag = g_pip_scope_max_mag > minimum_mag ? g_pip_scope_max_mag : minimum_mag;
	const float exit_pupil_mm = CalculateExitPupilMm(ObjectiveDiameterMm());
	const float eye_relief_mm = EyeReliefMm();
	auto& viewport = Device.m_SecondViewport;
	const auto& eyepiece = viewport.eyepiece;
	const auto& objective = viewport.objective;

	Fvector lens_right = eyepiece.m_W.i;
	Fvector lens_up = eyepiece.m_W.j;
	Fvector optical_axis;
	optical_axis.sub(objective.m_W.c, eyepiece.m_W.c);
	const bool objective_valid = objective.radius > EPS && optical_axis.square_magnitude() > EPS;
	if (!objective_valid)
		optical_axis.set(eyepiece.m_W.k);
	lens_right.normalize_safe();
	lens_up.normalize_safe();
	optical_axis.normalize_safe();

	Fvector lens_center_view, lens_right_view, lens_up_view, axis_view;
	Device.mView.transform_tiny(lens_center_view, eyepiece.m_W.c);
	Device.mView.transform_dir(lens_right_view, lens_right);
	Device.mView.transform_dir(lens_up_view, lens_up);
	Device.mView.transform_dir(axis_view, optical_axis);
	lens_right_view.normalize_safe();
	lens_up_view.normalize_safe();
	axis_view.normalize_safe();

	Fvector eye_ray = lens_center_view;
	eye_ray.normalize_safe();
	const float forward = eye_ray.dotproduct(axis_view);
	const float inverse_forward = _abs(forward) > 0.001f ? 1.f / forward : 0.f;
	Fvector2 raw_eye_offset_mm;
	raw_eye_offset_mm.set(-eye_ray.dotproduct(lens_right_view) * inverse_forward * eye_relief_mm,
		-eye_ray.dotproduct(lens_up_view) * inverse_forward * eye_relief_mm);
	const Fvector2 eye_offset_mm = UpdateVirtualEye(raw_eye_offset_mm);
	const float inverse_lens_diameter = eyepiece.radius > EPS ? 0.5f / eyepiece.radius : 0.f;

	RCache.set_c("svp_aperture", ps_r__svp_aperture ? 1.f : 0.f, g_pip_scope_magnification, minimum_mag, maximum_mag);
	RCache.set_c("svp_eyebox", eye_offset_mm.x, eye_offset_mm.y, exit_pupil_mm * 0.5f, pupil_mm * 0.5f);
	const float exit_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		MakeResponse(ps_svp_exit_curve_low, ps_svp_exit_curve_high), g_pip_scope_magnification,
		ps_svp_exit_scale, ps_svp_exit_offset);
	const float tunnel_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		MakeResponse(ps_svp_tunnel_curve_low, ps_svp_tunnel_curve_high), g_pip_scope_magnification,
		ps_svp_tunnel_scale, 0.f);
	RCache.set_c("svp_optic_profile", ps_s3ds_tunneling_parallax, ps_s3ds_tunneling_min,
		ps_s3ds_tunneling_max, tunnel_response);
	RCache.set_c("svp_pupil_model", PupilFieldScale(), exit_response, ps_svp_tunnel_offset, 0.f);
	RCache.set_c("svp_lens_center", eyepiece.m_W.c.x, eyepiece.m_W.c.y, eyepiece.m_W.c.z, inverse_lens_diameter);
	RCache.set_c("svp_lens_right", lens_right.x, lens_right.y, lens_right.z, 0.f);
	RCache.set_c("svp_lens_up", lens_up.x, lens_up.y, lens_up.z, 0.f);

	if (ps_r__svp_diag)
	{
		static u32 next_dump = 0;
		if (Device.dwTimeGlobal - next_dump > 1000)
		{
			next_dump = Device.dwTimeGlobal;
			PipMsg("[SVP-APERTURE] mag %.2f range %.2f-%.2f raw %.2f,%.2fmm residual %.2f,%.2fmm exit %.1fmm relief %.1fmm tracking=%s objective=%s",
				g_pip_scope_magnification, minimum_mag, maximum_mag, raw_eye_offset_mm.x, raw_eye_offset_mm.y,
				eye_offset_mm.x, eye_offset_mm.y, exit_pupil_mm, eye_relief_mm,
				viewport.svp_eye_tracking_suspended ? "held" : "follow", objective_valid ? "live" : "fallback");
		}
	}
}
} // namespace

void BindPhysicalOptics()
{
	float environment_brightness = 0.f;
	const float pupil_mm = CalculateEyePupilMm(environment_brightness);
	UpdateEyeboxLimit(pupil_mm);
	BindScopeExposure(pupil_mm, environment_brightness);
	BindAperture(pupil_mm);
}
} // namespace SvpOpticsRuntime
