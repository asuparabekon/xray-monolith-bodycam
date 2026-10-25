#include "stdafx.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/SkeletonX.h"
#include "svp_optics.h"

#if defined(USE_DX11)
#include "r4_3dss_reticle.h"
#include "svp_stats.h"
u32 CRenderTarget::draw_reflex(bool svp, bool reticle_light)
{
	PIX_EVENT_F("RENDER_REFLEX_SIGHTS x%u", (u32)RImplementation.GMBase.RGraph.mapReflexHUDSorted.size());
	// 3DSS reticle glow, out of bloom (a reflex in the capture has the caller's glow targets)
	const bool reflex_glow = !svp && !RImplementation.GMBase.RGraph.mapReflexHUDSorted.empty();
	C3DSSReticleGlow reticle_glow(reflex_glow ? this : nullptr, reticle_light);
	reticle_glow.note(RImplementation.GMBase.RGraph.mapReflexHUDSorted);

	Fmatrix FTold = Device.mFullTransform;
	if (svp)
	{
		// Match the objective world and weapon projection
		Device.mFullTransform.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		RCache.set_xform_view(Device.matrices[1].mView);
		RCache.set_xform_project(Device.matrices[1].mProject);
	}
	else
	{
		Device.mFullTransform = Device.mFullTransformHud;
		RCache.set_xform_project(Device.mProjectHud);
	}
	RImplementation.rmNear();

	static u32 s_node_diag_ms = 0;
	const bool node_diag = svp && ps_r__svp_diag && Device.dwTimeGlobal - s_node_diag_ms > 1000;
	if (node_diag)
		s_node_diag_ms = Device.dwTimeGlobal;

	auto& nodes = RImplementation.GMBase.RGraph.mapReflexHUDSorted;
	u32 selected_index = u32(-1);
	Fmatrix selected_world = {};
	bool selected_frozen = false;
	bool selected_straddle = false;
	Fvector selected_shift = {};
	float selected_score = flt_max;
	float hybrid_front = -1.f;
	if (svp)
	{
		auto& vp = Device.m_SecondViewport;
		Fvector objective_axis = vp.objective.m_W.k;
		if (_valid(objective_axis) && objective_axis.square_magnitude() > EPS)
			objective_axis.normalize();
		else
			objective_axis.set(Device.matrices[1].mView._13, Device.matrices[1].mView._23, Device.matrices[1].mView._33);

		for (u32 index = 0; index < nodes.size(); ++index)
		{
			auto& N = nodes[index];
			if (!N.pVisual || !N.pSE || !N.pMatrix)
				continue;

			CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
			BOOL snapshot_visible = TRUE;
			const bool bone_visible =
				!sk || (sk->SVP_BoneSnapshotVisible(snapshot_visible) ? !!snapshot_visible : sk->SVP_LensBoneVisible());
			const void* owner = sk ? sk->SVP_SkeletonOwner() : nullptr;
			const bool root_match = !vp.svp_lens_root || N.pMatrix == vp.svp_lens_root;
			const bool owner_match = vp.svp_lens_owner && owner && owner == vp.svp_lens_owner;
			const bool related = root_match || owner_match;

			Fmatrix refW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
			Fmatrix boundsW = refW;
			Fmatrix latched_bone;
			const bool has_latched_bone = sk && RImplementation.GMBase.svp_lens_bone_of(N.pVisual, latched_bone);
			if (has_latched_bone)
				boundsW.mulB_43(latched_bone);
			const bool frozen_reflex = svp_objective_hud_current() && sk && sk->SVP_BoneSnapshotReady();
			if (!frozen_reflex)
			{
				Fmatrix live_bone;
				if (has_latched_bone && sk->SVP_LensBoneXform(live_bone))
				{
					Fmatrix inv;
					inv.invert(live_bone);
					refW.mulB_43(latched_bone);
					refW.mulB_43(inv);
				}
			}

			const auto& vis = N.pVisual->getVisData();
			Fvector world_center;
			boundsW.transform_tiny(world_center, vis.sphere.P);
			Fvector view_center;
			Device.matrices[1].mView.transform_tiny(view_center, world_center);
			const float world_scale = _max(boundsW.i.magnitude(), _max(boundsW.j.magnitude(), boundsW.k.magnitude()));
			const float world_radius = vis.sphere.R * world_scale;
			// capture content ahead of the objective, its near extent bounds the near derive
			if (bone_visible && related && _valid(view_center) && _valid(world_radius) && world_radius > EPS)
			{
				const float front = _max(view_center.z - world_radius, 0.f);
				hybrid_front = (hybrid_front < 0.f) ? front : _min(hybrid_front, front);
			}
			const float near_plane = _max(vp.svp_near, EPS);
			const float far_plane = _max(vp.svp_far, near_plane + EPS);

			Fvector axis_delta;
			axis_delta.sub(world_center, vp.objective.m_W.c);
			const float axis_depth = axis_delta.dotproduct(objective_axis);
			Fvector radial;
			radial.mad(axis_delta, objective_axis, -axis_depth);
			const float axis_offset = radial.magnitude();

			bool visible = bone_visible && related && _valid(view_center) && _valid(world_radius) && world_radius > EPS &&
				_valid(axis_depth) && _valid(axis_offset) && axis_depth + world_radius > -EPS &&
				view_center.z + world_radius > near_plane && view_center.z - world_radius < far_plane;
			float ndc_x = 0.f;
			float ndc_y = 0.f;
			// a sphere straddling the camera plane projects to infinity, the depth test above
			// already admits it so the screen bounds refine only a fully forward candidate
			const bool straddle = view_center.z - world_radius <= near_plane;
			if (visible && !straddle)
			{
				const Fmatrix& P = Device.matrices[1].mProject;
				const float clip_x = view_center.x * P._11 + view_center.y * P._21 + view_center.z * P._31 + P._41;
				const float clip_y = view_center.x * P._12 + view_center.y * P._22 + view_center.z * P._32 + P._42;
				const float clip_w = view_center.x * P._14 + view_center.y * P._24 + view_center.z * P._34 + P._44;
				const float inv_w = _abs(clip_w) > EPS_S ? 1.f / clip_w : 0.f;
				ndc_x = clip_x * inv_w;
				ndc_y = clip_y * inv_w;
				const float depth = _max(view_center.z - world_radius, near_plane);
				const float radius_x = _abs(P._11) * world_radius / depth;
				const float radius_y = _abs(P._22) * world_radius / depth;
				visible =
					clip_w > EPS && ndc_x + radius_x > -1.f && ndc_x - radius_x < 1.f && ndc_y + radius_y > -1.f && ndc_y - radius_y < 1.f;
			}

			const float score = axis_offset / _max(_max(axis_depth, world_radius), EPS);
			if (node_diag)
			{
				auto tx = N.pVisual->GetTexture();
				PipMsg(
					"[SVP-HYBRID] candidate=%u tex=%s view=(%.4f %.4f %.4f) ndc=(%.3f %.3f) r=%.2fcm axis=(%.2f %.2f) score=%.4f visible=%d bone=%d related=%d role=%u frozen=%d owner=%p",
					index, tx ? tx->cName.c_str() : "?", view_center.x, view_center.y, view_center.z, ndc_x, ndc_y, world_radius * 100.f,
					axis_depth * 100.f, axis_offset * 100.f, score, visible ? 1 : 0, bone_visible ? 1 : 0, related ? 1 : 0, N.hud_role,
					frozen_reflex ? 1 : 0, owner);
			}
			if (visible && score < selected_score)
			{
				selected_index = index;
				selected_world = refW;
				selected_frozen = frozen_reflex;
				selected_straddle = straddle;
				selected_score = score;
				selected_shift.set(0.f, 0.f, 0.f);
				if (straddle)
				{
					// minimum axial push that clears the near plane with one near depth spare
					const float push = near_plane * 2.f + world_radius - view_center.z;
					selected_shift.mul(objective_axis, push);
				}
			}
		}

		// published for the next camera build so the derived near never clips the capture
		vp.svp_hybrid_front = hybrid_front;
		vp.svp_hybrid_front_frame = Device.dwFrame;
		vp.svp_hybrid_front_session = vp.GetSVPSession();
		vp.svp_hybrid_front_epoch = vp.svp_optic_epoch;
	}

	u32 drawn = 0;
	u32 node_index = 0;
	for (auto& N : nodes)
	{
		if (svp && node_index != selected_index)
		{
			node_index++;
			continue;
		}
		if (!N.pVisual || !N.pSE || !N.pMatrix)
		{
			node_index++;
			continue;
		}
		RCache.set_Element(N.pSE);
		Fmatrix refW = svp ? selected_world : *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		// a mesh straddling the camera plane cannot rasterize, the minimum axial push
		// clears the near plane and the window keeps filling the view
		if (svp && selected_straddle)
			refW.c.add(selected_shift);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(N.pVisual);
		const bool frozen_reflex = svp ? selected_frozen : svp_objective_hud_current() && sk && sk->SVP_BoneSnapshotReady();
		// Match the objective pass bone pose
		if (!svp && !frozen_reflex)
		{
			Fmatrix bL, bNow;
			if (sk && RImplementation.GMBase.svp_lens_bone_of(N.pVisual, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				refW.mulB_43(bL);
				refW.mulB_43(inv);
			}
		}
		if (node_diag)
		{
			auto tx = N.pVisual->GetTexture();
			Fvector draw_view;
			Device.matrices[1].mView.transform_tiny(draw_view, refW.c);
			PipMsg("[SVP-HYBRID] selected=%u tex=%s score=%.4f frozen=%d straddle=%d drawView=(%.3f %.3f %.3f) vp=%ux%u owner=%p",
				node_index, tx ? tx->cName.c_str() : "?", selected_score, frozen_reflex ? 1 : 0, selected_straddle ? 1 : 0, draw_view.x,
				draw_view.y, draw_view.z, Device.dwWidth, Device.dwHeight, sk ? sk->SVP_SkeletonOwner() : nullptr);
		}
		RCache.set_xform_world(refW);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();
		const bool frozen_was = g_svp_hud_frozen_pass;
		const bool history_was = g_svp_hud_history_write;
		if (frozen_reflex)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = false;
		}
		N.pVisual->Render(0);
		g_svp_hud_frozen_pass = frozen_was;
		g_svp_hud_history_write = history_was;
		drawn++;
		node_index++;
	}

	RImplementation.rmNormal();
	Device.mFullTransform = FTold;
	if (svp)
		RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
	return drawn;
}

// Render an engaged hybrid through the objective camera
bool CRenderTarget::draw_hybrid_reflex()
{
	extern Fvector4 ps_s3ds_param_3;
	extern int ps_markswitch_current;
	auto& G = RImplementation.GMBase.RGraph;
	auto& vp = Device.m_SecondViewport;
	constexpr int mark_magnifier_type = 12;
	const float reticle_value = ps_s3ds_param_3.y;
	const bool reticle_valid =
		_valid(reticle_value) && reticle_value >= 0.f && reticle_value <= float(u8(-1)) && floorf(reticle_value) == reticle_value;
	const int reticle_type = reticle_valid ? static_cast<int>(reticle_value) : -1;
	const bool camera_current = vp.svp_camera_frame == Device.dwFrame && vp.svp_camera_session == vp.GetSVPSession();
	const bool lens_current =
		vp.svp_lens_frame == Device.dwFrame && vp.svp_lens_root != nullptr && vp.svp_lens_visual != nullptr && vp.eyepiece.radius > EPS;
	const bool target_current = RImplementation.TargetSVP == this && RImplementation.Target == this;
	const auto& optic = vp.RenderOpticConfig();
	const bool identity_current = optic.typed_route && optic.valid && optic.frame == Device.dwFrame &&
		optic.session == vp.GetSVPSession() && (optic.scope[0] || optic.diagnostic_scope[0]);
	const bool type_current = identity_current && optic.reticle_type == reticle_type;
	const bool legacy_hybrid = !optic.has_hybrid_reflex && optic.reticle_type == mark_magnifier_type;
	const bool hybrid_eligible = type_current && (optic.hybrid_reflex || legacy_hybrid);

	enum class HybridState : u32
	{
		Inactive,
		WrongDomain,
		NotHybrid,
		Thermal,
		StaleCamera,
		StaleLens,
		StaleIdentity,
		StaleType,
		Empty,
		NoDraw,
		Drawn
	};

	HybridState state = HybridState::Inactive;
	u32 drawn = 0;
	if (!Device.true_pip_on || !vp.IsSVPActive())
		state = HybridState::Inactive;
	else if (vp.svp_camera_domain != CSecondVPParams::camera_objective || vp.svp_front_use_m <= EPS || vp.objective.radius <= EPS)
		state = HybridState::WrongDomain;
	else if (!reticle_valid)
		state = HybridState::StaleType;
	else if (svp_thermal_overlay_active())
		state = HybridState::Thermal;
	else if (!camera_current || !target_current)
		state = HybridState::StaleCamera;
	else if (!lens_current)
		state = HybridState::StaleLens;
	else if (!identity_current)
		state = HybridState::StaleIdentity;
	else if (!type_current)
		state = HybridState::StaleType;
	else if (!hybrid_eligible)
		state = HybridState::NotHybrid;
	else if (G.mapScopeHUDSorted.empty() || G.mapReflexHUDSorted.empty())
		state = HybridState::Empty;
	else
	{
		// Post processed color stays intact while the optical reticle remains sharp. The Reference image took
		// its capture during the combine, so for it the reflex goes into a capture-space target of its own
		// (draw_reference_capture_light shows it there).
		const CRenderTarget* M = RImplementation.TargetMain;
		const bool reference = M && M->scope_reference_current() && scope_reference_raw_frame == Device.dwFrame &&
			scope_reference_raw_stamp == M->scope_reference_packet.Stamp();
		reticle_capture_begin(reference);
		u_setrt(Width, Height, (reference ? rt_reticle_capture_light : rt_secondVP)->pRT, nullptr, nullptr, nullptr);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();
		C3DSSReticleGlow reticle_glow(rt_reticle_capture_emission, rt_reticle_capture_glow);
		reticle_glow.note(G.mapReflexHUDSorted);
		drawn = draw_reflex(true);
		state = drawn ? HybridState::Drawn : HybridState::NoDraw;
	}

	static u32 s_diag_ms = 0;
	static u32 s_last_state = u32(-1);
	static int s_last_type = -2;
	static int s_last_mark = -2;
	static u32 s_last_epoch = u32(-1);
	const u32 state_value = static_cast<u32>(state);
	const bool changed = state_value != s_last_state || reticle_type != s_last_type || ps_markswitch_current != s_last_mark ||
		vp.svp_optic_epoch != s_last_epoch;
	if (ps_r__svp_diag && (changed || Device.dwTimeGlobal - s_diag_ms > 1000))
	{
		s_diag_ms = Device.dwTimeGlobal;
		s_last_state = state_value;
		s_last_type = reticle_type;
		s_last_mark = ps_markswitch_current;
		s_last_epoch = vp.svp_optic_epoch;
		const char* state_name = state == HybridState::Drawn ? "drawn"
			: state == HybridState::NoDraw					 ? "no_draw"
			: state == HybridState::Empty					 ? "empty"
			: state == HybridState::StaleType				 ? "stale_type"
			: state == HybridState::StaleIdentity			 ? "stale_identity"
			: state == HybridState::StaleLens				 ? "stale_lens"
			: state == HybridState::StaleCamera				 ? "stale_camera"
			: state == HybridState::Thermal					 ? "thermal"
			: state == HybridState::NotHybrid				 ? "not_hybrid"
			: state == HybridState::WrongDomain				 ? "wrong_domain"
															 : "inactive";
		LPCSTR optic_name = optic.scope[0] ? optic.scope : (optic.diagnostic_scope[0] ? optic.diagnostic_scope : "legacy");
		PipMsg(
			"[SVP-HYBRID] state=%s path=objective-mesh target=secondvp projection=scene rtype=%d config_type=%u authored=%d hybrid=%d legacy=%d eligible=%d image=%.0f mark=%d mag=%.2f scope=%u reflex=%u camera=%d lens=%d target_ok=%d typed=%d identity=%d type_ok=%d optic=%s spec=%s gen=%u drawn=%u session=%u epoch=%u frame=%u",
			state_name, reticle_type, optic.reticle_type, optic.has_hybrid_reflex ? 1 : 0, optic.hybrid_reflex ? 1 : 0,
			legacy_hybrid ? 1 : 0, hybrid_eligible ? 1 : 0, ps_s3ds_param_3.x, ps_markswitch_current, vp.svp_mag,
			(u32)G.mapScopeHUDSorted.size(), (u32)G.mapReflexHUDSorted.size(), camera_current ? 1 : 0, lens_current ? 1 : 0,
			target_current ? 1 : 0, optic.typed_route ? 1 : 0, identity_current ? 1 : 0, type_current ? 1 : 0, optic_name,
			optic.spec_section[0] ? optic.spec_section : "none", optic.generation, drawn, vp.GetSVPSession(), vp.svp_optic_epoch,
			Device.dwFrame);
	}

	if (state == HybridState::Drawn)
	{
		if (ps_r__svp_stats)
			++svp_stats_reflex_capture;
		return true;
	}
	return false;
}

#endif
