#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/SkeletonCustom.h"
#include "../xrRender/SkeletonX.h"
#include "../xrRender/svp_objective_offset.h"
#include "svp_camera.h"
#include "svp_camera_tracking.h"
#include "svp_optics.h"

// objective distance in eyepiece radii when no front lens is known, the authored-set front distance
// (mod_system_3dss_objective_lenses median 12.7 mean 14.0)
static constexpr float svp_fallback_objective_radii = 14.f;

// pip front/second focal-plane world points (scope_w_ffp/sfp); the scope shader projects the SVP image through them
static void svp_update_focal_planes()
{
	auto e = Device.m_SecondViewport.eyepiece;
	auto o = Device.m_SecondViewport.objective;

	Fvector p_e = { 0, 0, 0 };
	e.m_W.transform(p_e);
	Fvector p_o = { 0, 0, 0 };
	o.m_W.transform(p_o);

	if (o.radius < EPS)
	{
		// no objective captured, place one at the authored-set front distance in eyepiece radii
		float distance = e.radius * svp_fallback_objective_radii;
		p_o = { 0, 0, distance };
		e.m_W.transform(p_o);
	}

	{
		// reproject the objective directly in front of the eyepiece (not all scopes are inline)
		float distance = p_o.distance_to(p_e);
		Fvector dir = { 0, 0, 1 };
		o.m_W.transform_dir(dir);
		p_o.set(dir.mul(distance).add(p_e));
	}

	Fvector p_d = Fvector(p_o).sub(p_e);

	// each focal anchor sits one eyepiece focal length inside its end of the tube (f_e ~ the
	// authored eye relief, symmetric focal model), the old 0.2/0.8 split stays as the cvar fallback
	float t1 = 0.2f, t2 = 0.8f;
	{
		const float L = p_d.magnitude();
		const float fe = svp_eye_relief_mm() * 0.001f;
		if (ps_r__svp_focal_derive && L > EPS && fe > 0.01f)
		{
			t1 = clampr(fe / L, 0.05f, 0.45f);
			t2 = clampr(1.f - fe / L, 0.55f, 0.95f);
		}
	}
	Device.m_SecondViewport.w_ffp = Fvector(p_d).mul(t1).add(p_e);
	Device.m_SecondViewport.w_sfp = Fvector(p_d).mul(t2).add(p_e);
}

// pip derive the eyepiece/objective from the captured scope-lens meshes, consumed by
// svpCamera and the activation gate (GetSVPCameraMatrix)
static xr_vector<Fvector4>
	g_pip_hud_geom; // pip diag: snapshot of HUD geometry centers (xyz) + radius (w), captured before render_hud clears the lists

// pip snapshot HUD geometry centers before render_hud clears the lists, deriveScopeLens
// geomscans them after the clear, the call site keeps its gate inline
void svp_snapshot_hud_geom()
{
	g_pip_hud_geom.clear();
	auto snap = [](auto& lst)
	{
		for (auto& H : lst)
		{
			if (!H.pVisual || !H.pMatrix)
				continue;
			auto& VV = H.pVisual->getVisData();
			Fvector w;
			RImplementation.GMBase.svp_pose_of(H.pMatrix)->transform_tiny(w, VV.sphere.P);
			Fvector4 e;
			e.set(w.x, w.y, w.z, VV.sphere.R);
			g_pip_hud_geom.push_back(e);
		}
	};
	snap(RImplementation.GMBase.RGraph.mapHUDSorted.Sorted);
	snap(RImplementation.GMBase.RGraph.mapHUD);
}

// pip the optics bus, resolve the per-optic inputs once from the fresh eyepiece so one precedence
// and one eps gate feed every consumer, authored_optics folds into the offset, mm from spec then w
static void svp_optics_resolve(CSecondVPParams* p, float er)
{
	const auto& config = p->RenderOpticConfig();
	Fvector4 off;
	if (ps_r__svp_authored_optics)
	{
		if (config.typed_route)
			off = config.has_objective_offset ? config.objective_offset : Fvector4{};
		else
			off = scope_objective_lens_offset;
	}
	else
		off.set(0.f, 0.f, 0.f, 0.f);
	p->svp_opt_offset = off;
	float mm = 0.f;
	const float authored_mm = config.typed_route ? (config.has_objective_mm ? config.objective_mm : 0.f) : ps_s3ds_objective_mm;
	if (authored_mm > EPS)
		mm = authored_mm;
	else if (off.w > EPS)
		mm = 2000.f * off.w * er; // authored w radius in eyepiece radii to mm
	p->svp_opt_obj_mm = mm;
	if (ps_r__svp_stats)
		++svp_stats_optic_resolve;
	if (ps_r__svp_cop_diag)
	{
		static u32 s_opt_ms = 0;
		if (Device.dwTimeGlobal - s_opt_ms > 1000)
		{
			s_opt_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-OPTICS] off %.2f,%.2f,%.2f,%.2f mm %.1f src %s", off.x, off.y, off.z, off.w, mm,
				(authored_mm > EPS) ? (config.typed_route ? (config.source[CSecondVPParams::optic_objective_mm][0]
																	? config.source[CSecondVPParams::optic_objective_mm]
																	: "typed_default")
														  : "legacy")
									: ((off.w > EPS) ? "w" : "none"));
		}
	}
}

// A detected objective offset measured around a different eyepiece is not used;
// the objective then comes from the objective mesh or the geometric fallback.
// Both centres are bind-pose model space, so no runtime pose is involved.
static bool svp_detected_offset_misplaced(const CSecondVPParams& p, CSkeletonX* sk, const Fvector& eye_bind, const void* visual)
{
	const auto& config = p.RenderOpticConfig();
	if (!sk ||
		!svp_offset_is_detected(config.typed_route, config.has_objective_offset, config.source[CSecondVPParams::optic_objective_offset]))
		return false;
	SLensDetection d;
	if (!sk->SVP_GetLensDetection(d) || !d.ok)
		return false;
	const float distance = d.eye_center.distance_to(eye_bind);
	if (!svp_detected_eyepiece_misplaced(distance, p.eyepiece.radius))
		return false;
	static const void* s_reported = nullptr;
	if (visual != s_reported)
	{
		s_reported = visual;
		PipMsg("[SVP-EYEDIV] detected objective offset not used: measured %.2f eyepiece radii away (%.2f cm)", distance / p.eyepiece.radius,
			distance * 100.f);
	}
	return true;
}

void CRender::deriveScopeLens()
{
	auto& viewport = Device.m_SecondViewport;
	viewport.svp_lens_root = nullptr;
	viewport.svp_lens_visual = nullptr;
	viewport.svp_lens_owner = nullptr;
	viewport.svp_lens_frame = Device.dwFrame;
	if (ps_r__svp_diag)
	{
		const auto& config = viewport.RenderOpticConfig();
		static u32 s_latch_generation = u32(-1);
		static u32 s_latch_token = u32(-1);
		if (config.generation != s_latch_generation || config.context_token != s_latch_token)
		{
			s_latch_generation = config.generation;
			s_latch_token = config.context_token;
			PipMsg("[SVP-CONFIG] latch frame=%u session=%u typed=%d token=%u gen=%u weapon_id=%u context=%s valid=%d", Device.dwFrame,
				viewport.GetSVPSession(), config.typed_route, config.context_token, config.generation, config.weapon_id, config.context,
				config.valid);
		}
	}

	// pip clear the flat-panel publish, re-set below when a flat window lens is the eyepiece
	viewport.svp_panel_flat = false;

	// multi-lens weapons carry several scope-lens meshes, pick the visible lens nearest the camera
	const void* best = nullptr;
	{
		static u32 s_lens_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_lens_diag_ms > 3000);
		if (diag)
			s_lens_diag_ms = Device.dwTimeGlobal;
		float best_score = 1e9f;
		const Fvector cam_p = Device.vCameraPosition;
		const Fvector cam_f = Device.vCameraDirection;
		for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
		{
			if (!N.pVisual || !N.pMatrix)
				continue;
			Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
			bool bone_vis = true;
			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			if (sk)
			{
				bone_vis = sk->SVP_LensBoneVisible();
				Fmatrix boneR;
				if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
					lensX.mulB_43(boneR);
			}
			auto& V = N.pVisual->getVisData();
			Fvector c;
			V.box.getcenter(c);
			Fvector cw;
			lensX.transform_tiny(cw, c);
			Fvector d;
			d.sub(cw, cam_p);
			const float dist = d.magnitude();
			if (!_valid(dist) || dist < 0.01f)
				continue;
			d.div(dist);
			const float fwd = d.dotproduct(cam_f);
			if (diag)
			{
				auto tx = N.pVisual->GetTexture();
				Fvector bd;
				V.box.getsize(bd);
				PipMsg("[SVP-LENS] %s dist=%.1fcm fwd=%.3f r=%.1fcm box=%.1fx%.1fx%.1fcm %s", bone_vis ? "vis " : "HIDE", dist * 100.f, fwd,
					V.sphere.R * 100.f, bd.x * 100.f, bd.y * 100.f, bd.z * 100.f, tx ? tx->cName.c_str() : "?");
			}
			if (!bone_vis)
				continue;
			if (dist < best_score)
			{
				best_score = dist;
				best = &N;
			}
		}
	}

	// pip diag, an aimed frame that derives no lens (hip frames also derive, gate on the raise)
	if (!best)
	{
		if (ps_r__svp_diag && g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_nb_ms = 0;
			if (Device.dwTimeGlobal - s_nb_ms > 500)
			{
				s_nb_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-LENS] no aimed lens picked, nodes=%u stale_r=%.1fcm", (u32)GMBase.RGraph.mapScopeHUDSorted.size(),
					Device.m_SecondViewport.dbg_eyepiece_r * 100.f);
			}
		}
	}

	for (auto& N : GMBase.RGraph.mapScopeHUDSorted)
	{
		if (&N != best)
			continue;

		// a skinned scope lens is positioned by its bone, the captured matrix is only the kinematics
		// root, fold in the lens bone skinning matrix so the eyepiece follows the glass on ADS and sway
		Fmatrix lensX = *GMBase.svp_pose_of(N.pMatrix);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		bool lens_bone = false;
		if (sk)
		{
			Fmatrix boneR;
			if (GMBase.svp_lens_bone_of(N.pVisual, boneR) || sk->SVP_LensBoneXform(boneR))
			{
				lensX.mulB_43(boneR);
				lens_bone = true;
			}
		}

		auto& V = N.pVisual->getVisData();
		Fvector c;
		V.box.getcenter(c); // AABB center fits a flat lens disc tighter than the bounding sphere center
		Fmatrix m_W = lensX;
		m_W.mulB_43(Fmatrix().translate(c));
		float radius = V.sphere.R;

		auto* p = &Device.m_SecondViewport;

		p->eyepiece.m_W = m_W;
		p->eyepiece.radius = radius;
		p->svp_lens_root = N.pMatrix;
		p->svp_lens_visual = N.pVisual;
		p->svp_lens_owner = sk ? sk->SVP_SkeletonOwner() : nullptr;

		// resolve the optics bus from the fresh eyepiece, the consumers below and downstream read
		// the record instead of the raw cvars
		svp_optics_resolve(p, radius);
		svp_epoch_update(N.pVisual, radius, *p);

		{
			static const void* s_root = nullptr;
			static const void* s_visual = nullptr;
			static u32 s_session = 0;
			static u32 s_epoch = 0;
			static u32 s_log_ms = 0;
			static Fvector s_relative_c = {};
			static Fvector s_relative_i = {};
			static Fvector s_relative_j = {};
			static Fvector s_relative_k = {};
			static bool s_valid = false;
			if (ps_r__svp_cop_diag >= 2)
			{
				Fmatrix root_inverse;
				root_inverse.invert(*GMBase.svp_pose_of(N.pMatrix));
				Fmatrix relative;
				relative.mul_43(root_inverse, m_W);
				Fvector relative_i = relative.i;
				Fvector relative_j = relative.j;
				Fvector relative_k = relative.k;
				relative_i.normalize_safe();
				relative_j.normalize_safe();
				relative_k.normalize_safe();
				const u32 session = p->GetSVPSession();
				const bool same =
					s_valid && s_root == N.pMatrix && s_visual == N.pVisual && s_session == session && s_epoch == p->svp_camera_epoch;
				const bool linked = same && Device.dwTimeGlobal - s_log_ms <= 500;
				if (!linked || Device.dwTimeGlobal - s_log_ms > 250)
				{
					Fvector relative_delta = {};
					float angle_delta = 0.f;
					if (linked)
					{
						relative_delta.sub(relative.c, s_relative_c);
						const float trace =
							relative_i.dotproduct(s_relative_i) + relative_j.dotproduct(s_relative_j) + relative_k.dotproduct(s_relative_k);
						angle_delta = rad2deg(acosf(std::clamp((trace - 1.f) * 0.5f, -1.f, 1.f)));
					}
					PipMsg(
						"[SVP-LENSREL] seed=%d dt_ms=%u relC=(%.5f,%.5f,%.5f) dRelC=(%.5f,%.5f,%.5f) relK=(%.5f,%.5f,%.5f) dAngleDeg=%.5f bone=%d root=%p visual=%p owner=%p session=%u epoch=%u frame=%u",
						linked ? 0 : 1, linked ? Device.dwTimeGlobal - s_log_ms : 0, relative.c.x, relative.c.y, relative.c.z,
						relative_delta.x, relative_delta.y, relative_delta.z, relative_k.x, relative_k.y, relative_k.z, angle_delta,
						lens_bone ? 1 : 0, N.pMatrix, N.pVisual, p->svp_lens_owner, session, p->svp_camera_epoch, Device.dwFrame);
					s_root = N.pMatrix;
					s_visual = N.pVisual;
					s_session = session;
					s_epoch = p->svp_camera_epoch;
					s_log_ms = Device.dwTimeGlobal;
					s_relative_c = relative.c;
					s_relative_i = relative_i;
					s_relative_j = relative_j;
					s_relative_k = relative_k;
					s_valid = true;
				}
			}
		}

		// ballistics sight line publishes as one record
		if (radius > EPS)
		{
			CSecondVPParams::SightSnapshot sight;
			sight.position.set(m_W.c);
			Fvector sk;
			sk.set(m_W.k);
			sk.normalize_safe();
			sight.direction.set(sk);
			Fmatrix root_inverse;
			root_inverse.invert(*GMBase.svp_pose_of(N.pMatrix));
			root_inverse.transform_tiny(sight.root_local_position, sight.position);
			root_inverse.transform_dir(sight.root_local_direction, sight.direction);
			sight.root_local_direction.normalize_safe();
			sight.root_token = reinterpret_cast<u64>(N.pMatrix);
			sight.root_role = N.hud_role;
			CSecondVPParams::WeaponPoseSnapshot pose;
			if (p->ReadWeaponPose(pose) && p->SnapshotExact(pose.frame, pose.session, Device.dwFrame))
				sight.weapon_id = pose.weapon_id;
			sight.root_local_valid = _valid(sight.root_local_position) && _valid(sight.root_local_direction) &&
				sight.root_local_direction.square_magnitude() > EPS;
			sight.lens_radius = radius;
			sight.frame = Device.dwFrame;
			sight.session = p->GetSVPSession();
			sight.optic_epoch = p->svp_optic_epoch;
			const auto& config = p->RenderOpticConfig();
			sight.optic_typed = config.typed_route;
			sight.optic_config_valid = config.valid;
			sight.optic_context_token = config.context_token;
			sight.optic_config_generation = config.generation;
			sight.optic_route_epoch = config.route_epoch;
			p->PublishSight(sight);
		}

		// panel aspect from the lens AABB, the two largest extents are the plane W:H (the ~0 axis is
		// the depth), ~1 for a round lens so a conventional scope keeps its square SVP
		Fvector bsz;
		V.box.getsize(bsz);
		float ea[3] = { _abs(bsz.x), _abs(bsz.y), _abs(bsz.z) };
		std::sort(ea, ea + 3, std::greater<float>());
		p->svp_panel_aspect = (ea[1] > EPS) ? (ea[0] / ea[1]) : 1.f;

		// pip flat-panel on-screen quad for the binocular brackets, plane half-extent world vectors
		// (mesh local x = width, y = height, z = normal)
		{
			extern Fvector4 ps_s3ds_param_3;
			if (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8)
			{
				Fvector we, he;
				m_W.transform_tiny(we, Fvector().set(bsz.x * 0.5f, 0.f, 0.f));
				m_W.transform_tiny(he, Fvector().set(0.f, bsz.y * 0.5f, 0.f));
				p->svp_panel_ax_w.sub(we, m_W.c);
				p->svp_panel_ax_h.sub(he, m_W.c);
				p->svp_panel_flat = true;
			}
		}

		if (p->eyepiece.radius > EPS)
		{
			// pip objective, prefer the real captured front lens on the optical axis, the
			// geomscan finds the forward-most on-axis node as the fallback plane
			float geom_front = -1.f;
			{
				const Fvector eye = p->eyepiece.m_W.c;
				Fvector axis;
				axis.set(p->eyepiece.m_W.k);
				axis.normalize();
				const float rr = p->eyepiece.radius;
				float fr = -1e9f;
				for (auto& g : g_pip_hud_geom)
				{
					Fvector wc;
					wc.set(g.x, g.y, g.z);
					Fvector d;
					d.sub(wc, eye);
					const float fwd = d.dotproduct(axis);
					if (fwd <= 0.f)
						continue;
					Fvector proj;
					proj.mad(eye, axis, fwd);
					const float perp = wc.distance_to(proj);
					if (perp < rr * 2.0f && (fwd + g.w) > fr)
						fr = fwd + g.w;
				}
				if (fr > 0.f)
				{
					geom_front = fr / rr;
					if (geom_front < 4.f)
						geom_front = 4.f;
					else if (geom_front > 30.f)
						geom_front = 30.f;
				}
			}

			bool have_obj = false;
			float dbg_cand_dist = -1.f; // objective-capture distance, used by the capture gate below
			// authored objective wins, place the front lens at the resolved z along the optical axis
			// with the lateral x/y offset and w radius, all in eyepiece radii
			const Fvector4& off = p->svp_opt_offset;
			if (off.z > EPS && !svp_detected_offset_misplaced(*p, sk, c, N.pVisual))
			{
				const float er = p->eyepiece.radius;
				Fvector fwd;
				fwd.set(p->eyepiece.m_W.k);
				fwd.normalize();
				Fvector ri;
				ri.set(p->eyepiece.m_W.i);
				ri.normalize();
				Fvector up;
				up.set(p->eyepiece.m_W.j);
				up.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				p->objective.m_W.c.mad(fwd, off.z * er);
				p->objective.m_W.c.mad(ri, off.x * er);
				p->objective.m_W.c.mad(up, off.y * er);
				p->objective.radius = (off.w > EPS) ? off.w * er : p->eyepiece.radius * ps_r__svp_obj_size;
				have_obj = true;
			}
			// authored objective already placed, else the first captured objective mesh is the next fallback
			const auto& objectives = GMBase.RGraph.mapScopeHUDObjective;
			if (!have_obj && !objectives.empty() && objectives.front().pVisual && objectives.front().pMatrix)
			{
				const auto& O = objectives.front();
				Fmatrix oX = *GMBase.svp_pose_of(O.pMatrix);
				if (CSkeletonX* osk = fast_dynamic_cast<CSkeletonX*>(O.pVisual))
				{
					Fmatrix boneR;
					if (GMBase.svp_lens_bone_of(O.pVisual, boneR) || osk->SVP_LensBoneXform(boneR))
						oX.mulB_43(boneR);
				}
				auto& OV = O.pVisual->getVisData();
				Fvector oc;
				OV.box.getcenter(oc);
				Fvector ow;
				oX.transform_tiny(ow, oc);
				dbg_cand_dist = ow.distance_to(p->eyepiece.m_W.c);
				// distinct from the eyepiece (a single-lens scope captures the same disc for both)
				if (OV.sphere.R > EPS && dbg_cand_dist > p->eyepiece.radius * 0.5f)
				{
					p->objective.m_W = p->eyepiece.m_W; // stabilized optical axis
					p->objective.m_W.c.set(ow); // real front-lens world position
					p->objective.radius = OV.sphere.R;
					have_obj = true;
				}
			}
			if (!have_obj)
			{
				// no distinct objective mesh, derive it geometrically along the optical axis,
				// the eyepiece radius is the only mesh-scale-robust unit
				Fvector fwd;
				fwd.set(p->eyepiece.m_W.k);
				fwd.normalize();
				p->objective.m_W = p->eyepiece.m_W;
				const float dist_r = (geom_front > 0.f ? geom_front : svp_fallback_objective_radii) * ps_r__svp_obj_dist;
				p->objective.m_W.c.mad(fwd, p->eyepiece.radius * dist_r);
				p->objective.radius = p->eyepiece.radius *
					ps_r__svp_obj_size; // eyepiece-relative objective radius (one global knob): the front-node geomscan swung ~6x conflating glass vs bell housing, eyepiece radius is the only mesh-scale-stable unit
			}
			svp_update_focal_planes(); // focal-plane points for the scope shader

			// pip [SVP-DETECT]/[SVP-EYEDIV] one-shot a/b, measured mesh geometry vs authored ltx vs
			// the live derivation, off unless r__svp_diag, latched per aimed optic
			{
				if (ps_r__svp_diag)
				{
					static const void* s_detect_last = nullptr;
					if (N.pVisual != s_detect_last)
					{
						const float er = p->eyepiece.radius;
						// authored objective resolved by the optics bus, no second precedence copy
						const float auth_w = p->svp_opt_offset.w;
						const float auth_mm = p->svp_opt_obj_mm;
						// live objective this frame, forward distance and radius in eyepiece radii
						Fvector le;
						le.sub(p->objective.m_W.c, p->eyepiece.m_W.c);
						const float live_z = (er > EPS) ? le.magnitude() / er : 0.f;
						const float live_w = (er > EPS) ? p->objective.radius / er : 0.f;
						// a weapon transition frame can carry a garbage pose, nan or inf skips the
						// latch so the next frame retries with a real one
						if (_valid(er) && _valid(live_z) && _valid(live_w))
						{
							s_detect_last = N.pVisual;
							SLensDetection d;
							const bool have = sk && sk->SVP_GetLensDetection(d);
							PipMsg(
								"[SVP-DETECT] switch=%d er=%.4f | authored z=%.2f w=%.3f mm=%.1f | live z=%.2f w=%.3f | detected ok=%d z=%.2f w=%.3f mm=%.1f src=%d",
								ps_r__svp_measured_optics, er, p->svp_opt_offset.z, auth_w, auth_mm, live_z, live_w, (int)(have && d.ok),
								(have && d.ok) ? d.offset.z : 0.f, (have && d.ok) ? d.offset.w : 0.f, (have && d.ok) ? d.mm : 0.f,
								(have && d.ok) ? d.source : -1);
							// live vs detected eyepiece radius, surfaces the near-bone contamination that skews
							// the analytic zoom ratio, read-only, the override stays deferred
							const float det_r = (have && d.ok) ? d.eye_radius : 0.f;
							const float eratio = (det_r > EPS && er > EPS) ? er / det_r : 0.f;
							PipMsg("[SVP-EYEDIV] live_r=%.4f detected_r=%.4f ratio=%.2f src=%d %s", er, det_r, eratio,
								(have && d.ok) ? d.source : -1, (eratio > 1.4f || (eratio > EPS && eratio < 0.7f)) ? "DIVERGE" : "agree");
						}
					}
				}
			}
		}
		break; // the first captured lens is the eyepiece
	}

	// pip DLSS reset when the lens first becomes valid (active can flip a frame before the capture),
	// render-thread edge state, inert at gate 0
	bool lens_valid = (Device.m_SecondViewport.eyepiece.radius > EPS);
	if (lens_valid && !Device.m_SecondViewport.m_lens_prev_valid)
		Device.m_SecondViewport.dlss_reset_next = true;
	Device.m_SecondViewport.m_lens_prev_valid = lens_valid;
}
