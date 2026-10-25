#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#include "svp_camera.h"
#include "svp_camera_debug.h"
#include "svp_camera_tracking.h"
#include "svp_physical_optics.h"
#include "svp_reference_capture_fit.h"
#include "svp_reference_entrance_policy.h"

static bool svp_rigid_camera_basis(Fmatrix& camera, const Fmatrix& main_eye, const Fvector& eyepiece, const Fvector& objective,
	const char*& forward_lane, const char*& basis_lane, bool& flipped)
{
	const Fvector raw_right = camera.i;
	Fvector forward = camera.k;
	bool main_fallback = false;
	forward_lane = "camera";

	if (!_valid(camera.c))
	{
		camera = main_eye;
		forward = camera.k;
		forward_lane = "main-eye";
		main_fallback = true;
	}
	else if (!_valid(forward) || forward.square_magnitude() <= EPS_S)
	{
		forward.sub(objective, eyepiece);
		if (_valid(forward) && forward.square_magnitude() > EPS_S)
			forward_lane = "optic-axis";
		else
		{
			camera = main_eye;
			forward = camera.k;
			forward_lane = "main-eye";
			main_fallback = true;
		}
	}
	forward.normalize_safe();

	Fvector projection;
	projection.set(forward);
	projection.mul(camera.j.dotproduct(forward));
	Fvector up;
	up.sub(camera.j, projection);
	Fvector right;
	if (_valid(up) && up.square_magnitude() > EPS_S)
	{
		up.normalize_safe();
		right.crossproduct(up, forward);
		right.normalize_safe();
		up.crossproduct(forward, right);
		up.normalize_safe();
		basis_lane = "up";
	}
	else
	{
		projection.set(forward);
		projection.mul(camera.i.dotproduct(forward));
		right.sub(camera.i, projection);
		if (_valid(right) && right.square_magnitude() > EPS_S)
		{
			right.normalize_safe();
			up.crossproduct(forward, right);
			up.normalize_safe();
			basis_lane = "right";
		}
		else
		{
			Fvector seed;
			_abs(forward.y) < 0.9f ? seed.set(0.f, 1.f, 0.f) : seed.set(1.f, 0.f, 0.f);
			right.crossproduct(seed, forward);
			right.normalize_safe();
			up.crossproduct(forward, right);
			up.normalize_safe();
			basis_lane = "world";
		}
	}

	Fvector raw_right_unit = raw_right;
	raw_right_unit.normalize_safe();
	flipped = _valid(raw_right_unit) && raw_right_unit.square_magnitude() > EPS_S && raw_right_unit.dotproduct(right) < 0.f;
	const Fvector position = camera.c;
	camera.set(right, up, forward, position);
	return main_fallback;
}

// Sub-pixel jitter for the optional second viewport upscaler; replace with its
// shared helper. Halton(2,3), 16-sample phase, returns a centered offset in [-0.5,0.5] px
static float svp_halton(u32 i, u32 b)
{
	float f = 1.0f, r = 0.0f;
	while (i > 0)
	{
		f /= (float)b;
		r += f * (float)(i % b);
		i /= b;
	}
	return r;
}
static Fvector2 svp_jitter_offset(u32 frame)
{
	const u32 phase = 16; // Sequence length for the current integration.
	u32 i = (frame % phase) + 1; // Halton is 1-based
	Fvector2 o;
	o.set(svp_halton(i, 2) - 0.5f, svp_halton(i, 3) - 0.5f);
	return o;
}
// apply a pixel jitter to a projection by shifting the post-perspective NDC center, the axis/sign/
// remap convention lives here so the eval swap is one line
static void svp_apply_jitter(Fmatrix& proj, Fvector2 px, float w, float h)
{
	proj.m[2][0] += 2.0f * px.x / w; // NDC x, clip.x gains z * this, the w-divide cancels z
	proj.m[2][1] -= 2.0f * px.y / h; // NDC y, negated for texture-down
}

// pip build the SVP camera (fills Device.matrices[1]) from the captured lens + the weapon
// zoom factor, called after the lens derives so TargetSVP->SetActive reads it ready
bool svpCamera()
{
	// the published zoom is raise transient free, unset falls back to the shader constant
	const float zoom_src = (Device.m_SecondViewport.svp_zoom_pub > 1.f) ? Device.m_SecondViewport.svp_zoom_pub
																		: g_pGamePersistent->m_pGShaderConstants->hud_params.y;
	// the scale rides the live fov so the scope keeps its fov-75 look at any user fov
	float svp_fov = svp_factor_to_fov(zoom_src, Device.m_SecondViewport.svp_fov_scale);
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	// the mag reads the steady wide aim fov (punch free from the weapon publish), the live decomposed
	// fov keeps feeding the vFov/projection so the scope image tracks the actual main view
	const float aim_fov_pub = Device.m_SecondViewport.svp_aim_fov;
	const float fov_aim = (aim_fov_pub > 1.f) ? deg2rad(aim_fov_pub) : fov;

	// a zoom-0 tube sight (1x thermal/nv) has no zoom fov and re-images at 1x, the near-0 value
	// would also blow up the vFov/offset tan() math, the floor sits under the highest ladder mag
	const float floor_scale = _max(Device.m_SecondViewport.svp_fov_scale, 0.1f);
	const float fov_floor = 0.5f * SVP_ZOOM_BASE_FOV * floor_scale / SVP_MAG_LIMIT;
	if (svp_fov < fov_floor)
		svp_fov = rad2deg(fov_aim);

	auto mm = Device.matrices[0];
	auto& params = Device.m_SecondViewport;
	const u32 camera_session = params.GetSVPSession();
	// Reference fits the current captured lens every frame; Gamma zoom/config stay native.
	const bool referenceEffects = RImplementation.TargetMain && RImplementation.TargetMain->scope_reference_prepare_capture();
	const float pupil_parity = params.RenderOpticConfig().typed_route ? params.RenderOpticConfig().pupil_parity : ps_s3ds_pupil_parity;

	// analytic eyepiece fit, a disc of radius r at view depth d projects to ndc height 2*r*_22/d
	// under mProjectHud, the fit is screen height 2 over that. depth on the view forward, exact off axis
	Fmatrix eyeW0;
	eyeW0.invert(mm.mView);
	Fvector camfwd;
	camfwd.set(eyeW0.k);
	camfwd.normalize();
	Fvector eyed;
	eyed.sub(params.eyepiece.m_W.c, eyeW0.c);
	const float lens_depth = eyed.dotproduct(camfwd);
	const float ndc_height = (lens_depth > EPS) ? (2.f * params.eyepiece.radius * mm.mProjectHud._22 / lens_depth) : 0.f;
	const bool analytic_ok = params.eyepiece.radius > EPS && lens_depth > EPS && _valid(ndc_height) && ndc_height > 1e-4f;
	const float ratio_analytic = analytic_ok ? (2.f / ndc_height) : 0.f;
	float ratio_magnification = 1.f;

	// the magnification of the scope (1X 4X etc)
	float scope_magnification = fov_aim / deg2rad(svp_fov);

	// flat screen optic (binocular), the panel is a see-through window so the svp fov is the
	// panel's angular subtense at the weapon zoom, magnification then tracks the stock look
	extern Fvector4 ps_s3ds_param_3;
	const bool flat_optic = ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8;
	const bool flat_window = flat_optic && Device.m_SecondViewport.svp_disc_px > 1.f;
	auto window_fov = [&](float zdeg)
	{
		const float p = _min(Device.m_SecondViewport.svp_disc_px / (float)Device.dwHeight, 1.5f);
		return 2.f * atanf(p * tanf(deg2rad(zdeg) * 0.5f));
	};
	if (flat_window)
	{
		// authored mag flat optics keep the clean optical mag, only the see-through panel takes the subtense ratio
		if (!params.svp_authored_mag)
			scope_magnification = fov_aim / window_fov(svp_fov);
		ratio_magnification = 1.f;
	}
	else if (referenceEffects)
	{
		// The shared latch is captured before deriveScopeLens and this camera build. Use that
		// frame's lens depth and final HUD projection (including Reference clip scale), not
		// a settled-ADS size carried across an animated raise, lean or action.
		const bool fresh_lens = params.svp_lens_frame == Device.dwFrame && params.svp_lens_root;
		ratio_magnification = SvpReferenceCapture::CurrentFit(params.eyepiece.radius, lens_depth, mm.mProjectHud._22, fresh_lens);
	}
	else
	{
		ratio_magnification = svp_track_capture_ratio(params, analytic_ok, ratio_analytic);
	}

	// magnification ceiling, the optic's authored config max zoom (hud_fov_params.x, the same source
	// as g_pip_scope_max_mag), held through section churn so a flip cannot push the mag past the optic
	scope_magnification = svp_clamp_magnification(scope_magnification, fov_aim);

	// pip ratio pipeline diag ([SVP-RATIO]), cross checks the analytic fit against the measured
	// screen ndc extent, they agree for a settled on axis pose, measured lives here for the check
	{
		if (ps_r__svp_diag)
		{
			static u32 s_rat_ms = 0;
			if (Device.dwTimeGlobal - s_rat_ms > 1000)
			{
				s_rat_ms = Device.dwTimeGlobal;
				Fvector4 top, bot;
				Fmatrix m_WVP = Fmatrix().mul(mm.mProjectHud, Fmatrix().mul(mm.mView, params.eyepiece.m_W));
				m_WVP.transform(top, { 0, params.eyepiece.radius, 0, 1 });
				m_WVP.transform(bot, { 0, -params.eyepiece.radius, 0, 1 });
				top.div(top.w);
				bot.div(bot.w);
				float meas_h = abs(top.y - bot.y);
				if (!_valid(meas_h) || meas_h < 0.001f)
					meas_h = 0.001f;
				float hf, ha, hn, hff;
				mm.mProjectHud.decompose_projection(hf, ha, hn, hff);
				PipMsg(
					"[SVP-RATIO] meas %.3f analytic %.3f final %.3f flat %d p3y %.1f measH %.3f ndcH %.3f depth %.1fcm r %.2fcm aim %.2f alt %d ok %d hfov %.1f referenceCurrent %d lensFrame %u frame %u",
					2.f / meas_h, ratio_analytic, ratio_magnification, (int)flat_window, ps_s3ds_param_3.y, meas_h, ndc_height,
					lens_depth * 100.f, params.eyepiece.radius * 100.f, g_pGamePersistent->m_pGShaderConstants->hud_params.x,
					(int)Device.m_SecondViewport.svp_alt_sight, (int)analytic_ok, rad2deg(hf), (int)referenceEffects, params.svp_lens_frame,
					Device.dwFrame);
			}
		}
	}

	// expose engine magnification so the shader has a curMag with no 3DSS config
	extern float g_pip_scope_magnification;
	extern float g_pip_scope_min_mag;
	extern float g_pip_scope_max_mag;
	extern float g_pip_scope_ratio;
	// eyepiece-fit factor, rated on-screen magnification = ratio * scope, a small clip-on window
	// legitimately fits past 8 so the cap bounds the total on-screen mag by the shared limit
	const float ratio_cap = (scope_magnification > EPS) ? _max(1.f, SVP_MAG_LIMIT / scope_magnification) : 1.f;
	const float ratio_use = (ratio_magnification > 0.5f) ? _min(ratio_magnification, ratio_cap) : 1.f;
	if (svp_fov > EPS)
	{
		g_pip_scope_magnification = scope_magnification;
		g_pip_scope_ratio = ratio_use;
		// derive min/max mag from hud_fov_params for variable reticles (fixed scope: x == y)
		const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
		// the 75-base bounds rescale to the aim fov, authored mins are 75-base too,
		// only the legacy optical-model min already rides the aim fov and passes through
		const float fscale = rad2deg(fov_aim) / 75.f;
		const float yscale = (_abs(fovp.y - fovp.x) < 0.01f || params.svp_min_75base) ? fscale : 1.f;
		if (flat_window && !params.svp_authored_mag)
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / window_fov(svp_factor_to_fov(fovp.x, fscale)) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / window_fov(svp_factor_to_fov(fovp.y, yscale)) : scope_magnification;
		}
		else
		{
			g_pip_scope_max_mag = (fovp.x > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp.x, fscale)) : scope_magnification;
			g_pip_scope_min_mag = (fovp.y > EPS) ? fov_aim / deg2rad(svp_factor_to_fov(fovp.y, yscale)) : scope_magnification;
		}
	}

	SSvpEyeSample eye_sample;
	if (scope_svp_enabled >= 2 && params.IsSVPActive())
		eye_sample = svp_update_eye_sample(mm.mView);

	// the fov we render at to get the correct zoom, the eyepiece-fit ratio scales the vFov
	float vFov = 2.0f * atan(tan(fov * 0.5f) / (ratio_use * scope_magnification));
	// flat window renders exactly the panel subtense (tan-correct, the mag division is not)
	if (flat_window)
	{
		if (params.svp_authored_mag)
		{
			// authored mag flat optic renders the floor panel subtense then the optical mag crops in
			const Fvector4& fovp = g_pGamePersistent->m_pGShaderConstants->hud_fov_params;
			const float fscale = rad2deg(fov_aim) / 75.f;
			const float yscale = (_abs(fovp.y - fovp.x) < 0.01f || params.svp_min_75base) ? fscale : 1.f;
			// without an authored min zoom the panel subtense is 0, keep the plain window fov
			vFov = (fovp.y > EPS) ? window_fov(fovp.y * yscale) / scope_magnification : fov / scope_magnification;
		}
		else
			vFov = fov / scope_magnification;
	}

	auto near_plane = fNearPlane;
	auto m_W_svpcam = params.eyepiece.m_W;
	params.svp_camera_domain = CSecondVPParams::camera_main_eye;
	Fvector2 principal_ndc = {};
	Fvector registration_eye_local = {};
	Fvector registration_objective_local = {};
	SvpPhysicalOptics::ObjectiveRegistration objective_registration;
	float near_min_axial = 0.f;
	bool near_manual = false;
	bool near_fresh = false;
	bool entrance_enabled = false;
	int entrance_ray_mode = 0;
	// True PiP falls back to the main eye until a valid objective is available
	if (scope_svp_enabled >= 2)
	{
		m_W_svpcam.c.set(eyeW0.c);
		params.svp_camera_domain = CSecondVPParams::camera_main_eye;
		Fvector ax;
		ax.set(params.eyepiece.m_W.k);
		ax.normalize_safe();
		Fvector objective_delta;
		objective_delta.sub(params.objective.m_W.c, params.eyepiece.m_W.c);
		const float front_use_lens = objective_delta.dotproduct(ax);
		float front_use = front_use_lens;
		// a clip-on ahead of the objective owns the entrance, the camera clears its far face
		if (SvpReferenceCapture::UseInferredClipOn(referenceEffects, ps_r__svp_clipon != 0, params.svp_clipon_axial, front_use) &&
			params.svp_hud_min_frame != u32(-1) && Device.dwFrame >= params.svp_hud_min_frame &&
			Device.dwFrame - params.svp_hud_min_frame <= 8 && params.svp_hud_min_session == params.GetSVPSession() &&
			params.svp_hud_min_epoch == params.svp_optic_epoch)
			front_use = params.svp_clipon_axial + R_VIEWPORT_NEAR;
		params.svp_front_use_m = (_valid(front_use) && front_use > EPS) ? front_use : 0.f;
		if (params.svp_front_use_m > EPS)
		{
			m_W_svpcam.c.set(params.objective.m_W.c);
			// the camera slides up the axis when the entrance sits past the objective lens
			if (front_use > front_use_lens + EPS)
				m_W_svpcam.c.mad(ax, front_use - front_use_lens);
			near_plane = svp_auto_near(params, fNearPlane, near_min_axial, near_manual, near_fresh);
			params.svp_camera_domain = CSecondVPParams::camera_objective;

			// an uncoupled display panel holds its principal point, the image never rides the eye
			if (ps_r__svp_weapon_continuity && !flat_optic && svp_optic_eye_coupled() && params.objective.radius > EPS)
			{
				Fmatrix eyepiece_inverse;
				if (_valid(params.eyepiece.m_W.i) && _valid(params.eyepiece.m_W.j) && _valid(params.eyepiece.m_W.k) &&
					_valid(params.eyepiece.m_W.c) && eyepiece_inverse.invert_b(params.eyepiece.m_W))
				{
					eyepiece_inverse.transform_tiny(registration_eye_local, eyeW0.c);
					eyepiece_inverse.transform_tiny(registration_objective_local, params.objective.m_W.c);
					objective_registration = SvpPhysicalOptics::MapObjectiveAxisToEyepiece(
						{ registration_eye_local.x, registration_eye_local.y, registration_eye_local.z },
						{ 0.f, 0.f, registration_objective_local.z }, { params.eyepiece.radius, params.eyepiece.radius });
					if (objective_registration.valid)
					{
						principal_ndc.set(objective_registration.principal.x, objective_registration.principal.y);
						entrance_enabled = true;
						entrance_ray_mode = 2;
					}
				}
			}
		}
	}

	if (scope_svp_enabled >= 2)
	{
		Fvector raw_len;
		raw_len.set(m_W_svpcam.i.magnitude(), m_W_svpcam.j.magnitude(), m_W_svpcam.k.magnitude());
		Fvector raw_dot;
		raw_dot.set(m_W_svpcam.i.dotproduct(m_W_svpcam.j), m_W_svpcam.i.dotproduct(m_W_svpcam.k), m_W_svpcam.j.dotproduct(m_W_svpcam.k));
		Fvector raw_cross;
		raw_cross.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
		const float raw_hand = raw_cross.dotproduct(m_W_svpcam.k);
		const char* forward_lane;
		const char* basis_lane;
		bool flipped;
		const bool main_fallback =
			svp_rigid_camera_basis(m_W_svpcam, eyeW0, params.eyepiece.m_W.c, params.objective.m_W.c, forward_lane, basis_lane, flipped);
		if (main_fallback)
		{
			near_plane = fNearPlane;
			params.svp_camera_domain = CSecondVPParams::camera_main_eye;
			params.svp_front_use_m = 0.f;
			entrance_enabled = false;
			entrance_ray_mode = 0;
			principal_ndc.set(0.f, 0.f);
			objective_registration = {};
		}

		static u32 s_basis_ms = 0;
		if (ps_r__svp_cop_diag && (main_fallback || Device.dwTimeGlobal - s_basis_ms > 500))
		{
			s_basis_ms = Device.dwTimeGlobal;
			Fvector final_cross;
			final_cross.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
			PipMsg(
				"[SVP-BASIS] rawLen=(%.4f,%.4f,%.4f) rawDot=(%.4f,%.4f,%.4f) rawHand=%.4f finalDot=(%.4f,%.4f,%.4f) finalHand=%.4f forward=%s basis=%s flipped=%d fallback=%d session=%u epoch=%u frame=%u",
				raw_len.x, raw_len.y, raw_len.z, raw_dot.x, raw_dot.y, raw_dot.z, raw_hand, m_W_svpcam.i.dotproduct(m_W_svpcam.j),
				m_W_svpcam.i.dotproduct(m_W_svpcam.k), m_W_svpcam.j.dotproduct(m_W_svpcam.k), final_cross.dotproduct(m_W_svpcam.k),
				forward_lane, basis_lane, flipped ? 1 : 0, main_fallback ? 1 : 0, camera_session, params.svp_camera_epoch, Device.dwFrame);
		}
	}

	// Aligning to view-up is optional. Raw mode keeps the camera, aperture and
	// reticle in the same weapon-relative eyepiece frame.
	if (ps_r__svp_roll_stabilize)
	{
		Fvector fwd, wup, right, up;
		fwd.set(m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
		fwd.normalize();
		wup.set(Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z);
		right.crossproduct(wup, fwd);
		if (right.magnitude() > EPS_S)
		{
			right.normalize();
			up.crossproduct(fwd, right);
			up.normalize();
			m_W_svpcam.i.set(right);
			m_W_svpcam.j.set(up);
			m_W_svpcam.k.set(fwd);
		}
	}

	// pip lens flip diagnostic ([SVP-ORIENT]), the mesh basis vs the final svp camera basis
	{
		if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS)
		{
			static u32 s_orient_ms = 0;
			if (Device.dwTimeGlobal - s_orient_ms > 500)
			{
				s_orient_ms = Device.dwTimeGlobal;
				PipMsg(
					"[SVP-ORIENT] meshUp=(%.2f,%.2f,%.2f) meshFwd=(%.2f,%.2f,%.2f) camUp=(%.2f,%.2f,%.2f) rollStab=%d camUp_out=(%.2f,%.2f,%.2f) camRight_out=(%.2f,%.2f,%.2f) camFwd_out=(%.2f,%.2f,%.2f)",
					params.eyepiece.m_W.j.x, params.eyepiece.m_W.j.y, params.eyepiece.m_W.j.z, params.eyepiece.m_W.k.x,
					params.eyepiece.m_W.k.y, params.eyepiece.m_W.k.z, Device.vCameraTop.x, Device.vCameraTop.y, Device.vCameraTop.z,
					ps_r__svp_roll_stabilize, m_W_svpcam.j.x, m_W_svpcam.j.y, m_W_svpcam.j.z, m_W_svpcam.i.x, m_W_svpcam.i.y,
					m_W_svpcam.i.z, m_W_svpcam.k.x, m_W_svpcam.k.y, m_W_svpcam.k.z);
			}
		}
	}

	// aspect = height/width (engine convention, matches SetActive's fASPECT); square == 1
	// derived from the same policy that sizes the target this frame, not the pre-frame target shape
	extern void svp_target_wh(u32&, u32&);
	u32 tw, th;
	svp_target_wh(tw, th);
	float aspect = (float)th / (float)tw;
	// a non-square SVP keeps the horizontal window subtense at vFov, the vertical follows the aspect
	float vfov_use = (aspect < 0.999f) ? (2.f * atanf(aspect * tanf(vFov * 0.5f))) : vFov;

	auto svp_proj = Fmatrix().build_projection(vfov_use, aspect, near_plane, fFarPlane);
	// Place the objective axis on its physical eyepiece chart point
	if (entrance_ray_mode == 2)
	{
		svp_proj._31 += principal_ndc.x;
		svp_proj._32 += principal_ndc.y;
	}
	// the HUD projection is the scene projection without the DLSS jitter
	const Fmatrix svp_proj_hud = svp_proj;

	if (scope_svp_enabled >= 2)
	{
		static u32 s_frontcam_ms = 0;
		if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_frontcam_ms > 500)
		{
			s_frontcam_ms = Device.dwTimeGlobal;
			Fvector ax;
			ax.set(params.eyepiece.m_W.k);
			ax.normalize_safe();
			Fvector axis_center;
			axis_center.mad(params.eyepiece.m_W.c, ax, params.svp_front_use_m);
			Fvector eye_to_eyepiece;
			eye_to_eyepiece.sub(params.eyepiece.m_W.c, eyeW0.c);
			const float eye_axial = eye_to_eyepiece.dotproduct(ax);
			Fvector eye_axis;
			eye_axis.mad(eyeW0.c, ax, eye_axial);
			PipMsg(
				"[SVP-CAM] domain=%s front=%.1fcm near=%.1fcm nearMode=%s minAxial=%.4fm nearFresh=%d nearBones=%u nearSkip=%u hybridFront=%.4fm eyeCoupling=%d objectiveLateral=%.1fcm eyeOff=%.1fcm raw=(%.1f,%.1f)mm principal=(%.5f,%.5f) entranceScale=%.2f parity=%.2f enabled=%d mag=%.2f opticEpoch=%u cameraEpoch=%u",
				svp_camera_domain_name(params.svp_camera_domain), params.svp_front_use_m * 100.f, near_plane * 100.f,
				near_manual ? "manual" : "auto", near_min_axial, near_fresh ? 1 : 0, params.svp_hud_min_bones, params.svp_hud_axis_skip,
				params.svp_hybrid_front, svp_optic_eye_coupled() ? 1 : 0, params.objective.m_W.c.distance_to(axis_center) * 100.f,
				params.eyepiece.m_W.c.distance_to(eye_axis) * 100.f, eye_sample.raw_mm.x, eye_sample.raw_mm.y, principal_ndc.x, principal_ndc.y, eye_sample.entrance_scale,
				std::clamp(pupil_parity, -1.f, 1.f), entrance_enabled ? 1 : 0, scope_magnification,
				params.svp_optic_epoch, params.svp_camera_epoch);
			const char* ray_mode = entrance_ray_mode == 2 ? "objective-register" : "fixed";
			Fvector handed;
			handed.crossproduct(m_W_svpcam.i, m_W_svpcam.j);
			Fvector base_forward = params.eyepiece.m_W.k;
			base_forward.normalize_safe();
			PipMsg(
				"[SVP-RAY] control=%d mode=%s registerVersion=1 raw=(%.2f,%.2f)mm eyeLocal=(%.5f,%.5f,%.5f) objectiveLocal=(%.5f,%.5f,%.5f) hit=(%.5f,%.5f) principal=(%.5f,%.5f) fraction=%.5f valid=%d inside=%d pupilMag=%.3f renderMag=%.3f fwdDot=%.5f handed=%.5f session=%u epoch=%u frame=%u",
				2, ray_mode, eye_sample.raw_mm.x, eye_sample.raw_mm.y, registration_eye_local.x, registration_eye_local.y,
				registration_eye_local.z, registration_objective_local.x, registration_objective_local.y, registration_objective_local.z,
				objective_registration.hit.x, objective_registration.hit.y, principal_ndc.x, principal_ndc.y,
				objective_registration.fraction, objective_registration.valid ? 1 : 0, objective_registration.inside_aperture ? 1 : 0,
				eye_sample.entrance_scale,
				scope_magnification, base_forward.dotproduct(m_W_svpcam.k),
				handed.dotproduct(m_W_svpcam.k), camera_session, params.svp_camera_epoch, Device.dwFrame);
		}
	}

	// pip DLSS jitter the SVP scene projection (gated), {0,0} otherwise, applied to mProject only
	Device.m_SecondViewport.svp_jitter_px.set(0, 0);
	if (ps_r__svp_dlss != 0)
	{
		const u32 jf = Device.dwFrame; // latch once, stable on the render thread this frame
		Fvector2 jpx = svp_jitter_offset(jf);
		svp_apply_jitter(svp_proj, jpx, (float)tw, (float)th); // the size the target takes this frame
		Device.m_SecondViewport.svp_jitter_px = jpx;
	}

	// the held dbg radius keeps the lines through a culled weapon, it zeroes on unscope
	if (scope_debug >= 2 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		debug_scope(m_W_svpcam, svp_proj);

	Device.matrices[1].mView.invert(m_W_svpcam);
	Device.matrices[1].mProject = svp_proj;
	Device.matrices[1].mProjectHud = svp_proj_hud;

	// pip cache the SVP scene constants for the DLSS eval inputs and the defocus bind (render thread,
	// written then read the same frame). svp_fov is radians from the projection, the basis is the camera world
	if (params.IsSVPActive() && params.GetSVPSession() == camera_session)
	{
		auto& vp = Device.m_SecondViewport;
		Device.matrices[1].mProject.decompose_projection(vp.svp_fov, vp.svp_aspect, vp.svp_near, vp.svp_far);
		vp.svp_cam_pos = m_W_svpcam.c;
		vp.svp_right = m_W_svpcam.i;
		vp.svp_up = m_W_svpcam.j;
		vp.svp_fwd = m_W_svpcam.k;
		vp.svp_camera_session = camera_session;
		vp.svp_camera_frame = Device.dwFrame;
	}

	svp_camera_diagnostics({ ratio_magnification, ratio_cap, scope_magnification, svp_fov, vFov, m_W_svpcam, principal_ndc, eye_sample,
		objective_registration.valid, entrance_ray_mode, camera_session, fNearPlane });

	return svp_camera_history(params, camera_session, entrance_ray_mode, referenceEffects);
}
