#include "stdafx.h"
#include "../xrRender/svp_console.h"

#if defined(USE_DX11)

bool CRenderTarget::svp_nvg_objective_pass()
{
	extern Fvector4 ps_dev_param_8;
	auto& vp = Device.m_SecondViewport;
	const u32 session = vp.GetSVPSession();
	const bool objective = ps_r__svp_nvg_objective && ps_dev_param_8.x >= 1.f
		&& Device.true_pip_on && vp.IsSVPActive()
		&& vp.m_render_pass_is_svp && this == RImplementation.TargetSVP;
	if (!objective)
		return false;

	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
	if (!RCache.get_c("svp_nvg_view"))
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			PipMsg("[SVP-NVG] objective sensor disabled missing svp_nvg_view");
		}
		return false;
	}

	vp.svp_nvg_objective_region = true;
	phase_nightvision();
	vp.svp_nvg_objective_region = false;
	if (vp.IsSVPActive() && vp.GetSVPSession() == session)
	{
		vp.svp_nvg_sensor_frame = Device.dwFrame;
		vp.svp_nvg_sensor_session = session;
	}
	return true;
}

bool CRenderTarget::svp_nvg_pass()
{
	extern Fvector4 ps_dev_param_8;
	const bool active = ps_dev_param_8.x >= 1.f;
	const bool split = ps_r__svp_nvg_objective && active && Device.true_pip_on
		&& Device.m_SecondViewport.IsSVPActive()
		&& this == RImplementation.TargetMain
		&& !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty();
	if (!split)
		return false;

	PIX_EVENT(svp_nvg_objective);
	Device.m_SecondViewport.svp_nvg_objective_region = false;
	EnsureScopeShaders();
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
	R_constant* objective_view = RCache.get_c("svp_nvg_view");
	if (!objective_view)
	{
		static bool warned = false;
		if (!warned)
		{
			warned = true;
			PipMsg("[SVP-NVG] disabled missing svp_nvg_view");
		}
		return false;
	}

	if (ps_r__svp_stats)
		++svp_stats_nvg_split;

	const u32 color = color_rgba(0, 0, 0, 255);
	const float width = float(Device.dwWidth);
	const float height = float(Device.dwHeight);
	u32 offset = 0;
	ref_rt& destination = RImplementation.o.dx10_msaa ? rt_Generic : rt_Color;

	u_setrt(destination, nullptr, nullptr, HW.pBaseZB);
	RCache.set_CullMode(CULL_NONE);

	auto draw_quad = [&]()
	{
		FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
		pv->set(0, height, EPS_S, 1.f, color, 0.f, 1.f); pv++;
		pv->set(0, 0, EPS_S, 1.f, color, 0.f, 0.f); pv++;
		pv->set(width, height, EPS_S, 1.f, color, 1.f, 1.f); pv++;
		pv->set(width, 0, EPS_S, 1.f, color, 1.f, 0.f); pv++;
		RCache.Vertex.Unlock(4, g_combine->vb_stride);
		RCache.set_Geometry(g_combine);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	};

	RCache.set_Element(s_nightvision->E[0]);
	RCache.set_ColorWriteEnable(0);
	RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0x80,
		D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	draw_quad();

	draw_scope(s_svp_distort_stamp, []()
	{
		RCache.set_c("scope_phase", 0);
		RCache.set_ColorWriteEnable(0);
		RCache.set_Stencil(TRUE, D3DCMP_ALWAYS, 0x80, 0xff, 0x80,
			D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	});
	RCache.set_ColorWriteEnable();

	auto& viewport = Device.m_SecondViewport;
	const bool objective_ready = viewport.svp_nvg_sensor_frame == Device.dwFrame
		&& viewport.svp_nvg_sensor_session == viewport.GetSVPSession();
	Device.m_SecondViewport.svp_nvg_objective_region = !objective_ready;
	RCache.set_Element(s_nightvision->E[objective_ready ? 0 : ps_r2_nightvision]);
	if (!objective_ready)
		RCache.set_c(objective_view, 1.f, 0.f, 0.f, 0.f);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x80, 0x80, 0x00);
	draw_quad();

	Device.m_SecondViewport.svp_nvg_objective_region = false;
	RCache.set_Element(s_nightvision->E[ps_r2_nightvision]);
	RCache.set_c(objective_view, 0.f, 0.f, 0.f, 0.f);
	RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x00, 0x80, 0x00);
	draw_quad();

	RCache.set_Stencil(FALSE);
	HW.pContext->CopyResource(rt_Generic_0->pSurface, destination->pSurface);

	if (ps_r__svp_diag)
	{
		static u32 last_log = 0;
		if (Device.dwTimeGlobal - last_log > 1000)
		{
			last_log = Device.dwTimeGlobal;
			const float generation = floorf(ps_dev_param_8.x);
			PipMsg("[SVP-NVG] objective=1 reflected=1 depth=%s gen=%.0f tubes=%.1f msaa=%d",
				objective_ready ? "svp" : "main-fallback",
				generation, (ps_dev_param_8.x - generation) * 10.f,
				RImplementation.o.dx10_msaa ? 1 : 0);
		}
	}

	return true;
}

#else

bool CRenderTarget::svp_nvg_objective_pass()
{
	return false;
}

bool CRenderTarget::svp_nvg_pass()
{
	return false;
}

#endif
