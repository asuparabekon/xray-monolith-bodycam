#include "stdafx.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/SkeletonX.h"
#include "svp_optics.h"

#if defined(USE_DX11)
struct SSvpObjectiveHudState
{
	u32 frame = u32(-1);
	u32 session = 0;
	u32 items = 0;
	u32 skinned = 0;
	u32 drawn = 0;
	u32 roots_missing = 0;
	u32 bones_missing = 0;
	bool active = false;
};

static SSvpObjectiveHudState s_svp_objective_hud;

static bool svp_objective_hud_role(u8 role)
{
	return role == IDSGraphManager::hud_hands || role == IDSGraphManager::hud_primary_item || role == IDSGraphManager::hud_offhand_item ||
		role == IDSGraphManager::hud_optic;
}

bool svp_objective_hud_current()
{
	auto& vp = Device.m_SecondViewport;
	return s_svp_objective_hud.active && s_svp_objective_hud.frame == Device.dwFrame && s_svp_objective_hud.session == vp.GetSVPSession() &&
		vp.svp_camera_frame == Device.dwFrame && vp.svp_camera_session == s_svp_objective_hud.session;
}

void svp_objective_hud_note_draw(u8 role)
{
	if (svp_objective_hud_current() && svp_objective_hud_role(role))
		++s_svp_objective_hud.drawn;
}

static void svp_objective_hud_bypass(LPCSTR reason)
{
	s_svp_objective_hud.active = false;
	s_svp_objective_hud.frame = u32(-1);
	static u32 s_bypass_diag_ms = 0;
	if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_bypass_diag_ms > 1000)
	{
		s_bypass_diag_ms = Device.dwTimeGlobal;
		PipMsg("[SVP-CONT] frame=%u path=native-objective pose=live pupil=centered reason=%s", Device.dwFrame, reason);
	}
}

void CRenderTarget::svp_objective_hud_prepare(bool svp_follows)
{
	s_svp_objective_hud = SSvpObjectiveHudState();

	auto& vp = Device.m_SecondViewport;
	if (!svp_follows || scope_svp_enabled < 2 || !Device.true_pip_on || !vp.IsSVPActive())
		return;
	if (!ps_r__svp_weapon_continuity)
	{
		svp_objective_hud_bypass("continuity-off");
		return;
	}
	if (this != RImplementation.TargetMain)
	{
		svp_objective_hud_bypass("target-owner");
		return;
	}
	if (vp.svp_camera_frame != Device.dwFrame)
	{
		svp_objective_hud_bypass("camera-frame");
		return;
	}
	const u32 session = vp.GetSVPSession();
	if (vp.svp_camera_session != session)
	{
		svp_objective_hud_bypass("camera-session");
		return;
	}
	if (vp.eyepiece.radius <= EPS)
	{
		svp_objective_hud_bypass("eyepiece");
		return;
	}
	if (vp.svp_front_use_m <= EPS)
	{
		svp_objective_hud_bypass("objective-camera");
		return;
	}

	u32 queued = 0;
	auto inspect = [&](auto& graph)
	{
		queued += (u32)graph.size();
		for (auto& item : graph)
		{
			if (!svp_objective_hud_role(item.hud_role))
				continue;
			if (!item.pVisual || !item.pMatrix || !item.pSE)
			{
				++s_svp_objective_hud.roots_missing;
				continue;
			}
			if (RImplementation.GMBase.svp_pose_of(item.pMatrix) == item.pMatrix)
				++s_svp_objective_hud.roots_missing;
			++s_svp_objective_hud.items;
			CSkeletonX* skeleton = fast_dynamic_cast<CSkeletonX*>(item.pVisual);
			if (skeleton)
			{
				++s_svp_objective_hud.skinned;
				if (!skeleton->SVP_BoneSnapshotReady())
					++s_svp_objective_hud.bones_missing;
			}
		}
	};
	auto& graph = RImplementation.GMBase.RGraph;
	inspect(graph.mapHUD);
	inspect(graph.mapHUDSorted.Sorted);
	inspect(graph.mapHUDSorted.Wmark);
	inspect(graph.mapHUDSorted.Emissive);
	inspect(graph.mapHUDSorted.Distort);
	if (!queued)
	{
		svp_objective_hud_bypass("hud-empty");
		return;
	}
	if (!s_svp_objective_hud.items)
	{
		svp_objective_hud_bypass("weapon-empty");
		return;
	}

	s_svp_objective_hud.frame = Device.dwFrame;
	s_svp_objective_hud.session = session;
	s_svp_objective_hud.active = true;
}

void CRenderTarget::svp_objective_hud_report()
{
	if (!svp_objective_hud_current())
		return;
	static u32 s_diag_ms = 0;
	if (ps_r__svp_cop_diag && Device.dwTimeGlobal - s_diag_ms > 1000)
	{
		s_diag_ms = Device.dwTimeGlobal;
		const bool exact = !s_svp_objective_hud.roots_missing && !s_svp_objective_hud.bones_missing;
		PipMsg(
			"[SVP-CONT] frame=%u path=native-objective pose=%s capture=entrance-pupil mapping=geometry depth=shared items=%u skinned=%u drawn=%u missing=%u/%u near=%.2fcm",
			Device.dwFrame, exact ? "same-frame" : "partial", s_svp_objective_hud.items, s_svp_objective_hud.skinned,
			s_svp_objective_hud.drawn, s_svp_objective_hud.roots_missing, s_svp_objective_hud.bones_missing,
			Device.m_SecondViewport.svp_near * 100.f);
	}
}

#endif
