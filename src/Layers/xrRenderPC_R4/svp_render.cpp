#include "stdafx.h"
#include "../../xrEngine/igame_persistent.h"
#include "svp_stats.h"
#include "svp_target.h"

namespace
{
struct ViewportHistory
{
	bool active = false;
	u32 opticEpoch = 0;
};
}

// The main pass kept its graph for an SVP pass that no longer runs this frame: drop what the SVP
// pass would have drained so it never replays into the next frame.
void CRender::drainKeptGraph()
{
	auto& graph = GMBase.RGraph;
	graph.clear_graph<false>(graph.mapStaticPasses, 0);
	graph.clear_graph<false>(graph.mapDynamicPasses, 0);
	graph.mapHUD.clear();
	graph.mapLOD.clear();
	graph.mapStaticSorted.Wmark.clear(); // wallmarks drain main first, the SVP pass clears them
	graph.mapDynamicSorted.Wmark.clear();
	graph.mapHUDSorted.Wmark.clear();
	// the grass set is kept only when the draw clears it (grass shadows clear it at frame end)
	if (Details && ps_ssfx_grass_shadows.x <= 0)
	{
		for (auto& list : Details->m_visibles)
			for (auto& vis : list)
				vis.clear_not_free();
	}
}

// PiP viewport scheduling. Keep the main graph until its scope replay has finished;
// lighting shares shadow maps, then captures the scope before the main combine.
void CRender::renderViewports(BOOL bSUN)
{
	// pip a stale hook must never survive into this frame (it captures this + TargetSVP)
	Device.m_SecondViewport.dual_accum = nullptr;
	// pip double-pass, the captured graph renders once for the main viewport and, when a scope drives
	// the SVP, a second time into TargetSVP, true_pip off keeps the single stock main pass
	bool svp = Device.true_pip_on && Device.m_SecondViewport.IsSVPActive();
	// pip lock the adaptive SVP size at ADS-in (svp false -> true) from the disc learned on the last aim, so the
	// target is sized once per aim and never resized mid-ADS (the live disc keeps learning for next time)
	// Retain the existing process lifetime, including across renderer resets.
	static ViewportHistory history;
	// an optic swap while aimed re-locks once the disc is re-learned for the new optic so the target
	// resizes once for it, the ads-in edge still owns the first lock
	const bool disc_swap = svp && Device.m_SecondViewport.svp_optic_epoch != history.opticEpoch
		&& svp_disc_epoch() == Device.m_SecondViewport.svp_optic_epoch;
	if ((svp && !history.active) || disc_swap)
		Device.m_SecondViewport.svp_disc_applied = Device.m_SecondViewport.svp_disc_px;
	if (!svp || disc_swap)
		history.opticEpoch = Device.m_SecondViewport.svp_optic_epoch;
	const bool svp_edge = (svp != history.active);
	history.active = svp;
	// pip allocate the SVP target before the main pass derives the camera into it (lazy, while a PiP scope
	// is active or any weapon reports a zoom factor, never when off)
	svp_stats::cpu_begin(svp_stats::CPU_MISC);
	if (Device.true_pip_on && (svp || (g_pGamePersistent &&
		g_pGamePersistent->m_pGShaderConstants->hud_params.y > 0.005f)))
		EnsureTargetSVP();
	// pip the lens content swaps at the scope edges, both taa histories seed from the current
	// frame so the stale scene never ghosts through the resolve
	if (svp_edge)
		ResetSVPHistory();
	svp_stats::cpu_end(svp_stats::CPU_MISC);
	// creation can fail at VRAM exhaustion, fall back to the single stock pass this frame
	if (svp && !TargetSVP)
		svp = false;
	// pip diag, an aimed weapon without an svp pass (y is nonzero even at hip, gate on x)
	{
		if (ps_r__svp_diag && Device.true_pip_on && !svp && g_pGamePersistent
			&& g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.05f)
		{
			static u32 s_ns_ms = 0;
			if (Device.dwTimeGlobal - s_ns_ms > 500)
			{
				s_ns_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-STATE] zoomed hud without an svp pass, active=%d target=%d",
					(int)Device.m_SecondViewport.IsSVPActive(), TargetSVP ? 1 : 0);
			}
		}
	}
	// pip [SVP-NVG] tuning diag, edge on generation change plus a slow heartbeat while active
	{
		extern Fvector4 ps_dev_param_7;
		extern Fvector4 ps_dev_param_8;
		static float s_nvg_gen = -1.f;
		static u32 s_nvg_ms = 0;
		const float genf = floorf(ps_dev_param_8.x);
		const bool edge = _abs(ps_dev_param_8.x - s_nvg_gen) > 0.001f;
		if (ps_r__svp_diag && (edge || (genf >= 1.f && Device.dwTimeGlobal - s_nvg_ms > 2000)))
		{
			s_nvg_gen = ps_dev_param_8.x;
			s_nvg_ms = Device.dwTimeGlobal;
			const float gain_off = floorf(ps_dev_param_8.w) / 10.f;
			PipMsg("[SVP-NVG] gen=%.0f tubes=%.1f gain_cur=%.2f gain_off=%.2f tm_scale=%.1f vig=%.2f r2nv=%d scoped=%d aim=%.2f p7=%.2f/%.2f/%.2f/%.2f",
				genf, (ps_dev_param_8.x - genf) * 10.f,
				floorf(ps_dev_param_8.y) / 10.f, gain_off, 10.f * gain_off,
				floorf(ps_dev_param_8.z) / 100.f, ps_r2_nightvision,
				(int)(Device.true_pip_on && Device.m_SecondViewport.IsSVPActive()),
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.x : 0.f,
				ps_dev_param_7.x, ps_dev_param_7.y, ps_dev_param_7.z, ps_dev_param_7.w);
		}
	}
	if (Device.true_pip_on)
		TargetMain->SetActive();
	svp_stats::section_begin(svp_stats::SEC_MAIN_GBUFFER);
	const u32 gbuffer_frame = Device.dwFrame; // grass builds its instance spans once per frame for both gbuffers
	const bool main_kept_graph = svp;
	renderGBuffer(!svp); // keep the priority-0 graph when an SVP pass follows
	svp_stats::section_end(svp_stats::SEC_MAIN_GBUFFER);
	if (svp)
	{
		// the scope pass needs this frame's camera, a stale one would replay the last aim's view
		auto& vp = Device.m_SecondViewport;
		if (vp.svp_camera_frame != Device.dwFrame || vp.svp_camera_session != vp.GetSVPSession())
			svp = false;
	}
	if (svp)
	{
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		EnsureTargetSVP();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);
		// a resize here can fail at VRAM exhaustion too
		if (!TargetSVP)
			svp = false;
	}
	if (main_kept_graph && !svp)
		drainKeptGraph();
	svp_stats::cpu_begin(svp_stats::CPU_HUD);
	TargetMain->svp_objective_hud_prepare(svp);
	svp_stats::cpu_end(svp_stats::CPU_HUD);
	if (svp)
	{
		// pip CPU-side cost probe for the SVP gbuffer, throttled log while r__svp_diag is on
		CTimer svp_t; svp_t.Start();
		const u32 calls0 = RCache.stat.calls;
		const u32 verts0 = RCache.stat.verts;
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		TargetSVP->SetActive();
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);
		R_ASSERT2(Device.dwFrame == gbuffer_frame, "svp gbuffer runs on a new frame, the grass instance spans are stale");
		svp_stats::section_begin(svp_stats::SEC_SVP_GBUFFER);
		renderGBuffer(true);
		svp_stats::section_end(svp_stats::SEC_SVP_GBUFFER);
		if (ps_r__svp_diag)
		{
			static u32 s_perf_ms = 0;
			if (Device.dwTimeGlobal - s_perf_ms > 1000)
			{
				s_perf_ms = Device.dwTimeGlobal;
				PipMsg("[SVP-PERF] gbuffer %.2fms calls %u verts %uk", svp_t.GetElapsed_sec() * 1000.f,
					RCache.stat.calls - calls0, (RCache.stat.verts - verts0) / 1000);
			}
		}
		svp_stats::cpu_begin(svp_stats::CPU_HUD);
		TargetMain->svp_objective_hud_report();
		svp_stats::cpu_end(svp_stats::CPU_HUD);
		svp_stats::cpu_begin(svp_stats::CPU_REPLAY);
		// pip stencil the dead corners after the native scope draw
		if (ps_r__svp_corner_mask && !RImplementation.o.dx10_msaa && TargetSVP)
			TargetSVP->stamp_svp_corner_mask();
		TargetMain->SetActive(); // shadow generation + main accumulation run on the main target
		svp_stats::cpu_end(svp_stats::CPU_REPLAY);

		// pip install the dual-accumulate hook, each shadow unit builds its map once on the main atlas
		// then this re-accumulates it into the SVP with no second shadow render
		Device.m_SecondViewport.dual_accum = [this](const std::function<void()>& accum)
		{
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			TargetSVP->SetActive();   // SVP gbuffer + accumulator (and, for now, the SVP shadow atlas)
			share_main_smaps();       // re-point the shadow atlas at the main maps the generation built
			Device.m_SecondViewport.force_svp_sss = (ps_r__svp_sss_sun != 0); // sun keeps the SSS contact term
			svp_stats::note_svp_blend();
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
			svp_stats::section_begin(svp_stats::SEC_SVP_LIGHTS);
			{ PIX_EVENT(SVP_ACCUM); accum(); } // SVP marginal lighting cost: accumulate this unit into the SVP (shared maps)
			svp_stats::section_end(svp_stats::SEC_SVP_LIGHTS);
			svp_stats::cpu_begin(svp_stats::CPU_LIGHTS);
			Device.m_SecondViewport.force_svp_sss = false;
			TargetMain->SetActive();  // restore for the next unit's generation on the main atlas
			svp_stats::cpu_end(svp_stats::CPU_LIGHTS);
		};
	}

	// single shared lighting pass, when svp the render_sun_cascades/render_lights dual-accumulate via
	// the hook and the SVP is combined + captured before the main combine composites it into the lens
	renderSceneLighting(bSUN, svp);
	Device.m_SecondViewport.dual_accum = nullptr;

	// pip close the whole-frame window then draw the stats panel over the finished image
	svp_stats::section_end(svp_stats::SEC_FRAME);
	svp_stats::frame_end(svp);
	svp_stats::draw_overlay();
}
