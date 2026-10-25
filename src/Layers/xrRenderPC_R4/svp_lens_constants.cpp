#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#include "svp_physical_optics.h"
#include "svp_optics.h"

#if defined(USE_DX11)
// per-scope objective diameter in mm, resolved by the optics bus (spec cvar then authored w), 0 = none
static float svp_objective_mm()
{
	return Device.m_SecondViewport.svp_opt_obj_mm;
}

// pip electronic overlay actually on screen, NV shows only with its overlay on (markswitch 0),
// thermal shows until the overlay is dropped (markswitch < 2)
static bool svp_overlay_active(float param3x, int markswitch)
{
	return (param3x >= 0.5f) && ((param3x < 1.5f) ? (markswitch == 0) : (markswitch < 2));
}
// pip thermal-typed optic with its overlay on, near-blur skips these so they keep full DoF
static bool svp_thermal_active(float param3x, int markswitch)
{
	return (param3x >= 1.5f) && svp_overlay_active(param3x, markswitch);
}

// pip the thermal overlay state for callers outside this file
bool svp_thermal_overlay_active()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	return svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current);
}

// pip a live electronic display (NV tube or thermal sensor) shows the 3DSS image, not the optical one
bool svp_electronic_image_active()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	return svp_overlay_active(ps_s3ds_param_3.x, ps_markswitch_current);
}

// pip a fresh forward body ahead of the objective, published by the hud drain
bool svp_clipon_resolved()
{
	auto& vp = Device.m_SecondViewport;
	return ps_r__svp_clipon && _valid(vp.svp_clipon_axial) && vp.svp_clipon_axial > 0.f && vp.svp_hud_min_frame != u32(-1) &&
		Device.dwFrame >= vp.svp_hud_min_frame && Device.dwFrame - vp.svp_hud_min_frame <= 8 &&
		vp.svp_hud_min_session == vp.GetSVPSession() && vp.svp_hud_min_epoch == vp.svp_optic_epoch;
}

// pip eye coupling from the typed profile, the legacy thermal read covers an untyped route
bool svp_optic_eye_coupled()
{
	// a clip-on presents through the host eyepiece so the display exemption never applies
	if (svp_clipon_resolved())
		return true;
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	if (config.typed_route)
		return config.eye_coupling;
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	return !svp_thermal_active(ps_s3ds_param_3.x, ps_markswitch_current);
}

// pip physical aperture cvars, registered in svp_console.cpp, drive the exit-pupil model at r__svp_aperture 1
extern int ps_r__svp_aperture;
extern int ps_r__svp_photo_model;
extern int ps_r__svp_authored_optics;
extern int ps_r__svp_diag;
extern float ps_r__svp_eyebox;
extern float ps_r__svp_twilight;
extern float ps_s3ds_transmission;
extern float ps_s3ds_twilight_strength;
extern float ps_s3ds_eye_relief_low_mm, ps_s3ds_eye_relief_high_mm;
extern float ps_s3ds_exit_pupil_low_mm, ps_s3ds_exit_pupil_high_mm;
extern float ps_s3ds_pupil_field_low, ps_s3ds_pupil_field_high;
extern float ps_s3ds_eye_tracking_speed, ps_s3ds_eye_tracking_accel_mm_s2, ps_s3ds_eye_tracking_limit_mm;
extern float ps_s3ds_tunneling_parallax, ps_s3ds_tunneling_min, ps_s3ds_tunneling_max;
extern float ps_s3ds_tunneling_softness;
extern float ps_svp_exit_scale, ps_svp_exit_offset, ps_svp_tunnel_scale, ps_svp_tunnel_offset, ps_svp_dim_scale, ps_svp_dim_offset;
extern float g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_max_mag;
extern Fvector4 ps_s3ds_param_1;
extern Fvector4 ps_svp_exit_curve_low, ps_svp_exit_curve_high;
extern Fvector4 ps_svp_tunnel_curve_low, ps_svp_tunnel_curve_high;
extern Fvector4 ps_svp_dim_curve_low, ps_svp_dim_curve_high;

// pip physical optics helpers, re-housed from the aperture model, the math matches the source lib
static SvpPhysicalOptics::MagnificationResponse svp_make_response(const Fvector4& low, const Fvector4& high)
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

// interpolate a low/high profile endpoint across the optic's zoom range
static float svp_interp_profile(float low, float high)
{
	return SvpPhysicalOptics::InterpolateMagnification(low, high, g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_max_mag);
}

float svp_eye_relief_mm()
{
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float low = config.typed_route ? config.eye_relief_low_mm : ps_s3ds_eye_relief_low_mm;
	const float high = config.typed_route ? config.eye_relief_high_mm : ps_s3ds_eye_relief_high_mm;
	return svp_interp_profile(low, high);
}

static float svp_pupil_field_scale()
{
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float low = config.typed_route ? config.pupil_field_low : ps_s3ds_pupil_field_low;
	const float high = config.typed_route ? config.pupil_field_high : ps_s3ds_pupil_field_high;
	return svp_interp_profile(low, high);
}

// exit pupil mm, authored low/high reciprocal-mag interp then objective/mag then the ocular-ratio proxy
static float svp_calc_exit_pupil_at_mag_mm(float objective_mm, float magnification)
{
	if (magnification <= 0.01f)
		return 0.f;

	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	float low = config.typed_route ? config.exit_pupil_low_mm : ps_s3ds_exit_pupil_low_mm;
	float high = config.typed_route ? config.exit_pupil_high_mm : ps_s3ds_exit_pupil_high_mm;
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
		return SvpPhysicalOptics::InterpolateReciprocalMagnification(low, high, magnification, g_pip_scope_min_mag, g_pip_scope_max_mag);
	}

	const float authored_exit = ps_s3ds_param_1.z > 0.01f ? ps_s3ds_param_1.z : 0.5f;
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : magnification;
	return authored_exit * Device.m_SecondViewport.eyepiece.radius * 2000.f * (minimum_mag / magnification);
}

static float svp_calc_exit_pupil_mm(float objective_mm)
{
	return svp_calc_exit_pupil_at_mag_mm(objective_mm, g_pip_scope_magnification);
}

// aperture twilight dimming, exit-pupil transmission scaled by the per-mag dim curve, outer gate assumed
static float svp_calc_twilight_dim(float pupil_mm, float environment_brightness)
{
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	const float pupil_ratio = _min(exit_pupil_mm / pupil_mm, 1.f);
	const float relative_brightness = ps_r__svp_photo_model ? pupil_ratio * pupil_ratio : pupil_ratio;
	const auto& config = Device.m_SecondViewport.RenderOpticConfig();
	const float authored_twilight = config.typed_route ? config.twilight_strength : ps_s3ds_twilight_strength;
	const float authored_transmission = config.typed_route ? config.transmission : ps_s3ds_transmission;
	const float twilight_strength = _min(ps_r__svp_twilight, 1.f) * clampr(authored_twilight, 0.f, 1.f);
	float dimming = clampr(authored_transmission, 0.f, 1.f) * (1.f + (_max(relative_brightness, 0.6f) - 1.f) * twilight_strength);
	const float response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_dim_curve_low, ps_svp_dim_curve_high), g_pip_scope_magnification, ps_svp_dim_scale, ps_svp_dim_offset);
	dimming = 1.f - clampr((1.f - dimming) * response, 0.f, 1.f);

	if (ps_r__svp_diag)
	{
		static u32 s_twl_ms = 0;
		if (Device.dwTimeGlobal - s_twl_ms > 1000)
		{
			s_twl_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", exit_pupil_mm, pupil_mm, environment_brightness, dimming);
		}
	}
	return dimming;
}

// aperture eyebox half angle, exit-pupil radius plus eye pupil over the interpolated eye relief
static void svp_update_eyebox_limit(float pupil_mm)
{
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && exit_pupil_mm > 0.01f && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
	{
		const float exit_radius = exit_pupil_mm * 0.0005f;
		const float pupil_radius = pupil_mm * 0.0005f;
		const float eye_relief = _max(svp_eye_relief_mm() * 0.001f, 0.05f);
		Device.m_SecondViewport.svp_eyebox_rad = atanf((exit_radius + pupil_radius) / eye_relief);
		return;
	}

	Device.m_SecondViewport.svp_eyebox_rad = 0.f;
	if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
	{
		static u32 s_ebg_ms = 0;
		if (Device.dwTimeGlobal - s_ebg_ms > 1000)
		{
			s_ebg_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f", ps_r__svp_authored_optics,
				Device.m_SecondViewport.svp_opt_offset.w, Device.m_SecondViewport.svp_opt_obj_mm, g_pip_scope_magnification, pupil_mm);
		}
	}
}

SSvpEyeSample svp_update_eye_sample(const Fmatrix& eye_view)
{
	auto& viewport = Device.m_SecondViewport;
	static SvpPhysicalOptics::EyeTrackingState tracking;
	static SSvpEyeSample cached;
	static u32 cached_frame = u32(-1);
	static u32 cached_session = 0;

	if (scope_svp_enabled < 2 || !viewport.IsSVPActive())
		return {};

	const u32 session = viewport.GetSVPSession();
	if (cached_session != session)
	{
		tracking = {};
		cached = {};
		cached_frame = u32(-1);
		cached_session = session;
	}
	if (cached_frame == Device.dwFrame)
		return cached;

	cached = {};
	cached_frame = Device.dwFrame;
	SSvpEyeSample& sample = cached;
	const auto& eyepiece = viewport.eyepiece;
	if (eyepiece.radius <= EPS)
		return sample;

	Fvector lens_right = eyepiece.m_W.i;
	Fvector lens_up = eyepiece.m_W.j;
	Fvector optical_axis = eyepiece.m_W.k;
	lens_right.normalize_safe();
	lens_up.normalize_safe();
	optical_axis.normalize_safe();

	Fvector lens_center_view, lens_right_view, lens_up_view, axis_view;
	eye_view.transform_tiny(lens_center_view, eyepiece.m_W.c);
	eye_view.transform_dir(lens_right_view, lens_right);
	eye_view.transform_dir(lens_up_view, lens_up);
	eye_view.transform_dir(axis_view, optical_axis);
	lens_right_view.normalize_safe();
	lens_up_view.normalize_safe();
	axis_view.normalize_safe();

	Fvector eye_ray = lens_center_view;
	eye_ray.normalize_safe();
	const float forward = eye_ray.dotproduct(axis_view);
	if (_abs(forward) <= 0.001f)
		return sample;

	const float eye_relief_mm = svp_eye_relief_mm();
	const float inverse_forward = 1.f / forward;
	sample.raw_mm.set(-eye_ray.dotproduct(lens_right_view) * inverse_forward * eye_relief_mm,
		-eye_ray.dotproduct(lens_up_view) * inverse_forward * eye_relief_mm);
	sample.eye_relief_mm = eye_relief_mm;
	const SvpPhysicalOptics::Vec2 raw = { sample.raw_mm.x, sample.raw_mm.y };
	const auto& config = viewport.RenderOpticConfig();
	const float tracking_limit = config.typed_route ? config.tracking_limit_mm : ps_s3ds_eye_tracking_limit_mm;
	const float tracking_speed = config.typed_route ? config.tracking_speed : ps_s3ds_eye_tracking_speed;
	const float tracking_accel = config.typed_route ? config.tracking_accel_mm_s2 : ps_s3ds_eye_tracking_accel_mm_s2;
	const SvpPhysicalOptics::Vec2 target = SvpPhysicalOptics::LimitEyeOffset(raw, tracking_limit);
	SvpPhysicalOptics::UpdateEyeTracking(tracking, target, viewport.svp_eye_tracking_suspended.load(std::memory_order_acquire),
		viewport.svp_camera_epoch, Device.dwFrame, Device.fTimeDelta, tracking_speed, tracking_accel);
	sample.residual_mm.set(sample.raw_mm.x - tracking.offset.x, sample.raw_mm.y - tracking.offset.y);

	const float objective_mm = svp_objective_mm();
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(objective_mm);
	if (objective_mm > 0.01f && exit_pupil_mm > 0.01f)
		sample.entrance_scale = objective_mm / exit_pupil_mm;
	else
		sample.entrance_scale = _max(g_pip_scope_magnification, 1.f);
	sample.valid = _valid(sample.raw_mm.x) && _valid(sample.raw_mm.y) && _valid(sample.residual_mm.x) && _valid(sample.residual_mm.y) &&
		_valid(sample.eye_relief_mm) && sample.eye_relief_mm > 0.01f && _valid(sample.entrance_scale) && sample.entrance_scale > 0.f;
	return sample;
}

static bool svp_project_direction_to_lens(
	const Fvector& direction, const CSecondVPParams::WeaponPoseSnapshot& pose, const Fmatrix& projection_matrix, Fvector4& alignment)
{
	const auto projection = SvpPhysicalOptics::ProjectRayToCenteredLens({ direction.x, direction.y, direction.z },
		{ pose.camera_right.x, pose.camera_right.y, pose.camera_right.z }, { pose.camera_up.x, pose.camera_up.y, pose.camera_up.z },
		{ pose.camera_forward.x, pose.camera_forward.y, pose.camera_forward.z }, projection_matrix._11, projection_matrix._22);
	if (!projection.valid)
		return false;

	alignment.set(std::clamp(projection.lens_offset.x, -1.f, 1.f), std::clamp(projection.lens_offset.y, -1.f, 1.f), 1.f, 0.f);
	return _valid(alignment);
}

static bool svp_project_world_point_to_lens(const Fvector& point, const Fmatrix& view, const Fmatrix& projection, Fvector4& alignment)
{
	Fmatrix view_projection;
	view_projection.mul(projection, view);

	Fvector4 clip;
	view_projection.transform(clip, { point.x, point.y, point.z, 1.f });
	if (!_valid(clip) || clip.w <= EPS)
		return false;

	const float inverse_w = 1.f / clip.w;
	alignment.set(std::clamp(clip.x * inverse_w * 0.5f, -1.f, 1.f), std::clamp(-clip.y * inverse_w * 0.5f, -1.f, 1.f), 1.f, 0.f);
	return _valid(alignment);
}

enum class ESvpLensAlignmentStatus : u8
{
	disabled,
	camera_stale,
	pose_stale,
	ray_invalid,
	route_mismatch,
	config_mismatch,
	projection_invalid,
	sight_stale,
	sight_config_mismatch,
	sight_invalid,
	sight_projection_invalid,
	valid
};

struct SSvpLensAlignment
{
	Fvector4 projectile = {};
	Fvector4 scope = {};
	ESvpLensAlignmentStatus status = ESvpLensAlignmentStatus::disabled;
};

static LPCSTR svp_lens_alignment_status(ESvpLensAlignmentStatus status)
{
	switch (status)
	{
	case ESvpLensAlignmentStatus::disabled: return "disabled";
	case ESvpLensAlignmentStatus::camera_stale: return "camera-stale";
	case ESvpLensAlignmentStatus::pose_stale: return "pose-stale";
	case ESvpLensAlignmentStatus::ray_invalid: return "ray-invalid";
	case ESvpLensAlignmentStatus::route_mismatch: return "route-mismatch";
	case ESvpLensAlignmentStatus::config_mismatch: return "config-mismatch";
	case ESvpLensAlignmentStatus::projection_invalid: return "projection-invalid";
	case ESvpLensAlignmentStatus::sight_stale: return "sight-stale";
	case ESvpLensAlignmentStatus::sight_config_mismatch: return "sight-config-mismatch";
	case ESvpLensAlignmentStatus::sight_invalid: return "sight-invalid";
	case ESvpLensAlignmentStatus::sight_projection_invalid: return "sight-projection-invalid";
	case ESvpLensAlignmentStatus::valid: return "valid-pip-projectile-and-sight";
	}
	return "unknown";
}

static SSvpLensAlignment svp_project_weapon_to_lens()
{
	SSvpLensAlignment result;
	auto& viewport = Device.m_SecondViewport;
	if (!ps_r__svp_aperture)
		return result;
	if (viewport.svp_camera_frame != Device.dwFrame || viewport.svp_camera_session != viewport.GetSVPSession())
	{
		result.status = ESvpLensAlignmentStatus::camera_stale;
		return result;
	}

	CSecondVPParams::WeaponPoseSnapshot pose;
	if (!viewport.ReadWeaponPose(pose) || !viewport.SnapshotExact(pose.frame, pose.session, Device.dwFrame))
	{
		result.status = ESvpLensAlignmentStatus::pose_stale;
		return result;
	}
	if (!_valid(pose.fire_ray_pos) || !_valid(pose.fire_ray_dir) || !_valid(pose.camera_pos) || !_valid(pose.camera_right) ||
		!_valid(pose.camera_up) || !_valid(pose.camera_forward) || pose.fire_ray_dir.square_magnitude() <= EPS ||
		pose.camera_right.square_magnitude() <= EPS || pose.camera_up.square_magnitude() <= EPS ||
		pose.camera_forward.square_magnitude() <= EPS)
	{
		result.status = ESvpLensAlignmentStatus::ray_invalid;
		return result;
	}

	const auto& config = viewport.RenderOpticConfig();
	if (pose.optic_typed != config.typed_route)
	{
		result.status = ESvpLensAlignmentStatus::route_mismatch;
		return result;
	}
	if (!CSecondVPParams::MatchesOpticConfig(pose, config))
	{
		result.status = ESvpLensAlignmentStatus::config_mismatch;
		return result;
	}

	Fvector convergence_target;
	if (pose.fire_ray_target_valid && _valid(pose.fire_ray_target))
		convergence_target.set(pose.fire_ray_target);
	else
	{
		Fvector fire_direction = pose.fire_ray_dir;
		fire_direction.normalize_safe();
		const float zero_distance = pose.fire_ray_zero > EPS ? pose.fire_ray_zero : 100.f;
		convergence_target.mad(pose.fire_ray_pos, fire_direction, zero_distance);
	}
	Fvector4 projectile_alignment = {};
	if (!svp_project_world_point_to_lens(convergence_target, Device.matrices[1].mView, Device.matrices[1].mProject, projectile_alignment))
	{
		result.status = ESvpLensAlignmentStatus::projection_invalid;
		return result;
	}

	// Use the sight published by deriveScopeLens() for this render frame.
	CSecondVPParams::SightSnapshot sight;
	if (!viewport.ReadSight(sight) || !viewport.SnapshotExact(sight.frame, sight.session, Device.dwFrame) ||
		sight.optic_epoch != viewport.svp_optic_epoch)
	{
		result.status = ESvpLensAlignmentStatus::sight_stale;
		return result;
	}
	if (!CSecondVPParams::MatchesOpticConfig(sight, config))
	{
		result.status = ESvpLensAlignmentStatus::sight_config_mismatch;
		return result;
	}
	if (!_valid(sight.direction) || sight.direction.square_magnitude() <= EPS)
	{
		result.status = ESvpLensAlignmentStatus::sight_invalid;
		return result;
	}
	Fvector scope_direction = sight.direction;
	scope_direction.normalize_safe();
	Fvector4 scope_alignment = {};
	if (!svp_project_direction_to_lens(scope_direction, pose, Device.matrices[1].mProject, scope_alignment))
	{
		result.status = ESvpLensAlignmentStatus::sight_projection_invalid;
		return result;
	}

	result.projectile = projectile_alignment;
	result.scope = scope_alignment;
	result.status = ESvpLensAlignmentStatus::valid;
	return result;
}

static void svp_log_aperture_state(
	const SSvpEyeSample& eye, const Fvector2& applied_eye, float exit_pupil_mm, float pupil_mm, const SSvpLensAlignment& alignment)
{
	if (!ps_r__svp_diag)
		return;

	static u32 s_apert_ms = 0;
	if (Device.dwTimeGlobal - s_apert_ms <= 1000)
		return;
	s_apert_ms = Device.dwTimeGlobal;

	const float magnification = _max(g_pip_scope_magnification, 1.f);
	PipMsg(
		"[SVP-APERT] raw %.2f,%.2fmm applied %.2f,%.2fmm exit_r %.2fmm pupil_r %.2fmm mag %.2f projectile_uv %.4f,%.4f scope_uv %.4f,%.4f tube_uv %.4f,%.4f valid %u status %s",
		eye.raw_mm.x, eye.raw_mm.y, applied_eye.x, applied_eye.y, exit_pupil_mm * 0.5f, pupil_mm * 0.5f, g_pip_scope_magnification,
		alignment.projectile.x, alignment.projectile.y, alignment.scope.x, alignment.scope.y, alignment.scope.x / magnification,
		alignment.scope.y / magnification, alignment.projectile.z > 0.5f ? 1u : 0u, svp_lens_alignment_status(alignment.status));
}

static void svp_log_reticle_fit(float slope, float scale, float eyepiece_radius, float axial_distance)
{
	if (!ps_r__svp_diag)
		return;

	static u32 s_reticle_ms = 0;
	if (Device.dwTimeGlobal - s_reticle_ms <= 1000)
		return;
	s_reticle_ms = Device.dwTimeGlobal;

	PipMsg("[SVP-RETZ] z=%.4f kg=%.4f R=%.4fcm L=%.4fcm fit=%d", slope, scale, eyepiece_radius * 100.f, axial_distance * 100.f,
		ps_r__svp_reticle_fit);
}

// physical aperture bind, always binds the aperture constants, x = 0 when the cvar is off
static void svp_bind_aperture(float pupil_mm)
{
	const float minimum_mag = g_pip_scope_min_mag > 0.01f ? g_pip_scope_min_mag : g_pip_scope_magnification;
	const float maximum_mag = g_pip_scope_max_mag > minimum_mag ? g_pip_scope_max_mag : minimum_mag;
	const float exit_pupil_mm = svp_calc_exit_pupil_mm(svp_objective_mm());
	auto& viewport = Device.m_SecondViewport;
	const auto& eyepiece = viewport.eyepiece;
	Fvector lens_right = eyepiece.m_W.i;
	Fvector lens_up = eyepiece.m_W.j;
	lens_right.normalize_safe();
	lens_up.normalize_safe();

	const SSvpEyeSample eye = svp_update_eye_sample(Device.matrices[0].mView);
	const Fvector2 raw_eye_offset_mm = eye.raw_mm;
	// Objective projection uses the raw offset in svp_camera. Aperture loss follows
	// the tracked eye so it can recenter independently of the render-camera domain.
	const Fvector2 aperture_eye_offset_mm = eye.residual_mm;
	const float inverse_lens_diameter = eyepiece.radius > EPS ? 0.5f / eyepiece.radius : 0.f;

	// a rigid display panel carries no eyepiece, its image ignores where the eye sits
	const bool digital_display = !svp_optic_eye_coupled();
	RCache.set_c("svp_aperture", ps_r__svp_aperture ? 1.f : 0.f, g_pip_scope_magnification, minimum_mag, maximum_mag);
	RCache.set_c("svp_eyebox", digital_display ? 0.f : aperture_eye_offset_mm.x, digital_display ? 0.f : aperture_eye_offset_mm.y,
		exit_pupil_mm * 0.5f, pupil_mm * 0.5f);
	// The Reference shader scales the eye offset by how far the exit pupil has
	// shrunk from its size at the lowest zoom.
	const float low_exit_mm = svp_calc_exit_pupil_at_mag_mm(svp_objective_mm(), minimum_mag);
	const float relief_mm = svp_eye_relief_mm();
	float source_zoom_gain = 1.f;
	const float max_zoom_gain = 10.f;
	if (!digital_display && _valid(low_exit_mm) && low_exit_mm > 0.f && _valid(exit_pupil_mm) && exit_pupil_mm > 0.f && _valid(pupil_mm) &&
		pupil_mm > 0.f && _valid(relief_mm) && relief_mm > 0.f)
		source_zoom_gain = std::clamp((low_exit_mm + pupil_mm) / (exit_pupil_mm + pupil_mm), 1.f, max_zoom_gain);
	RCache.set_c("reference_source_zoom", source_zoom_gain, 0.f, 0.f, 0.f);
	const float exit_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_exit_curve_low, ps_svp_exit_curve_high), g_pip_scope_magnification, ps_svp_exit_scale, ps_svp_exit_offset);
	const float tunnel_response = SvpPhysicalOptics::ApplyMagnificationResponse(
		svp_make_response(ps_svp_tunnel_curve_low, ps_svp_tunnel_curve_high), g_pip_scope_magnification, ps_svp_tunnel_scale, 0.f);
	const auto& config = viewport.RenderOpticConfig();
	const float tunnel_parallax = digital_display ? 0.f : (config.typed_route ? config.tunneling_parallax : ps_s3ds_tunneling_parallax);
	const float tunnel_min = digital_display ? 0.f : (config.typed_route ? config.tunneling_min : ps_s3ds_tunneling_min);
	const float tunnel_max = digital_display ? 0.f : (config.typed_route ? config.tunneling_max : ps_s3ds_tunneling_max);
	const float tunnel_softness = digital_display ? 0.f : (config.typed_route ? config.tunneling_softness : ps_s3ds_tunneling_softness);
	RCache.set_c("svp_optic_profile", tunnel_parallax, tunnel_min, tunnel_max, tunnel_response);
	RCache.set_c("svp_pupil_model", svp_pupil_field_scale(), exit_response, digital_display ? 0.f : ps_svp_tunnel_offset, tunnel_softness);
	RCache.set_c("svp_lens_center", eyepiece.m_W.c.x, eyepiece.m_W.c.y, eyepiece.m_W.c.z, inverse_lens_diameter);
	RCache.set_c("svp_lens_right", lens_right.x, lens_right.y, lens_right.z, 0.f);
	RCache.set_c("svp_lens_up", lens_up.x, lens_up.y, lens_up.z, 0.f);
	const SSvpLensAlignment alignment = svp_project_weapon_to_lens();
	RCache.set_c("svp_barrel_alignment", alignment.projectile);
	RCache.set_c("svp_scope_alignment", alignment.scope);
	// ads reflection dead zone, z enables it, w is the reflection visibility once aligned
	RCache.set_c("svp_lens_deadzone", 0.f, 0.f, 1.f, 1.f);
	svp_log_aperture_state(eye, aperture_eye_offset_mm, exit_pupil_mm, pupil_mm, alignment);
}

void svp_bind_lens_constants()
{
	RCache.set_c("scope_svp", (int)Device.m_SecondViewport.IsSVPActive());
	RCache.set_c("scope_debug", (int)scope_debug);
	// the projection that placed the PiP image, atlas reticles map lens UV to world angle through it
	if (Device.m_SecondViewport.IsSVPActive())
	{
		const Fmatrix& P = Device.matrices[1].mProject;
		RCache.set_c("svp_reticle_projection", 1.f / P._11, 1.f / P._22, P._31, P._32);
	}
	else
		RCache.set_c("svp_reticle_projection", 0.f, 0.f, 0.f, 0.f);
	Fvector pt = { 0, 0, 0 };
	Device.m_SecondViewport.eyepiece.m_W.transform(pt);
	RCache.set_c("scope_w_eyepiece", pt.x, pt.y, pt.z, 1.0f);
	const Fvector& w_ffp = Device.m_SecondViewport.w_ffp;
	const Fvector& w_sfp = Device.m_SecondViewport.w_sfp;
	RCache.set_c("scope_w_ffp", w_ffp.x, w_ffp.y, w_ffp.z, 1.0f);
	RCache.set_c("scope_w_sfp", w_sfp.x, w_sfp.y, w_sfp.z, 1.0f);
	// pip reticle collimator geometry 2*ocular_radius/eye_distance, the shader rebuilds the
	// authored reticle magnification as a centered field under true PiP
	{
		Fvector ed;
		ed.sub(Device.m_SecondViewport.eyepiece.m_W.c, Device.vCameraPosition);
		const float dist = _max(ed.magnitude(), 0.02f);
		float kg = 2.f * Device.m_SecondViewport.eyepiece.radius / dist;
		clamp(kg, 0.02f, 3.f);
		// y = true-scale parallax, the real reticle shift is ~0.15 mrad at full eye deflection
		extern float g_pip_scope_ratio;
		float par = 0.f;
		if (ps_r__svp_parallax > 0.f && g_pip_scope_magnification > 0.01f)
		{
			const float eff_mag = _max(g_pip_scope_ratio * g_pip_scope_magnification, 1.f);
			const float hfov = deg2rad(_max(Device.fFOV, 1.f));
			par = ps_r__svp_parallax * 0.00075f * kg * eff_mag / hfov;
		}
		// z is the sine exact rim slope 2R/sqrt(RR+LL) on the axial depth, the stock
		// per pixel tangent saturates at the rim where kg in x stays the linear tan
		float zslope = 0.f;
		const float axial_distance = _max(ed.dotproduct(Device.vCameraDirection), 0.02f);
		if (ps_r__svp_reticle_fit)
		{
			const float R = Device.m_SecondViewport.eyepiece.radius;
			zslope = 2.f * R / sqrtf(R * R + axial_distance * axial_distance);
			clamp(zslope, 0.02f, 3.f);
		}
		svp_log_reticle_fit(zslope, kg, Device.m_SecondViewport.eyepiece.radius, axial_distance);
		RCache.set_c("svp_optics", kg, par, zslope, _max(g_pip_scope_ratio, 1.f));
	}
	// pip scope-local exposure, x = 0 off else 2^bias
	{
		// y = exit-pupil twilight dimming, exit pupil (ocular*ratio shrunk by zoom) vs the
		// env-adapted eye pupil squared, electronic sights exempt
		extern Fvector4 ps_s3ds_param_3;
		// nvg on = the tube gain owns the scope brightness, our exposure lifts stand down
		extern Fvector4 ps_dev_param_8;
		const bool nvg_on = ps_dev_param_8.x >= 1.f;
		// env-adapted eye pupil (mm), shared by the twilight dimming and the eyebox
		float envb = 0.f, pupil_mm = 0.f;
		if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
		{
			CEnvDescriptor& E = *g_pGamePersistent->Environment().CurrentEnv;
			envb = 0.299f * E.sun_color.x + 0.587f * E.sun_color.y + 0.114f * E.sun_color.z +
				0.5f * (0.299f * E.hemi_color.x + 0.587f * E.hemi_color.y + 0.114f * E.hemi_color.z);
			if (ps_r__svp_photo_model)
			{
				// Moon-Spencer pupil response, luminance anchored so envb 1 reads as an
				// overcast day (~2500 cd/m2) and envb 0.01 as moonlight (~0.25)
				const float L = 2500.f * envb * envb;
				pupil_mm = 4.9f - 3.f * tanhf(0.4f * log10f(_max(L, 1e-4f)));
				clamp(pupil_mm, 2.f, 8.f);
			}
			else
				pupil_mm = 6.f - 3.5f * _min(envb / 0.25f, 1.f);
		}
		float dim = 0.f;
		extern int ps_markswitch_current;
		// twilight dims passive optics, NV/thermal are exempt only while their overlay is actually
		// active (markswitch 0 for NV, < 2 for thermal), else the scope shows a plain image
		const bool overlay_active = svp_overlay_active(ps_s3ds_param_3.x, ps_markswitch_current);
		if (ps_r__svp_twilight > 0.f && !overlay_active && g_pip_scope_magnification > 0.01f && pupil_mm > EPS)
		{
			// aperture uses the exit-pupil transmission + dim curve, else our current twilight
			if (ps_r__svp_aperture)
				dim = svp_calc_twilight_dim(pupil_mm, envb);
			else
			{
				const float mn = (g_pip_scope_min_mag > 0.01f) ? g_pip_scope_min_mag : g_pip_scope_magnification;
				// exit pupil = objective diameter / magnification, real per-scope objective when
				// known, else the ocular-ratio proxy
				const float omm = svp_objective_mm();
				float ep_mm;
				if (omm > 0.01f)
					ep_mm = omm / g_pip_scope_magnification;
				else
				{
					const float xp = (ps_s3ds_param_1.z > 0.01f) ? ps_s3ds_param_1.z : 0.5f;
					ep_mm = xp * Device.m_SecondViewport.eyepiece.radius * 2000.f * (mn / g_pip_scope_magnification);
				}
				// relative brightness is the pupil ratio squared, the legacy model used it linearly
				const float dr = _min(ep_mm / pupil_mm, 1.f);
				const float d = ps_r__svp_photo_model ? dr * dr : dr;
				dim = 1.f + (_max(d, 0.6f) - 1.f) * _min(ps_r__svp_twilight, 1.f);
				if (ps_r__svp_diag)
				{
					static u32 s_twl_ms = 0;
					if (Device.dwTimeGlobal - s_twl_ms > 1000)
					{
						s_twl_ms = Device.dwTimeGlobal;
						PipMsg("[SVP-TWL] ep %.1fmm pupil %.1fmm env %.2f dim %.2f", ep_mm, pupil_mm, envb, dim);
					}
				}
			}
		}
		// pip eyebox half angle from the real exit pupil, the sight anchor bound reads it
		{
			// aperture uses the exit-pupil radius + interpolated eye relief, else our current eyebox
			if (ps_r__svp_aperture)
				svp_update_eyebox_limit(pupil_mm);
			else
			{
				const float eb_omm = svp_objective_mm();
				if (ps_r__svp_eyebox > 0.f && ps_r__svp_authored_optics && eb_omm > 0.01f && g_pip_scope_magnification > 0.01f &&
					pupil_mm > EPS)
				{
					const float ep_r = eb_omm * 0.0005f / g_pip_scope_magnification;
					const float p_r = pupil_mm * 0.0005f;
					// full-blackout half angle for the sight anchor bound, arm = authored eye relief
					const float eb_arm = _max(svp_eye_relief_mm() * 0.001f, 0.05f);
					Device.m_SecondViewport.svp_eyebox_rad = atanf((ep_r + p_r) / eb_arm);
				}
				else
				{
					// no authored bound on this optic, drop the previous scope's so the anchor
					// falls back to its own default instead of a stale narrow eyebox
					Device.m_SecondViewport.svp_eyebox_rad = 0.f;
					if (ps_r__svp_eyebox > 0.f && ps_r__svp_diag)
					{
						static u32 s_ebg_ms = 0;
						if (Device.dwTimeGlobal - s_ebg_ms > 1000)
						{
							s_ebg_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-EYEBOX] gated off, authored %d obj_w %.3f obj_mm %.1f mag %.2f pupil %.1f",
								ps_r__svp_authored_optics, Device.m_SecondViewport.svp_opt_offset.w, Device.m_SecondViewport.svp_opt_obj_mm,
								g_pip_scope_magnification, pupil_mm);
						}
					}
				}
			}
		}
		// crescent drive, exposure zw carries the latched swing side, the tangent offset is
		// scaled by pupil over eye relief so the bite depth reads the same on every scope
		float st = 0.f;
		float sw_k = 0.f;
		const float shadow_g = Device.m_SecondViewport.svp_shadow_gain;
		{
			st = (g_pip_scope_magnification > 0.01f) ? (g_pip_scope_magnification - 1.f) / 7.f : 0.f;
			clamp(st, 0.f, 1.f);
			Device.m_SecondViewport.svp_mag = _max(g_pip_scope_magnification, 0.f);
			const float snug = 1.3f + (0.55f - 1.3f) * st;
			float gp = shadow_g * 4.f;
			clamp(gp, 0.f, 1.f);
			float z_eff = ps_s3ds_param_1.z * (3.0f + (snug - 3.0f) * gp);
			if (z_eff < 0.08f)
				z_eff = 0.08f;
			sw_k = 0.4f * z_eff / _max(svp_eye_relief_mm() * 0.1f, 0.05f);
		}
		RCache.set_c("svp_exposure", (ps_r__svp_local_exposure && !nvg_on) ? powf(2.f, ps_r__svp_exposure_bias) : 0.f, nvg_on ? 0.f : dim,
			Device.m_SecondViewport.svp_swing_x * shadow_g * sw_k, Device.m_SecondViewport.svp_swing_y * shadow_g * sw_k);
		// pip physical aperture, exit-pupil transmission + virtual-eye eyebox, x = 0 when disabled
		svp_bind_aperture(pupil_mm);
		// pip glass2: x = lens coating strength, y = heat mirage (sun elevation + magnification)
		{
			float mirage = 0.f;
			if (ps_r__svp_mirage > 0.f && g_pip_scope_magnification > 0.01f && g_pGamePersistent &&
				g_pGamePersistent->Environment().CurrentEnv)
			{
				CEnvDescriptor& Em = *g_pGamePersistent->Environment().CurrentEnv;
				const float heat = _max(-Em.sun_dir.y, 0.f); // high overhead sun heats the ground
				const float clear = 1.f - _min(Em.rain_density, 1.f);
				const float magf = _min(_max((g_pip_scope_magnification - 2.f) / 6.f, 0.f), 1.f);
				mirage = ps_r__svp_mirage * heat * clear * magf;
				if (ps_r__svp_diag)
				{
					static u32 s_mir_ms = 0;
					if (Device.dwTimeGlobal - s_mir_ms > 1000)
					{
						s_mir_ms = Device.dwTimeGlobal;
						PipMsg("[SVP-MIRAGE] heat %.2f clear %.2f magf %.2f -> %.3f", heat, clear, magf, mirage);
					}
				}
			}
			// w = flat-panel V-crop: (svp W/H) / panel aspect. 1 when the SVP already matches the
			// panel (non-square path, no crop needed); 1/aspect when the SVP is square (fallback)
			float vcrop = 1.f;
			if (RImplementation.TargetSVP && RImplementation.TargetSVP->Height > 0 && Device.m_SecondViewport.svp_panel_aspect > 0.01f)
				vcrop = ((float)RImplementation.TargetSVP->Width / (float)RImplementation.TargetSVP->Height) /
					Device.m_SecondViewport.svp_panel_aspect;
			RCache.set_c("svp_glass2", ps_r__svp_coating, mirage, 0.f, vcrop);
			Device.m_SecondViewport.svp_panel_vcrop = vcrop; // pip binocular bracket mapping reads it
			// pip glass3: x = sharpen amount, y = field-stop onset, z = sharpen radial falloff, w = sharpen inner crisp radius
			// pip field-stop onset, a stop at the field edge blurred by the viewing aperture
			// which is the exit pupil capped by the eye pupil, 1 = off
			float fs_onset = 1.f;
			{
				const float fs_omm = svp_objective_mm();
				const float fs_fov = Device.m_SecondViewport.svp_fov;
				if (ps_r__svp_field_stop && fs_omm > 0.01f && g_pip_scope_magnification > 0.01f && fs_fov > 0.01f && pupil_mm > EPS)
				{
					const float ep_r = fs_omm * 0.0005f / g_pip_scope_magnification;
					const float ap_r = _min(ep_r, pupil_mm * 0.0005f);
					const float er = _max(svp_eye_relief_mm() * 0.001f, 0.05f);
					const float app_half = g_pip_scope_magnification * fs_fov * 0.5f;
					const float penumbra = _min(atanf(ap_r / er) / app_half, 1.f);
					fs_onset = 1.f - 0.5f * penumbra;
					if (ps_r__svp_diag)
					{
						static u32 s_fs_ms = 0;
						if (Device.dwTimeGlobal - s_fs_ms > 1000)
						{
							s_fs_ms = Device.dwTimeGlobal;
							PipMsg("[SVP-FSTOP] ep %.1fmm pupil %.1fmm onset %.3f", ep_r * 2000.f, pupil_mm, fs_onset);
						}
					}
				}
			}
			RCache.set_c("svp_glass3", ps_r__svp_sharpen, fs_onset, ps_r__svp_sharpen_falloff, ps_r__svp_sharpen_inner);
			// pip glass4: x = nvg bleach roll-off, y = nvg auto-gain, w = shadow swing envelope
			RCache.set_c("svp_glass4", ps_r__svp_nvg_bleach, ps_r__svp_nvg_sensitivity, 0.f, shadow_g);
		}
	}
}
#endif
