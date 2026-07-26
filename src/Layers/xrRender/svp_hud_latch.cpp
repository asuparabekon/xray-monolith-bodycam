#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/CustomHUD.h"

#include "FBasicVisual.h"
#include "CHudInitializer.h"

#include "fhierrarhyvisual.h"
#include "SkeletonCustom.h"
#include "SkeletonX.h"
#include "../../xrEngine/fmesh.h"
#include "flod.h"

#include "../../xrEngine/xr_object.h"

using namespace R_dsgraph;

// pip object-space render (skinning) matrix of the lens bone, the SVP eyepiece uses it so a skinned
// scope lens follows its bone through ADS and sway instead of the kinematics root
bool CSkeletonX::SVP_LensBoneXform(Fmatrix& out)
{
	if (!Parent)
		return false;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return false;
	out = Parent->LL_GetBoneInstance(bone).mRenderTransform;
	return true;
}

// pip lens bone visibility, hidden markswitch lens meshes must not win the eyepiece pick
bool CSkeletonX::SVP_LensBoneVisible()
{
	if (!Parent)
		return true;
	BOOL visible;
	if (SVP_BoneSnapshotVisible(visible))
		return !!visible;
	xrCriticalSectionGuard guard(&Parent->UCalc_Mutex);
	u16 bone;
	if (RenderMode == RM_SINGLE)
		bone = (u16)RMS_boneid;
	else if (BonesUsed.size())
		bone = BonesUsed[0];
	else
		return true;
	return !!Parent->LL_GetBoneVisible(bone);
}

// pip forward the owning kinematics measured lens fit, lets the render side query detection
// from a lens leaf visual, false when unparented
bool CSkeletonX::SVP_GetLensDetection(SLensDetection& out)
{
	if (!Parent)
		return false;
	return Parent->GetLensDetection(out);
}

// pip capture one HUD root sample for both viewports and every late lens draw
void CDSGraphManager::svp_latch_hud_poses()
{
	m_svp_pose.clear();
	m_svp_pose_frame = Device.dwFrame;
	auto grab = [this](Fmatrix* p)
	{
		if (!p)
			return;
		for (const auto& pose : m_svp_pose)
			if (pose.source == p)
				return;
		m_svp_pose.emplace_back();
		m_svp_pose.back().source = p;
		m_svp_pose.back().value = *p;
	};
	for (auto& n : RGraph.mapHUD)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Sorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Wmark)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Emissive)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapHUDSorted.Distort)
		grab(n.pMatrix);
#if defined(USE_DX11)
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab(n.pMatrix);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab(n.pMatrix);

	extern int scope_svp_enabled;
	extern int ps_r__svp_weapon_continuity;
	const bool exact_pose = scope_svp_enabled >= 2 && ps_r__svp_weapon_continuity
		&& Device.true_pip_on;
	if (exact_pose)
	{
		svp_hud_bone_snapshot_begin();
		auto capture = [](dxRender_Visual* v)
		{
			CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
			if (skeleton)
				skeleton->SVP_CaptureBoneSnapshot();
		};
		for (auto& n : RGraph.mapHUD)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Sorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Wmark)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Emissive)
			capture(n.pVisual);
		for (auto& n : RGraph.mapHUDSorted.Distort)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDSorted)
			capture(n.pVisual);
		for (auto& n : RGraph.mapScopeHUDObjective)
			capture(n.pVisual);
		for (auto& n : RGraph.mapReflexHUDSorted)
			capture(n.pVisual);
	}

	m_svp_bone.clear();
	m_svp_bone_frame = Device.dwFrame;
	auto grab_bone = [this](dxRender_Visual* v)
	{
		if (!v)
			return;
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
				return;
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(v);
		Fmatrix b;
		if (sk && sk->SVP_LensBoneXform(b))
		{
			m_svp_bone.emplace_back();
			m_svp_bone.back().visual = v;
			m_svp_bone.back().value = b;
		}
	};
	for (auto& n : RGraph.mapScopeHUDSorted)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapScopeHUDObjective)
		grab_bone(n.pVisual);
	for (auto& n : RGraph.mapReflexHUDSorted)
		grab_bone(n.pVisual);
#endif
}

// the latched pose while this frame's latch is live, else the caller's pointer unchanged
Fmatrix* CDSGraphManager::svp_pose_of(Fmatrix* p)
{
	if (m_svp_pose_frame == Device.dwFrame)
		for (auto& pose : m_svp_pose)
			if (pose.source == p)
				return &pose.value;
	return p;
}

// the latched lens bone while this frame's latch is live, false falls back to the live read
bool CDSGraphManager::svp_lens_bone_of(dxRender_Visual* v, Fmatrix& out)
{
	CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(v);
	if (skeleton && skeleton->SVP_BoneSnapshotXform(out))
		return true;
	if (m_svp_bone_frame == Device.dwFrame)
		for (const auto& bone : m_svp_bone)
			if (bone.visual == v)
			{
				out = bone.value;
				return true;
			}
	return false;
}

static LPCSTR svp_hud_role_name(u8 role)
{
	switch (role)
	{
	case IDSGraphManager::hud_hands: return "hands";
	case IDSGraphManager::hud_primary_item: return "primary";
	case IDSGraphManager::hud_offhand_item: return "offhand";
	case IDSGraphManager::hud_optic: return "optic";
	case IDSGraphManager::hud_prop: return "prop";
	default: return "unknown";
	}
}

static bool svp_basis_coords(const Fvector& origin, const Fvector& right,
	const Fvector& up, const Fvector& forward, const Fvector& point, Fvector& coords)
{
	Fvector r = right;
	Fvector u = up;
	Fvector f = forward;
	if (r.square_magnitude() <= EPS || u.square_magnitude() <= EPS
		|| f.square_magnitude() <= EPS)
		return false;
	r.normalize();
	u.normalize();
	f.normalize();
	Fvector delta;
	delta.sub(point, origin);
	coords.set(delta.dotproduct(r), delta.dotproduct(u), delta.dotproduct(f));
	return _valid(coords);
}

static bool svp_basis_direction(const Fvector& right, const Fvector& up,
	const Fvector& forward, const Fvector& direction, Fvector& coords)
{
	Fvector r = right;
	Fvector u = up;
	Fvector f = forward;
	Fvector d = direction;
	if (r.square_magnitude() <= EPS || u.square_magnitude() <= EPS
		|| f.square_magnitude() <= EPS || d.square_magnitude() <= EPS)
		return false;
	r.normalize();
	u.normalize();
	f.normalize();
	d.normalize();
	coords.set(d.dotproduct(r), d.dotproduct(u), d.dotproduct(f));
	return _valid(coords);
}

struct SSvpRelativeProbe
{
	dxRender_Visual* visual = nullptr;
	Fmatrix* root = nullptr;
	const void* owner = nullptr;
	Fvector camera = {};
	Fvector lens = {};
	Fvector root_lens = {};
	Fvector axis_i = {};
	Fvector axis_j = {};
	Fvector axis_k = {};
	float forward = -flt_max;
	u8 role = IDSGraphManager::hud_unknown;
	bool skinned = false;
	bool snapshot = false;
	bool valid = false;
};

static bool svp_axial_bounds(const Fbox& box, const Fmatrix& world,
	const Fvector& origin, const Fvector& axis, float& lower, float& upper)
{
	lower = 1e9f;
	upper = -1e9f;
	for (u32 corner = 0; corner < 8; ++corner)
	{
		Fvector local;
		local.set((corner & 1) ? box.max.x : box.min.x,
			(corner & 2) ? box.max.y : box.min.y,
			(corner & 4) ? box.max.z : box.min.z);
		Fvector point;
		world.transform_tiny(point, local);
		Fvector offset;
		offset.sub(point, origin);
		const float axial = offset.dotproduct(axis);
		if (!_valid(axial))
			return false;
		lower = _min(lower, axial);
		upper = _max(upper, axial);
	}
	return lower <= upper;
}

static bool svp_objective_hud_filter_ready()
{
	extern int scope_svp_enabled;
	extern int ps_r__svp_optic_body_suppress;
	auto& vp = Device.m_SecondViewport;
	return ps_r__svp_optic_body_suppress && scope_svp_enabled >= 2
		&& Device.true_pip_on && vp.IsSVPActive()
		&& vp.svp_camera_domain == CSecondVPParams::camera_objective
		&& vp.SnapshotExact(vp.svp_camera_frame, vp.svp_camera_session, Device.dwFrame)
		&& vp.eyepiece.radius > EPS && vp.objective.radius > EPS;
}

void CDSGraphManager::svp_classify_objective_hud(dxRender_Visual* visual, Fmatrix* matrix,
	u8 role, SSvpHudAdmission& admission)
{
	admission = SSvpHudAdmission();
	if (!svp_objective_hud_filter_ready() || !visual || !matrix)
		return;

	auto& vp = Device.m_SecondViewport;
	const Fvector origin = vp.eyepiece.m_W.c;
	Fvector axis;
	axis.sub(vp.objective.m_W.c, origin);
	const float tube = axis.magnitude();
	if (!_valid(tube) || tube <= EPS)
	{
		admission.reason = "tube";
		return;
	}
	axis.div(tube);
	admission.objective = tube;

	Fmatrix world = *svp_pose_of(matrix);
	if (CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(visual))
	{
		Fmatrix bone;
		if (svp_lens_bone_of(visual, bone) || skeleton->SVP_LensBoneXform(bone))
			world.mulB_43(bone);
	}

	Fvector center;
	world.transform_tiny(center, visual->vis.sphere.P);
	Fvector offset;
	offset.sub(center, origin);
	admission.axial = offset.dotproduct(axis);
	Fvector on_axis;
	on_axis.mad(origin, axis, admission.axial);
	admission.radial = on_axis.distance_to(center);
	admission.radius = visual->vis.sphere.R;

	const bool role_ok = role == IDSGraphManager::hud_primary_item
		|| role == IDSGraphManager::hud_optic;
	const bool size_ok = _valid(admission.radius) && admission.radius > EPS
		&& admission.radius < tube * 1.75f;
	const float body_radius = std::max(vp.eyepiece.radius, vp.objective.radius) * 1.75f + 0.01f;
	const bool wrap = admission.radial < admission.radius * 0.6f
		&& admission.radius < tube * 0.3f;
	const bool axis_overlap = _valid(admission.radial)
		&& (admission.radial < body_radius || wrap);
	const bool bounds_ok = svp_axial_bounds(visual->vis.box, world, origin, axis,
		admission.axial_lo, admission.axial_hi);
	const float plane_epsilon = 0.001f;
	const bool contains_objective = bounds_ok
		&& admission.axial_lo <= tube + plane_epsilon
		&& admission.axial_hi >= tube - plane_epsilon;
	const bool local = admission.axial > -0.6f * tube
		&& admission.axial < 1.4f * tube;

	admission.candidate = role_ok && size_ok && axis_overlap && bounds_ok;
	admission.reject = admission.candidate && local && contains_objective;
	admission.forward = admission.candidate
		&& admission.axial_lo > tube + plane_epsilon;
	if (admission.reject)
		admission.reason = "objective-body";
	else if (admission.forward)
		admission.reason = "forward";
	else if (!role_ok)
		admission.reason = "role";
	else if (!size_ok)
		admission.reason = "size";
	else if (!axis_overlap)
		admission.reason = "axis";
	else if (!bounds_ok)
		admission.reason = "bounds";
	else if (!local)
		admission.reason = "range";
	else
		admission.reason = "plane-miss";
}

// pip drain only the weapon list into the scope image, the caller owns the view/projection
void CDSGraphManager::r_dsgraph_render_hud_svp()
{
	PROF_EVENT("r_dsgraph_render_hud_svp");
	// same camera and projection as the world, real depth so the scene occludes the weapon
	// sliver and vice versa (rmNear depth-fronting belonged to the retired separate camera)
	RImplementation.rmNormal();
	auto& graph = RGraph.mapHUD;
	if (!graph.empty())
	{
		std::sort(graph.begin(), graph.end());
		const auto& vp = Device.m_SecondViewport;
		Fvector ax;
		ax.sub(vp.objective.m_W.c, vp.eyepiece.m_W.c);
		const float len2 = ax.square_magnitude();
		const float tube = _sqrt(len2);
		const bool objective_filter = svp_objective_hud_filter_ready();
		u32 admit_candidates = 0;
		u32 admit_suppressed = 0;
		u32 admit_clipon_unresolved = 0;
		extern int ps_r__svp_cop_diag;
		static u32 s_hud_diag_ms = 0;
		const bool diag = ps_r__svp_cop_diag && (Device.dwTimeGlobal - s_hud_diag_ms > 3000);
		const bool relative_diag = ps_r__svp_cop_diag >= 2
			&& vp.svp_lens_frame == Device.dwFrame
			&& vp.SnapshotExact(vp.svp_camera_frame, vp.svp_camera_session, Device.dwFrame)
			&& vp.svp_lens_root && vp.svp_lens_visual;
		SSvpRelativeProbe relative_probe;
		if (diag)
		{
			s_hud_diag_ms = Device.dwTimeGlobal;
			PipMsg("[SVP-HUD] tube=%.1fcm items=%u objectiveFilter=%d",
				tube * 100.f, (u32)graph.size(), objective_filter ? 1 : 0);
		}
		extern int ps_r__svp_stats;
		extern u32 svp_stats_hud_cull_reject;
		for (auto& item : graph)
		{
			if (svp_cull_reject(item.pVisual, svp_pose_of(item.pMatrix)))
			{
				if (ps_r__svp_stats)
					++svp_stats_hud_cull_reject;
				continue;
			}
			dxRender_Visual* V = item.pVisual;
			VERIFY(V && V->shader._get());
			bool drop = false;
			float t = 0.f, rad = -1.f;
			float axial_lo = 0.f, axial_hi = 0.f;
			LPCSTR admission = "outside";
			if (objective_filter)
			{
				SSvpHudAdmission objective_admission;
				svp_classify_objective_hud(V, item.pMatrix, item.hud_role, objective_admission);
				t = tube > EPS ? objective_admission.axial / tube : 0.f;
				rad = objective_admission.radial;
				axial_lo = objective_admission.axial_lo;
				axial_hi = objective_admission.axial_hi;
				drop = objective_admission.reject;
				admission = objective_admission.reason;
				if (objective_admission.candidate)
					++admit_candidates;
				if (objective_admission.reject)
					++admit_suppressed;
				if (objective_admission.forward)
					++admit_clipon_unresolved;
			}
			if (diag)
			{
				auto tx = V->GetTexture();
				if (objective_filter)
					PipMsg("[SVP-ADMIT] %s role=%s t=%.2f rad=%.1fcm R=%.1fcm axial=[%.1f,%.1f]cm objective=%.1fcm reason=%s visual=%p root=%p lensVisual=%p lensRoot=%p %s",
						drop ? "SUPPRESS" : "retain", svp_hud_role_name(item.hud_role), t,
						rad * 100.f, V->vis.sphere.R * 100.f, axial_lo * 100.f, axial_hi * 100.f,
						tube * 100.f, admission, (void*)V, (void*)item.pMatrix,
						vp.svp_lens_visual, vp.svp_lens_root,
						tx ? tx->cName.c_str() : "?");
				else
					PipMsg("[SVP-HUD] keep t=%.2f rad=%.1fcm R=%.1fcm %s",
						t, rad * 100.f, V->vis.sphere.R * 100.f, tx ? tx->cName.c_str() : "?");
			}
			if (drop) continue;
			if (relative_diag && item.hud_role == hud_primary_item
				&& V != vp.svp_lens_visual)
			{
				Fmatrix root_world = *svp_pose_of(item.pMatrix);
				Fmatrix point_world = root_world;
				CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(V);
				bool snapshot = false;
				bool comparable = skeleton != nullptr;
				if (comparable)
				{
					Fmatrix bone;
					if (skeleton->SVP_RigidBoneSnapshotXform(bone))
					{
						point_world.mulB_43(bone);
						snapshot = true;
					}
					else
						comparable = false;
				}
				Fvector point;
				point_world.transform_tiny(point, V->vis.sphere.P);
				Fvector camera;
				if (comparable && svp_basis_coords(vp.svp_cam_pos, vp.svp_right, vp.svp_up,
					vp.svp_fwd, point, camera) && camera.z > EPS
					&& camera.z > relative_probe.forward)
				{
					Fvector lens;
					Fvector root_lens;
					if (svp_basis_coords(vp.eyepiece.m_W.c, vp.eyepiece.m_W.i,
						vp.eyepiece.m_W.j, vp.eyepiece.m_W.k, point, lens)
						&& svp_basis_coords(vp.eyepiece.m_W.c, vp.eyepiece.m_W.i,
							vp.eyepiece.m_W.j, vp.eyepiece.m_W.k, root_world.c, root_lens)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.i, relative_probe.axis_i)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.j, relative_probe.axis_j)
						&& svp_basis_direction(vp.eyepiece.m_W.i, vp.eyepiece.m_W.j,
							vp.eyepiece.m_W.k, point_world.k, relative_probe.axis_k))
					{
						relative_probe.visual = V;
						relative_probe.root = item.pMatrix;
						relative_probe.owner = skeleton ? skeleton->SVP_SkeletonOwner() : nullptr;
						relative_probe.camera = camera;
						relative_probe.lens = lens;
						relative_probe.root_lens = root_lens;
						relative_probe.forward = camera.z;
						relative_probe.role = item.hud_role;
						relative_probe.skinned = skeleton != nullptr;
						relative_probe.snapshot = snapshot;
						relative_probe.valid = true;
					}
				}
			}
			RCache.set_Element(item.pSE);
			RCache.set_xform_world(*svp_pose_of(item.pMatrix));
			RImplementation.apply_object(item.pObject);
			RImplementation.apply_lmaterial();
			V->Render(svp_drain_lod(item.ssa, V->vis.sphere.R));
#if RENDER == R_R4
			extern void svp_objective_hud_note_draw(u8);
			svp_objective_hud_note_draw(item.hud_role);
#endif
		}
		if (relative_diag && relative_probe.valid)
		{
			static dxRender_Visual* s_visual = nullptr;
			static const void* s_root = nullptr;
			static const void* s_owner = nullptr;
			static const void* s_lens_root = nullptr;
			static const void* s_lens_visual = nullptr;
			static const void* s_lens_owner = nullptr;
			static u32 s_session = 0;
			static u32 s_epoch = 0;
			static u32 s_log_ms = 0;
			static Fvector s_camera = {};
			static Fvector s_lens = {};
			static Fvector s_root_lens = {};
			static Fvector s_axis_i = {};
			static Fvector s_axis_j = {};
			static Fvector s_axis_k = {};
			static bool s_valid = false;

			const bool same = s_valid && s_visual == relative_probe.visual
				&& s_root == relative_probe.root
				&& s_owner == relative_probe.owner
				&& s_lens_root == vp.svp_lens_root
				&& s_lens_visual == vp.svp_lens_visual
				&& s_lens_owner == vp.svp_lens_owner
				&& s_session == vp.GetSVPSession()
				&& s_epoch == vp.svp_camera_epoch;
			const bool linked = same && Device.dwTimeGlobal - s_log_ms <= 500;
			if (!linked || Device.dwTimeGlobal - s_log_ms > 250)
			{
				const u32 dt_ms = linked ? Device.dwTimeGlobal - s_log_ms : 0;
				Fvector camera_delta = {};
				Fvector lens_delta = {};
				Fvector root_delta = {};
				float angle_delta = 0.f;
				if (linked)
				{
					camera_delta.sub(relative_probe.camera, s_camera);
					lens_delta.sub(relative_probe.lens, s_lens);
					root_delta.sub(relative_probe.root_lens, s_root_lens);
					const float relative_trace =
						relative_probe.axis_i.dotproduct(s_axis_i)
						+ relative_probe.axis_j.dotproduct(s_axis_j)
						+ relative_probe.axis_k.dotproduct(s_axis_k);
					const float angle_cos = (relative_trace - 1.f) * 0.5f;
					angle_delta = rad2deg(acosf(std::clamp(angle_cos, -1.f, 1.f)));
				}
				const int same_owner = relative_probe.owner == vp.svp_lens_owner ? 1 : 0;
				auto texture = relative_probe.visual->GetTexture();
				PipMsg("[SVP-REL] probe=forward-primary seed=%d dt_ms=%u pointLens=(%.5f,%.5f,%.5f) dPoint=(%.5f,%.5f,%.5f) axisK=(%.5f,%.5f,%.5f) dAngleDeg=%.5f rootLens=(%.5f,%.5f,%.5f) dRoot=(%.5f,%.5f,%.5f) camera=(%.5f,%.5f,%.5f) dCamera=(%.5f,%.5f,%.5f) sameRoot=%d sameOwner=%d skinned=%d snapshot=%d root=%p lensRoot=%p visual=%p lensVisual=%p role=%s texture=%s session=%u epoch=%u frame=%u",
					linked ? 0 : 1, dt_ms,
					relative_probe.lens.x, relative_probe.lens.y, relative_probe.lens.z,
					lens_delta.x, lens_delta.y, lens_delta.z,
					relative_probe.axis_k.x, relative_probe.axis_k.y, relative_probe.axis_k.z,
					angle_delta,
					relative_probe.root_lens.x, relative_probe.root_lens.y, relative_probe.root_lens.z,
					root_delta.x, root_delta.y, root_delta.z,
					relative_probe.camera.x, relative_probe.camera.y, relative_probe.camera.z,
					camera_delta.x, camera_delta.y, camera_delta.z,
					relative_probe.root == vp.svp_lens_root ? 1 : 0, same_owner,
					relative_probe.skinned ? 1 : 0, relative_probe.snapshot ? 1 : 0,
					(void*)relative_probe.root, vp.svp_lens_root,
					(void*)relative_probe.visual, vp.svp_lens_visual,
					svp_hud_role_name(relative_probe.role),
					texture ? texture->cName.c_str() : "?",
					vp.GetSVPSession(), vp.svp_camera_epoch, Device.dwFrame);
				s_log_ms = Device.dwTimeGlobal;
				s_visual = relative_probe.visual;
				s_root = relative_probe.root;
				s_owner = relative_probe.owner;
				s_lens_root = vp.svp_lens_root;
				s_lens_visual = vp.svp_lens_visual;
				s_lens_owner = vp.svp_lens_owner;
				s_session = vp.GetSVPSession();
				s_epoch = vp.svp_camera_epoch;
				s_camera = relative_probe.camera;
				s_lens = relative_probe.lens;
				s_root_lens = relative_probe.root_lens;
				s_axis_i = relative_probe.axis_i;
				s_axis_j = relative_probe.axis_j;
				s_axis_k = relative_probe.axis_k;
				s_valid = true;
			}
		}
		else if (relative_diag)
		{
			static u32 s_unavailable_ms = 0;
			if (Device.dwTimeGlobal - s_unavailable_ms > 1000)
			{
				s_unavailable_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-REL] probe=forward-primary unavailable reason=no-comparable-rigid-primary root=%p owner=%p session=%u epoch=%u frame=%u",
					vp.svp_lens_root, vp.svp_lens_owner,
					vp.GetSVPSession(), vp.svp_camera_epoch, Device.dwFrame);
			}
		}
		if (diag && objective_filter)
			PipMsg("[SVP-ADMIT] summary candidates=%u suppressed=%u clipon-unresolved=%u front=%.1fcm frame=%u session=%u",
				admit_candidates, admit_suppressed, admit_clipon_unresolved,
				vp.svp_front_use_m * 100.f, vp.svp_camera_frame, vp.svp_camera_session);
		graph.clear();
	}
	RCache.set_xform_world(Fidentity);
	RImplementation.rmNormal();
}
