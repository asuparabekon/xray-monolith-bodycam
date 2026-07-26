#include "stdafx.h"
#include "../xrCDB/frustum.h"
#include "xr_ioconsole.h"
#include "xr_input.h"
#include "../xrCore/profiler.h"

#pragma warning(disable:4995)
// mmsystem.h
#define MMNOSOUND
#define MMNOMIDI
#define MMNOAUX
#define MMNOMIXER
#define MMNOJOY
#include <mmsystem.h>
// d3dx9.h
#include <d3dx9.h>
#pragma warning(default:4995)

#include "x_ray.h"
#include "discord\discord.h"
#include "render.h"
#include <chrono>

// must be defined before include of FS_impl.h
#define INCLUDE_FROM_ENGINE
#include "../xrCore/FS_impl.h"

#ifdef INGAME_EDITOR
# include "../include/editor/ide.hpp"
# include "engine_impl.hpp"
#endif // #ifdef INGAME_EDITOR

#include "xrSash.h"
#include "igame_persistent.h"

#include "CustomHUD.h"
#include "EngineThreading.h"
#include "IGame_Level.h"

#include "Rain.h"
#include "../Layers/xrRender/svp_console.h"

#pragma comment( lib, "d3dx9.lib" )

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;
ENGINE_API CRenderDevice* DevicePtr = nullptr;

ENGINE_API xr_atomic_bool g_bRendering = false;
extern ENGINE_API float psHUD_FOV;

BOOL g_bLoaded = FALSE;
ref_light precache_light = 0;

BOOL mt_calc_bones = TRUE;
BOOL psLua_ParallelGC = TRUE;
BOOL psLua_ParallelGC_debug = FALSE;

extern discord::Core* discord_core;
extern bool use_discord;

extern Fvector4 ps_ssfx_grass_interactive;

#ifdef ECO_RENDER
std::chrono::high_resolution_clock::time_point tlastf = std::chrono::high_resolution_clock::now(), tcurrentf = std::
	                                               chrono::high_resolution_clock::now();
std::chrono::duration<float> time_span;
ENGINE_API float refresh_rate = 0;
#endif // ECO_RENDER


BOOL CRenderDevice::Begin()
{
	PROF_EVENT("Render: Begin");

#ifndef DEDICATED_SERVER
	switch (m_pRender->GetDeviceState())
	{
	case IRenderDeviceRender::dsOK:
		break;

	case IRenderDeviceRender::dsLost:
		// If the device was lost, do not render until we get it back
		Sleep(33);
		return FALSE;
		break;

	case IRenderDeviceRender::dsNeedReset:
		// Check if the device is ready to be reset
		Reset();
		break;

	default:
		R_ASSERT(0);
	}

	m_pRender->Begin();

	FPU::m24r();
	g_bRendering = true;
#endif
	return TRUE;
}

void CRenderDevice::Clear()
{
	m_pRender->Clear();
}

extern void CheckPrivilegySlowdown();


void CRenderDevice::End(void)
{
	PROF_EVENT("Render: End");

#ifndef DEDICATED_SERVER


#ifdef INGAME_EDITOR
    bool load_finished = false;
#endif // #ifdef INGAME_EDITOR
	if (dwPrecacheFrame)
	{
		::Sound->set_master_volume(0.f);
		dwPrecacheFrame--;

		if (!dwPrecacheFrame)
		{
#ifdef INGAME_EDITOR
            load_finished = true;
#endif // #ifdef INGAME_EDITOR

			m_pRender->updateGamma();

			if (precache_light)
			{
				precache_light->set_active(false);
				precache_light.destroy();
			}
			::Sound->set_master_volume(1.f);

			m_pRender->ResourcesDestroyNecessaryTextures();

			Msg("* [x-ray]: Handled Necessary Textures Destruction");
			Memory.mem_compact();
			//Msg("* MEMORY USAGE: %lld K", Memory.mem_usage() / 1024);
			//Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);

#ifdef FIND_CHUNK_BENCHMARK_ENABLE
            g_find_chunk_counter.flush();
#endif // FIND_CHUNK_BENCHMARK_ENABLE

			CheckPrivilegySlowdown();

			if (g_pGamePersistent->GameType() == 1) //haCk
			{
				WINDOWINFO wi;
				GetWindowInfo(m_hWnd, &wi);
				if (wi.dwWindowStatus != WS_ACTIVECAPTION)
					Pause(TRUE, TRUE, TRUE, "application start");
			}
		}
	}

	g_bRendering = false;
	// end scene
	// Present goes here, so call OA Frame end.
	if (g_SASH.IsBenchmarkRunning())
		g_SASH.DisplayFrame(Device.fTimeGlobal);
	m_pRender->End();

# ifdef INGAME_EDITOR
    if (load_finished && m_editor)
        m_editor->on_load_finished();
# endif // #ifdef INGAME_EDITOR
#endif
}

void CRenderDevice::PreCache(u32 amount, bool b_draw_loadscreen, bool b_wait_user_input)
{
#ifdef DEDICATED_SERVER
    amount = 0;
#else
	if (m_pRender->GetForceGPU_REF())
		amount = 0;
#endif

	dwPrecacheFrame = dwPrecacheTotal = amount;
	if (amount && !precache_light && g_pGameLevel && g_loading_events.empty())
	{
		precache_light = ::Render->light_create();
		precache_light->set_shadow(false);
		precache_light->set_position(vCameraPosition);
		precache_light->set_color(255, 255, 255);
		precache_light->set_range(5.0f);
		precache_light->set_active(true);
	}

	if (amount && b_draw_loadscreen && !load_screen_renderer.b_registered)
	{
		load_screen_renderer.start(b_wait_user_input);
	}
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

extern bool IsMainMenuActive(); //ECO_RENDER add

static HMONITOR g_StartupMonitor = NULL;

#include "MonitorList.h"

static void InitMonitor()
{
	if (g_StartupMonitor)
		return;

	HMONITOR chosen = ResolveSelectedMonitor();
	if (chosen)
	{
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfoA(chosen, &mi))
		{
			g_StartupMonitor = chosen;
			return;
		}
		Msg("! vid_monitor: resolved handle is invalid, using Auto");
	}

	POINT cursorPos;
	GetCursorPos(&cursorPos);
	g_StartupMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
}

ENGINE_API void ResetStartupMonitor()
{
	g_StartupMonitor = NULL;
}

ENGINE_API void SetStartupMonitor(HMONITOR h)
{
	g_StartupMonitor = h;
}

ENGINE_API HMONITOR GetStartupMonitor()
{
	InitMonitor();
	return g_StartupMonitor;
}

void GetMonitorResolution(u32& horizontal, u32& vertical)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		horizontal = mi.rcMonitor.right - mi.rcMonitor.left;
		vertical = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}
	else
	{
		RECT desktop;
		const HWND hDesktop = GetDesktopWindow();
		GetWindowRect(hDesktop, &desktop);
		horizontal = desktop.right - desktop.left;
		vertical = desktop.bottom - desktop.top;
	}
}

void GetMonitorPosition(int& x, int& y)
{
	InitMonitor();

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	if (GetMonitorInfoA(g_StartupMonitor, &mi))
	{
		x = mi.rcMonitor.left;
		y = mi.rcMonitor.top;
	}
	else
	{
		x = 0;
		y = 0;
	}
}

float GetMonitorRefresh()
{
	DEVMODE lpDevMode;
	memset(&lpDevMode, 0, sizeof(DEVMODE));
	lpDevMode.dmSize = sizeof(DEVMODE);
	lpDevMode.dmDriverExtra = 0;

	if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode) == 0)
	{
		return 1.f / 60.f;
	}
	else
		return 1.f / lpDevMode.dmDisplayFrequency;
}

extern int ps_framelimiter;
extern u32 g_screenmode;

CTimer FreezeTimer;
void mt_FreezeThread(void *ptr) {
	float freezetime = 0.f;
	float repeatcheck = 500.f;

	while (true)
	{
		PROF_EVENT();

		if (g_loading_events.size())
			freezetime = 25000.0f;
		else
			freezetime = 5000.0f;

		repeatcheck = 500.f;

		START_PROFILE("Check timer");
		if (FreezeTimer.GetElapsed_sec()*1000.f > freezetime)
		{
			xrLogger::FlushLog();
			repeatcheck = 5000.f;
		}
		STOP_PROFILE;

		Sleep(repeatcheck);
	}
}

void CRenderDevice::on_idle()
{

	FreezeTimer.Start();

	if (!b_is_Ready)
	{
		Sleep(100);
		return;
	}

	PROF_FRAME("Main Thread");

#ifdef DEDICATED_SERVER
    u32 FrameStartTime = TimerGlobal.GetElapsed_ms();
#endif

	START_PROFILE("Set stat gathering");
	if (psDeviceFlags.test(rsStatistic))
		g_bEnableStatGather = TRUE;
	else g_bEnableStatGather = FALSE;
	STOP_PROFILE;

	if (g_loading_events.size())
	{
		{
			PROF_EVENT("Loading...");
			if (g_loading_events.front()())
				g_loading_events.pop_front();
		}
		PROF_EVENT("LoadDraw");
		pApp->LoadDraw();
		return;
	}

	if (!Device.dwPrecacheFrame && !g_SASH.IsBenchmarkRunning() && g_bLoaded)
	{
		PROF_EVENT("Start xrSASH Benchmark");
		g_SASH.StartBenchmark();
	}

	if (Device.ModelDefferClear)
	{
		Device.ModelDefferClear();
	}

	{
		PROF_EVENT("seqParallelBeforRender");
		for (auto& it : Device.seqParallelBeforRender)
			it();

		Device.seqParallelBeforRender.clear();
	}

	FrameMove();

    if (g_pGamePersistent != nullptr)
    {
        PROF_EVENT("Update Particles");
        g_pGamePersistent->UpdateParticles();
    }
    secondary_tasks.run(&XRay::Engine::PreRenderThread);

	// Precache
	if (dwPrecacheFrame)
	{
		PROF_EVENT("Precache frame");
		float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
		float angle = PI_MUL_2 * factor;
		vCameraDirection.set(_sin(angle), 0, _cos(angle));
		vCameraDirection.normalize();
		vCameraTop.set(0, 1, 0);
		vCameraRight.crossproduct(vCameraTop, vCameraDirection);

		mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
	}

	// Matrices
	START_PROFILE("Matrices");
	mFullTransform.mul(mProject, mView);
	mFullTransformHud.mul(mProjectHud, mView);
	mFullTransformCam.mul(mProjectCam, mView);
	m_pRender->SetCacheXform(mView, mProject);

	// advance per-viewport history and store the main camera in slot 0
	// slot 1 = SVP, filled by svpCamera in the render layer
	Device.matrices_previous[0] = Device.matrices[0];
	Device.matrices_previous[1] = Device.matrices[1];
	Device.matrices[0].mView = mView;
	Device.matrices[0].mProject = mProject;
	Device.matrices[0].mProjectHud = mProjectHud;

	mViewHud_prev = mViewHud;
	mProjectHud_prev = mProjectHud;
	mFullTransformHud_prev = mFullTransformHud;
	mViewCam_prev = mViewCam;
	mProjectCam_prev = mProjectCam;
	mFullTransformCam_prev = mFullTransformCam;

	// Previous frame data -- 
	mView_prev = mView_saved;
	mProject_prev = mProject_saved;
	mFullTransform_prev = mFullTransform_saved; // Unused?

	m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);

	// pip true hud fov renders the weapon at the scene perspective while fully aimed through a PiP scope
	extern int g_svp_hud_true_fov;
	const float hud_fov_deg = (g_svp_hud_true_fov && true_pip_on && m_SecondViewport.IsSVPActive()
		&& g_pGamePersistent && g_pGamePersistent->m_pGShaderConstants->hud_params.x > 0.999f) ? fFOV : psHUD_FOV * 83.f;
	mProjectHud.build_projection(deg2rad(hud_fov_deg), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	mProjectCam.build_projection(deg2rad(83.f), fASPECT, R_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv->far_plane);
	
	mViewHud.set(mView);
	mViewCam.set(mView);
	mFullTransformHud.mul(mProjectHud, mViewHud);
	mFullTransformCam.mul(mProjectCam, mViewCam);
	if (true_pip_on)
		Device.matrices[0].mProjectHud = mProjectHud;

	// Save previous frame grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;

	GData.prev_pos[0].set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);
	GData.prev_dir[0].set(0.0f, -99.0f, 0.0f, 1.0f);

	for (int pBend = 1; pBend < _min(16, ps_ssfx_grass_interactive.y + 1); pBend++)
	{
		GData.prev_pos[pBend].set(GData.pos[pBend].x, GData.pos[pBend].y, GData.pos[pBend].z, GData.radius_curr[pBend]);
		GData.prev_dir[pBend].set(GData.dir[pBend].x, GData.dir[pBend].y, GData.dir[pBend].z, GData.str[pBend]);
	}

	// Save wind animation position
	wind_anim_prev = wind_anim_saved;
	wind_anim_saved = g_pGamePersistent->Environment().wind_anim;

	//RCache.set_xform_view ( mView );
	//RCache.set_xform_project ( mProject );
	D3DXMatrixInverse((D3DXMATRIX*)&mInvFullTransform, 0, (D3DXMATRIX*)&mFullTransform);

	vCameraPosition_saved = vCameraPosition;
	mFullTransform_saved = mFullTransform;
	mView_saved = mView;
	mProject_saved = mProject;

	STOP_PROFILE;

    // TODO: Try to move this upper
    secondary_tasks.run(&XRay::Engine::PreRenderPostTransformsThread);
	if (mt_calc_bones)
		secondary_tasks.run(&XRay::Engine::CalculateBonesThread);
	else
		XRay::Engine::CalculateBonesThread();

	Device.isRendering = true;
	Device.LuaGCDone = false;
	Device.LuaGCCount = 0;

	secondary_tasks.run(&XRay::Engine::GameThread);
	
#ifdef ECO_RENDER // ECO_RENDER START
	if (Device.Paused() || IsMainMenuActive() || ps_framelimiter)
	{
		PROF_EVENT("Eco Render");

		if (refresh_rate == 0)
			refresh_rate = GetMonitorRefresh();

		float rr;

		if (ps_framelimiter)
			rr = 1.f / ps_framelimiter;
		else
			rr = refresh_rate;

		time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		while (time_span.count() < rr)
		{
			tcurrentf = std::chrono::high_resolution_clock::now();
			time_span = std::chrono::duration_cast<std::chrono::duration<float>>(tcurrentf - tlastf);
		}
		tlastf = std::chrono::high_resolution_clock::now();
	}
#endif // ECO_RENDER END

#ifndef DEDICATED_SERVER
	Statistic->RenderTOTAL_Real.FrameStart();
	Statistic->RenderTOTAL_Real.Begin();

	if (b_is_Active && Begin())
	{
		START_PROFILE("Process seqRender");
		seqRender.Process(rp_Render);
		STOP_PROFILE;

		if (psDeviceFlags.test(rsCameraPos) || psDeviceFlags.test(rsStatistic) || Statistic->errors.size())
		{
			PROF_EVENT("Draw statistics");
			Statistic->Show();
		}

		End();
	}
	Statistic->RenderTOTAL_Real.End();
	Statistic->RenderTOTAL_Real.FrameEnd();
	Statistic->RenderTOTAL.accum = Statistic->RenderTOTAL_Real.accum;
#endif 
	Device.isRendering = false;

	secondary_tasks.wait();

	if (psLua_ParallelGC_debug && psLua_ParallelGC && Device.LuaGCDebug)
	{
		Device.LuaGCDebug();
	}

#ifdef DEDICATED_SERVER
    u32 FrameEndTime = TimerGlobal.GetElapsed_ms();
    u32 FrameTime = (FrameEndTime - FrameStartTime);
    u32 DSUpdateDelta = 1000 / g_svDedicateServerUpdateReate;
    if (FrameTime < DSUpdateDelta)
        Sleep(DSUpdateDelta - FrameTime);
#endif
	if (!b_is_Active)
		Sleep(1);
}

#ifdef INGAME_EDITOR
void CRenderDevice::message_loop_editor()
{
    m_editor->run();
    m_editor_finalize(m_editor);
    xr_delete(m_engine);
}
#endif // #ifdef INGAME_EDITOR

void CRenderDevice::Screenshot()
{
	PROF_EVENT();
	Render->Screenshot();
}

void CRenderDevice::message_loop()
{
#ifdef INGAME_EDITOR
    if (editor())
    {
        message_loop_editor();
        return;
    }
#endif
	MSG msg;
	PeekMessage(&msg, NULL, 0U, 0U, PM_NOREMOVE);
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		on_idle();
	}
}

void mt_DiscordThread(void*)
{
	while (true)
	{
		if (!pApp)
		{
			Msg("[Discord] pApp destroyed, killing thread");
			return;
		}

		//Discord
		if (use_discord && psDeviceFlags2.test(rsDiscord))
		{
			START_PROFILE("Discord");
			discord_core->RunCallbacks();
			updateDiscordPresence();
			STOP_PROFILE;
			Sleep(int(discord_update_rate * 1000));
		}
		else
		{
			Sleep(1000); // Sleep for 1 second if Discord is not used or disabled
		}
	}
}

void CRenderDevice::Run()
{
	// DUMP_PHASE;
	g_bLoaded = FALSE;
	Log("Starting engine...");
	thread_name("X-RAY Primary thread");
	// Startup timers and calculate timer delta
	dwTimeGlobal = 0;
	Timer_MM_Delta = 0;
	{
		u32 time_mm = timeGetTime();
		while (timeGetTime() == time_mm); // wait for next tick
		u32 time_system = timeGetTime();
		u32 time_local = TimerAsync();
		Timer_MM_Delta = time_system - time_local;
	}

	// Start extra threads
	thread_spawn(mt_FreezeThread, "Freeze detecting thread", 0, 0);
	thread_spawn(mt_DiscordThread, "X-RAY Discord thread", 0, 0);

	// Message cycle
	seqAppStart.Process(rp_AppStart);

	//m_pRender->ClearTarget();
	SetForegroundWindow(m_hWnd);
	message_loop();

	seqAppEnd.Process(rp_AppEnd);

	secondary_tasks.wait();
	ParticleWorkerCallback.clear();
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void CRenderDevice::FrameMove()
{
	PROF_EVENT("Render: Frame Move");

	if (InterlockedExchange(&g_monitor_list_dirty, 0))
		refresh_vid_monitor_list();

	dwFrame++;
	Core.dwFrame = dwFrame;
	dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;
	if (psDeviceFlags.test(rsConstantFPS))
	{
		PROF_EVENT("Constant FPS");

		// 20ms = 50fps
		//fTimeDelta = 0.020f;
		//fTimeGlobal += 0.020f;
		//dwTimeDelta = 20;
		//dwTimeGlobal += 20;
		// 33ms = 30fps
		fTimeDelta = 0.033f;
		fTimeGlobal += 0.033f;
		dwTimeDelta = 33;
		dwTimeGlobal += 33;
	}
	else
	{
		PROF_EVENT("Timer FPS");

		// Timer
		float fPreviousFrameTime = Timer.GetElapsed_sec();
		Timer.Start(); // previous frame
		fTimeDelta = 0.1f * fTimeDelta + 0.9f * fPreviousFrameTime;
		// smooth random system activity - worst case ~7% error
		//fTimeDelta = 0.7f * fTimeDelta + 0.3f*fPreviousFrameTime; // smooth random system activity
		if (fTimeDelta > .1f)
			fTimeDelta = .1f; // limit to 15fps minimum
		if (fTimeDelta <= 0.f)
			fTimeDelta = EPS_S + EPS_S; // limit to 15fps minimum
		if (Paused())
			fTimeDelta = 0.0f;
		// u64 qTime = TimerGlobal.GetElapsed_clk();
		fTimeGlobal = TimerGlobal.GetElapsed_sec(); //float(qTime)*CPU::cycles2seconds;
		u32 _old_global = dwTimeGlobal;
		dwTimeGlobal = TimerGlobal.GetElapsed_ms();
		dwTimeDelta = dwTimeGlobal - _old_global;
	}

	// Frame move
	Statistic->EngineTOTAL.Begin();

	START_PROFILE("Process seqFrame");
	Device.seqFrame.Process(rp_Frame);
	STOP_PROFILE;
	
	g_bLoaded = TRUE;
	
	Statistic->EngineTOTAL.End();
}

ENGINE_API BOOL bShowPauseString = TRUE;

void CRenderDevice::Pause(BOOL bOn, BOOL bTimer, BOOL bSound, LPCSTR reason)
{
	PROF_EVENT();

	static int snd_emitters_ = -1;

	if (g_bBenchmark)
		return;
#ifndef DEDICATED_SERVER
	if (bOn)
	{
		if (!Paused())
			bShowPauseString =
#ifdef INGAME_EDITOR
                editor() ? FALSE :
#endif // #ifdef INGAME_EDITOR
#ifdef DEBUG
                !xr_strcmp(reason, "li_pause_key_no_clip") ? FALSE :
#endif // DEBUG
				TRUE;

		if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()))
		{
			g_pauseMngr().Pause(true);
#ifdef DEBUG
            if (!xr_strcmp(reason, "li_pause_key_no_clip"))
                TimerGlobal.Pause(FALSE);
#endif // DEBUG
		}

		if (bSound && ::Sound)
		{
			snd_emitters_ = ::Sound->pause_emitters(true);
#ifdef DEBUG
			// Log("snd_emitters_[true]",snd_emitters_);
#endif // DEBUG
		}
	}
	else
	{
		if (bTimer && g_pauseMngr().Paused())
		{
			fTimeDelta = EPS_S + EPS_S;
			g_pauseMngr().Pause(false);
		}

		if (bSound)
		{
			if (snd_emitters_ > 0) //avoid crash
			{
				snd_emitters_ = ::Sound->pause_emitters(false);
#ifdef DEBUG
				// Log("snd_emitters_[false]",snd_emitters_);
#endif
			}
			else
			{
#ifdef DEBUG
                Log("Sound->pause_emitters underflow");
#endif
			}
		}
	}

#endif
}

bool CRenderDevice::Paused()
{
	return g_pauseMngr().Paused();
}

void CRenderDevice::OnWM_Activate(WPARAM wParam, LPARAM lParam)
{
	u16 fActive = LOWORD(wParam);
	BOOL fMinimized = (BOOL)HIWORD(wParam);
	BOOL bActive = ((fActive != WA_INACTIVE) && (!fMinimized)) ? TRUE : FALSE;

	if (psDeviceFlags2.test(rsAlwaysActive) && g_screenmode != 2)
	{
		Device.b_is_Active = TRUE;

		if (Device.b_hide_cursor != bActive)
		{
			Device.b_hide_cursor = bActive;

			if (Device.b_hide_cursor)
			{
				ShowCursor(FALSE);
				if (m_hWnd)
				{
					RECT winRect;
					GetClientRect(m_hWnd, &winRect);
					MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
					ClipCursor(&winRect);
				}
				pInput->OnAppActivate();
			}
			else
			{
				ShowCursor(TRUE);
				ClipCursor(NULL);
				pInput->OnAppDeactivate();
			}
		}

		return;
	}

	if (bActive != Device.b_is_Active)
	{
		Device.b_is_Active = bActive;

		if (Device.b_is_Active)
		{
			Device.seqAppActivate.Process(rp_AppActivate);
			app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;

#ifndef DEDICATED_SERVER
# ifdef INGAME_EDITOR
            if (!editor())
# endif // #ifdef INGAME_EDITOR
			ShowCursor(FALSE);
			if (m_hWnd)
			{
				RECT winRect;
				GetClientRect(m_hWnd, &winRect);
				MapWindowPoints(m_hWnd, nullptr, reinterpret_cast<LPPOINT>(&winRect), 2);
				ClipCursor(&winRect);
			}
#endif // #ifndef DEDICATED_SERVER
		}
		else
		{
			app_inactive_time_start = TimerMM.GetElapsed_ms();
			Device.seqAppDeactivate.Process(rp_AppDeactivate);
			ShowCursor(TRUE);
			ClipCursor(NULL);
		}
	}
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt)
{
	PROF_EVENT();

	if (mt)
		seqFrameMT.Add(f, REG_PRIORITY_HIGH);
	else
		seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f)
{
	PROF_EVENT();

	seqFrameMT.Remove(f);
	seqFrame.Remove(f);
}

CLoadScreenRenderer::CLoadScreenRenderer()
	: b_registered(false)
{
}

void CLoadScreenRenderer::start(bool b_user_input)
{
	PROF_EVENT();

	Device.seqRender.Add(this, 0);
	b_registered = true;
	b_need_user_input = b_user_input;
}

void CLoadScreenRenderer::stop()
{
	PROF_EVENT();

	if (!b_registered)
		return;
	Device.seqRender.Remove(this);
	pApp->destroy_loading_shaders();
	b_registered = false;
	b_need_user_input = false;
}

void CLoadScreenRenderer::OnRender()
{
	PROF_EVENT();

	pApp->load_draw_internal();
}

void CSecondVPParams::SetSVPActive(bool bState) //--#SM+#-- +SecondVP+
{
	const bool was_active = isActive.load(std::memory_order_acquire);
	if (scope_svp_enabled < 2)
	{
		if (bState != was_active)
		{
			if (!bState)
				isActive.store(false, std::memory_order_release);
			m_svp_session.fetch_add(1, std::memory_order_acq_rel);
			if (bState)
			{
				ClearWeaponPose();
				ClearSight();
				dlss_reset_next = true;
				isActive.store(true, std::memory_order_release);
			}
		}
		if (!bState)
		{
			ClearWeaponPose();
			ClearSight();
		}
		if (g_pGamePersistent != NULL)
			g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = bState ? 1.0f : 0.0f;
		return;
	}
	if (bState != was_active)
	{
		if (!bState)
		{
			isActive.store(false, std::memory_order_release);
			m_svp_session.fetch_add(1, std::memory_order_acq_rel);
		}
		if (bState)
		{
			xrCriticalSectionGuard guard(m_snapshot_lock);
			m_weapon_pose = WeaponPoseSnapshot{};
			dlss_reset_next = true;
			isActive.store(true, std::memory_order_release);
		}
	}
	if (!bState)
	{
		xrCriticalSectionGuard guard(m_snapshot_lock);
		m_weapon_pose = WeaponPoseSnapshot{};
		m_sight = SightSnapshot{};
	}
	if (g_pGamePersistent != NULL)
		g_pGamePersistent->m_pGShaderConstants->m_blender_mode.z = bState ? 1.0f : 0.0f;
}

void CSecondVPParams::PublishWeaponPose(const WeaponPoseSnapshot& pose)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_weapon_pose = pose;
}

bool CSecondVPParams::ReadWeaponPose(WeaponPoseSnapshot& pose) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	pose = m_weapon_pose;
	return pose.frame != u32(-1);
}

void CSecondVPParams::ClearWeaponPose()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_weapon_pose = WeaponPoseSnapshot{};
}

void CSecondVPParams::PublishSight(const SightSnapshot& sight)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_sight = sight;
}

bool CSecondVPParams::ReadSight(SightSnapshot& sight) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	sight = m_sight;
	return sight.frame != u32(-1);
}

void CSecondVPParams::ClearSight()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_sight = SightSnapshot{};
}

void CSecondVPParams::AppendFireTrace(const FireTrace& trace)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	m_fire_traces[m_fire_trace_head % 16] = trace;
	++m_fire_trace_head;
}

void CSecondVPParams::ReadFireTraces(FireTrace (&traces)[16]) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	for (u32 i = 0; i < 16; ++i)
		traces[i] = m_fire_traces[i];
}

static void svp_hash_bytes(u64& hash, const void* data, size_t size)
{
	const u8* bytes = static_cast<const u8*>(data);
	for (size_t i = 0; i < size; ++i)
	{
		hash ^= bytes[i];
		hash *= 1099511628211ull;
	}
}

static u64 svp_hash_optic_config(const CSecondVPParams::OpticConfig& config)
{
	u64 hash = 14695981039346656037ull;
	svp_hash_bytes(hash, &config.has_objective_offset, sizeof(config.has_objective_offset));
	svp_hash_bytes(hash, &config.has_hybrid_reflex, sizeof(config.has_hybrid_reflex));
	svp_hash_bytes(hash, &config.hybrid_reflex, sizeof(config.hybrid_reflex));
	svp_hash_bytes(hash, &config.zoom_type, sizeof(config.zoom_type));
	svp_hash_bytes(hash, &config.reticle_type, sizeof(config.reticle_type));
	svp_hash_bytes(hash, &config.weapon_id, sizeof(config.weapon_id));
	svp_hash_bytes(hash, &config.objective_offset, sizeof(config.objective_offset));
	svp_hash_bytes(hash, &config.objective_mm, sizeof(config.objective_mm));
	svp_hash_bytes(hash, &config.middle_grey, sizeof(config.middle_grey));
	svp_hash_bytes(hash, &config.adapt_speed, sizeof(config.adapt_speed));
	svp_hash_bytes(hash, &config.zero_m, sizeof(config.zero_m));
	svp_hash_bytes(hash, &config.tunneling_parallax, sizeof(config.tunneling_parallax));
	svp_hash_bytes(hash, &config.tunneling_min, sizeof(config.tunneling_min));
	svp_hash_bytes(hash, &config.tunneling_max, sizeof(config.tunneling_max));
	svp_hash_bytes(hash, &config.tracking_speed, sizeof(config.tracking_speed));
	svp_hash_bytes(hash, &config.tracking_accel_mm_s2, sizeof(config.tracking_accel_mm_s2));
	svp_hash_bytes(hash, &config.tracking_limit_mm, sizeof(config.tracking_limit_mm));
	svp_hash_bytes(hash, &config.eye_relief_low_mm, sizeof(config.eye_relief_low_mm));
	svp_hash_bytes(hash, &config.eye_relief_high_mm, sizeof(config.eye_relief_high_mm));
	svp_hash_bytes(hash, &config.exit_pupil_low_mm, sizeof(config.exit_pupil_low_mm));
	svp_hash_bytes(hash, &config.exit_pupil_high_mm, sizeof(config.exit_pupil_high_mm));
	svp_hash_bytes(hash, &config.pupil_parity, sizeof(config.pupil_parity));
	svp_hash_bytes(hash, &config.pupil_field_low, sizeof(config.pupil_field_low));
	svp_hash_bytes(hash, &config.pupil_field_high, sizeof(config.pupil_field_high));
	svp_hash_bytes(hash, &config.transmission, sizeof(config.transmission));
	svp_hash_bytes(hash, &config.twilight_strength, sizeof(config.twilight_strength));
	svp_hash_bytes(hash, &config.physical_min, sizeof(config.physical_min));
	svp_hash_bytes(hash, &config.physical_max, sizeof(config.physical_max));
	svp_hash_bytes(hash, config.context, sizeof(config.context));
	svp_hash_bytes(hash, config.weapon, sizeof(config.weapon));
	svp_hash_bytes(hash, config.scope, sizeof(config.scope));
	svp_hash_bytes(hash, config.diagnostic_scope, sizeof(config.diagnostic_scope));
	svp_hash_bytes(hash, config.identity_source, sizeof(config.identity_source));
	svp_hash_bytes(hash, config.profile, sizeof(config.profile));
	svp_hash_bytes(hash, config.spec, sizeof(config.spec));
	svp_hash_bytes(hash, config.model, sizeof(config.model));
	svp_hash_bytes(hash, config.binding, sizeof(config.binding));
	svp_hash_bytes(hash, config.binding_section, sizeof(config.binding_section));
	svp_hash_bytes(hash, config.source, sizeof(config.source));
	return hash;
}

static u32 svp_next_nonzero(u32& value)
{
	++value;
	if (!value)
		++value;
	return value;
}

bool CSecondVPParams::ConnectOpticApi(u32 version)
{
	if (version != optic_api_version)
		return false;

	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (m_optic_api_connected.load(std::memory_order_relaxed))
		return true;

	ResetOpticConfigLocked();
	m_optic_api_connected.store(true, std::memory_order_release);
	return true;
}

void CSecondVPParams::SetOpticScopeMode(u8 mode)
{
	const u8 previous = m_optic_scope_mode.load(std::memory_order_acquire);
	if (previous == mode)
		return;
	if (!mode)
		m_optic_scope_mode.store(0, std::memory_order_release);

	xrCriticalSectionGuard guard(m_snapshot_lock);
	ResetOpticConfigLocked();
	if (mode)
		m_optic_scope_mode.store(mode, std::memory_order_release);
}

u32 CSecondVPParams::BeginOpticContext(LPCSTR context, LPCSTR weapon, u32 weapon_id,
	LPCSTR scope, u8 zoom_type,
	LPCSTR identity_source, LPCSTR diagnostic_scope)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	OpticConfig next;
	next.context_token = svp_next_nonzero(m_optic_token_counter);
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.zoom_type = zoom_type;
	next.weapon_id = weapon_id;
	xr_strcpy(next.context, sizeof(next.context), context ? context : "");
	xr_strcpy(next.weapon, sizeof(next.weapon), weapon ? weapon : "");
	xr_strcpy(next.scope, sizeof(next.scope), scope ? scope : "");
	xr_strcpy(next.identity_source, sizeof(next.identity_source), identity_source ? identity_source : "");
	xr_strcpy(next.diagnostic_scope, sizeof(next.diagnostic_scope), diagnostic_scope ? diagnostic_scope : "");
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
	return next.context_token;
}

bool CSecondVPParams::PublishOpticConfig(u32 context_token, const OpticConfig& config)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (!context_token || context_token != m_optic_accepted.context_token ||
		xr_strcmp(config.context, m_optic_accepted.context) ||
		xr_strcmp(config.weapon, m_optic_accepted.weapon) ||
		config.weapon_id != m_optic_accepted.weapon_id ||
		xr_strcmp(config.scope, m_optic_accepted.scope) ||
		xr_strcmp(config.identity_source, m_optic_accepted.identity_source) ||
		xr_strcmp(config.diagnostic_scope, m_optic_accepted.diagnostic_scope) ||
		config.zoom_type != m_optic_accepted.zoom_type)
		return false;

	OpticConfig next = config;
	next.valid = true;
	next.context_token = context_token;
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.frame = u32(-1);
	next.session = 0;
	next.fingerprint = svp_hash_optic_config(next);
	if (m_optic_accepted.valid && next.fingerprint == m_optic_accepted.fingerprint)
		return true;

	next.generation = svp_next_nonzero(m_optic_generation_counter);
	m_optic_accepted = next;
	return true;
}

bool CSecondVPParams::RejectOpticConfig(u32 context_token)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (!context_token || context_token != m_optic_accepted.context_token)
		return false;

	OpticConfig next;
	next.context_token = m_optic_accepted.context_token;
	next.zoom_type = m_optic_accepted.zoom_type;
	next.weapon_id = m_optic_accepted.weapon_id;
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	xr_strcpy(next.context, sizeof(next.context), m_optic_accepted.context);
	xr_strcpy(next.weapon, sizeof(next.weapon), m_optic_accepted.weapon);
	xr_strcpy(next.scope, sizeof(next.scope), m_optic_accepted.scope);
	xr_strcpy(next.identity_source, sizeof(next.identity_source), m_optic_accepted.identity_source);
	xr_strcpy(next.diagnostic_scope, sizeof(next.diagnostic_scope), m_optic_accepted.diagnostic_scope);
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
	return true;
}

bool CSecondVPParams::ClearOpticConfig(u32 context_token)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	if (!context_token || context_token != m_optic_accepted.context_token)
		return false;

	OpticConfig next;
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
	return true;
}

void CSecondVPParams::InvalidateOpticConfig()
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	ResetOpticConfigLocked();
}

void CSecondVPParams::ResetOpticConfigLocked()
{
	u32 route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed) + 1;
	if (!route_epoch)
		route_epoch = 1;
	m_optic_route_epoch.store(route_epoch, std::memory_order_release);

	OpticConfig next;
	next.generation = svp_next_nonzero(m_optic_generation_counter);
	next.route_epoch = route_epoch;
	next.fingerprint = svp_hash_optic_config(next);
	m_optic_accepted = next;
}

bool CSecondVPParams::ReadOpticConfig(OpticConfig& config) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	config = m_optic_accepted;
	return config.valid;
}

void CSecondVPParams::LatchOpticConfig(u32 frame, u32 session)
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	const u32 route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
	if (m_optic_active.frame == frame)
		return;
	const bool enabled = IsOpticApiEnabled();
	m_optic_active = enabled ? m_optic_accepted : m_optic_neutral;
	m_optic_active.typed_route = enabled;
	m_optic_active.frame = frame;
	m_optic_active.session = session;
	m_optic_active.route_epoch = route_epoch;
}

const CSecondVPParams::OpticConfig& CSecondVPParams::RenderOpticConfig() const
{
	return m_optic_active;
}

void CSecondVPParams::ReadOpticConfigState(OpticConfig& accepted, OpticConfig& active, u32& route_epoch) const
{
	xrCriticalSectionGuard guard(m_snapshot_lock);
	accepted = m_optic_accepted;
	active = m_optic_active;
	route_epoch = m_optic_route_epoch.load(std::memory_order_relaxed);
}

bool CSecondVPParams::IsSVPFrame() //--#SM+#-- +SecondVP+
{
	if (Device.true_pip_on)
		return m_render_pass_is_svp;
	return IsSVPActive() && Device.dwFrame % frameDelay == 0;
}

void CRenderDevice::prepare_matrices()
{
	auto svp = m_SecondViewport.IsSVPFrame();
	// per-viewport previous matrices (0 = main, 1 = SVP) for motion vectors
	mView_prev = Device.matrices_previous[svp].mView;
	mProject_prev = Device.matrices_previous[svp].mProject;
	m_pRender->SetCacheXform_prev(mView_prev, mProject_prev);
	// grass + wind prev stay once-per-frame in the device frame fn, not here, because
	// prepare_matrices runs per SetActive and wind prev=saved/saved=cur is not idempotent
}
