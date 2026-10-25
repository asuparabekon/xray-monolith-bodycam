#include "stdafx.h"
#include "r4_texture_copy.h"
#if defined(USE_DX11)
#include "../xrRender/FBasicVisual.h"
#include "r4_3dss_reticle.h"
#include "r4_reticle_atlas.h"
#include "r4_reticle_vector.h"

// Lens/reflex shaders write scene-linear core and glow emission to MRT slots 2/3.
// After tone mapping, reticle_glow_compose applies exposure and three blur lobes.
// Keeping emission separate prevents the reticle from feeding the scene bloom.
// Disabled bloom exclusion uses the shaders' inline-lighting path instead.

// the glow passes. Lobes: 0 half-size copy of the glow source, 1 quarter-size copy of half 0, 2..5 a Gaussian
// along one axis over quarter 0, quarter 1, half 0 and half 1. Final: 0 the tight lobe's Gaussian along x over the
// glow source, 1 the composite over the frame.
class CBlender_reticle_glow : public IBlender
{
	bool last; // the tight lobe and the composite
public:
	LPCSTR getComment() override { return "3DSS reticle glow"; }
	BOOL canBeDetailed() override { return FALSE; }
	BOOL canBeLMAPped() override { return FALSE; }
	explicit CBlender_reticle_glow(bool last) : last(last) { description.CLS = 0; }

	void Compile(CBlender_Compile& C) override
	{
		IBlender::Compile(C);
		static const LPCSTR sources[] = { "$user$reticle_glow_source", "$user$reticle_glow_half0", "$user$reticle_glow_quarter0",
			"$user$reticle_glow_quarter1", "$user$reticle_glow_half0", "$user$reticle_glow_half1" };
		if (last && C.iElement == 0)
		{
			C.r_Pass("stub_screen_space", "reticle_glow_blur", FALSE, FALSE, FALSE);
			C.r_dx10Texture("s_glow_source", "$user$reticle_glow_source");
		}
		else if (!last)
		{
			C.r_Pass("stub_screen_space", C.iElement < 2 ? "reticle_glow_down" : "reticle_glow_blur", FALSE, FALSE, FALSE);
			C.r_dx10Texture("s_glow_source", sources[C.iElement]);
		}
		else
		{
			C.r_Pass("stub_screen_space", "reticle_emission_combine", FALSE, FALSE, FALSE, TRUE, D3DBLEND_ONE, D3DBLEND_INVSRCALPHA);
			C.r_dx10Texture("s_reticle_emission", "$user$reticle_emission");
			C.r_dx10Texture("s_glow_tight", "$user$reticle_glow_full");
			C.r_dx10Texture("s_glow_mid", "$user$reticle_glow_half0");
			C.r_dx10Texture("s_glow_wide", "$user$reticle_glow_quarter0");
			C.r_dx10Texture("s_tonemap", "$user$tonemap");
		}
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
	}
};

// main view, with the render target: the glow targets and their passes, so the first sight drawn compiles nothing
void CRenderTarget::reticle_glow_create()
{
	rt_reticle_emission.create("$user$reticle_emission", Width, Height, D3DFMT_A16B16G16R16F);
	rt_reticle_glow_source.create("$user$reticle_glow_source", Width, Height, D3DFMT_A16B16G16R16F);
	rt_reticle_image_emission.create("$user$reticle_image_emission", Width, Height, D3DFMT_A16B16G16R16F);
	rt_reticle_image_glow.create("$user$reticle_image_glow", Width, Height, D3DFMT_A16B16G16R16F);
	const u32 hw = _max(Width / 2, 1u), hh = _max(Height / 2, 1u);
	rt_reticle_glow_full.create("$user$reticle_glow_full", Width, Height, D3DFMT_A16B16G16R16F);
	rt_reticle_glow_half[0].create("$user$reticle_glow_half0", hw, hh, D3DFMT_A16B16G16R16F);
	rt_reticle_glow_half[1].create("$user$reticle_glow_half1", hw, hh, D3DFMT_A16B16G16R16F);
	rt_reticle_glow_quarter[0].create("$user$reticle_glow_quarter0", _max(hw / 2, 1u), _max(hh / 2, 1u), D3DFMT_A16B16G16R16F);
	rt_reticle_glow_quarter[1].create("$user$reticle_glow_quarter1", _max(hw / 2, 1u), _max(hh / 2, 1u), D3DFMT_A16B16G16R16F);
	static CBlender_reticle_glow lobes(false), composite(true);
	s_reticle_glow.create(&lobes, "reticle_glow");
	s_reticle_glow_final.create(&composite, "reticle_glow_final");
	reticle_glow_dirty = true;
}

// main view, before any HUD or lens pass of the frame
void CRenderTarget::reticle_glow_begin()
{
	if (this != RImplementation.TargetMain)
		return;
	reticle_vector_edit_apply(); // the reticle editor's queued rebuilds, on the render thread
	if (!reticle_glow_dirty)
		return;
	const FLOAT clear[4] = { 0.f, 0.f, 0.f, 0.f };
	HW.pContext->ClearRenderTargetView(rt_reticle_emission->pRT, clear);
	HW.pContext->ClearRenderTargetView(rt_reticle_glow_source->pRT, clear);
	reticle_glow_dirty = false;
}

static u32 s_reticle_glow_size_frame = u32(-1);
static float s_reticle_glow_size = 1.f;

// a sight drawn into the glow targets this frame: the frame's glow lobes take the largest glow size among them
void CRenderTarget::reticle_glow_note(dxRender_Visual* visual)
{
	const float size = visual ? reticle_atlas_glow_size(visual->GetTexture()) : 0.f;
	if (size <= 0.f)
		return;
	if (s_reticle_glow_size_frame != Device.dwFrame)
	{
		s_reticle_glow_size_frame = Device.dwFrame;
		s_reticle_glow_size = size;
	}
	else
		s_reticle_glow_size = _max(s_reticle_glow_size, size);
}

static void reticle_glow_rebind(ID3DRenderTargetView* const views[4], ID3DDepthStencilView* depth)
{
	RCache.Invalidate();
	for (u32 i = 0; i < 4; ++i)
		RCache.set_RT(views[i], i);
	RCache.set_ZB(depth);
}

C3DSSReticleGlow::C3DSSReticleGlow(CRenderTarget* target, bool light)
{
	if (!target)
		return;
	const bool active = light && target == RImplementation.TargetMain && reticle_glow_wanted();
	if (active)
	{
		target->reticle_glow_frame = Device.dwFrame;
		target->reticle_glow_dirty = true;
	}
	hold(active ? target->rt_reticle_emission->pRT : nullptr, active ? target->rt_reticle_glow_source->pRT : nullptr, light);
}

C3DSSReticleGlow::C3DSSReticleGlow(const ref_rt& core, const ref_rt& glow)
{
	const bool active = core && glow && reticle_glow_wanted();
	hold(active ? core->pRT : nullptr, active ? glow->pRT : nullptr, true);
}

void C3DSSReticleGlow::hold(ID3DRenderTargetView* core, ID3DRenderTargetView* glow, bool light)
{
	for (u32 i = 0; i < 4; ++i)
		saved[i] = RCache.get_RT(i);
	depth = RCache.get_ZB();
	bound = core && glow;
	changed = bound || !light;
	if (!changed)
		return;
	saved_reflex_glow = g_reticle_reflex_glow;
	g_reticle_reflex_glow = bound ? 1.f : -1.f;
	ID3DRenderTargetView* views[4] = { saved[0], saved[1], bound ? core : saved[2], bound ? glow : saved[3] };
	reticle_glow_rebind(views, depth);
}

C3DSSReticleGlow::~C3DSSReticleGlow()
{
	if (!changed)
		return;
	g_reticle_reflex_glow = saved_reflex_glow;
	reticle_glow_rebind(saved, depth);
}

// The stock 3DSS reticle pass: the reflex reticles, then the lenses, which show the frame as it is after the
// reflexes (a magnifier shows the red dot behind it). A lens shows the reflex glow through its image from copies
// of the glow targets taken with the frame (reticle_atlas_reflex_glow.y).
void CRenderTarget::render_3DSS_reticles()
{
	auto& G = RImplementation.GMBase.RGraph;
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();

	bool reflex_glow = false;
	if (!G.mapScopeReflexHUD.empty())
	{
		C3DSSReticleGlow glow(this);
		glow.note(G.mapScopeReflexHUD);
		reflex_glow = glow.active();
		RImplementation.GMBase.r_dsgraph_render_ReflexSorted();
	}

	CopyTextureSurface(rt_Generic_2->pTexture._get(), RImplementation.Target->rt_Position->pTexture._get());

	CopyTextureSurface(rt_Generic_temp->pTexture._get(), rt_Generic_0->pTexture._get());

	if (reflex_glow && !G.mapScopeHUDSorted.empty())
	{
		HW.pContext->CopyResource(rt_reticle_image_emission->pSurface, rt_reticle_emission->pSurface);
		HW.pContext->CopyResource(rt_reticle_image_glow->pSurface, rt_reticle_glow_source->pSurface);
		g_reticle_image_glow = 1.f;
	}
	if (!G.mapScopeHUDSorted.empty())
	{
		C3DSSReticleGlow glow(this);
		glow.note(G.mapScopeHUDSorted);
		RImplementation.render_Reticle();
	}
	g_reticle_image_glow = 0.f;
}

// after the final tone curve and the HDR10 bloom, into the frame target combine_2 wrote; done already this
// frame when night vision took the reticle (reticle_glow_compose_before_nightvision)
void CRenderTarget::reticle_glow_compose()
{
	if (this != RImplementation.TargetMain || reticle_glow_frame != Device.dwFrame || reticle_glow_composed_frame == Device.dwFrame)
		return;
	reticle_glow_composite(RImplementation.o.dx10_msaa ? rt_Generic : rt_Color, false);
}

// Night vision intensifies the reticle's light like the rest of the scene: under it the reticle's light is added
// to the image the night vision pass reads (rt_Generic_0, encoded by the installed combine), before the pass,
// instead of over the final frame.
void CRenderTarget::reticle_glow_compose_before_nightvision()
{
	if (this != RImplementation.TargetMain || reticle_glow_frame != Device.dwFrame)
		return;
	reticle_glow_composite(rt_Generic_0, true);
	reticle_glow_composed_frame = Device.dwFrame;
}

// the glow lobes from this frame's reticle targets, then the cores and lobes over target (night vision: in the
// encoding of its combine)
void CRenderTarget::reticle_glow_composite(const ref_rt& target, bool night_vision)
{
	const float w = float(Device.dwWidth), h = float(Device.dwHeight);
	u32 offset = 0;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, g_combine->vb_stride, offset);
	const u32 C = color_rgba(0, 0, 0, 255);
	pv->set(0, h, EPS_S, 1.f, C, 0.f, 1.f);
	pv++;
	pv->set(0, 0, EPS_S, 1.f, C, 0.f, 0.f);
	pv++;
	pv->set(w, h, EPS_S, 1.f, C, 1.f, 1.f);
	pv++;
	pv->set(w, 0, EPS_S, 1.f, C, 1.f, 0.f);
	pv++;
	RCache.Vertex.Unlock(4, g_combine->vb_stride);

	// lobe sizes are set for 1080p and follow the resolution and the drawn sight's glow size
	const float px = h / 1080.f * (s_reticle_glow_size_frame == Device.dwFrame ? s_reticle_glow_size : 1.f);
	const Fvector& size = ps_r__reticle_glow_size;
	const Fvector& weight = ps_r__reticle_glow_weight;
	auto pass = [&](ref_shader& shader, u32 element, const ref_rt& target, float dx, float dy, float sigma)
	{
		set_viewport_size(HW.pContext, float(target->dwWidth), float(target->dwHeight));
		u_setrt(target, nullptr, nullptr, nullptr);
		RCache.set_Element(shader->E[element]);
		RCache.set_c("reticle_glow_blur", dx, dy, sigma, 0.f);
		RCache.set_Geometry(g_combine);
		RCache.set_CullMode(CULL_NONE);
		RCache.set_Stencil(FALSE);
		RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	};
	if (weight.y > 0.f || weight.z > 0.f)
		pass(s_reticle_glow, 0, rt_reticle_glow_half[0], 0, 0, 0);
	if (weight.z > 0.f)
	{
		pass(s_reticle_glow, 1, rt_reticle_glow_quarter[0], 0, 0, 0);
		pass(s_reticle_glow, 2, rt_reticle_glow_quarter[1], 1, 0, size.z * px / 4.f);
		pass(s_reticle_glow, 3, rt_reticle_glow_quarter[0], 0, 1, size.z * px / 4.f);
	}
	if (weight.y > 0.f)
	{
		pass(s_reticle_glow, 4, rt_reticle_glow_half[1], 1, 0, size.y * px / 2.f);
		pass(s_reticle_glow, 5, rt_reticle_glow_half[0], 0, 1, size.y * px / 2.f);
	}
	if (weight.x > 0.f)
		pass(s_reticle_glow_final, 0, rt_reticle_glow_full, 1, 0, size.x * px);

	set_viewport_size(HW.pContext, w, h);
	u_setrt(target, nullptr, nullptr, HW.pBaseZB);
	RCache.set_Element(s_reticle_glow_final->E[1]);
	RCache.set_c("reticle_glow_weight", weight.x, weight.y, weight.z, ps_r__reticle_glow_shoulder);
	RCache.set_c("reticle_glow_target", night_vision ? 1.f : 0.f, 0.f, 0.f, 0.f);
	RCache.set_c(
		"reticle_glow_tint", ps_r__reticle_glow_tint, size.x * px, ps_r__reticle_glow_exposure, ps_r__reticle_glow_halo);
	// the main view's tone map as r4_rendertarget_phase_luminance.cpp sets it, with the glow's floor beside its own
	const float amount = ps_r2_ls_flags.test(R2FLAG_TONEMAP) ? ps_r2_tonemap_amount : 0.f;
	RCache.set_c("reticle_glow_exposure", 1.f + (ps_r2_tonemap_middlegray - 1.f) * amount, amount,
		1.f + (ps_r2_tonemap_low_lum - 1.f) * amount, 1.f + (ps_r__reticle_glow_lowlum - 1.f) * amount);
	RCache.set_Geometry(g_combine);
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}
#endif
