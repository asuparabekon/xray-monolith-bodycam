#include "stdafx.h"
#include "r4.h"
#include "svp_stats.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/GameFont.h"
#include "../../Include/xrRender/UIRender.h"
#include "../../Include/xrRender/UIShader.h"
#include "../../Include/xrRender/FactoryPtr.h"

// gate cvar + the shared cull counters, all defined in svp_console.cpp
extern int ps_r__svp_stats;
extern u32 svp_stats_ssa_culled;
extern u32 svp_stats_cull_reject;
extern u32 svp_stats_cull_reject_ident;
extern u32 svp_stats_lights_mirrored;
extern u32 svp_stats_lights_skipped;
extern u32 svp_stats_taa_stamp;
extern u32 svp_stats_nvg_split;
extern u32 svp_stats_lod_scale;
extern u32 svp_stats_hud_cull_reject;
extern u32 svp_stats_grass_cull_reject;
extern u32 svp_stats_reflex_capture;
extern u32 svp_stats_distort_guard;
extern u32 svp_stats_nvg_sky;
extern u32 svp_stats_disc_latch;
extern u32 svp_stats_fwd_keep;
extern u32 svp_stats_optic_resolve;
extern u32 svp_stats_lean_flags;
extern u32 svp_stats_copies;
extern u32 svp_stats_copy_kb;
extern u32 svp_stats_copy_kb_cat[3];
extern void (*svp_copy_timer_hook)(u32 cat, bool begin);
extern u32 svp_stats_tiny;
extern u32 svp_stats_shadow;
// live screen-space pass configuration, defined in xrRender_console.cpp
extern Fvector4 ps_ssfx_ao;
extern Fvector4 ps_ssfx_il;
extern Fvector4 ps_ssfx_ssr;
extern int ps_ssfx_ao_quality;
extern int ps_ssfx_il_quality;
extern int ps_ssfx_ssr_quality;
// sss weapon dof drive plus the nvg flag that forces the beefs dof variant
extern Fvector4 ps_ssfx_wpn_dof_1;
extern float ps_ssfx_wpn_dof_2;
extern Fvector4 ps_dev_param_8;
extern int ps_r__svp_wpn_dof;
extern int ps_r__svp_diag;
// adaptive-res grow gate, defined in svp_console.cpp
extern int ps_r__svp_adaptive_grow;
// lean post gate, defined in svp_console.cpp
extern int ps_r__pp_lean;
// engine scope magnification, defined in xrRender_console.cpp
extern float g_pip_scope_magnification;

namespace
{
	using namespace svp_stats;

	// gpu-ms color thresholds, cell turns yellow past warn, red past crit
	const double GPU_WARN_MS = 3.0;
	const double GPU_CRIT_MS = 6.0;
	const double AVG_TAU_SEC = 1.0;   // rolling-average time constant for the panel numbers
	const double ROW_MIN_MS = 0.005;  // a row prints once its average clears this

	const u32 RING = 4;         // frames of query latency, readback is non-blocking on the oldest
	const u32 MAX_PAIRS = 128;  // timestamp pairs per frame, past this the frame flags overflow
	const u32 FOOT_MAX = 10;    // free-form footer lines the panel sizes its width to

	struct sec_data
	{
		double cpu_ms;
		double gpu_ms;
		u32 calls, verts, polys;
	};

	struct stats_frame
	{
		sec_data sec[SEC_COUNT];
		u32 main_lights, main_shadowed, svp_blends, sun_passes, ssa_culled;
		u32 cull_reject, cull_reject_ident, lights_mirrored, lights_skipped;
		u32 taa_stamp, nvg_split;
		u32 lod_scale, hud_cull_reject, grass_cull_reject, reflex_capture;
		u32 distort_guard, nvg_sky, disc_latch, fwd_keep;
		u32 svp_w, svp_h;
		u32 svp_epoch, optic_resolve;
		float svp_disc, svp_disc_learned, svp_mag;
		bool svp_grow;
		double frame_ms;
		bool svp_active;
		float sun_shafts; // weather sunshafts intensity, the phase_sunshafts early-out reads the same value
		u32 lean_flags;
		bool lean_on;
		u32 copies, copy_kb, rtsw, shadow, tiny, smap;
		u32 copy_kb_cat[3];
	};

	// pipe-panel quantities that carry a rolling average, order matches the rows
	enum { PIPE_COPIES = 0, PIPE_COPY_KB, PIPE_RTSW, PIPE_SHADOW, PIPE_TINY,
		PIPE_CP_HIST_KB, PIPE_CP_TAIL_KB, PIPE_CP_SCENE_KB, PIPE_COUNT };

	struct frame_slot
	{
		ID3D11Query* disjoint;
		ID3D11Query* ts[MAX_PAIRS * 2];
		u8 sec_of_pair[MAX_PAIRS];
		u32 pair_count;
		bool overflow;
		bool in_flight;
		stats_frame data;
	};

	bool s_created = false;
	u32 s_create_retry_ms = 0;
	frame_slot s_frames[RING];
	u32 s_frame_no = 0;
	frame_slot* s_cur = nullptr;

	// per-section transient begin snapshots, a section never re-enters itself so one slot each
	CTimer s_sec_timer[SEC_COUNT];
	u32 s_sec_calls0[SEC_COUNT];
	u32 s_sec_verts0[SEC_COUNT];
	u32 s_sec_polys0[SEC_COUNT];
	int s_sec_open_pair[SEC_COUNT];

	// last fully-resolved frame, what the panel draws
	stats_frame s_snap;
	bool s_snap_valid = false;

	// per-section rolling gpu average, advanced only on frames that resolved valid timestamps
	double s_sec_avg[SEC_COUNT];
	double s_pipe_avg[PIPE_COUNT];
	bool s_avg_seeded = false;

	CTimer s_frame_timer;
	bool s_frame_timer_started = false;

	// rolling ~1s frame-time window for the min/avg/max spike readout, each sample tagged with dwTimeGlobal
	const u32 FT_WIN = 512;
	float s_ft_ms[FT_WIN];
	u32 s_ft_time[FT_WIN];
	u32 s_ft_head = 0;

	CGameFont* s_font = nullptr;
	FactoryPtr<IUIShader>* s_shader = nullptr;

	// effective sss weapon dof lanes, the binder feeds zeros while a scope suppresses weapon dof
	struct dof_state { float x, y, z, w, p; bool nvg, heavy; };

	dof_state dof_now()
	{
		dof_state d;
		const bool off = (ps_r__svp_wpn_dof == 0) && Device.true_pip_on
			&& Device.m_SecondViewport.IsSVPActive();
		d.x = off ? 0.f : ps_ssfx_wpn_dof_1.x;
		d.y = off ? 0.f : ps_ssfx_wpn_dof_1.y;
		d.z = off ? 0.f : ps_ssfx_wpn_dof_1.z;
		d.w = off ? 0.f : ps_ssfx_wpn_dof_1.w;
		d.p = off ? 0.f : ps_ssfx_wpn_dof_2;
		d.nvg = (ps_dev_param_8.x >= 1.f);
		// the 16 tap loop runs where blur_w exceeds zero and every path into it scales by w
		d.heavy = (d.w > 0.f) || d.nvg;
		return d;
	}

	// one line per cheap to expensive crossing, values that never cross the boundary stay silent
	void dof_state_check()
	{
		if (ps_r__svp_stats == 0 && ps_r__svp_diag == 0)
			return;
		const dof_state d = dof_now();
		const int now = d.heavy ? 1 : 0;
		static int s_last = -1;
		static u32 s_last_ms = 0;
		if (now == s_last)
			return;
		// rate cap only, the next frame retries so a real crossing is never swallowed
		if (s_last >= 0 && Device.dwTimeGlobal - s_last_ms < 500)
			return;
		s_last = now;
		s_last_ms = Device.dwTimeGlobal;
		PipMsg("[SVP-DOF] %s w=%.3f z=%.3f x=%.3f y=%.3f p=%.3f nvg=%d frame=%u",
			d.heavy ? "EXPENSIVE" : "cheap", d.w, d.z, d.x, d.y, d.p, d.nvg ? 1 : 0, Device.dwFrame);
	}

	// hook body for the shared copy sites, maps their category onto our accumulating sections
	void copy_timer(u32 cat, bool begin)
	{
		if (cat >= 3)
			return;
		const section_e s = section_e(SEC_CP_HIST + cat);
		if (begin) section_begin(s); else section_end(s);
	}

	bool ensure_created()
	{
		if (s_created)
			return true;
		if (!HW.pDevice)
			return false;
		if (s_create_retry_ms && Device.dwTimeGlobal < s_create_retry_ms)
			return false;
		D3D11_QUERY_DESC dd = {}; dd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		D3D11_QUERY_DESC td = {}; td.Query = D3D11_QUERY_TIMESTAMP;
		for (u32 f = 0; f < RING; ++f)
		{
			frame_slot& s = s_frames[f];
			s.disjoint = nullptr;
			s.pair_count = 0; s.overflow = false; s.in_flight = false;
			if (FAILED(HW.pDevice->CreateQuery(&dd, &s.disjoint)))
				goto create_failed;
			for (u32 i = 0; i < MAX_PAIRS * 2; ++i)
			{
				s.ts[i] = nullptr;
				if (FAILED(HW.pDevice->CreateQuery(&td, &s.ts[i])))
					goto create_failed;
			}
		}
		// stat_font is the engine stats face, device-independent so it tracks screen height
		s_font = xr_new<CGameFont>("stat_font", CGameFont::fsDeviceIndependent);
		s_shader = xr_new<FactoryPtr<IUIShader>>();
		// ui_console is an opaque white 32x32, hud_default multiplies it by the vertex color so the
		// backing fill is the vertex color, ui_empty is fully transparent and drew nothing
		(*s_shader)->create("hud\\default", "ui\\ui_console");
		s_created = true;
		s_create_retry_ms = 0;
		s_snap_valid = false;
		s_frame_no = 0;
		s_ft_head = 0;
		s_avg_seeded = false;
		ZeroMemory(s_sec_avg, sizeof(s_sec_avg)); // no stale averages from a prior session
		ZeroMemory(s_pipe_avg, sizeof(s_pipe_avg));
		svp_copy_timer_hook = &copy_timer; // shared copy sites can reach the query pool now
		ZeroMemory(s_ft_time, sizeof(s_ft_time)); // drop any stale window samples from a prior session
		return true;

	create_failed:
		s_create_retry_ms = Device.dwTimeGlobal + 1000;
		for (u32 f = 0; f < RING; ++f)
		{
			frame_slot& s = s_frames[f];
			_RELEASE(s.disjoint);
			for (u32 i = 0; i < MAX_PAIRS * 2; ++i)
				_RELEASE(s.ts[i]);
		}
		return false;
	}

	bool resolve_slot(frame_slot& s)
	{
		if (!s.in_flight)
			return true;

		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
		if (HW.pContext->GetData(s.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
			return false;

		UINT64 timestamp[MAX_PAIRS * 2] = {};
		for (u32 p = 0; p < s.pair_count * 2; ++p)
		{
			if (HW.pContext->GetData(s.ts[p], &timestamp[p], sizeof(timestamp[p]),
				D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
				return false;
		}

		s.in_flight = false;
		if (dj.Disjoint || dj.Frequency == 0)
			return true; // clock skipped this frame, drop it

		for (u32 i = 0; i < SEC_COUNT; ++i)
			s.data.sec[i].gpu_ms = 0.0;
		for (u32 p = 0; p < s.pair_count; ++p)
		{
			const UINT64 t0 = timestamp[p * 2];
			const UINT64 t1 = timestamp[p * 2 + 1];
			if (t1 > t0)
				s.data.sec[s.sec_of_pair[p]].gpu_ms += double(t1 - t0) * 1000.0 / double(dj.Frequency);
		}
		s_snap = s.data;
		s_snap_valid = true;

		// alpha from the frame delta so the ~1s constant holds at any frame rate, first frame seeds outright
		const double pipe_raw[PIPE_COUNT] = {
			double(s.data.copies), double(s.data.copy_kb), double(s.data.rtsw),
			double(s.data.shadow), double(s.data.tiny),
			double(s.data.copy_kb_cat[0]), double(s.data.copy_kb_cat[1]), double(s.data.copy_kb_cat[2])
		};
		const float dt = Device.fTimeDelta;
		if (!s_avg_seeded)
		{
			for (u32 i = 0; i < SEC_COUNT; ++i)
				s_sec_avg[i] = s.data.sec[i].gpu_ms;
			for (u32 i = 0; i < PIPE_COUNT; ++i)
				s_pipe_avg[i] = pipe_raw[i];
			s_avg_seeded = true;
		}
		else if (dt > 0.f)
		{
			const double a = 1.0 - exp(-double(dt) / AVG_TAU_SEC);
			for (u32 i = 0; i < SEC_COUNT; ++i)
				s_sec_avg[i] += a * (s.data.sec[i].gpu_ms - s_sec_avg[i]);
			for (u32 i = 0; i < PIPE_COUNT; ++i)
				s_pipe_avg[i] += a * (pipe_raw[i] - s_pipe_avg[i]);
		}
		return true;
	}

	u32 gpu_color(double ms)
	{
		if (ms > GPU_CRIT_MS) return color_rgba(255, 90, 90, 255);
		if (ms > GPU_WARN_MS) return color_rgba(255, 215, 90, 255);
		return color_rgba(205, 225, 205, 255);
	}

	void fmt_count(char* out, size_t n, u32 v)
	{
		if (v >= 1000000) xr_sprintf(out, n, "%.1fM", v / 1000000.0);
		else if (v >= 10000) xr_sprintf(out, n, "%uk", v / 1000);
		else xr_sprintf(out, n, "%u", v);
	}
}

namespace svp_stats
{
	void frame_begin()
	{
		dof_state_check(); // runs before the stats gate so r__svp_diag alone still catches the crossing
		if (ps_r__svp_stats == 0)
		{
			if (s_created)
				release();
			return;
		}
		if (!ensure_created())
			return;

		double fms = 0.0;
		if (s_frame_timer_started)
			fms = s_frame_timer.GetElapsed_sec() * 1000.0;
		s_frame_timer.Start();
		s_frame_timer_started = true;

		++s_frame_no;
		s_cur = &s_frames[s_frame_no % RING];
		// the reclaimed slot is RING frames old, resolve it before overwriting its queries
		if (s_cur->in_flight && !resolve_slot(*s_cur))
		{
			s_cur = nullptr;
			return;
		}
		s_cur->pair_count = 0;
		s_cur->overflow = false;
		s_cur->in_flight = false;
		for (u32 i = 0; i < SEC_COUNT; ++i)
		{
			s_cur->data.sec[i].cpu_ms = 0.0;
			s_cur->data.sec[i].gpu_ms = 0.0;
			s_cur->data.sec[i].calls = s_cur->data.sec[i].verts = s_cur->data.sec[i].polys = 0;
			s_sec_open_pair[i] = -1;
		}
		s_cur->data.main_lights = s_cur->data.main_shadowed = 0;
		s_cur->data.svp_blends = s_cur->data.sun_passes = 0;
		s_cur->data.frame_ms = fms;
		// shared cull counters, one frame of accumulation each
		svp_stats_ssa_culled = 0;
		svp_stats_cull_reject = 0;
		svp_stats_cull_reject_ident = 0;
		svp_stats_lights_mirrored = 0;
		svp_stats_lights_skipped = 0;
		svp_stats_taa_stamp = 0;
		svp_stats_nvg_split = 0;
		svp_stats_lod_scale = 0;
		svp_stats_hud_cull_reject = 0;
		svp_stats_grass_cull_reject = 0;
		svp_stats_reflex_capture = 0;
		svp_stats_distort_guard = 0;
		svp_stats_nvg_sky = 0;
		svp_stats_disc_latch = 0;
		svp_stats_fwd_keep = 0;
		svp_stats_optic_resolve = 0;
		svp_stats_lean_flags = 0;
		svp_stats_copies = 0;
		svp_stats_copy_kb = 0;
		svp_stats_copy_kb_cat[0] = svp_stats_copy_kb_cat[1] = svp_stats_copy_kb_cat[2] = 0;
		svp_stats_tiny = 0;
		svp_stats_shadow = 0;
		// feed the rolling ~1s window for the spike readout, skip the first frame's null delta
		if (fms > 0.0)
		{
			s_ft_ms[s_ft_head] = (float)fms;
			s_ft_time[s_ft_head] = Device.dwTimeGlobal;
			s_ft_head = (s_ft_head + 1) % FT_WIN;
		}
		HW.pContext->Begin(s_cur->disjoint);
	}

	void section_begin(section_e s)
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		s_sec_calls0[s] = RCache.stat.calls;
		s_sec_verts0[s] = RCache.stat.verts;
		s_sec_polys0[s] = RCache.stat.polys;
		s_sec_timer[s].Start();
		if (s_cur->pair_count < MAX_PAIRS)
		{
			u32 p = s_cur->pair_count++;
			s_cur->sec_of_pair[p] = (u8)s;
			s_sec_open_pair[s] = (int)p;
			HW.pContext->End(s_cur->ts[p * 2]); // timestamp query, End marks the instant
		}
		else
		{
			s_cur->overflow = true;
			s_sec_open_pair[s] = -1;
		}
	}

	void section_end(section_e s)
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		s_cur->data.sec[s].cpu_ms += s_sec_timer[s].GetElapsed_sec() * 1000.0;
		s_cur->data.sec[s].calls += RCache.stat.calls - s_sec_calls0[s];
		s_cur->data.sec[s].verts += RCache.stat.verts - s_sec_verts0[s];
		s_cur->data.sec[s].polys += RCache.stat.polys - s_sec_polys0[s];
		int p = s_sec_open_pair[s];
		if (p >= 0)
		{
			HW.pContext->End(s_cur->ts[p * 2 + 1]);
			s_sec_open_pair[s] = -1;
		}
	}

	void note_main_lights(u32 total, u32 shadowed)
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		s_cur->data.main_lights += total;
		s_cur->data.main_shadowed += shadowed;
	}

	void note_svp_blend()
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		++s_cur->data.svp_blends;
	}

	void note_sun()
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		++s_cur->data.sun_passes;
	}

	void frame_end(bool svp_active)
	{
		if (ps_r__svp_stats == 0 || !s_cur)
			return;
		stats_frame& d = s_cur->data;
		d.svp_active = svp_active;
		d.ssa_culled = svp_stats_ssa_culled;
		d.cull_reject = svp_stats_cull_reject;
		d.cull_reject_ident = svp_stats_cull_reject_ident;
		d.lights_mirrored = svp_stats_lights_mirrored;
		d.lights_skipped = svp_stats_lights_skipped;
		d.taa_stamp = svp_stats_taa_stamp;
		d.nvg_split = svp_stats_nvg_split;
		d.lod_scale = svp_stats_lod_scale;
		d.hud_cull_reject = svp_stats_hud_cull_reject;
		d.grass_cull_reject = svp_stats_grass_cull_reject;
		d.reflex_capture = svp_stats_reflex_capture;
		d.distort_guard = svp_stats_distort_guard;
		d.nvg_sky = svp_stats_nvg_sky;
		d.disc_latch = svp_stats_disc_latch;
		d.fwd_keep = svp_stats_fwd_keep;
		CRenderTarget* S = RImplementation.TargetSVP;
		d.svp_w = (svp_active && S) ? S->Width : 0;
		d.svp_h = (svp_active && S) ? S->Height : 0;
		d.svp_disc = Device.m_SecondViewport.svp_disc_applied;
		d.svp_disc_learned = Device.m_SecondViewport.svp_disc_px;
		d.svp_epoch = Device.m_SecondViewport.svp_optic_epoch;
		d.optic_resolve = svp_stats_optic_resolve;
		d.svp_grow = (ps_r__svp_adaptive_grow != 0);
		d.svp_mag = g_pip_scope_magnification;
		d.sun_shafts = 0.f;
		if (g_pGamePersistent && g_pGamePersistent->Environment().CurrentEnv)
			d.sun_shafts = g_pGamePersistent->Environment().CurrentEnv->m_fSunShaftsIntensity;
		d.lean_flags = svp_stats_lean_flags;
		d.lean_on = (ps_r__pp_lean != 0);
		d.copies = svp_stats_copies;
		d.copy_kb = svp_stats_copy_kb;
		for (u32 i = 0; i < 3; ++i)
			d.copy_kb_cat[i] = svp_stats_copy_kb_cat[i];
		d.tiny = svp_stats_tiny;
		d.shadow = svp_stats_shadow;
		// the backend zeroes stat once per frame in OnFrameBegin so this is already a per-frame count
		d.rtsw = RCache.stat.target_rt;
		d.smap = RImplementation.o.smapsize;
		HW.pContext->End(s_cur->disjoint);
		s_cur->in_flight = true;
	}

	void draw_overlay()
	{
		if (ps_r__svp_stats == 0 || !s_created)
			return;
		if (!s_snap_valid || !s_font || !s_shader || !UIRender)
			return;

		const stats_frame& d = s_snap;
		const bool full = (ps_r__svp_stats >= 2);

		// section totals per viewport, svp mirrors nest inside the main lighting so its column is the
		// scope-only slice while the main column carries the whole pass
		double svp_gpu = d.sec[SEC_SVP_GBUFFER].gpu_ms + d.sec[SEC_SVP_LIGHTS].gpu_ms
			+ d.sec[SEC_SVP_EMISSIVE].gpu_ms + d.sec[SEC_SVP_COMBINE].gpu_ms;
		double main_gpu = d.sec[SEC_MAIN_GBUFFER].gpu_ms + d.sec[SEC_MAIN_LIGHTS].gpu_ms
			+ d.sec[SEC_MAIN_EMISSIVE].gpu_ms + d.sec[SEC_MAIN_COMBINE].gpu_ms;
		double svp_cpu = d.sec[SEC_SVP_GBUFFER].cpu_ms + d.sec[SEC_SVP_LIGHTS].cpu_ms
			+ d.sec[SEC_SVP_EMISSIVE].cpu_ms + d.sec[SEC_SVP_COMBINE].cpu_ms;
		double main_cpu = d.sec[SEC_MAIN_GBUFFER].cpu_ms + d.sec[SEC_MAIN_LIGHTS].cpu_ms
			+ d.sec[SEC_MAIN_EMISSIVE].cpu_ms + d.sec[SEC_MAIN_COMBINE].cpu_ms;
		u32 svp_calls = d.sec[SEC_SVP_GBUFFER].calls + d.sec[SEC_SVP_LIGHTS].calls
			+ d.sec[SEC_SVP_EMISSIVE].calls + d.sec[SEC_SVP_COMBINE].calls;
		u32 main_calls = d.sec[SEC_MAIN_GBUFFER].calls + d.sec[SEC_MAIN_LIGHTS].calls
			+ d.sec[SEC_MAIN_EMISSIVE].calls + d.sec[SEC_MAIN_COMBINE].calls;
		u32 svp_verts = d.sec[SEC_SVP_GBUFFER].verts + d.sec[SEC_SVP_LIGHTS].verts
			+ d.sec[SEC_SVP_EMISSIVE].verts + d.sec[SEC_SVP_COMBINE].verts;
		u32 main_verts = d.sec[SEC_MAIN_GBUFFER].verts + d.sec[SEC_MAIN_LIGHTS].verts
			+ d.sec[SEC_MAIN_EMISSIVE].verts + d.sec[SEC_MAIN_COMBINE].verts;
		u32 svp_polys = d.sec[SEC_SVP_GBUFFER].polys + d.sec[SEC_SVP_LIGHTS].polys
			+ d.sec[SEC_SVP_EMISSIVE].polys + d.sec[SEC_SVP_COMBINE].polys;
		u32 main_polys = d.sec[SEC_MAIN_GBUFFER].polys + d.sec[SEC_MAIN_LIGHTS].polys
			+ d.sec[SEC_MAIN_EMISSIVE].polys + d.sec[SEC_MAIN_COMBINE].polys;

		CGameFont& F = *s_font;
		const float H = (float)Device.dwHeight;
		const float W = (float)Device.dwWidth;
		const float hi = 0.0135f;    // font height as a screen fraction, readable at 1080p and 4k
		F.SetHeightI(hi);
		const float line = H * hi;
		const float step = line * 1.32f;
		const float pad = line * 0.7f;
		const float digit = F.SizeOf_("0");
		const float cell = digit * 7.5f;             // widest data cell "1234567" / "123.45"
		const float label_w = F.SizeOf_("emissive") + digit;
		const float gap = digit * 1.5f;

		// rolling ~1s frame-time window, min/avg/max exposes hitches vs steady cost
		float ft_min = 0.f, ft_max = 0.f, ft_avg = 0.f;
		{
			const u32 now = Device.dwTimeGlobal;
			double sum = 0.0; u32 cnt = 0;
			for (u32 i = 0; i < FT_WIN; ++i)
			{
				if (s_ft_time[i] == 0 || now - s_ft_time[i] > 1000)
					continue;
				const float v = s_ft_ms[i];
				if (cnt == 0 || v < ft_min) ft_min = v;
				if (v > ft_max) ft_max = v;
				sum += v; ++cnt;
			}
			if (cnt) ft_avg = (float)(sum / cnt);
		}

		// whole scene-render cpu vs gpu, the frame section wraps gbuffer through combine
		const double fcpu = d.sec[SEC_FRAME].cpu_ms;
		const double fgpu = d.sec[SEC_FRAME].gpu_ms;
		LPCSTR bound = (fcpu > fgpu * 1.15) ? "cpu-bound" : (fgpu > fcpu * 1.15) ? "gpu-bound" : "even";

		// free-form footer lines, built first so the panel sizes its width to the widest of them
		char foot[FOOT_MAX][96];
		u32 nf = 0;
		xr_sprintf(foot[nf++], "frame %.2f ms  %.0f fps", d.frame_ms, d.frame_ms > 0.01 ? 1000.0 / d.frame_ms : 0.0);
		xr_sprintf(foot[nf++], "cpu %.2f  gpu %.2f ms", fcpu, fgpu);
		// bound verdict on its own row so it stays legible against the moving numbers
		xr_sprintf(foot[nf++], "%s", bound);
		xr_sprintf(foot[nf++], "1s min %.2f avg %.2f max %.2f", ft_min, ft_avg, ft_max);
		xr_sprintf(foot[nf++], "svp %ux%u mag %.1fx epoch %u res %u", d.svp_w, d.svp_h, d.svp_mag, d.svp_epoch, d.optic_resolve);
		xr_sprintf(foot[nf++], "res learn %.0f apply %.0f side %u grow %s", d.svp_disc_learned, d.svp_disc, d.svp_w, d.svp_grow ? "on" : "off");
		xr_sprintf(foot[nf++], "cull ssa %u rej %u i%u hud %u grass %u  lights m%u s%u", d.ssa_culled, d.cull_reject, d.cull_reject_ident, d.hud_cull_reject, d.grass_cull_reject, d.lights_mirrored, d.lights_skipped);
		if (full)
		{
			xr_sprintf(foot[nf++], "stamp taa %u nvg %u distort %u nvgsky %u", d.taa_stamp, d.nvg_split, d.distort_guard, d.nvg_sky);
			xr_sprintf(foot[nf++], "fire lod %u reflex %u disc %u fwd %u", d.lod_scale, d.reflex_capture, d.disc_latch, d.fwd_keep);
		}

		const u32 lines = 1u + (full ? 4u : 0u) + 7u + nf; // header + section gpu + fixed columns + footer

		const float col_w = label_w + cell + gap + cell;
		float content_w = col_w;
		for (u32 i = 0; i < nf; ++i)
			content_w = _max(content_w, F.SizeOf_(foot[i]));
		const float panel_w = content_w + 2.f * pad;
		const float panel_h = lines * step + 2.f * pad;
		const float right = W - W * 0.012f;
		const float panel_l = right - panel_w;
		const float top = H * 0.02f;
		const float label_l = panel_l + pad;
		const float svp_r = label_l + label_w + cell;
		const float main_r = svp_r + gap + cell;

		// dark translucent backing rect at ~75% over the opaque-white ui texture
		u32 back = color_rgba(14, 17, 21, 191);
		UIRender->SetShader(**s_shader);
		UIRender->StartPrimitive(6, IUIRender::ptTriList, IUIRender::pttTL);
		UIRender->PushPoint(panel_l, top, 0.f, back, 0.f, 0.f);
		UIRender->PushPoint(panel_l + panel_w, top, 0.f, back, 1.f, 0.f);
		UIRender->PushPoint(panel_l + panel_w, top + panel_h, 0.f, back, 1.f, 1.f);
		UIRender->PushPoint(panel_l, top, 0.f, back, 0.f, 0.f);
		UIRender->PushPoint(panel_l + panel_w, top + panel_h, 0.f, back, 1.f, 1.f);
		UIRender->PushPoint(panel_l, top + panel_h, 0.f, back, 0.f, 1.f);
		UIRender->FlushPrimitive();

		const u32 c_hdr = color_rgba(175, 200, 235, 255);
		const u32 c_txt = color_rgba(205, 218, 218, 255);
		const u32 c_dim = color_rgba(120, 130, 130, 255);
		float y = top + pad;

		auto row = [&](u32 lc, LPCSTR label, u32 sc, LPCSTR sv, u32 mc, LPCSTR mv, bool has_m)
		{
			F.SetAligment(CGameFont::alLeft); F.SetColor(lc); F.Out(label_l, y, "%s", label);
			F.SetAligment(CGameFont::alRight); F.SetColor(sc); F.Out(svp_r, y, "%s", sv);
			if (has_m) { F.SetColor(mc); F.Out(main_r, y, "%s", mv); }
			y += step;
		};

		row(c_hdr, "svp stats", c_hdr, "SVP", c_hdr, "MAIN", true);

		char sb[24], mb[24];
		if (full)
		{
			xr_sprintf(sb, "%.2f", d.sec[SEC_SVP_GBUFFER].gpu_ms); xr_sprintf(mb, "%.2f", d.sec[SEC_MAIN_GBUFFER].gpu_ms);
			row(c_txt, "gbuffer", gpu_color(d.sec[SEC_SVP_GBUFFER].gpu_ms), sb, gpu_color(d.sec[SEC_MAIN_GBUFFER].gpu_ms), mb, true);
			xr_sprintf(sb, "%.2f", d.sec[SEC_SVP_LIGHTS].gpu_ms); xr_sprintf(mb, "%.2f", d.sec[SEC_MAIN_LIGHTS].gpu_ms);
			row(c_txt, "lights", gpu_color(d.sec[SEC_SVP_LIGHTS].gpu_ms), sb, gpu_color(d.sec[SEC_MAIN_LIGHTS].gpu_ms), mb, true);
			xr_sprintf(sb, "%.2f", d.sec[SEC_SVP_EMISSIVE].gpu_ms); xr_sprintf(mb, "%.2f", d.sec[SEC_MAIN_EMISSIVE].gpu_ms);
			row(c_txt, "emissive", gpu_color(d.sec[SEC_SVP_EMISSIVE].gpu_ms), sb, gpu_color(d.sec[SEC_MAIN_EMISSIVE].gpu_ms), mb, true);
			xr_sprintf(sb, "%.2f", d.sec[SEC_SVP_COMBINE].gpu_ms); xr_sprintf(mb, "%.2f", d.sec[SEC_MAIN_COMBINE].gpu_ms);
			row(c_txt, "combine", gpu_color(d.sec[SEC_SVP_COMBINE].gpu_ms), sb, gpu_color(d.sec[SEC_MAIN_COMBINE].gpu_ms), mb, true);
		}

		xr_sprintf(sb, "%.2f", svp_gpu); xr_sprintf(mb, "%.2f", main_gpu);
		row(c_txt, "gpu ms", gpu_color(svp_gpu), sb, gpu_color(main_gpu), mb, true);
		xr_sprintf(sb, "%.2f", svp_cpu); xr_sprintf(mb, "%.2f", main_cpu);
		row(c_txt, "cpu ms", c_txt, sb, c_txt, mb, true);
		fmt_count(sb, sizeof(sb), svp_calls); fmt_count(mb, sizeof(mb), main_calls);
		row(c_txt, "draws", c_txt, sb, c_txt, mb, true);
		fmt_count(sb, sizeof(sb), svp_verts); fmt_count(mb, sizeof(mb), main_verts);
		row(c_txt, "verts", c_txt, sb, c_txt, mb, true);
		fmt_count(sb, sizeof(sb), svp_polys); fmt_count(mb, sizeof(mb), main_polys);
		row(c_txt, "polys", c_txt, sb, c_txt, mb, true);
		xr_sprintf(sb, "%u", d.svp_blends); xr_sprintf(mb, "%u", d.main_lights);
		row(c_txt, "lights", c_txt, sb, c_txt, mb, true);
		xr_sprintf(mb, "%u", d.main_shadowed);
		row(c_txt, "shadow", c_dim, "-", c_txt, mb, true);

		// footer, the free-form lines built above, first line highlighted
		F.SetAligment(CGameFont::alLeft);
		for (u32 i = 0; i < nf; ++i)
		{
			F.SetColor(i == 0 ? c_hdr : c_txt);
			F.Out(label_l, y, "%s", foot[i]);
			y += step;
		}

		// combine box geometry, the pipe box below anchors off it
		float brk_l = 0.f, brk_top = 0.f, brk_w = 0.f, brk_h = 0.f;

		// second box under the main panel, splits the main combine bucket into its post passes
		{
			struct brk_row { LPCSTR name; section_e s; bool always; };
			static const brk_row brk[] = {
				{ "ao",       SEC_C_AO,       true  },
				{ "il",       SEC_C_IL,       false },
				{ "sky",      SEC_C_SKY,      false },
				{ "combine1", SEC_C_COMBINE1, true  },
				{ "ssr",      SEC_C_SSR,      true  },
				{ "water",    SEC_C_WATER,    false },
				{ "rain",     SEC_C_RAIN,     false },
				{ "fwd",      SEC_C_FWD,      false },
				{ "bloom",    SEC_C_BLOOM,    false },
				{ "sunshaft", SEC_C_SUNSHAFT, false },
				{ "fog",      SEC_C_FOG,      false },
				{ "taa",      SEC_C_TAA,      true  },
				{ "blur",     SEC_C_BLUR,     false },
				{ "dof",      SEC_C_DOF,      false },
				{ "lut",      SEC_C_LUT,      false },
				{ "combine2", SEC_C_COMBINE2, true  },
			};
			const u32 brk_n = sizeof(brk) / sizeof(brk[0]);

			// other is whatever the bucket holds past the timed sub passes, nothing hides in it silently
			// the average of a difference is the difference of the averages, same alpha and same frames
			double sub_sum = 0.0, sub_sum_avg = 0.0;
			for (u32 i = 0; i < brk_n; ++i)
			{
				sub_sum += d.sec[brk[i].s].gpu_ms;
				sub_sum_avg += s_sec_avg[brk[i].s];
			}
			const double other = d.sec[SEC_MAIN_COMBINE].gpu_ms - sub_sum;
			const double other_avg = s_sec_avg[SEC_MAIN_COMBINE] - sub_sum_avg;

			char btail[2][96];
			u32 bt = 0;
			xr_sprintf(btail[bt++], "sun %s shafts %.3f", d.sun_passes ? "on" : "off", d.sun_shafts);
			// lean row, gate state then a letter per skip that actually fired this frame
			{
				char lf[64]; lf[0] = 0;
				if (d.lean_flags & LEAN_LUT) xr_strcat(lf, " LUT");
				if (d.lean_flags & LEAN_WATER) xr_strcat(lf, " WATER");
				if (d.lean_flags & LEAN_GLASS) xr_strcat(lf, " GLASS");
				xr_sprintf(btail[bt++], "lean %u%s", d.lean_on ? 1u : 0u, lf);
			}

			// visibility keys on the average so a content-driven row cannot pop as the camera turns
			u32 shown = 1; // the other row always prints
			for (u32 i = 0; i < brk_n; ++i)
				if (brk[i].always || s_sec_avg[brk[i].s] >= ROW_MIN_MS)
					++shown;

			const float blabel_w = F.SizeOf_("combine1") + digit;
			float bcontent_w = blabel_w + cell + gap + cell;
			for (u32 i = 0; i < bt; ++i)
				bcontent_w = _max(bcontent_w, F.SizeOf_(btail[i]));
			const float bpanel_w = bcontent_w + 2.f * pad;
			const float bpanel_h = (1u + shown + bt) * step + 2.f * pad;
			const float bpanel_l = right - bpanel_w;
			const float btop = top + panel_h + step * 0.4f;
			const float blabel_l = bpanel_l + pad;
			const float bval_r = blabel_l + blabel_w + cell;
			const float bavg_r = bval_r + gap + cell;
			brk_l = bpanel_l; brk_top = btop; brk_w = bpanel_w; brk_h = bpanel_h;

			UIRender->SetShader(**s_shader);
			UIRender->StartPrimitive(6, IUIRender::ptTriList, IUIRender::pttTL);
			UIRender->PushPoint(bpanel_l, btop, 0.f, back, 0.f, 0.f);
			UIRender->PushPoint(bpanel_l + bpanel_w, btop, 0.f, back, 1.f, 0.f);
			UIRender->PushPoint(bpanel_l + bpanel_w, btop + bpanel_h, 0.f, back, 1.f, 1.f);
			UIRender->PushPoint(bpanel_l, btop, 0.f, back, 0.f, 0.f);
			UIRender->PushPoint(bpanel_l + bpanel_w, btop + bpanel_h, 0.f, back, 1.f, 1.f);
			UIRender->PushPoint(bpanel_l, btop + bpanel_h, 0.f, back, 0.f, 1.f);
			UIRender->FlushPrimitive();

			float by = btop + pad;
			auto brow = [&](u32 lc, LPCSTR label, u32 vc, LPCSTR val, u32 ac, LPCSTR avg)
			{
				F.SetAligment(CGameFont::alLeft); F.SetColor(lc); F.Out(blabel_l, by, "%s", label);
				F.SetAligment(CGameFont::alRight); F.SetColor(vc); F.Out(bval_r, by, "%s", val);
				F.SetColor(ac); F.Out(bavg_r, by, "%s", avg);
				by += step;
			};

			brow(c_hdr, "combine", c_hdr, "MS", c_hdr, "AVG");
			char bb[24], ba[24];
			for (u32 i = 0; i < brk_n; ++i)
			{
				const double ms = d.sec[brk[i].s].gpu_ms;
				const double avg = s_sec_avg[brk[i].s];
				if (!brk[i].always && avg < ROW_MIN_MS)
					continue;
				xr_sprintf(bb, "%.2f", ms);
				xr_sprintf(ba, "%.2f", avg);
				brow(c_txt, brk[i].name, gpu_color(ms), bb, gpu_color(avg), ba);
			}
			xr_sprintf(bb, "%.2f", other);
			xr_sprintf(ba, "%.2f", other_avg);
			brow(c_dim, "other", c_txt, bb, c_txt, ba);

			F.SetAligment(CGameFont::alLeft);
			F.SetColor(c_txt);
			for (u32 i = 0; i < bt; ++i)
			{
				F.Out(blabel_l, by, "%s", btail[i]);
				by += step;
			}
		}

		// third box, pipeline waste, sits left of the combine box and stacks under it when width runs out
		{
			// untracked is the frame window minus every bucket we time, the honesty row
			double tracked = d.sec[SEC_MAIN_GBUFFER].gpu_ms + d.sec[SEC_MAIN_LIGHTS].gpu_ms
				+ d.sec[SEC_MAIN_EMISSIVE].gpu_ms + d.sec[SEC_MAIN_COMBINE].gpu_ms
				+ d.sec[SEC_SVP_GBUFFER].gpu_ms + d.sec[SEC_SVP_LIGHTS].gpu_ms
				+ d.sec[SEC_SVP_EMISSIVE].gpu_ms + d.sec[SEC_SVP_COMBINE].gpu_ms;
			double tracked_avg = s_sec_avg[SEC_MAIN_GBUFFER] + s_sec_avg[SEC_MAIN_LIGHTS]
				+ s_sec_avg[SEC_MAIN_EMISSIVE] + s_sec_avg[SEC_MAIN_COMBINE]
				+ s_sec_avg[SEC_SVP_GBUFFER] + s_sec_avg[SEC_SVP_LIGHTS]
				+ s_sec_avg[SEC_SVP_EMISSIVE] + s_sec_avg[SEC_SVP_COMBINE];
			const double untracked = d.sec[SEC_FRAME].gpu_ms - tracked;
			const double untracked_avg = s_sec_avg[SEC_FRAME] - tracked_avg;

			char ptail[3][96];
			u32 pt = 0;
			xr_sprintf(ptail[pt++], "smap %u", d.smap);
			xr_sprintf(ptail[pt++], "ao %.1f q%d  il %.1f q%d  ssr %.1f q%d",
				ps_ssfx_ao.x, ps_ssfx_ao_quality, ps_ssfx_il.x, ps_ssfx_il_quality,
				ps_ssfx_ssr.x, ps_ssfx_ssr_quality);
			// w gates the 16 tap loop, p widens it over the periphery, nvg forces the beefs spiral
			const dof_state dn = dof_now();
			xr_sprintf(ptail[pt++], "dof w %.2f z %.2f p %.2f nvg %d %s",
				dn.w, dn.z, dn.p, dn.nvg ? 1 : 0, dn.heavy ? "HEAVY" : "cheap");

			// the per-category megabytes ride in the label so the row keeps the two data columns
			char cplab[3][32];
			const section_e cpsec[3] = { SEC_CP_HIST, SEC_CP_TAIL, SEC_CP_SCENE };
			const u32 cpavg[3] = { PIPE_CP_HIST_KB, PIPE_CP_TAIL_KB, PIPE_CP_SCENE_KB };
			LPCSTR cpname[3] = { "cp hist", "cp tail", "cp scene" };
			for (u32 i = 0; i < 3; ++i)
				xr_sprintf(cplab[i], "%s %.0fmb", cpname[i], s_pipe_avg[cpavg[i]] / 1024.0);

			const u32 prows = 9;
			float plabel_w = F.SizeOf_("untracked") + digit;
			for (u32 i = 0; i < 3; ++i)
				plabel_w = _max(plabel_w, F.SizeOf_(cplab[i]) + digit);
			float pcontent_w = plabel_w + cell + gap + cell;
			for (u32 i = 0; i < pt; ++i)
				pcontent_w = _max(pcontent_w, F.SizeOf_(ptail[i]));
			const float ppanel_w = pcontent_w + 2.f * pad;
			const float ppanel_h = (1u + prows + pt) * step + 2.f * pad;
			const float side_gap = step * 0.4f;
			const float left_edge = W * 0.012f;
			// beside the combine box when it fits, otherwise stacked under it on the same right edge
			const bool beside = (brk_l - side_gap - ppanel_w) >= left_edge;
			const float ppanel_l = beside ? (brk_l - side_gap - ppanel_w) : (right - ppanel_w);
			const float ptop = beside ? brk_top : (brk_top + brk_h + side_gap);
			const float plabel_l = ppanel_l + pad;
			const float pval_r = plabel_l + plabel_w + cell;
			const float pavg_r = pval_r + gap + cell;

			UIRender->SetShader(**s_shader);
			UIRender->StartPrimitive(6, IUIRender::ptTriList, IUIRender::pttTL);
			UIRender->PushPoint(ppanel_l, ptop, 0.f, back, 0.f, 0.f);
			UIRender->PushPoint(ppanel_l + ppanel_w, ptop, 0.f, back, 1.f, 0.f);
			UIRender->PushPoint(ppanel_l + ppanel_w, ptop + ppanel_h, 0.f, back, 1.f, 1.f);
			UIRender->PushPoint(ppanel_l, ptop, 0.f, back, 0.f, 0.f);
			UIRender->PushPoint(ppanel_l + ppanel_w, ptop + ppanel_h, 0.f, back, 1.f, 1.f);
			UIRender->PushPoint(ppanel_l, ptop + ppanel_h, 0.f, back, 0.f, 1.f);
			UIRender->FlushPrimitive();

			float py = ptop + pad;
			auto prow = [&](u32 lc, LPCSTR label, u32 vc, LPCSTR val, u32 ac, LPCSTR avg)
			{
				F.SetAligment(CGameFont::alLeft); F.SetColor(lc); F.Out(plabel_l, py, "%s", label);
				F.SetAligment(CGameFont::alRight); F.SetColor(vc); F.Out(pval_r, py, "%s", val);
				F.SetColor(ac); F.Out(pavg_r, py, "%s", avg);
				py += step;
			};

			char pb[24], pa[24];
			prow(c_hdr, "pipe", c_hdr, "NOW", c_hdr, "AVG");
			xr_sprintf(pb, "%.2f", untracked); xr_sprintf(pa, "%.2f", untracked_avg);
			prow(c_txt, "untracked", gpu_color(untracked), pb, gpu_color(untracked_avg), pa);
			xr_sprintf(pb, "%u", d.copies); xr_sprintf(pa, "%.1f", s_pipe_avg[PIPE_COPIES]);
			prow(c_txt, "copies", c_txt, pb, c_txt, pa);
			xr_sprintf(pb, "%.1f", d.copy_kb / 1024.0); xr_sprintf(pa, "%.1f", s_pipe_avg[PIPE_COPY_KB] / 1024.0);
			prow(c_dim, "copy mb", c_txt, pb, c_txt, pa);
			xr_sprintf(pb, "%u", d.rtsw); xr_sprintf(pa, "%.0f", s_pipe_avg[PIPE_RTSW]);
			prow(c_txt, "rtsw", c_txt, pb, c_txt, pa);
			xr_sprintf(pb, "%u", d.shadow); xr_sprintf(pa, "%.1f", s_pipe_avg[PIPE_SHADOW]);
			prow(c_txt, "shadow", c_txt, pb, c_txt, pa);
			fmt_count(pb, sizeof(pb), d.tiny); xr_sprintf(pa, "%.0f", s_pipe_avg[PIPE_TINY]);
			prow(c_txt, "tiny", c_txt, pb, c_txt, pa);
			for (u32 i = 0; i < 3; ++i)
			{
				const double ms = d.sec[cpsec[i]].gpu_ms;
				const double avg = s_sec_avg[cpsec[i]];
				xr_sprintf(pb, "%.2f", ms); xr_sprintf(pa, "%.2f", avg);
				prow(c_txt, cplab[i], gpu_color(ms), pb, gpu_color(avg), pa);
			}

			F.SetAligment(CGameFont::alLeft);
			F.SetColor(c_dim);
			for (u32 i = 0; i < pt; ++i)
			{
				F.Out(plabel_l, py, "%s", ptail[i]);
				py += step;
			}
		}

		F.OnRender();
	}

	void release()
	{
		for (u32 f = 0; f < RING; ++f)
		{
			frame_slot& s = s_frames[f];
			_RELEASE(s.disjoint);
			for (u32 i = 0; i < MAX_PAIRS * 2; ++i)
				_RELEASE(s.ts[i]);
			s.pair_count = 0; s.overflow = false; s.in_flight = false;
		}
		svp_copy_timer_hook = nullptr; // the query pool is gone, shared sites must not call in
		xr_delete(s_font);
		if (s_shader) { xr_delete(s_shader); s_shader = nullptr; }
		s_cur = nullptr;
		s_created = false;
		s_create_retry_ms = 0;
		s_snap_valid = false;
		s_frame_timer_started = false;
	}
}
