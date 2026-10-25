#include "stdafx.h"
#include "../../Include/xrRender/DebugRender.h"
#include "../../xrEngine/svp_crash_context.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#include "svp_camera_debug.h"
#include "svp_report.h"

// pip scope_debug >= 2 world overlay, eyepiece (blue) objective (yellow) camera (white)
// via DRender->add_lines, flushed by the stock debug render
void debug_scope(Fmatrix scope_camera, const Fmatrix& projection)
{
	auto dbg_line = [](const Fvector& a, const Fvector& b, u32 color, bool bHud)
	{
		Fvector v[2] = { a, b };
		u16 idx[2] = { 0, 1 };
		DRender->add_lines(v, 2, idx, 1, color, bHud);
	};

	auto draw_circle = [&](Fmatrix m, u32 color, bool bHud)
	{
		const int n = 100;
		Fvector v0 = { 0, 0, 0 };
		for (int i = 0; i <= n; i++)
		{
			float angle = float(i) / float(n) * PI * 2.0f;
			Fvector v1 = { cosf(angle), sinf(angle), 0.f };
			m.transform(v1);
			if (i > 0)
				dbg_line(v0, v1, color, bHud);
			v0 = v1;
		}
	};

	auto draw_lens = [&](CRenderDevice::CSecondVPParams::Lens lens, u32 color)
	{
		draw_circle(Fmatrix(lens.m_W).mulB_43(Fmatrix().scale(lens.radius, lens.radius, 0.f)), color, true);
		Fvector v0 = { 0, 0, 0 }, v1 = { 0, 0, 100 };
		lens.m_W.transform(v0);
		lens.m_W.transform(v1);
		dbg_line(v0, v1, color, true);
		// up-spoke, a circle + optical axis can't show a ROLL about the axis (rotationally symmetric),
		// so draw the lens up-vector as a radial spoke, a rolled lens shows the spoke pointing off-up
		Fvector u0 = { 0, 0, 0 }, u1 = { 0, lens.radius, 0 };
		lens.m_W.transform(u0);
		lens.m_W.transform(u1);
		dbg_line(u0, u1, color, true);
	};

	auto draw_camera = [&](u32 color)
	{
		const float cm = 1.0f / 100.0f;
		draw_circle(Fmatrix(scope_camera).mulB_43(Fmatrix().scale(.25f * cm, .25f * cm, 0.f)), color, true);
	};

	// pip wireframe cube at a transform (orientation + position), half-extent h, 12 edges
	auto draw_cube = [&](const Fmatrix& m, float h, u32 color)
	{
		Fvector c[8];
		for (int i = 0; i < 8; i++)
		{
			Fvector q;
			q.set((i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h);
			m.transform_tiny(c[i], q);
		}
		static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 },
			{ 2, 6 }, { 3, 7 } };
		for (int k = 0; k < 12; k++)
			dbg_line(c[e[k][0]], c[e[k][1]], color, true);
	};

	auto& p = Device.m_SecondViewport;
	// a culled weapon leaves the live radii 0 this frame, fall back to the held debug radii
	CRenderDevice::CSecondVPParams::Lens eye = p.eyepiece;
	if (eye.radius <= EPS)
		eye.radius = p.dbg_eyepiece_r;
	draw_lens(eye, 0xff0000ff); // eyepiece blue
	// objective yellow at the CAMERA (the real entrance the scope views from), the stored p.objective.m_W
	// is a forward math intermediate (it derives the camera pull-back d), not the visible front lens
	CRenderDevice::CSecondVPParams::Lens objAtCam = p.objective;
	if (objAtCam.radius <= EPS)
		objAtCam.radius = p.dbg_objective_r;
	objAtCam.m_W = scope_camera;
	draw_lens(objAtCam, 0xffffff00); // objective yellow (at the camera/entrance)
	// orange disc at the true derived objective, its gap from the yellow camera disc shows the pull-back
	CRenderDevice::CSecondVPParams::Lens objTrue = p.objective;
	if (objTrue.radius <= EPS)
		objTrue.radius = p.dbg_objective_r;
	draw_circle(Fmatrix(objTrue.m_W).mulB_43(Fmatrix().scale(objTrue.radius, objTrue.radius, 0.f)), 0xffff8000, true);
	draw_camera(0xffffffff); // scope cam white
	// Magenta marks the live SVP camera
	// It shows main eye fallback versus the objective at a glance
	draw_cube(scope_camera, eye.radius * 0.6f, 0xffff00ff);

	// green frustum follows the final projection
	if (_abs(projection._11) > EPS && _abs(projection._22) > EPS)
	{
		const float d = 30.f;
		Fvector corner[4];
		for (int i = 0; i < 4; i++)
		{
			const float sx = (i == 0 || i == 3) ? -1.f : 1.f;
			const float sy = (i < 2) ? 1.f : -1.f;
			const float vx = (sx - projection._31) / projection._11;
			const float vy = (sy - projection._32) / projection._22;
			corner[i].mad(scope_camera.c, scope_camera.k, d);
			corner[i].mad(scope_camera.i, vx * d);
			corner[i].mad(scope_camera.j, vy * d);
			dbg_line(scope_camera.c, corner[i], 0xff00ff00, true);
		}
		for (int i = 0; i < 4; i++)
			dbg_line(corner[i], corner[(i + 1) & 3], 0xff00ff00, true);
	}

	// cyan front plane disc at the objective distance
	{
		float vfov, aspect, near_plane, far_plane;
		Fmatrix projection_copy = projection;
		projection_copy.decompose_projection(vfov, aspect, near_plane, far_plane);
		Fvector ax;
		ax.sub(p.objective.m_W.c, p.eyepiece.m_W.c);
		const float tube = ax.magnitude();
		if (tube > EPS)
		{
			ax.div(tube);
			const float front = (p.svp_front_use_m > EPS) ? p.svp_front_use_m : tube;
			Fmatrix fm;
			fm.identity();
			fm.k.set(ax);
			Fvector seed = (_abs(ax.y) < 0.9f) ? Fvector{ 0, 1, 0 } : Fvector{ 1, 0, 0 };
			fm.i.crossproduct(seed, ax);
			fm.i.normalize();
			fm.j.crossproduct(ax, fm.i);
			fm.c.mad(p.eyepiece.m_W.c, ax, front);
			const float fr = std::max(eye.radius, objAtCam.radius) * 1.75f;
			draw_circle(Fmatrix(fm).mulB_43(Fmatrix().scale(fr, fr, 0.f)), 0xff00ffff, true);
			static u32 s_cam_ms = 0;
			if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_cam_ms > 1000)
			{
				s_cam_ms = Device.dwTimeGlobal;
				Fvector ec;
				ec.sub(scope_camera.c, p.eyepiece.m_W.c);
				extern Fvector4 ps_s3ds_param_3;
				PipMsg(
					"[SVP-CAM] pos=(%.2f,%.2f,%.2f) fwd=(%.2f,%.2f,%.2f) vfov=%.2fdeg cam2eye=%.1fcm tube=%.1fcm frontplane=%.1fcm it=%.0f",
					scope_camera.c.x, scope_camera.c.y, scope_camera.c.z, scope_camera.k.x, scope_camera.k.y, scope_camera.k.z,
					rad2deg(vfov), ec.magnitude() * 100.f, tube * 100.f, front * 100.f, ps_s3ds_param_3.x);
			}
		}
	}
}

// pip [3DB] ballistics overlay core, has_sight draws the captured red sight line and gaps the fire
// axis against it (else the camera axis), draw_lens adds the eyepiece/objective markers + zeroed ray
static void svp_3db_overlay(float fNearPlane, bool has_sight, const Fvector& sight_org, const Fvector& sight_axis, bool draw_lens,
	const Fvector& eyepiece_pos, bool has_objective, const Fvector& objective_pos)
{
	auto& vp = Device.m_SecondViewport;
	CSecondVPParams::WeaponPoseSnapshot pose;
	const bool pose_valid = vp.ReadWeaponPose(pose) && vp.SnapshotExact(pose.frame, pose.session, Device.dwFrame);
	// display distance, the ranged zero re-picks every tick and would teleport every
	// endpoint while panning, settle it for drawing, the log keeps the live number
	const float D_live = (pose.fire_ray_zero > 0.f) ? pose.fire_ray_zero : 100.f;
	static float s_disp_D = 0.f;
	static u32 s_disp_frame = 0;
	if (Device.dwFrame != s_disp_frame + 1 || s_disp_D <= 0.f)
		s_disp_D = D_live;
	else
		s_disp_D += (D_live - s_disp_D) * (1.f - expf(-Device.fTimeDelta / 0.25f));
	s_disp_frame = Device.dwFrame;
	const float D = s_disp_D;
	Fmatrix eyeW2;
	eyeW2.invert(Device.matrices[0].mView);
	// clip to the near plane, a behind-plane endpoint rasterizes as a screen streak
	const float znear = fNearPlane + 0.01f;
	auto line = [&](const Fvector& a, const Fvector& b, u32 color)
	{
		Fvector ca;
		ca.sub(a, eyeW2.c);
		Fvector cb;
		cb.sub(b, eyeW2.c);
		const float za = ca.dotproduct(eyeW2.k), zb = cb.dotproduct(eyeW2.k);
		if (za < znear && zb < znear)
			return;
		Fvector aa = a, bb = b;
		if (za < znear)
			aa.lerp(a, b, (znear - za) / (zb - za));
		else if (zb < znear)
			bb.lerp(a, b, (znear - za) / (zb - za));
		Fvector v[2] = { aa, bb };
		u16 i[2] = { 0, 1 };
		DRender->add_lines(v, 2, i, 1, color, true);
	};
	auto cross = [&](const Fvector& p, float s, u32 color)
	{
		Fvector a, b;
		a.set(p);
		a.mad(eyeW2.i, -s);
		b.set(p);
		b.mad(eyeW2.i, s);
		line(a, b, color);
		a.set(p);
		a.mad(eyeW2.j, -s);
		b.set(p);
		b.mad(eyeW2.j, s);
		line(a, b, color);
	};

	// yellow muzzle marker, red eyepiece and cyan objective only with a captured lens
	if (pose_valid)
		cross(pose.muzzle_pos, 0.01f, 0xffffff00);
	if (draw_lens)
	{
		cross(eyepiece_pos, 0.01f, 0xffff0000);
		if (has_objective)
			cross(objective_pos, 0.01f, 0xff00ffff);
	}

	// the sight line to the zero distance, drawn from the stable published copy ballistics converge on
	if (has_sight)
	{
		Fvector sight;
		sight.mad(sight_org, sight_axis, D);
		line(sight_org, sight, 0xffff0000);
		cross(sight, D * 0.02f, 0xffff0000);
	}

	// the camera crosshair ray from the mirrored actor eye, the ballistic truth when aimpos
	// is off, stays the shooter's eye while demo_record flies the device camera
	Fvector cpos, cfwd;
	if (pose.eye_ray_dir.square_magnitude() > EPS)
	{
		cpos.set(pose.eye_ray_pos);
		cfwd.set(pose.eye_ray_dir);
	}
	else
	{
		cpos.set(eyeW2.c);
		cfwd.set(eyeW2.k);
	}
	cfwd.normalize_safe();
	Fvector chp;
	chp.mad(cpos, cfwd, D);
	line(cpos, chp, 0xff4080ff);
	cross(chp, D * 0.02f, 0xff4080ff);

	// the raw fire axis, where bullets go with g_svp_zero 0
	if (pose_valid)
	{
		Fvector faxis;
		faxis.set(pose.fire_ray_dir);
		faxis.normalize_safe();
		Fvector fire;
		fire.mad(pose.fire_ray_pos, faxis, D);
		line(pose.fire_ray_pos, fire, 0xff00ff00);
		cross(fire, D * 0.02f, 0xff00ff00);
		// the gap reads the fire axis against the sight line when captured, else the aim axis
		float c = has_sight ? sight_axis.dotproduct(faxis) : cfwd.dotproduct(faxis);
		clamp(c, -1.f, 1.f);
		static u32 s_aim_ms = 0;
		if (Device.dwTimeGlobal - s_aim_ms > 1000)
		{
			s_aim_ms = Device.dwTimeGlobal;
			if (has_sight)
				PipMsg("[3DB] axis gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, pose.fire_ray_zero);
			else
				PipMsg("[3DB] fire-aim gap %.2f mrad at %.0fm (zero %.0f)", acosf(c) * 1000.f, D, pose.fire_ray_zero);
		}

		// the zeroed departure ray the shot actually flies, only with a captured sight line
		if (has_sight && ps_r__3db_debug >= 2 && pose.fire_ray_zero > 0.f)
		{
			Fvector zp;
			zp.mad(sight_org, sight_axis, D);
			line(pose.fire_ray_pos, zp, 0xffffffff);
		}
	}

	// fading shot tracers, brightness decays over 5s
	if (ps_r__3db_debug >= 3)
	{
		CSecondVPParams::FireTrace traces[16];
		vp.ReadFireTraces(traces);
		for (const auto& tr : traces)
		{
			const u32 age = Device.dwTimeGlobal - tr.time_ms;
			if (!tr.time_ms || age >= 5000)
				continue;
			const u32 k = 255u - age * 255u / 5000u;
			const u32 color = 0xff000000 | (k << 16) | (k << 8);
			Fvector end;
			end.mad(tr.pos, tr.dir, D * 2.f);
			line(tr.pos, end, color);
		}
	}
}

// pip [3DB] overlay for sights without a captured pip lens, reflex and irons keep 3d
// ballistics so the sight independent markers draw here, the pip overlay owns the rest
void ballistics_debug_overlay()
{
	float _, fov, fNearPlane, fFarPlane;
	Device.matrices[0].mProject.decompose_projection(fov, _, fNearPlane, fFarPlane);
	const Fvector zero = { 0.f, 0.f, 0.f };
	svp_3db_overlay(fNearPlane, false, zero, zero, false, zero, false, zero);
}

void svp_camera_diagnostics(const SSvpCameraDiagnostics& frame)
{
	extern float g_pip_scope_min_mag, g_pip_scope_max_mag;
	const auto& params = Device.m_SecondViewport;
	const auto& ratio_magnification = frame.ratio_magnification;
	const auto& ratio_cap = frame.ratio_cap;
	const auto& scope_magnification = frame.scope_magnification;
	const auto& svp_fov = frame.svp_fov;
	const auto& vFov = frame.vFov;
	const auto& m_W_svpcam = frame.m_W_svpcam;
	const auto& principal_ndc = frame.principal_ndc;
	const auto& eye_sample = frame.eye_sample;
	const auto& registration_valid = frame.registration_valid;
	const auto& entrance_ray_mode = frame.entrance_ray_mode;
	const auto& camera_session = frame.camera_session;
	const auto& fNearPlane = frame.fNearPlane;
	// pip snapshot the svp runtime state into the crash-context ring for tester crash logs
	{
		auto& vp = Device.m_SecondViewport;
		SvpCrashFrame cf;
		cf.frame = Device.dwFrame;
		cf.mode = scope_svp_enabled;
		cf.active = vp.IsSVPActive();
		cf.render_pass_is_svp = vp.m_render_pass_is_svp;
		cf.hud_front_m = vp.svp_front_use_m;
		cf.mag = vp.svp_mag;
		cf.fov_deg = rad2deg(vp.svp_fov);
		cf.disc_px = vp.svp_disc_px;
		svp_crash_context_push(cf);
	}

	// pip optics diagnostic: throttled [SVPCOP] log of the camera center-of-projection offset from the
	// eye, settled frames only (ADS transitions blow up ratio_magnification)
	if (ps_r__svp_cop_diag && params.eyepiece.radius > EPS && ratio_magnification > 1.0f && ratio_magnification < ratio_cap)
	{
		static u32 s_last_ms = 0;
		static float s_last_mag = 0.f;
		const float eff_mag = ratio_magnification * scope_magnification;
		const bool mag_moved = (s_last_mag < EPS) || (fabsf(eff_mag - s_last_mag) > 0.03f * s_last_mag);
		if (Device.dwTimeGlobal - s_last_ms > 400 || mag_moved)
		{
			s_last_ms = Device.dwTimeGlobal;
			s_last_mag = eff_mag;
			Fmatrix eyeW;
			eyeW.invert(Device.matrices[0].mView);
			Fvector camdir;
			camdir.set(eyeW.k);
			camdir.normalize();
			Fvector d;
			d.sub(m_W_svpcam.c, eyeW.c);
			const float fwd = d.dotproduct(camdir);
			Fvector fwd_v;
			fwd_v.set(camdir);
			fwd_v.mul(fwd);
			Fvector lat_v;
			lat_v.sub(d, fwd_v);
			Fvector eyefwd;
			eyefwd.set(params.eyepiece.m_W.k);
			eyefwd.normalize();
			Fvector od;
			od.sub(params.objective.m_W.c, params.eyepiece.m_W.c);
			// signed scope cant vs world up, proves whether lean actually rolls the weapon on a rig
			float cant = 0.f;
			{
				Fvector jup;
				jup.set(params.eyepiece.m_W.j);
				jup.normalize();
				Fvector lvl;
				lvl.set(0.f, 1.f, 0.f);
				lvl.mad(eyefwd, -lvl.dotproduct(eyefwd));
				if (lvl.magnitude() > EPS)
				{
					lvl.normalize();
					Fvector cx;
					cx.crossproduct(lvl, jup);
					cant = rad2deg(atan2f(cx.dotproduct(eyefwd), lvl.dotproduct(jup)));
				}
			}
			PipMsg(
				"[SVPCOP] mode=%d mag=%.3f eff=%.3f min=%.3f max=%.3f ratio=%.3f svpfov=%.2f vfov=%.2f cop_cm=%.2f fwd_cm=%.2f lat_cm=%.2f eye_r_cm=%.2f obj_fwd_cm=%.2f obj_r_cm=%.2f cant=%.1f",
				scope_svp_enabled, scope_magnification, eff_mag, g_pip_scope_min_mag, g_pip_scope_max_mag, ratio_magnification, svp_fov,
				rad2deg(vFov), d.magnitude() * 100.f, fwd * 100.f, lat_v.magnitude() * 100.f, params.eyepiece.radius * 100.f,
				od.dotproduct(eyefwd) * 100.f, params.objective.radius * 100.f, cant);
		}
	}

	// pip [SVP-AIM] compares objective registration against the main HUD projection
	if (ps_r__svp_cop_diag >= 2 && params.eyepiece.radius > EPS)
	{
		Fmatrix vpm;
		vpm.mul(Device.matrices[0].mProjectHud, Device.matrices[0].mView);
		auto project_screen = [&](const Fvector& point, Fvector2& screen)
		{
			Fvector4 clip;
			vpm.transform(clip, { point.x, point.y, point.z, 1.f });
			if (clip.w <= EPS || !_valid(clip.x) || !_valid(clip.y) || !_valid(clip.z) || !_valid(clip.w))
				return false;
			screen.x = (clip.x / clip.w * 0.5f + 0.5f) * float(Device.dwWidth);
			screen.y = (0.5f - clip.y / clip.w * 0.5f) * float(Device.dwHeight);
			return _valid(screen.x) && _valid(screen.y);
		};
		Fvector mapped_local;
		mapped_local.set(principal_ndc.x * params.eyepiece.radius, principal_ndc.y * params.eyepiece.radius, 0.f);
		Fvector mapped_world;
		params.eyepiece.m_W.transform_tiny(mapped_world, mapped_local);
		Fvector camera_forward = m_W_svpcam.k;
		camera_forward.normalize_safe();
		Fvector forward_world;
		forward_world.mad(m_W_svpcam.c, camera_forward, 1.f);
		Fmatrix svp_vpm;
		svp_vpm.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		Fvector4 forward_clip;
		svp_vpm.transform(forward_clip, { forward_world.x, forward_world.y, forward_world.z, 1.f });
		Fvector2 forward_ndc = {};
		const bool forward_valid =
			forward_clip.w > EPS && _valid(forward_clip.x) && _valid(forward_clip.y) && _valid(forward_clip.z) && _valid(forward_clip.w);
		if (forward_valid)
		{
			forward_ndc.x = forward_clip.x / forward_clip.w;
			forward_ndc.y = forward_clip.y / forward_clip.w;
		}
		Fvector rendered_local;
		rendered_local.set(forward_ndc.x * params.eyepiece.radius, forward_ndc.y * params.eyepiece.radius, 0.f);
		Fvector rendered_world;
		params.eyepiece.m_W.transform_tiny(rendered_world, rendered_local);
		Fvector2 lens_screen;
		Fvector2 objective_screen;
		Fvector2 mapped_screen;
		Fvector2 rendered_screen;
		if (project_screen(params.eyepiece.m_W.c, lens_screen) && project_screen(params.objective.m_W.c, objective_screen) &&
			project_screen(mapped_world, mapped_screen) && forward_valid && project_screen(rendered_world, rendered_screen))
		{
			const Fvector2 target = { objective_screen.x - lens_screen.x, objective_screen.y - lens_screen.y };
			const Fvector2 applied = { mapped_screen.x - lens_screen.x, mapped_screen.y - lens_screen.y };
			const Fvector2 residual = { mapped_screen.x - objective_screen.x, mapped_screen.y - objective_screen.y };
			const Fvector2 rendered_residual = { rendered_screen.x - objective_screen.x, rendered_screen.y - objective_screen.y };
			PipMsg(
				"[SVP-AIM] lens=(%.1f,%.1f) objective=(%.1f,%.1f) target=(%.1f,%.1f)px mapped=(%.1f,%.1f) applied=(%.1f,%.1f)px geometricResidual=(%.2f,%.2f)px forwardNdc=(%.5f,%.5f) rendered=(%.1f,%.1f) renderResidual=(%.2f,%.2f)px jitter=(%.3f,%.3f)px raw=(%.2f,%.2f)mm principal=(%.5f,%.5f) valid=%d mode=%d session=%u opticEpoch=%u cameraEpoch=%u frame=%u",
				lens_screen.x, lens_screen.y, objective_screen.x, objective_screen.y, target.x, target.y, mapped_screen.x, mapped_screen.y,
				applied.x, applied.y, residual.x, residual.y, forward_ndc.x, forward_ndc.y, rendered_screen.x, rendered_screen.y,
				rendered_residual.x, rendered_residual.y, Device.m_SecondViewport.svp_jitter_px.x, Device.m_SecondViewport.svp_jitter_px.y,
				eye_sample.raw_mm.x, eye_sample.raw_mm.y, principal_ndc.x, principal_ndc.y, registration_valid ? 1 : 0, entrance_ray_mode,
				camera_session, params.svp_optic_epoch, params.svp_camera_epoch, Device.dwFrame);
		}
	}

	// pip [3DB] overlay, 1 = markers + fire (green) camera (blue) sight (red) rays,
	// 2 = zeroed departure ray (white), 3 = fading shot tracers
	{
		if (ps_r__3db_debug > 0 && (params.eyepiece.radius > EPS || params.dbg_eyepiece_r > EPS))
		{
			auto& vp = Device.m_SecondViewport;
			// the sight line from the stable published copy, else the eyepiece axis
			Fvector s_org, saxis;
			CSecondVPParams::SightSnapshot sight;
			const bool sight_ok = vp.ReadSight(sight) && vp.SnapshotExact(sight.frame, sight.session, Device.dwFrame) &&
				sight.optic_epoch == vp.svp_optic_epoch;
			if (sight_ok)
			{
				s_org.set(sight.position);
				saxis.set(sight.direction);
			}
			else
			{
				s_org.set(params.eyepiece.m_W.c);
				saxis.set(params.eyepiece.m_W.k);
			}
			saxis.normalize_safe();
			const bool has_obj = (params.objective.radius > EPS || params.dbg_objective_r > EPS);
			svp_3db_overlay(fNearPlane, true, s_org, saxis, true, params.eyepiece.m_W.c, has_obj, params.objective.m_W.c);
		}
	}

	svp_report_configuration();

	// pip [SVP-LEDGER] once-per-session inert-path sweep, after enough scoped time to exercise the
	// gated paths, flags any instrumented counter still zero while its gate cvar is on
	{
		extern void svp_ledger_sweep();
		const float SVP_LEDGER_SETTLE_S = 60.f; // diagnostic patience window in scoped seconds, not a rendering threshold
		static float s_ledger_scoped_s = 0.f;
		static bool s_ledger_swept = false;
		if (!s_ledger_swept)
		{
			s_ledger_scoped_s += Device.fTimeDelta;
			if (s_ledger_scoped_s >= SVP_LEDGER_SETTLE_S)
			{
				s_ledger_swept = true;
				svp_ledger_sweep();
			}
		}
	}
}
