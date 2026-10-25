#include "stdafx.h"
#include "r4.h"
#include "svp_target.h"
#include "svp_target_size.h"
#include "svp_stats.h"
#include "../../xrEngine/igame_persistent.h"

extern int ps_r__svp_flat_window;
extern Fvector4 ps_s3ds_param_3;
extern float ps_r__svp_adaptive_res;
extern int ps_r__svp_adaptive_grow;

namespace
{
svp::TargetSettings target_settings()
{
	return {ps_r__svp_dlss != 0, ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8,
		ps_r__svp_adaptive_grow != 0, ps_r__svp_adaptive_res,
		ps_r__svp_supersample, ps_r__svp_render_scale};
}

svp::TargetFrame target_frame()
{
	return {Device.svp_height(), Device.dwHeight, Device.m_SecondViewport.svp_disc_applied,
		Device.m_SecondViewport.svp_panel_aspect};
}
}

u32 svp_render_extent()
{
	return svp::RenderExtent(target_settings(), target_frame());
}

void svp_target_wh(u32& svp_w, u32& svp_h)
{
	const auto size = svp::Size(target_settings(), target_frame());
	svp_w = size.width;
	svp_h = size.height;
}

// pip allocate the square SVP target the first time an SVP pass runs, with PiP off this is never
// called so the SVP RTs cost nothing, the scene render uses svp_render_extent (display-res at gate 0)
void CRender::EnsureTargetSVP()
{
	if (TargetMain)
		TargetMain->EnsureScopeShaders(); // pip load the lens glue shaders on first aim (idempotent)
	u32 svp_w, svp_h;
	svp_target_wh(svp_w, svp_h);
	if (TargetSVP)
	{
		// pip recreate if the DLSS gate flipped (gbuffer + rt_secondVP UAV change at gate != 0) or the
		// supersample / panel-aspect changed the size, so r__svp_supersample applies live (no vid_restart)
		const bool gate_ok = ((ps_r__svp_dlss != 0) == TargetSVP->m_svp_dlss_built);
		const bool size_ok = (TargetSVP->Width == svp_w && TargetSVP->Height == svp_h);
		if (gate_ok && size_ok)
			return;
		xr_delete(TargetSVP);
	}
	TargetSVP = xr_new<CRenderTarget>("svp", svp_w, svp_h);
	// CHK_DX is a no-op so a VRAM-exhausted creation fails silently, never keep a half-built target
	const bool svp_rt_ok = TargetSVP->rt_secondVP && TargetSVP->rt_secondVP->pSurface
		&& TargetSVP->rt_baseRT && TargetSVP->rt_baseRT->pRT
		&& TargetSVP->rt_baseZB && TargetSVP->rt_baseZB->pZRT
		&& TargetSVP->rt_Color && TargetSVP->rt_Color->pRT
		&& TargetSVP->rt_Position && TargetSVP->rt_Position->pRT;
	if (!svp_rt_ok)
	{
		Msg("! [SVP-ALLOC] SVP target creation FAILED (out of VRAM?), scope pass disabled");
		xr_delete(TargetSVP);
		return;
	}
	Device.m_SecondViewport.dlss_reset_next = true; // pip DLSS history reset, SVP surface (re)created incl. res change
	if (ps_r__svp_diag)
		PipMsg("[SVP-ALLOC] svp=%ux%u (svp_height=%u aspect=%.2f supersample=%.2f dlss=%d)",
			svp_w, svp_h, Device.svp_height(), Device.m_SecondViewport.svp_panel_aspect, ps_r__svp_supersample, ps_r__svp_dlss);
}

// optic epoch of the last settled disc learn, svp_disc_px belongs to this optic
static u32 s_disc_px_epoch = 0;
u32 svp_disc_epoch() { return s_disc_px_epoch; }

// Shared by stock PiP and the Reference optical image.
void svp_track_disc(u32 width, u32 height)
{
	// latch the on-screen eyepiece disc px for adaptive SVP resolution, learn only the
	// settled aimed disc so a raise transient or quick peek never freezes a partial value
	if (RImplementation.TargetSVP && Device.m_SecondViewport.eyepiece.radius > EPS)
	{
		auto& vpd = Device.m_SecondViewport;
		const Fmatrix& MH = Device.mFullTransformHud;
		auto toPx = [&](const Fvector& wp, float& sx, float& sy)
		{
			const float x = wp.x * MH._11 + wp.y * MH._21 + wp.z * MH._31 + MH._41;
			const float y = wp.x * MH._12 + wp.y * MH._22 + wp.z * MH._32 + MH._42;
			const float w = wp.x * MH._14 + wp.y * MH._24 + wp.z * MH._34 + MH._44;
			const float iw = (fabsf(w) > 1e-6f) ? 1.0f / w : 0.0f;
			sx = (x * iw * 0.5f + 0.5f) * (float)width;
			sy = (1.0f - (y * iw * 0.5f + 0.5f)) * (float)height;
		};
		Fvector ei, li;
		li.set(vpd.eyepiece.radius, 0.f, 0.f);
		vpd.eyepiece.m_W.transform_tiny(ei, li);
		float cx, cy, ix, iy;
		toPx(vpd.eyepiece.m_W.c, cx, cy);
		toPx(ei, ix, iy);
		const float disc = 2.0f * sqrtf((ix - cx) * (ix - cx) + (iy - cy) * (iy - cy)); // on-screen disc diameter px
		// weapon raise factor, 1 only when fully aimed, gates learning to the settled pose
		const float rot = (g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants)
			? g_pGamePersistent->m_pGShaderConstants->hud_params.x
			: 0.f;
		// subscribe to the optic epoch, a swap under the jump threshold still re-learns the
		// new optic's disc, the epoch is consumed only after a settled re-learn
		const bool disc_epoch_swap = (vpd.svp_optic_epoch != s_disc_px_epoch);
		if (disc > 1.f && disc == disc && rot > 0.999f) // settled, ignore NaN / degenerate
		{
			float& latched = vpd.svp_disc_px;
			const float prev_latched = latched;
			if (latched <= 0.f || disc > latched + 24.f || disc_epoch_swap) // first settle, a jump, or an optic swap
				latched = disc;
			else if (disc < latched * 0.85f) // big drop, swapped to a smaller optic
				latched = disc;
			if (latched != prev_latched)
			{
				if (ps_r__svp_stats)
					++svp_stats_disc_latch;
				svp_ledger_disc_latch = 1;
			} // overlay + ledger proof the latch moved
			s_disc_px_epoch = vpd.svp_optic_epoch;
		}
		static u32 s_svpres_t = 0;
		if (ps_r__svp_diag && disc > 1.f && Device.dwTimeGlobal - s_svpres_t > 700)
		{
			s_svpres_t = Device.dwTimeGlobal;
			const u32 sres = RImplementation.TargetSVP->Width;
			extern float g_pip_scope_magnification;
			const float lin = (float)sres / disc;
			PipMsg("[SVP-RES] mag=%.1f svp=%ux%u disc=%.0fpx latch=%.0f adapt=%.2f overrender=%.2fx_linear %.2fx_area",
				g_pip_scope_magnification, sres, sres, disc, vpd.svp_disc_px, ps_r__svp_adaptive_res, lin, lin * lin);
		}
	}
}
