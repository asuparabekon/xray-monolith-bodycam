#include "stdafx.h"
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h" // SCOPE_PHASE_RETICLE
#include "r4_3dss_reticle.h"

// PiP forwards reticle emission to the shared 3DSS glow passes. Lens composites
// supply their own reticle emission; hybrid magnifiers also carry capture-space
// reflex emission. Reference captures precede reflex drawing, so that path stores
// the reflex separately and projects it through the capture-coordinate target.

// the emission variants include the 3DSS lens shaders; EnsureScopeShaders creates them when those are installed
void CRenderTarget::draw_reticle_emission(const ref_shader& shader, ID3DDepthStencilView* depth)
{
	if (!shader)
		return;
	const bool separate = reticle_glow_wanted();
	if (separate)
	{
		u_setrt(rt_reticle_emission, rt_reticle_glow_source, nullptr, depth);
		reticle_glow_frame = Device.dwFrame;
		reticle_glow_dirty = true;
		for (const auto& lens : RImplementation.GMBase.RGraph.mapScopeHUDSorted)
			reticle_glow_note(lens.pVisual);
	}
	else
		u_setrt(rt_Generic_0, nullptr, nullptr, depth);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	const CRenderTarget* S = RImplementation.TargetSVP;
	const float capture = separate && S && S->reticle_capture_frame == Device.dwFrame && !S->reticle_capture_light_current() ? 1.f : 0.f;
	// the lens shaders take their screen derivatives in main view pixels (output_res), which the stock image pass
	// sets only for its own shader; without it the capture coordinate and the reticle angle are not defined
	const float w = float(Width), h = float(Height);
	draw_scope(shader,
		[capture, w, h]
		{
			RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE);
			RCache.set_c("output_res", w, h, 1.f / w, 1.f / h);
			RCache.set_c("reticle_capture_glow", capture, 0.f, 0.f, 0.f);
		});
}

// a hybrid reflex's targets in capture space, the size of the capture, cleared for this frame's draw: its glow
// targets, and with light (the Reference image shows the capture) the target the reflex itself draws into
void CRenderTarget::reticle_capture_begin(bool light)
{
	const u32 w = rt_secondVP->dwWidth, h = rt_secondVP->dwHeight;
	const FLOAT clear[4] = { 0.f, 0.f, 0.f, 0.f };
	auto target = [&](ref_rt& rt, LPCSTR name)
	{
		// create() returns the registered target by name whatever its size, so drop a stale one first
		if (rt && (rt->dwWidth != w || rt->dwHeight != h))
			rt.destroy();
		if (!rt)
			rt.create(name, w, h, D3DFMT_A16B16G16R16F);
		HW.pContext->ClearRenderTargetView(rt->pRT, clear);
	};
	if (light)
	{
		target(rt_reticle_capture_light, "$user$reticle_capture_light");
		reticle_capture_light_frame = Device.dwFrame;
	}
	if (!reticle_glow_wanted())
		return;
	target(rt_reticle_capture_emission, "$user$reticle_capture_emission");
	target(rt_reticle_capture_glow, "$user$reticle_capture_glow");
	reticle_capture_frame = Device.dwFrame;
}

// the hybrid reflex of this frame went into the capture-space target for the Reference image
bool CRenderTarget::reticle_capture_light_current() const
{
	return reticle_capture_light_frame == Device.dwFrame;
}

// main view, after the Reference image and its reticle: the hybrid reflex where the image shows the capture
// ($user$svp_reference_capture_tc, written by the image pass), through the image's pupil transmission. The reflex's
// own light adds to the frame, its atlas reticle core and glow source to the glow targets. The image pass kept to
// the visible lens with the depth test, or with deferred glass the glass tag does it here; no depth test either way.
void CRenderTarget::draw_reference_capture_light()
{
	const CRenderTarget* S = RImplementation.TargetSVP;
	if (!S || !S->reticle_capture_light_current() || !rt_scope_reference_capture_tc || !s_scope_reference_capture_light)
		return;
	const bool glow = S->reticle_capture_frame == Device.dwFrame;
	if (glow)
	{
		u_setrt(rt_Generic_0, rt_reticle_emission, rt_reticle_glow_source, nullptr);
		reticle_glow_frame = Device.dwFrame;
		reticle_glow_dirty = true;
	}
	else
		u_setrt(rt_Generic_0, nullptr, nullptr, nullptr);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	const float deferred = scope_glass_deferred_current() ? 1.f : 0.f;
	draw_scope(
		s_scope_reference_capture_light, [glow, deferred] { RCache.set_c("reticle_capture_glow", glow ? 1.f : 0.f, deferred, 0.f, 0.f); });
}

// Atlas reticle paint is lit by the image the lens shows: points $user$viewport2 at that image for the PiP reticle
// phase (the eyepiece capture) and back at the main view's copy afterwards
void CRenderTarget::reticle_atlas_image(const ref_rt& image)
{
	if (!image)
		return;
	ref_texture t;
	t.create(r2_RT_secondVP);
	t->surface_set(image->pSurface);
	RCache.Invalidate();
}
#endif
