#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#include "svp_camera_tracking.h"

LPCSTR svp_camera_domain_name(CSecondVPParams::ECameraDomain domain)
{
	switch (domain)
	{
	case CSecondVPParams::camera_main_eye: return "main-eye";
	case CSecondVPParams::camera_objective: return "objective";
	default: return "unknown";
	}
}

// objective near plane, r__svp_near above zero is a manual override, otherwise it tracks the nearest
// drawn weapon extent the drain published last frame, rising slowly and dropping at once
float svp_auto_near(CSecondVPParams& vp, float cap, float& out_min, bool& out_manual, bool& out_fresh)
{
	static float s_slew = R_VIEWPORT_NEAR;
	static u32 s_frame = u32(-1);
	static u32 s_epoch = u32(-1);
	static u32 s_session = 0;
	out_min = vp.svp_hud_min_axial; // raw published value, the log needs the sentinel verbatim
	out_fresh = false;
	out_manual = (ps_r__svp_near > 0.f);
	if (out_manual)
	{
		s_slew = R_VIEWPORT_NEAR;
		s_frame = Device.dwFrame;
		s_epoch = vp.svp_optic_epoch;
		return ps_r__svp_near;
	}
	if (!(cap > R_VIEWPORT_NEAR))
		return R_VIEWPORT_NEAR;
	// this runs once per svp camera build, which is not once per main frame, so the window spans
	// from the previous build rather than a fixed frame
	const u32 prev_build = s_frame;
	const bool gap =
		(s_frame == u32(-1)) || (Device.dwFrame < s_frame) || (Device.dwFrame - s_frame > 8) || (s_session != vp.GetSVPSession());
	if (gap || s_epoch != vp.svp_optic_epoch)
		s_slew = R_VIEWPORT_NEAR;
	s_frame = Device.dwFrame;
	s_epoch = vp.svp_optic_epoch;
	s_session = vp.GetSVPSession();

	// a missed publish holds the last value, real transitions floor through the slew reset above
	float target = s_slew;
	const bool fresh = vp.svp_hud_min_frame != u32(-1) && prev_build != u32(-1) && !gap && vp.svp_hud_min_frame >= prev_build &&
		vp.svp_hud_min_session == vp.GetSVPSession() && vp.svp_hud_min_epoch == vp.svp_optic_epoch;
	out_fresh = fresh;
	if (!fresh)
	{
		static u32 s_stale_ms = 0;
		if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_stale_ms > 5000)
		{
			s_stale_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-CAM] near hold, no fresh publish, r__svp_optic_body_suppress off kills the derive");
		}
	}
	if (fresh)
	{
		// half the measured clearance absorbs the one frame of lag, the sentinel means nothing
		// ahead so the cap is safe, and a zero reaches the plane so only the floor is
		target = (out_min < 0.f) ? cap : (out_min > 0.f ? 0.5f * out_min : (float)R_VIEWPORT_NEAR);
	}
	// an engaged hybrid publishes its reflex near extent, the near plane never clips the capture
	if (vp.svp_hybrid_front >= 0.f && vp.svp_hybrid_front_frame != u32(-1) && prev_build != u32(-1) && !gap &&
		vp.svp_hybrid_front_frame >= prev_build && vp.svp_hybrid_front_session == vp.GetSVPSession() &&
		vp.svp_hybrid_front_epoch == vp.svp_optic_epoch)
		target = _min(target, _max(0.5f * vp.svp_hybrid_front, (float)R_VIEWPORT_NEAR));
	clamp(target, (float)R_VIEWPORT_NEAR, cap);
	const float dt = Device.fTimeDelta;
	const float a = (dt > 0.f) ? (1.f - exp(-dt / 0.25f)) : 1.f;
	s_slew += a * (target - s_slew);
	s_slew = _min(target, s_slew); // rises on the constant, drops the frame the target does
	clamp(s_slew, (float)R_VIEWPORT_NEAR, cap);
	return s_slew;
}

// pip keeps optic identity separate from physical camera input changes
// target sizing follows identity while camera history follows both
void svp_epoch_update(const void* lens_visual, float lens_radius, CSecondVPParams& params)
{
	// relative eyepiece-radius jump marking a different optic, migrated from the ratio reseed seed
	const float optic_change = 0.05f;
	struct SCameraInputs
	{
		Fvector4 offset = {};
		float objective_mm = 0.f;
		float eye_relief_low_mm = 0.f;
		float eye_relief_high_mm = 0.f;
		float exit_pupil_low_mm = 0.f;
		float exit_pupil_high_mm = 0.f;
		u32 config_generation = 0;
	};
	const auto& optic_config = params.RenderOpticConfig();
	SCameraInputs current;
	current.offset = params.svp_opt_offset;
	current.objective_mm = params.svp_opt_obj_mm;
	current.eye_relief_low_mm = optic_config.typed_route ? optic_config.eye_relief_low_mm : ps_s3ds_eye_relief_low_mm;
	current.eye_relief_high_mm = optic_config.typed_route ? optic_config.eye_relief_high_mm : ps_s3ds_eye_relief_high_mm;
	current.exit_pupil_low_mm = optic_config.typed_route ? optic_config.exit_pupil_low_mm : ps_s3ds_exit_pupil_low_mm;
	current.exit_pupil_high_mm = optic_config.typed_route ? optic_config.exit_pupil_high_mm : ps_s3ds_exit_pupil_high_mm;
	current.config_generation = optic_config.typed_route ? optic_config.generation : 0;
	static const void* s_prev_vis = nullptr;
	static float s_prev_r = 0.f;
	static SCameraInputs s_prev_inputs;
	static u32 s_optic_epoch = 0;
	static u32 s_camera_epoch = 0;
	if (lens_radius <= EPS)
		return;
	const bool have_prev = (s_prev_vis != nullptr) && (s_prev_r > EPS);
	const bool vis_change = have_prev && (lens_visual != s_prev_vis);
	const bool r_change = have_prev && (_abs(lens_radius - s_prev_r) > s_prev_r * optic_change);
	auto differs = [](float a, float b, float epsilon) { return !_valid(a) || !_valid(b) || _abs(a - b) > epsilon; };
	const bool input_change = have_prev &&
		(differs(current.offset.x, s_prev_inputs.offset.x, 0.0001f) || differs(current.offset.y, s_prev_inputs.offset.y, 0.0001f) ||
			differs(current.offset.z, s_prev_inputs.offset.z, 0.0001f) || differs(current.offset.w, s_prev_inputs.offset.w, 0.0001f) ||
			differs(current.objective_mm, s_prev_inputs.objective_mm, 0.01f) ||
			differs(current.eye_relief_low_mm, s_prev_inputs.eye_relief_low_mm, 0.01f) ||
			differs(current.eye_relief_high_mm, s_prev_inputs.eye_relief_high_mm, 0.01f) ||
			differs(current.exit_pupil_low_mm, s_prev_inputs.exit_pupil_low_mm, 0.01f) ||
			differs(current.exit_pupil_high_mm, s_prev_inputs.exit_pupil_high_mm, 0.01f) ||
			current.config_generation != s_prev_inputs.config_generation);
	const bool optic_change_now = vis_change || r_change;
	if (optic_change_now)
		++s_optic_epoch;
	if (optic_change_now || input_change)
	{
		++s_camera_epoch;
		if (ps_r__svp_cop_diag)
			PipMsg("[SVP-EPOCH] optic=%u camera=%u reason=%s r %.2f->%.2fcm", s_optic_epoch, s_camera_epoch,
				vis_change ? (r_change ? "visual-radius" : "visual") : (r_change ? "radius" : "inputs"), s_prev_r * 100.f,
				lens_radius * 100.f);
	}
	s_prev_vis = lens_visual;
	s_prev_r = lens_radius;
	s_prev_inputs = current;
	params.svp_optic_epoch = s_optic_epoch;
	params.svp_camera_epoch = s_camera_epoch;
}

float svp_track_capture_ratio(const CSecondVPParams& params, bool analytic_ok, float ratio_analytic)
{
	// coast the analytic fit through the raise and one pole it while settled, the raw lens pose
	// bobs but the along axis depth barely moves so an unsettled aim or alt sight holds the value
	const float aim_rot = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
	const bool alt_sight = Device.m_SecondViewport.svp_alt_sight;
	static float s_ratio = 0.f;
	static u32 s_ratio_frame = 0;
	static u32 s_ratio_epoch = 0;
	const bool gap = (Device.dwFrame != s_ratio_frame + 1);
	s_ratio_frame = Device.dwFrame;
	// subscribe to the optic epoch, a magnifier flip or scope swap bumps it with no frame gap so
	// the fit drops the old optic's value and reseeds at the next settle
	const bool epoch = (params.svp_optic_epoch != s_ratio_epoch);
	s_ratio_epoch = params.svp_optic_epoch;
	const bool can_track = (aim_rot > 0.999f) && !alt_sight;
	// Invalidate even when this frame has no usable lens geometry. Otherwise the
	// epoch is consumed above and the next valid frame inherits the previous optic.
	if (gap || epoch || !_valid(s_ratio) || s_ratio <= 0.f)
		s_ratio = 0.f;
	if (analytic_ok)
	{
		// a session gap or an optic swap drops the latch, the raise value is meaningless so the
		// seed waits for the first settled frame, then the one pole eases residual pose noise
		if (s_ratio <= 0.f)
			s_ratio = can_track ? ratio_analytic : 0.f;
		else if (can_track)
			s_ratio += (ratio_analytic - s_ratio) * (1.f - expf(-Device.fTimeDelta / 0.15f));
	}
	return (s_ratio > 0.f) ? s_ratio : ratio_analytic;
}

bool svp_camera_history(
	const CSecondVPParams& params, u32 camera_session, int entrance_ray_mode, bool referenceEffects)
{
	static bool s_camera_valid = false;
	static bool s_reference_camera = false;
	static u32 s_camera_frame = u32(-1);
	static u32 s_camera_session = 0;
	static u32 s_camera_epoch = 0;
	static CSecondVPParams::ECameraDomain s_camera_domain = CSecondVPParams::camera_main_eye;
	static int s_camera_ray_mode = 0;
	const bool first_camera = !s_camera_valid;
	const bool frame_gap = s_camera_valid && Device.dwFrame != s_camera_frame + 1;
	const bool session_change = s_camera_valid && camera_session != s_camera_session;
	const bool epoch_change = s_camera_valid && params.svp_camera_epoch != s_camera_epoch;
	const bool domain_change = s_camera_valid && params.svp_camera_domain != s_camera_domain;
	const bool ray_change = s_camera_valid && entrance_ray_mode != s_camera_ray_mode;
	const bool history_reset = first_camera || frame_gap || session_change || epoch_change || domain_change || ray_change ||
		referenceEffects != s_reference_camera;
	if (history_reset)
	{
		if (ps_r__svp_cop_diag)
		{
			LPCSTR reason = first_camera
				? "initial"
				: (session_change
						  ? "session"
						  : (frame_gap ? "gap" : (epoch_change ? "camera" : (domain_change ? "domain" : (ray_change ? "ray" : "reference-effects")))));
			PipMsg("[SVP-CAM] history=reset reason=%s previous=%s current=%s frame=%u session=%u", reason,
				svp_camera_domain_name(s_camera_domain), svp_camera_domain_name(params.svp_camera_domain), Device.dwFrame, camera_session);
		}
	}
	s_camera_valid = true;
	s_reference_camera = referenceEffects;
	s_camera_frame = Device.dwFrame;
	s_camera_session = camera_session;
	s_camera_epoch = params.svp_camera_epoch;
	s_camera_domain = params.svp_camera_domain;
	s_camera_ray_mode = entrance_ray_mode;
	return history_reset;
}

float svp_clamp_magnification(float scope_magnification, float fov_aim)
{
	const Fvector4& fovp_c = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
	const float fscale_c = rad2deg(fov_aim) / 75.f;
	const float cfg_max = (fovp_c.x > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp_c.x, fscale_c)) : 0.f;
	static float s_mag_ceiling = 0.f;
	static u32 s_ceiling_frame = 0;
	if (Device.dwFrame != s_ceiling_frame + 1)
		s_mag_ceiling = 0.f; // session gap drops the hold
	s_ceiling_frame = Device.dwFrame;
	const float rot_c = g_pGamePersistent->m_pGShaderConstants->hud_params.x;
	if (cfg_max > EPS && rot_c > 0.999f)
		s_mag_ceiling = cfg_max; // latch the settled config max
	const float ceiling = (s_mag_ceiling > EPS) ? s_mag_ceiling : cfg_max;
	if (ceiling > EPS && scope_magnification > ceiling)
		scope_magnification = ceiling;
	return scope_magnification;
}
