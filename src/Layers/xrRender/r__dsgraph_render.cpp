#include "stdafx.h"

#include "../../xrEngine/render.h"
#include "../../xrEngine/irenderable.h"
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

#include "../../Include/xrRender/ParticleCustom.h"

using namespace R_dsgraph;

extern float r_ssaDISCARD;
extern float r_ssaLOD_A; // lod-out threshold, the overlay counts main-view draws below it
extern float r_ssaDONTSORT;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

// pip svp stats overlay, tallies the cull skips only while the overlay is on
extern int ps_r__svp_stats;
extern u32 svp_stats_ssa_culled;
extern u32 svp_stats_cull_reject;
extern u32 svp_stats_lod_scale;
extern u32 svp_ledger_ssa_culled;
extern u32 svp_ledger_lod_scale;

ICF float calcLOD(float ssa/*fDistSq*/, float R)
{
	return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

// pip SVP LOD, scales the captured main-frustum ssa to the SVP's true pixel coverage,
// armed only for the SVP gbuffer pass
static float s_svp_ssa_scale = 1.f;
static bool s_svp_lod_on = false;
ICF float svp_ssa(float ssa) { return s_svp_lod_on ? ssa * s_svp_ssa_scale : ssa; }

// pip SVP small-object cull, skips items below an ssa threshold pre-scaled for the SVP
// coverage, 0 = off, armed only for the SVP gbuffer pass
static float s_svp_ssa_cull = 0.f;

// armed only around current frame objective HUD lists
static bool s_svp_objective_suppress_wpnfx = false;
static LPCSTR s_svp_objective_hud_bucket = nullptr;

static bool svp_exact_hud_pose_window()
{
#if RENDER == R_R4
	extern bool svp_objective_hud_current();
	return svp_objective_hud_current() && Device.true_pip_on;
#else
	return false;
#endif
}

static bool svp_mode2_hud_window()
{
#if RENDER == R_R4
	extern int scope_svp_enabled;
	auto& vp = Device.m_SecondViewport;
	return scope_svp_enabled >= 2 && Device.true_pip_on && vp.IsSVPFrame();
#else
	return false;
#endif
}

static bool svp_objective_hud_window()
{
#if RENDER == R_R4
	auto& vp = Device.m_SecondViewport;
	return svp_mode2_hud_window() && vp.svp_camera_frame == Device.dwFrame
		&& vp.svp_camera_session == vp.GetSVPSession()
		&& vp.svp_front_use_m > EPS;
#else
	return false;
#endif
}

static bool svp_reject_hud_window()
{
	return svp_mode2_hud_window() && !svp_objective_hud_window();
}

struct SSvpHudPosePass
{
#if RENDER == R_R4
	bool frozen;
	bool history;
#endif
	SSvpHudPosePass(bool active, bool write_history)
	{
#if RENDER == R_R4
		extern bool g_svp_hud_frozen_pass;
		extern bool g_svp_hud_history_write;
		frozen = g_svp_hud_frozen_pass;
		history = g_svp_hud_history_write;
		if (active)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = write_history;
		}
#endif
	}
	~SSvpHudPosePass()
	{
#if RENDER == R_R4
		extern bool g_svp_hud_frozen_pass;
		extern bool g_svp_hud_history_write;
		g_svp_hud_frozen_pass = frozen;
		g_svp_hud_history_write = history;
#endif
	}
};

// pip lod for the SVP weapon drain, the inline lod helpers stay reachable from the drain's own TU
float CDSGraphManager::svp_drain_lod(float ssa, float R) { return calcLOD(svp_ssa(ssa), R); }

void CDSGraphManager::svp_set_lod_scale(float s)
{
	s_svp_ssa_scale = s;
	s_svp_lod_on = (s < 0.999f); // active only when it actually lowers detail
	if (s_svp_lod_on && ps_r__svp_stats) ++svp_stats_lod_scale; // overlay proof the lod scale armed this frame
	if (s_svp_lod_on) svp_ledger_lod_scale = 1; // ledger ever-armed latch
}

// strength scales the LOD-out ssa, cov divides it back to a main-frustum ssa so the loop can test item.ssa
// directly. higher strength culls bigger items, lower cov (low mag) culls more (objects are tiny in the scope)
void CDSGraphManager::svp_set_ssa_cull(float strength, float cov)
{
	s_svp_ssa_cull = (strength > 0.f && cov > 1e-4f) ? (r_ssaGLOD_end * strength / cov) : 0.f;
}

template<typename T, bool Reverse>
void CDSGraphManager::r_dsgraph_render_graph_sorted(R_dsgraph::mapDSGraphItems<T, Reverse>& graph, bool _clear)
{
    if (graph.empty())
        return;

    std::sort(graph.begin(), graph.end());

	for (auto& item : graph)
	{
		if (svp_cull_reject(item.pVisual, item.pMatrix)) { if (ps_r__svp_stats) ++svp_stats_cull_reject; continue; } // pip skip off-cone SVP geometry
		if (s_svp_ssa_cull > 0.f && !item.pMatrix && item.ssa < s_svp_ssa_cull) { if (ps_r__svp_stats) ++svp_stats_ssa_culled; if (!svp_ledger_ssa_culled) svp_ledger_ssa_culled = 1; continue; } // pip skip tiny STATIC clutter only (null matrix), never dynamic NPCs/items (matrix carriers)
		if (ps_r__svp_stats && !Device.m_SecondViewport.m_render_pass_is_svp && item.ssa < r_ssaLOD_A) ++svp_stats_tiny; // overlay count only, main view
		dxRender_Visual* V = item.pVisual;
		VERIFY(V && V->shader._get());
		if (s_svp_objective_suppress_wpnfx)
		{
			SSvpHudAdmission admission;
			svp_classify_objective_hud(V, item.pMatrix, item.hud_role, admission);
			extern int ps_r__svp_cop_diag;
			static u32 s_admission_diag_ms = 0;
			if (admission.candidate && ps_r__svp_cop_diag
				&& Device.dwTimeGlobal - s_admission_diag_ms > 3000)
			{
				s_admission_diag_ms = Device.dwTimeGlobal;
				auto texture = V->GetTexture();
				PipMsg("[SVP-ADMIT] %s queue=%s role=%u t=%.2f rad=%.1fcm R=%.1fcm reason=%s visual=%p root=%p lensVisual=%p lensRoot=%p %s",
					admission.reject ? "SUPPRESS" : "retain",
					s_svp_objective_hud_bucket ? s_svp_objective_hud_bucket : "sorted",
					item.hud_role,
					admission.objective > EPS ? admission.axial / admission.objective : 0.f,
					admission.radial * 100.f, admission.radius * 100.f, admission.reason,
					(void*)V, (void*)item.pMatrix,
					Device.m_SecondViewport.svp_lens_visual,
					Device.m_SecondViewport.svp_lens_root,
					texture ? texture->cName.c_str() : "?");
			}
			if (admission.reject)
				continue;
			IParticleCustom* particle = V->dcast_ParticleCustom();
			if (particle && particle->GetHudMode() && particle->GetWeaponFX())
				continue;
		}
		RCache.set_Element(item.pSE);
		// hud matrices resolve through the pose latch, everything else passes through untouched
		RCache.set_xform_world(*svp_pose_of(item.pMatrix));
		RImplementation.apply_object(item.pObject);
		RImplementation.apply_lmaterial();
		//if (item.b_hud_mode)
		//{
		//	//new feature
		//}
		V->Render(calcLOD(svp_ssa(item.ssa), V->vis.sphere.R));
#if RENDER == R_R4
		if (s_svp_objective_suppress_wpnfx)
		{
			extern void svp_objective_hud_note_draw(u8);
			svp_objective_hud_note_draw(item.hud_role);
		}
#endif
	}

	if (_clear)
		graph.clear();

	RCache.set_xform_world(Fidentity);
}

void CDSGraphManager::r_dsgraph_render_graph(RenderQueueArray& queues, u32 _priority, bool _clear, bool static_geometry)
{
	RCache.set_xform_world(Fidentity);

	for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
	{
		auto& queue = queues[_priority][iPass];
		if (queue.empty())
			continue;

		// 1. Sort by generated sort key to replicate previous fixed map behaviour
		if (queue.size() < 4096)
            std::sort(queue.begin(), queue.end());
		else
            xr_parallel_sort(queue.begin(), queue.end());

		// 2. Render
		vs_type pVS = nullptr;

#if defined(USE_DX10) || defined(USE_DX11)
		gs_type pGS = nullptr;
#endif

		ps_type pPS = nullptr;

#ifdef USE_DX11
		hs_type pHS = nullptr;
		ds_type pDS = nullptr;
#endif
		
		R_constant_table* pCS = nullptr;
		ID3DState* pState = nullptr;
		STextureList* pTextures = nullptr;

        u64 high = 0;

        for (auto& packet : queue)
        {
            if (svp_cull_reject(packet.item.pVisual, packet.item.pMatrix)) { if (ps_r__svp_stats) ++svp_stats_cull_reject; continue; } // pip skip off-cone SVP geometry
            if (s_svp_ssa_cull > 0.f && !packet.item.pMatrix && packet.item.ssa < s_svp_ssa_cull) { if (ps_r__svp_stats) ++svp_stats_ssa_culled; if (!svp_ledger_ssa_culled) svp_ledger_ssa_culled = 1; continue; } // pip skip tiny STATIC clutter only (null matrix), never dynamic NPCs/items (matrix carriers)
            if (ps_r__svp_stats && !Device.m_SecondViewport.m_render_pass_is_svp && packet.item.ssa < r_ssaLOD_A) ++svp_stats_tiny; // overlay count only, main view
            auto& currentKey = packet.sortKey;
            if (currentKey.high != high)
            {
                high = currentKey.high;

                if (packet.pState != pState)
                {
                    pState = packet.pState;
                    RCache.set_States(pState);
                }

#if defined(USE_DX10) || defined(USE_DX11)
                if (packet.pGS != pGS)
                {
                    pGS = packet.pGS;
                    RCache.set_GS(pGS);
                }
#endif

#ifdef USE_DX11
                if (packet.pHS != pHS)
                {
                    pHS = packet.pHS;
                    RCache.set_HS(pHS);
                }
                if (packet.pDS != pDS)
                {
                    pDS = packet.pDS;
                    RCache.set_DS(pDS);
                }
#endif
            }

            // Compare low key stuff regardless, too high collision probability
            if (packet.pVS != pVS)
            {
                pVS = packet.pVS;
                RCache.set_VS(pVS);
            }

            if (packet.pPS != pPS)
            {
                pPS = packet.pPS;
                RCache.set_PS(pPS);
            }

            if (packet.pCS != pCS)
            {
                pCS = packet.pCS;
                RCache.set_Constants(pCS);
            }

            if (packet.pTextures != pTextures)
            {
                pTextures = packet.pTextures;
                RCache.set_Textures(pTextures);
                RImplementation.apply_lmaterial();
            }

			auto& item = packet.item;
			if (!static_geometry)
			{
				RCache.set_xform_world(*item.pMatrix);
				RImplementation.apply_object(item.pObject);
				RImplementation.apply_lmaterial();
			}

			float LOD = calcLOD(svp_ssa(item.ssa), item.pVisual->vis.sphere.R);
#ifdef USE_DX11
			RCache.LOD.set_LOD(LOD);
#endif
			item.pVisual->Render(LOD);
		}

		if (_clear)
			queue.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void CDSGraphManager::r_dsgraph_render_hud(bool _clear)
{
	PROF_EVENT("r_dsgraph_render_hud");
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapHUD, _clear);

	RImplementation.rmNormal();

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes 		
	if (scope_3D_fake_enabled)
	{
		RCache.set_RT(RImplementation.Target->rt_ssfx_temp->pRT, 3); // Render scope_3D to any buffer

		r_dsgraph_render_graph_sorted(RGraph.mapScopeHUD);

		if (!RImplementation.o.ssfx_motionvectors)
			RCache.set_RT(NULL, 3);
		else
			RCache.set_RT(RImplementation.Target->rt_ssfx_motion_vectors->pRT, 3);
	}
#endif

	if (RGraph.mapCamAttached.size())
	{
		RImplementation.rmNear();

		// Change projection
		initializer.SetCamMode();

		// Rendering
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttached);

		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_render_hud_ui()
{
	PROF_EVENT("r_dsgraph_render_hud_ui");
	CHudInitializer initializer(true);

	RImplementation.rmNear();
	g_hud->RenderActiveItemUI();
	RImplementation.rmNormal();
}

void CDSGraphManager::r_dsgraph_render_cam_ui()
{
	PROF_EVENT("r_dsgraph_render_cam_ui");

	// Change projection
	CHudInitializer initializer(2);
	
	// Rendering
	RImplementation.rmNear();
	g_hud->RenderCamAttachedUI();
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_sorted(bool render_hud)
{
	{
		PROF_EVENT("r_dsgraph_render_sorted");
		// combine order, only the last pass (main) clears the shared sorted list, else main loses translucents
		const bool clear_sorted = svp_clear_shared_list(true);
		r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Sorted, clear_sorted);
		r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Sorted, clear_sorted);
	}

	if (render_hud)
		r_dsgraph_render_sorted_hud();

	// Camera Script Attachments
	if (RGraph.mapCamAttachedSorted.Sorted.size())
	{
		RImplementation.rmNear();
		// Change projection
		CHudInitializer initializer(2);

		// Rendering
		// combine order, main clears last so cam-attached overlays survive the scope pass
		r_dsgraph_render_graph_sorted(RGraph.mapCamAttachedSorted.Sorted, svp_clear_shared_list(true));
		RImplementation.rmNormal();
	}
}

void CDSGraphManager::r_dsgraph_capture_hud()
{
	if (g_hud)
	{
		g_hud->Render_Last(dcast_IPortalTraverser());
		set_Object();
	}
}

#if defined(USE_DX11)
//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_ScopeSorted()  //  Redotix99: for 3D Shader Based Scopes 	
{
	// Change projection
	CHudInitializer initializer(true);

	// Rendering
	RImplementation.rmNear();
	r_dsgraph_render_graph_sorted(RGraph.mapScopeHUDSorted, true);
	RImplementation.rmNormal();
}
#endif

void CDSGraphManager::r_dsgraph_render_sorted_hud()
{
	PROF_EVENT("r_dsgraph_render_sorted_hud");
#if	RENDER==R_R4
	{
		// guard the seed CopyResource, it is a silent no-op + debug-layer spam when accum and generic0
		// formats mismatch (MSAA vs 1x, or HDR-off A8 vs A16F)
		auto* dst = (ID3DTexture2D*)RImplementation.Target->rt_Accumulator->pSurface;
		auto* src = (ID3DTexture2D*)RImplementation.Target->rt_Generic_0->pSurface;
		D3D_TEXTURE2D_DESC dd, sd;
		dst->GetDesc(&dd);
		src->GetDesc(&sd);
		if (dd.Format == sd.Format && dd.SampleDesc.Count == sd.SampleDesc.Count && dd.Width == sd.Width && dd.Height == sd.Height)
			HW.pContext->CopyResource(dst, src);
	}
#endif
	const bool clear_hud = svp_clear_shared_list(true);
	if (svp_reject_hud_window())
	{
		if (clear_hud)
			RGraph.mapHUDSorted.Sorted.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initializer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	// combine order, main clears last so hud-sorted overlays survive the scope pass
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "sorted" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, clear_hud);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_emissive(bool clear, bool renderHUD)
{
	PROF_EVENT("r_dsgraph_render_emissive");
#if	RENDER!=R_R1
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Emissive, clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Emissive, clear);
	if (svp_reject_hud_window())
	{
		if (clear)
			RGraph.mapHUDSorted.Emissive.clear();
		return;
	}
	
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initializer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "emissive" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Emissive, clear);
	
	if (renderHUD)
	{
		s_svp_objective_hud_bucket = objective_hud ? "sorted" : nullptr;
		r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Sorted, false);
	}
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;

	RImplementation.rmNormal();
#endif
}

void CDSGraphManager::r_dsgraph_render_water_ssr()
{
#ifdef USE_DX11
	PROF_EVENT("r_dsgraph_render_water_ssr");
	std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
	for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

		RCache.set_Shader(RImplementation.Target->s_ssfx_water_ssr);

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		RCache.set_c("cam_pos", RImplementation.Target->Position_previous.x, RImplementation.Target->Position_previous.y, RImplementation.Target->Position_previous.z, 0.0f);

		// Previous matrix data
		RCache.set_c("m_current", RImplementation.Target->Matrix_current);
		RCache.set_c("m_previous", RImplementation.Target->Matrix_previous);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
#endif
}

void CDSGraphManager::r_dsgraph_render_water(bool clearGraph)
{
	PROF_EVENT("r_dsgraph_render_water_ssr");
    std::sort(RGraph.mapWater.begin(), RGraph.mapWater.end());
    for (auto& N : RGraph.mapWater)
	{
		dxRender_Visual* V = N.pVisual;
		VERIFY(V);

#ifdef USE_DX11
		if (RImplementation.o.ssfx_water)
		{
			RCache.set_Shader(RImplementation.Target->s_ssfx_water);
		}
#endif

		RCache.set_xform_world(*N.pMatrix);
		RImplementation.apply_object(N.pObject);
		RImplementation.apply_lmaterial();

		// Wind settings
		float WindDir = g_pGamePersistent->Environment().CurrentEnv->wind_direction;
		float WindVel = g_pGamePersistent->Environment().CurrentEnv->wind_velocity;
		RCache.set_c("wind_setup", WindDir, WindVel, 0, 0);

		V->Render(calcLOD(N.ssa, V->vis.sphere.R));
	}
	// pip keep the shared captured water list for the next viewport, only the final (main) pass clears
	// it, the SVP combine runs first and must not consume mapWater or the main loses its periphery water
	if (clearGraph)
		RGraph.mapWater.clear();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_wmarks()
{
	PROF_EVENT("r_dsgraph_render_wmarks");
#if	RENDER!=R_R1
	// gbuffer order, main drains wmarks first so the last pass (SVP when scoped) clears
	const bool clear_wmark = svp_clear_shared_list(false);
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Wmark, clear_wmark);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Wmark, clear_wmark);
	if (svp_reject_hud_window())
	{
		if (clear_wmark)
			RGraph.mapHUDSorted.Wmark.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initalizer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "wmark" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Wmark, clear_wmark);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void CDSGraphManager::r_dsgraph_render_distort()
{
	PROF_EVENT("r_dsgraph_render_distort");
	// combine order, main clears distort last, else main loses distortion + the bDistort-gated screen filters
	const bool _clear = svp_clear_shared_list(true);
	// Rendering
	r_dsgraph_render_graph_sorted(RGraph.mapStaticSorted.Distort, _clear);
	r_dsgraph_render_graph_sorted(RGraph.mapDynamicSorted.Distort, _clear);
	// pip the muzzle heat billboard sits on the entrance pupil in the scope pass and warps the whole
	// image, world distortion above stays, the hud layer is skipped
	if (ps_r__svp_skip_hud_distort && Device.true_pip_on && Device.m_SecondViewport.m_render_pass_is_svp)
		return;
	if (svp_reject_hud_window())
	{
		if (_clear)
			RGraph.mapHUDSorted.Distort.clear();
		return;
	}
	const bool objective_hud = svp_objective_hud_window();
	const bool frozen_hud = svp_exact_hud_pose_window();
	CHudInitializer initalizer(!objective_hud);
	SSvpHudPosePass pose(frozen_hud, true);
	if (objective_hud)
		RImplementation.rmNormal();
	else
		RImplementation.rmNear();
	s_svp_objective_suppress_wpnfx = objective_hud;
	s_svp_objective_hud_bucket = objective_hud ? "distort" : nullptr;
	r_dsgraph_render_graph_sorted(RGraph.mapHUDSorted.Distort, _clear);
	s_svp_objective_suppress_wpnfx = false;
	s_svp_objective_hud_bucket = nullptr;
	RImplementation.rmNormal();
}

#include"LightTrack.h"
void CDSGraphManager::r_dsgraph_capture_static()
{
	PROF_EVENT("r_dsgraph_capture_static")
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		// Traverse sector/portal structure
		if (psDeviceFlags.test(rsDrawStatic))
		{
			if (dbg)
			{
				const u32 sector_nodes = u32(m_sector_frustums.size());
				dbg->static_sector_nodes += sector_nodes;
				if (opt_bucket)
					dbg->static_sector_nodes_opt += sector_nodes;
				else
					dbg->static_sector_nodes_noopt += sector_nodes;
			}

			// Determine visibility for static geometry hierrarhy
			for (auto& pair : m_sector_frustums)
			{
				xr_vector<CFrustum>& frustums = pair.val.first;
				if (frustums.empty())
					continue;

				if (frustums.size() == 1)
				{
					if (dbg)
					{
						++dbg->static_frustum_nodes;
						++dbg->static_add_root_calls;
						if (opt_bucket)
						{
							++dbg->static_frustum_nodes_opt;
							++dbg->static_add_root_calls_opt;
						}
						else
						{
							++dbg->static_frustum_nodes_noopt;
							++dbg->static_add_root_calls_noopt;
						}
					}
					add_Static((IRenderVisual*)pair.key->root(), frustums.front(), frustums.front().getMask());
					continue;
				}

				xr_vector<u32> masks;
				masks.reserve(frustums.size());
				for (CFrustum& frustum_node : frustums)
					masks.push_back(frustum_node.getMask());

				if (dbg)
				{
					dbg->static_frustum_nodes += u32(frustums.size());
					++dbg->static_add_root_calls;
					if (opt_bucket)
					{
						dbg->static_frustum_nodes_opt += u32(frustums.size());
						++dbg->static_add_root_calls_opt;
					}
					else
					{
						dbg->static_frustum_nodes_noopt += u32(frustums.size());
						++dbg->static_add_root_calls_noopt;
					}
				}

				add_Static_MultiFrustum((IRenderVisual*)pair.key->root(), frustums, masks);
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_lights()
{
	PROF_EVENT("r_dsgraph_capture_lights")
	g_SpatialSpaceLights->q_frustum
	(
		lstLights,
		ISpatial_DB::O_ORDERED,
		STYPE_LIGHTSOURCE,
		i_frustum
	);

#if	RENDER==R_R1
	std::sort(lstLights.begin(), lstLights.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
	{
		if (!_1.get() || !_2.get()) return false;

		return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
	});
#endif

	for (ISpatialShared spatial : lstLights)
	{
		if (0 == spatial) continue; spatial->spatial_updatesector();
		CSector* sector = (CSector*)spatial->spatial.sector;
		if (0 == sector) continue;

		if (!RImplementation.HOM.visible(spatial->spatial.sphere)) continue;

		if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
		{
			// lightsource
			if (light* L = (light*)(spatial->dcast_Light()))
			{
#if	RENDER==R_R1
				RImplementation.L_DB->add_light(L);
#else
				if (L->get_LOD() > ps_r2_shadow_lod_min && L->has_light_visible_from_sectors(*this))
				{
					RImplementation.Lights.add_light(L);
				}
#endif
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture_dynamic(CObject* O)
{
	PROF_EVENT("r_dsgraph_capture_dynamic")
	const bool dbg_enabled = PortalTraverseDbg_Enabled();
	PortalTraverseDebugStats* dbg = dbg_enabled ? &PortalTraverseDbg_Get() : nullptr;
	const bool opt_bucket = PortalTraverseDbg_IsOptions(i_options);

	if (i_start)
	{
		if (psDeviceFlags.test(rsDrawDynamic))
		{
			// Traverse object database
			g_SpatialSpace->q_frustum
			(
				lstRenderables,
				ISpatial_DB::O_ORDERED,
				i_doptions,
				i_frustum
			);
			if (dbg)
			{
				const u32 spatial_count = u32(lstRenderables.size());
				dbg->dynamic_spatials += spatial_count;
				if (opt_bucket)
					dbg->dynamic_spatials_opt += spatial_count;
				else
					dbg->dynamic_spatials_noopt += spatial_count;
			}
			set_Object();
#if	RENDER==R_R1
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				std::sort(lstRenderables.begin(), lstRenderables.end(), [](const ISpatialShared& _1, const ISpatialShared& _2) noexcept
				{
					if (!_1.get() || !_2.get()) return false;

					return	_1->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved) < _2->spatial.sphere.P.distance_to_sqr(Device.vCameraPosition_saved);
				});

				if (ps_actor_shadow_flags.test(1))
					g_hud->Render_First(dcast_IPortalTraverser());

				r_dsgraph_capture_hud();
			}
#endif
			u32 uID_LTRACK = u32(-1);
			if (i_mask[CDSGraphManager::fl_normal])//normal phase
			{
				// update light-vis for current entity / actor
				if (CObject* O = g_pGameLevel->CurrentViewEntity())
				{
					if (!O->getDestroy())
					{
						if (CROS_impl* R = (CROS_impl*)O->ROS())
							R->update(O);
					}
				}

				RImplementation.uLastLTRACK++;
				if (!lstRenderables.empty())
				{
					uID_LTRACK = RImplementation.uLastLTRACK % lstRenderables.size();
#if	RENDER!=R_R1
					// update light-vis for selected entity
					// track lighting environment
					if (IRenderable* renderable = (IRenderable*)lstRenderables[uID_LTRACK]->dcast_Renderable())
					{
						if (CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
							T->update(renderable);
					}
#endif
				}

			}

			// Determine visibility for dynamic part of scene
			for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
			{
				ISpatialShared spatial = lstRenderables[o_it];
				if (0 == spatial) continue;
				CSector* sector = (CSector*)spatial->spatial.sector;
				if (0 == sector) continue;

				if (i_mask[CDSGraphManager::fl_normal] && !RImplementation.HOM.visible(spatial->spatial.sphere))
					continue;

#if	RENDER==R_R1
				if ((spatial->spatial.type & STYPE_GLOW))
				{
					if (CGlow* glow = spatial->dcast_CGlow())
					{
						// It may be an glow
						RImplementation.L_Glows->add(glow);
					}
					continue;
				}
#endif

//				if ((spatial->spatial.type & STYPE_LIGHTSOURCE))
//				{
//					// lightsource
//					if (light* L = (light*)(spatial->dcast_Light()))
//					{
//#if	RENDER==R_R1
//						RImplementation.L_DB->add_light(L);
//#else
//						if (L->get_LOD() > EPS_L && L->has_light_visible_from_sectors(PT))
//						{
//							RImplementation.Lights.add_light(L);
//						}
//#endif
//					}
//					continue;
//				}

				if(!(spatial->spatial.type & STYPE_RENDERABLE) && !(spatial->spatial.type & STYPE_PARTICLE) && !(spatial->spatial.type & STYPE_RENDERABLESHADOW))
					continue;
				if (!is_sector_visible(sector))
					continue;

				for (CFrustum& frustum : m_sector_frustums.find(sector)->val.first)
				{
					if (dbg)
					{
						++dbg->dynamic_frustum_tests;
						if (opt_bucket)
							++dbg->dynamic_frustum_tests_opt;
						else
							++dbg->dynamic_frustum_tests_noopt;
					}

					if (frustum.testSphere_dirty(spatial->spatial.sphere.P, spatial->spatial.sphere.R))
					{
						if (dbg)
						{
							++dbg->dynamic_frustum_hits;
							if (opt_bucket)
								++dbg->dynamic_frustum_hits_opt;
							else
								++dbg->dynamic_frustum_hits_noopt;
						}

						// renderable
						IRenderable* renderable = spatial->dcast_Renderable();
						if (0 == renderable) break;

						if (O && O->dcast_Renderable() == renderable) break;

						// Rendering
#if	RENDER==R_R1
						if (i_mask[CDSGraphManager::fl_normal] && o_it == uID_LTRACK && renderable->renderable_ROS())
						{
							// track lighting environment
							if(CROS_impl* T = (CROS_impl*)renderable->renderable_ROS())
								T->update(renderable);
						}
#endif
						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object(renderable);

						renderable->renderable_Render(dcast_IPortalTraverser());
						if (dbg)
						{
							++dbg->dynamic_rendered;
							if (opt_bucket)
								++dbg->dynamic_rendered_opt;
							else
								++dbg->dynamic_rendered_noopt;
						}

						if (i_mask[CDSGraphManager::fl_normal] && !(spatial->spatial.type & STYPE_PARTICLE))
							set_Object();
						break;
					}
				}
			}
		}
	}
}

void CDSGraphManager::r_dsgraph_capture(bool lights, bool dynamic, CObject* O)
{
	PROF_EVENT("r_dsgraph_capture")
	r_dsgraph_capture_static();

	if(lights)
		r_dsgraph_capture_lights();

	if (dynamic)
		r_dsgraph_capture_dynamic(O);
}
