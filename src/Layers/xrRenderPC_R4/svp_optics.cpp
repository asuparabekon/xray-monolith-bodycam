#include "stdafx.h"
#include "r4_texture_copy.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/SkeletonX.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"
#include "../../xrEngine/svp_gameplay_cvars.h"
#if defined(USE_DX11)
#include "../../../gamedata/shaders/r3/scope_defines.h"
#include "svp_shader_probe.h"
#include "svp_physical_optics.h"
#include "svp_optics.h"
#include "svp_target.h"
#include "svp_lens_lighting.h"
#include "svp_reference_pupil_tracking.h"
#include "svp_stats.h"
#include "svp_prefixed_blender.h"
#include "scope_vslr_renderer.h"
#include "scope_glass_head.h"
#endif

#if defined(USE_DX11) //  Redotix99: for 3D Shader Based Scopes
static SvpReferencePupil::Eye s_reference_pupil;

#include "svp_reference_renderer.inl"
// Reference highlight, blur and dispersion filters, rendered at half the size of the capture.
void CRenderTarget::phase_scope_reference_filters(const ref_rt& source, float threshold, float range, float contrast)
{
	const u32 width = source->dwWidth / 2, height = source->dwHeight / 2;
	// In filter-mode order: highlights, horizontal blur, vertical blur, dispersion.
	static const char* names[] = { "$user$svp_reference_high", "$user$svp_reference_high_x", "$user$svp_reference_high_blur",
		"$user$svp_reference_dispersion" };
	for (unsigned i = 0; i < 4; ++i)
		svp_reference_target(rt_scope_reference_filter[i], names[i], width, height);
	if (!s_scope_reference_filter)
		s_scope_reference_filter.create("scope_reference_filter");
	if (!t_scope_reference_filter_source)
		t_scope_reference_filter_source.create("$user$svp_reference_filter_source");

	D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	HW.pContext->RSGetViewports(&viewportCount, savedViewports);
	for (unsigned mode = 0; mode < 4; ++mode)
	{
		svp_reference_unbind();
		// Highlights and dispersion read the capture; each blur reads the target before it.
		const ref_rt& input = (mode == 0 || mode == 3) ? source : rt_scope_reference_filter[mode - 1];
		t_scope_reference_filter_source->surface_set(input->pSurface);
		u_setrt(rt_scope_reference_filter[mode], nullptr, nullptr, nullptr);
		set_viewport_size(HW.pContext, float(width), float(height));
		RCache.set_Element(s_scope_reference_filter->E[0]);
		RCache.set_c("scope_reference_filter_texel", 1.f / input->dwWidth, 1.f / input->dwHeight, 0.f, 0.f);
		RCache.set_c("scope_reference_filter_threshold", threshold);
		RCache.set_c("scope_reference_filter_range", range);
		RCache.set_c("scope_reference_filter_contrast", contrast);
		RCache.set_c("scope_reference_filter_mode", int(mode));
		svp_reference_fullscreen(g_combine);
	}
	HW.pContext->RSSetViewports(viewportCount, savedViewports);
}

// Draws the optical image on the lens node selected in scope_reference_packet.
// Returns false when the node drew nothing.
bool CRenderTarget::phase_scope_reference_composite(
	const ref_rt& destination, ID3DDepthStencilView* depth, const Fvector4 (&constants)[SvpReferenceOpticsFrame::ConstantCount])
{
	const auto& ocular = Device.m_SecondViewport.eyepiece;
	if (!(ocular.radius > EPS))
		return false;
	// The shader scales lengths by the radius alone, which holds for a rigid lens transform only.
	const auto& lensBasis = ocular.m_W;
	if (_abs(lensBasis.i.square_magnitude() - 1.f) > 0.001f || _abs(lensBasis.j.square_magnitude() - 1.f) > 0.001f ||
		_abs(lensBasis.k.square_magnitude() - 1.f) > 0.001f || _abs(lensBasis.i.dotproduct(lensBasis.j)) > 0.001f ||
		_abs(lensBasis.i.dotproduct(lensBasis.k)) > 0.001f || _abs(lensBasis.j.dotproduct(lensBasis.k)) > 0.001f)
		return false;

	static const char* names[] = { "$user$svp_reference_capture", "$user$svp_reference_translucent", "$user$svp_reference_opaque" };
	for (unsigned i = 0; i < 3; ++i)
		if (!t_scope_reference_composite[i])
			t_scope_reference_composite[i].create(names[i]);
	t_scope_reference_composite[0]->surface_set(RImplementation.TargetSVP->rt_scope_reference_raw->pSurface);
	for (unsigned i = 1; i < 3; ++i)
	{
		ID3DBaseTexture* surface = scope_reference_packet.materials[i - 1]->surface_get();
		t_scope_reference_composite[i]->surface_set(surface);
		_RELEASE(surface);
	}
	if (!s_scope_reference_mesh)
		s_scope_reference_mesh.create("scope_reference_mesh");

	D3D11_VIEWPORT saved[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
	HW.pContext->RSGetViewports(&count, saved);
	// a hybrid reflex drawn into the capture this frame needs to know where the image takes the capture
	const auto* capture = RImplementation.TargetSVP;
	u_setrt(destination, capture->reticle_capture_light_current() ? rt_scope_reference_capture_tc : ref_rt(), nullptr, depth);
	set_viewport_size(HW.pContext, float(destination->dwWidth), float(destination->dwHeight));
	const auto callsBefore = RCache.stat.calls;
	draw_scope(s_scope_reference_mesh,
		[&]
		{
			const auto& p = scope_reference_packet;
			const auto& viewport = Device.m_SecondViewport;
			Fvector right = ocular.m_W.i, up = ocular.m_W.j, forward = ocular.m_W.k;
			right.normalize_safe();
			up.normalize_safe();
			forward.normalize_safe();
			RCache.set_c("reference_source_geometry", p.sourceRadius / ocular.radius, 0.f, 0.f, 0.f);
			RCache.set_c("reference_source_baseline", p.sourceBaselineEye.x, p.sourceBaselineEye.y, p.sourceBaselineEye.z, 0.f);
			RCache.set_c("reference_ocular_baseline", p.ocularBaselineEye.x, p.ocularBaselineEye.y, p.ocularBaselineEye.z, 0.f);
			Fvector eyeDelta, eye;
			eyeDelta.sub(Device.vCameraPosition, ocular.m_W.c);
			eye.set(eyeDelta.dotproduct(right), eyeDelta.dotproduct(up), eyeDelta.dotproduct(forward));
			const auto& config = viewport.RenderOpticConfig();
			const SvpReferencePupil::Identity identity = { p.session, p.routeEpoch, p.opticEpoch, viewport.svp_camera_epoch };
			const auto tracked = s_reference_pupil.Update(p.classicPupilTracking, p.classicFollowEye, p.active, identity, Device.dwFrame,
				eye.x - p.ocularBaselineEye.x, eye.y - p.ocularBaselineEye.y, -eye.z, svp_eye_relief_mm(),
				config.typed_route ? config.tracking_limit_mm : ps_s3ds_eye_tracking_limit_mm,
				config.typed_route ? config.tracking_speed : ps_s3ds_eye_tracking_speed,
				config.typed_route ? config.tracking_accel_mm_s2 : ps_s3ds_eye_tracking_accel_mm_s2,
				viewport.svp_eye_tracking_suspended.load(std::memory_order_acquire), Device.dwTimeGlobal);
			RCache.set_c("reference_tracked_pupil", tracked.x, tracked.y, tracked.active ? 1.f : 0.f, 0.f);
			RCache.set_c("reference_source_socket", p.sourceSocket.x, p.sourceSocket.y, p.sourceSocket.z, 0.f);
			RCache.set_c("reference_source_tangent", p.sourceTangent.x, p.sourceTangent.y, p.sourceTangent.z, 0.f);
			RCache.set_c("reference_source_bitangent", p.sourceBitangent.x, p.sourceBitangent.y, p.sourceBitangent.z, 0.f);
			RCache.set_c("reference_ocular_forward", forward.x, forward.y, forward.z, 0.f);
			for (unsigned i = 0; i < SvpReferenceOpticsFrame::ConstantCount; ++i)
				RCache.set_ca("scope_reference_cb", i, constants[i]);
			RCache.set_c("scope_reference_active", p.active ? 1.f : 0.f, 0.f, 0.f, 0.f);
			RCache.set_CullMode(CULL_NONE);
			RCache.set_Stencil(FALSE);
			if (!depth)
				RCache.set_Z(FALSE);
		});
	HW.pContext->RSSetViewports(count, saved);
	return RCache.stat.calls != callsBefore;
}

void CRenderTarget::EnsureScopeShaders()
{
	if (m_scope_shaders_ready)
		return;
	m_scope_shaders_ready = true;
	s_scope_debug.create("scope_debug");
	s_svp_nearblur.create("svp_nearblur");
	// The lens glue is built on the 3D Shader Scopes framework (scope_3dss_common.h from 3DSS,
	// scope_common.h from the GAMMA Patch). Without it, as on plain Anomaly, no lens is ever
	// captured and these stay empty; draw_scope skips an empty shader.
	auto shader_file = [](LPCSTR file)
	{
		string_path name;
		strconcat(sizeof(name), name, ::Render->getShaderPath(), file);
		return !!FS.exist("$game_shaders$", name);
	};
	if (!shader_file("scope_3dss_common.h") || !shader_file("scope_common.h") || !shader_file("scope_color_write.ps"))
	{
		Msg("* [SVP] 3D Shader Scopes framework not installed, scope lens glue disabled");
		return;
	}
	s_scope_color_write.create("scope_color_write");
	s_scope_depth_write.create("scope_depth_write");
	s_svp_distort_stamp.create("svp_distort_stamp");
	s_svp_taa_stamp.create("svp_taa_stamp");
	s_svp_lens_stencil.create("svp_lens_stencil");
	s_svp_lens_stencil_glass.create("svp_lens_stencil_glass");
	s_scope_glass_fallback.create("scope_glass_fallback");
	s_scope_reference_reticle.create("scope_reference_reticle");
	s_scope_reference_reticle_deferred.create("scope_reference_reticle_deferred");
	// the atlas reticle glow of the lens composites (svp_reticle_emission.cpp)
	s_reticle_emission_pip.create("scope_reticle_emission");
	s_reticle_emission_glass.create("scope_glass_fallback_emission");
	s_reticle_emission_reference.create("scope_reference_reticle_emission");
	s_reticle_emission_reference_deferred.create("scope_reference_reticle_deferred_emission");
	s_scope_reference_capture_light.create("scope_reference_capture_light");
}

// pip r__scope_debug overlay, a top-left grid of the main + SVP views, their ssfx buffers (prev-frame,
// prev-pos, motion vectors) and the shadow map, main viewport only, binds each $main/$svp RT by name
void CRenderTarget::phase_scope_debug()
{
	if (!scope_debug || Device.m_SecondViewport.IsSVPFrame())
		return;

	EnsureScopeShaders();
	if (!s_scope_debug)
		return;

	// snapshot the finished main view so the overlay can sample it without reading the RT it draws into
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);

	auto M = RImplementation.TargetMain;
	auto S = RImplementation.TargetSVP;
	auto bind = [M](LPCSTR name, ref_rt& rt)
	{
		// fall back to a valid RT when the source is absent (no SSS = no ssfx buffers) so the debug
		// technique bind always resolves to a real texture instead of an unregistered name
		ref_rt& src = rt ? rt : M->rt_Generic_0;
		if (!src)
			return;
		ref_texture t;
		t.create(name);
		// raw pSurface, surface_get would AddRef a reference nobody releases (per-frame leak)
		t->surface_set(src->pSurface);
	};
	bind("$user$viewport2$main", M->rt_secondVP);
	bind("$user$ssfx_prev_p$main", M->rt_Position); // no MT prev-pos buffer, show the gbuffer position
	bind("$user$ssfx_motion_vectors$main", M->rt_ssfx_motion_vectors);
	bind("$user$ssfx_prev_frame$main", M->rt_ssfx_prev_frame);
	bind("$user$smap_depth", M->rt_smap_depth);
	if (S)
	{
		bind("$user$viewport2$svp", S->rt_secondVP);
		bind("$user$ssfx_prev_p$svp", S->rt_Position);
		bind("$user$ssfx_motion_vectors$svp", S->rt_ssfx_motion_vectors);
		bind("$user$ssfx_prev_frame$svp", S->rt_ssfx_prev_frame);
	}
	// the bind cache keys on CTexture identity so the remaps above are invisible to it
	RCache.Invalidate();

	// draw onto the CURRENT target phase_combine left bound (the final LDR image), not a fresh RT or the
	// following HUD/UI passes would render offscreen
	RCache.set_CullMode(CULL_NONE);
	RCache.set_Stencil(FALSE);

	u32 Offset = 0;
	u32 C = color_rgba(0, 0, 0, 255);
	float d_Z = EPS_S;
	float d_W = 1.0f;
	float w = float(Device.dwWidth);
	float h = float(Device.dwHeight);

	// fullscreen triangle, the shader discards everything outside the top-left quarter grid
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(3, g_combine->vb_stride, Offset);
	pv->set(0, h * 2, d_Z, d_W, C, 0.f, 2.f);
	pv++;
	pv->set(0, 0, d_Z, d_W, C, 0.f, 0.f);
	pv++;
	pv->set(w * 2, 0, d_Z, d_W, C, 2.f, 0.f);
	pv++;
	RCache.Vertex.Unlock(3, g_combine->vb_stride);

	RCache.set_Geometry(g_combine);
	RCache.set_Element(s_scope_debug->E[1]);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 3, 0, 1);
}

// pip zero the geometry stencil in the four dead corners outside the inscribed eyepiece disc so
// every downstream >=1 test (lights, combine, wallmarks) skips them, no shader or accum edits
void CRenderTarget::stamp_svp_corner_mask()
{
	// square rt only, a flat panel fills the whole rect and has no dead corner
	extern Fvector4 ps_s3ds_param_3;
	if (Width != Height || (ps_r__svp_flat_window && (int)ps_s3ds_param_3.y == 8))
		return;

	// corner-triangle leg, a quarter of the side stays clear of the inscribed disc edge
	const float side = float(Width);
	const float leg = side * 0.25f;
	const float z = EPS_S;
	const u32 C = color_rgba(255, 255, 255, 255);

	// each corner is one triangle packed as a quad, the duplicated vertex collapses the second tri
	u32 Offset;
	FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(16, g_combine->vb_stride, Offset);
	auto corner = [&](float cx, float cy, float ax, float ay, float bx, float by)
	{
		pv->set(cx, cy, z, 1.f, C, 0, 0);
		pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0);
		pv++;
		pv->set(bx, by, z, 1.f, C, 0, 0);
		pv++;
		pv->set(ax, ay, z, 1.f, C, 0, 0);
		pv++;
	};
	corner(0.f, 0.f, leg, 0.f, 0.f, leg);
	corner(side, 0.f, side - leg, 0.f, side, leg);
	corner(0.f, side, leg, side, 0.f, side - leg);
	corner(side, side, side - leg, side, side, side - leg);
	RCache.Vertex.Unlock(16, g_combine->vb_stride);

	u_setrt(NULL, NULL, NULL, baseZB); // only the svp depth-stencil, no color target
	RCache.set_Element(s_occq->E[1]);
	RCache.set_Geometry(g_combine);
	// replace the whole stencil byte with 0 in the corners, clearing the bit0 geometry marker
	StateManager.SetStencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xff, 0xff, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
	StateManager.SetColorWriteEnable(0);
	StateManager.SetDepthFunc(D3DCMP_ALWAYS);
	StateManager.SetDepthEnable(FALSE);
	StateManager.SetCullMode(D3DCULL_NONE);
	RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 16, 0, 8);
	StateManager.SetColorWriteEnable(D3D_COLOR_WRITE_ENABLE_ALL);
}

// pip stash the SVP combined color in rt_secondVP so the scope lens can sample it
void CRenderTarget::phase_svp_capture()
{
	PIX_EVENT(PHASE_SCOPE_SVP_CAPTURE);
	if (ps_r__svp_dlss != 0)
	{
		// Pass the capture targets and cached camera constants to the upscaler.
		SvpDlssInputs in;
		auto& vp = Device.m_SecondViewport;
		in.viewport_id = 1; // stable SVP handle for DLSS history (main = 0), NOT the per-frame dwViewport
		in.color_srv = rt_Generic_0->pTexture->get_SRView();
		in.render_extent = { (u32)Width, (u32)Height };
		in.depth_srv = rt_baseZB ? rt_baseZB->pTexture->get_SRView() : nullptr;
		in.mvec_srv = rt_ssfx_motion_vectors->pTexture->get_SRView();
		in.out_rtv = rt_secondVP->pRT;
		in.out_uav = rt_secondVP->pUAView;
		in.display_extent = { (u32)Width, (u32)Height }; // The copy backend uses the render extent.
		in.view = Device.matrices[1].mView;
		in.proj = Device.matrices[1].mProject;
		in.view_proj.mul(Device.matrices[1].mProject, Device.matrices[1].mView);
		in.prev_view = Device.matrices_previous[1].mView;
		in.prev_proj = Device.matrices_previous[1].mProject;
		in.prev_view_proj.mul(Device.matrices_previous[1].mProject, Device.matrices_previous[1].mView);
		in.jitter_px = vp.svp_jitter_px;
		in.near_plane = vp.svp_near;
		in.far_plane = vp.svp_far;
		in.fov = vp.svp_fov;
		in.aspect = vp.svp_aspect;
		in.cam_pos = vp.svp_cam_pos;
		in.up = vp.svp_up;
		in.right = vp.svp_right;
		in.fwd = vp.svp_fwd;
		in.reset = vp.dlss_reset_next.exchange(false);
		EvalSVP_DLSS(in);
		return;
	}
	// pip near-field defocus, thermal falls through to the plain copy
	if (svp_nearblur_pass())
		return;
	// rt_secondVP alpha is garbage (nothing writes it) and must stay UNREAD, the scope shaders sample
	// .rgb only and the lens composite blends srcalpha with its OWN forced o.a, never the source alpha
	HW.pContext->CopyResource(rt_secondVP->pSurface, rt_Generic_0->pSurface);
}

void CRenderTarget::phase_scope_glass_deferred()
{
	scope_glass_deferred_frame = u32(-1);
	scope_glass_deferred_owner = nullptr;
	// The composition is validated without MSAA, with FP16 blending and the dynamic sun; elsewhere the
	// glass stays out of the G-buffer and the forward glass path draws it.
	if (this != RImplementation.TargetMain || Device.m_SecondViewport.m_render_pass_is_svp || RImplementation.o.dx10_msaa ||
		!RImplementation.o.fp16_blend || RImplementation.o.sunstatic)
		return;
	// scope_reference_refresh() resets the capture camera stamp, so it runs
	// only when the camera pass has not already submitted this frame's packet.
	if (!scope_reference_material_current())
		scope_reference_refresh();
	if (!prepare_scope_physical_glass() || RImplementation.GMBase.RGraph.mapPhysicalScopeGlass.empty())
		return;
	scope_glass_deferred_owner = RImplementation.GMBase.RGraph.mapPhysicalScopeGlass.front().pObject;
	const auto before = RCache.stat.calls;
	// The scene G-buffer is bound: the glass is drawn as a material, before light accumulation.
	RImplementation.GMBase.r_dsgraph_render_physical_glass(true);
	if (RCache.stat.calls > before)
		scope_glass_deferred_frame = Device.dwFrame;
}

bool CRenderTarget::prepare_scope_physical_glass()
{
	if (!ps_r__svp_reference_coating || !scope_reference_material_current() || svp_electronic_image_active())
		return false;
	const char* aliases[] = { "$user$svp_reference_glass_spots", "$user$svp_reference_glass_surface" };
	for (unsigned i = 0; i < 2; ++i)
	{
		if (!scope_reference_packet.glassMaterials[i])
			return false;
		if (!t_scope_reference_glass_inputs[i])
			t_scope_reference_glass_inputs[i].create(aliases[i]);
		auto* surface = scope_reference_packet.glassMaterials[i]->surface_get();
		if (!surface)
			return false;
		t_scope_reference_glass_inputs[i]->surface_set(surface);
		_RELEASE(surface);
	}
	return true;
}
void CRenderTarget::bind_scope_physical_glass(bool enabled)
{
	const bool transmittedFocus = enabled && scope_reference_current();
	// The combine derives its gloss control from the sun colour; the material draw needs the same value.
	light* sun = (light*)RImplementation.Lights.sun_adapted._get();
	const float glossControl = sun ? u_diffuse2s(sun->color.r, sun->color.g, sun->color.b) : u_diffuse2s(0.f, 0.f, 0.f);
	RCache.set_c("scope_reference_physical_glass", enabled ? 1.f : 0.f, transmittedFocus ? 1.f : 0.f, glossControl, 0.f);
	if (enabled)
		for (unsigned i = 0; i < SvpReferenceOpticsFrame::GlassConstantCount; ++i)
			RCache.set_ca("scope_reference_glass_cb", i, scope_reference_packet.glassConstants[i]);
}

namespace
{
// Lens glass composition (scope_glass_compose.ps, scope_glass_compose_nvg.ps): the G-buffer, the
// optical image, the weather cubes and the scope reflection capture at glass pixels.
class SvpGlassComposeBlender final : public IBlender
{
public:
	explicit SvpGlassComposeBlender(LPCSTR pixel_shader) : pixel_shader(pixel_shader) {}
	LPCSTR getComment() override { return "Lens glass composition"; }
	BOOL canBeLMAPped() override { return FALSE; }
	void Compile(CBlender_Compile& C) override
	{
		IBlender::Compile(C);
		if (C.iElement != 0)
			return;
		C.r_Pass("scope_screen_quad", pixel_shader, FALSE, FALSE, FALSE, FALSE);
		C.r_Stencil(TRUE, D3DCMP_LESSEQUAL, 0xff, 0x00);
		C.r_StencilRef(0x01);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_diffuse", r2_RT_albedo);
		C.r_dx10Texture("s_tonemap", r2_RT_luminance_cur);
		C.r_dx10Texture("s_scope_glass_motion", r2_RT_ssfx_motion_vectors);
		C.r_dx10Texture("scope_reference_ocular", "$user$svp_reference_ocular_input");
		C.r_dx10Texture("env_s0", r2_T_envs0);
		C.r_dx10Texture("env_s1", r2_T_envs1);
		C.r_dx10Texture("s_lens_env0", r2_T_envs0);
		C.r_dx10Texture("s_lens_env1", r2_T_envs1);
		scope_vslr_bind(C);
		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_linear");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
	}

private:
	LPCSTR pixel_shader;
};
}

bool CRenderTarget::scope_glass_forward_layer_allowed() const
{
	return !(scope_glass_deferred_current() && ps_r2_nightvision > 0);
}

// Main view, with the render target: the first aim compiles and links nothing. Probed first: the resource
// manager ends the game on a compile error and stubs a missing file; without them the glass stays forward.
void CRenderTarget::scope_glass_compose_create()
{
	if (!svp_pixel_shader_compiles("scope_glass_compose") || !svp_pixel_shader_compiles("scope_glass_compose_nvg"))
	{
		Msg("! [SVP] scope_glass_compose shaders do not compile; the scope glass stays on the forward path");
		return;
	}
	SvpGlassComposeBlender compose("scope_glass_compose"), compose_nvg("scope_glass_compose_nvg");
	s_scope_glass_compose.create(&compose, "scope_glass_compose");
	s_scope_glass_compose_nvg.create(&compose_nvg, "scope_glass_compose_nvg");
}

// At glass pixels: after the installed combine, or under night vision before it.
bool CRenderTarget::scope_glass_compose_begin(bool night_vision)
{
	if (this != RImplementation.TargetMain || !scope_glass_deferred_current() || (ps_r2_nightvision > 0) != night_vision
		|| !s_scope_glass_compose)
		return false;
	scope_vslr_request();
	return true;
}

void CRenderTarget::scope_glass_compose_draw(u32 offset)
{
	svp_stats::section_begin(svp_stats::SEC_GLASS_COMPOSE);
	RCache.set_c("scope_glass_deferred_control", 1.f, scope_reference_composited_frame == Device.dwFrame ? 1.f : 0.f,
		scope_reference_packet.toneMapInputGamma, float(ps_r__svp_lens_debug));
	SvpLensLighting::BindObjectLighting(scope_glass_deferred_owner);
	RCache.set_c("scope_glass_source17", scope_reference_packet.glassConstants[17]);
	RCache.set_c("scope_glass_source18", scope_reference_packet.glassConstants[18]);
	// The G-buffer holds HUD view depth; XY is rebuilt with the world projection.
	RCache.set_c(
		"scope_glass_hud_ray_scale", Device.mProject._11 / Device.mProjectHud._11, Device.mProject._22 / Device.mProjectHud._22, 0.f, 0.f);
	RCache.set_c("scope_glass_vslr", scope_vslr_published());
	RCache.set_c("scope_glass_vslr_fade", scope_vslr_fade(), 0.f, 0.f, 0.f);
	scope_glass_head_bind(scope_reference_composited_frame == Device.dwFrame);
	scope_vslr_consumer_begin();
	RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
	scope_vslr_consumer_end();
	svp_stats::section_end(svp_stats::SEC_GLASS_COMPOSE);
}

// pip render the captured lens meshes with shader se, the bind callback sets the scope_phase
// (IMAGE/RETICLE/SHADOW/LENS) that scope_color_write composites into the lens
void CRenderTarget::draw_scope(ref_shader se, std::function<void()> bind, bool objective)
{
	auto elem = se ? se->E[0] : nullptr;
	if (!elem)
		return;

	Fmatrix FTold = Device.mFullTransform;
	Device.mFullTransform = Device.mFullTransformHud;
	RCache.set_xform_project(Device.mProjectHud);
	RImplementation.rmNear();

	// per-lens dump, which texture each lens draw samples as s_reticle + the optic type/geometry
	static u32 s_lens_ms = 0;
	const bool lens_diag = ps_r__svp_diag && Device.dwTimeGlobal - s_lens_ms > 1000;
	if (lens_diag)
		s_lens_ms = Device.dwTimeGlobal;

	u32 lens_idx = 0;
	auto& lenses = objective ? RImplementation.GMBase.RGraph.mapScopeHUDObjective : RImplementation.GMBase.RGraph.mapScopeHUDSorted;
	for (auto& N : lenses)
	{
		if (scope_reference_packet.ocularVisual &&
			(N.pVisual != scope_reference_packet.ocularVisual || N.pMatrix != scope_reference_packet.ocularRoot))
			continue;
		dxRender_Visual* V = N.pVisual;
		if (!V || !N.pMatrix)
			continue;

		CTexture* tex = V->GetTexture();
		// per-lens marker, names the reticle source texture so a capture shows which texture each
		// scope_color_write draw samples as s_reticle (gated on r__gpu_markers)
		PIX_EVENT_F("scope_lens tex=%s", tex ? tex->cName.c_str() : "none");
		if (lens_diag)
		{
			extern Fvector4 ps_s3ds_param_3;
			extern float g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio;
			PipMsg("[SVP-LENS] %u tex %s rtype %d itype %d eyep_r %.4f mag %.2f min %.2f ratio %.2f", lens_idx,
				tex ? tex->cName.c_str() : "none", (int)ps_s3ds_param_3.y, (int)ps_s3ds_param_3.x, Device.m_SecondViewport.eyepiece.radius,
				g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_ratio);
		}
		++lens_idx;
		if (tex)
		{
			// a lens that is never drawn with its own material still keeps its texture loaded and in use
			if (!tex->flags.bLoaded && !tex->IsLoading())
				DEV->QueueTextureLoad(ref_texture(tex));
			tex->dwLastUsedFrame = Device.dwFrame;
			// surface_get AddRefs (and services staging), release the ref once the alias holds its own
			ID3DBaseTexture* s = tex->surface_get();
			t_reticle->surface_set(s);
			_RELEASE(s);
		}
		else
			t_reticle->surface_set(nullptr); // never the previous lens's reticle

		RCache.set_Element(elem);
		if (objective)
			RCache.set_CullMode(CULL_CW);
		// reuse the main HUD root for every late lens phase
		Fmatrix lensW = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
		CSkeletonX* sk = fast_dynamic_cast<CSkeletonX*>(V);
		const bool frozen_lens = svp_objective_hud_current() && sk && sk->SVP_BoneSnapshotReady();
		// the quad skins from the live bone palette, folding latched bone x live inverse into the
		// world cancels any bone step since the housing draw (the lens glass rides one bone)
		if (!frozen_lens)
		{
			Fmatrix bL, bNow;
			if (sk && RImplementation.GMBase.svp_lens_bone_of(V, bL) && sk->SVP_LensBoneXform(bNow))
			{
				Fmatrix inv;
				inv.invert(bNow);
				lensW.mulB_43(bL);
				lensW.mulB_43(inv);
			}
		}
		RCache.set_xform_world(lensW);
		SvpLensLighting::BindObjectLighting(N.pObject);
		RImplementation.apply_lmaterial();

		svp_bind_lens_constants();

		bind();
		const bool frozen_was = g_svp_hud_frozen_pass;
		const bool history_was = g_svp_hud_history_write;
		if (frozen_lens)
		{
			g_svp_hud_frozen_pass = true;
			g_svp_hud_history_write = false;
		}
		V->Render(0);
		g_svp_hud_frozen_pass = frozen_was;
		g_svp_hud_history_write = history_was;
	}

	RImplementation.rmNormal();
	if (objective)
		RCache.set_CullMode(CULL_CCW);
	Device.mFullTransform = FTold;
	RCache.set_xform_project(Device.mProject);
}

// pip 3DSS 5.x late-pass meshes that are not lenses (transparent ADS housings, canted cutouts,
// thermal-overlay reticles) stay out of the eyepiece capture. They draw here, where stock 3DSS
// draws its late pass. The PiP lens first takes the stencil value the 3DSS lens shader writes
// (254, which these meshes test against); the scene copy they blur is taken before the lens draws
// (scope_overlay_scene_copy), as the stock pass does, unless refresh_scene asks for it here.
void CRenderTarget::scope_overlay_scene_copy()
{
	if (RImplementation.GMBase.RGraph.mapScopeHUDOverlay.empty() || this != RImplementation.TargetMain)
		return;
	ID3DBaseTexture* dst = rt_Generic_temp->pTexture->surface_get();
	ID3DBaseTexture* src = rt_Generic_0->pTexture->surface_get();
	HW.pContext->CopyResource(dst, src);
	_RELEASE(src);
	_RELEASE(dst);
}

void CRenderTarget::draw_scope_overlays(ID3DDepthStencilView* zb, bool stamp_lens, bool refresh_scene)
{
	auto& overlays = RImplementation.GMBase.RGraph.mapScopeHUDOverlay;
	if (overlays.empty() || this != RImplementation.TargetMain)
		return;
	PIX_EVENT(SCOPE_PHASE_OVERLAYS);

	if (stamp_lens)
	{
		EnsureScopeShaders();
		u_setrt(rt_Generic_0, nullptr, nullptr, zb);
		RCache.set_CullMode(CULL_CCW);
		draw_scope(s_svp_lens_stencil, [] {});
		draw_scope(s_svp_lens_stencil, [] {}, true);
		// deferred physical glass holds the depth in front of the lens, stamp where it is the visible lens
		if (scope_glass_deferred_current())
		{
			draw_scope(s_svp_lens_stencil_glass, [] {});
			draw_scope(s_svp_lens_stencil_glass, [] {}, true);
		}
	}

	if (refresh_scene)
		scope_overlay_scene_copy();
	u_setrt(rt_Generic_0, nullptr, nullptr, zb);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	RImplementation.o.distortion = RImplementation.o.distortion_enabled;
	RImplementation.GMBase.r_dsgraph_render_ScopeOverlay();
	RImplementation.o.distortion = FALSE;
}

void CRenderTarget::phase_3DSSReticle(bool object_lens_pass)
{
	// With deferred glass the combine has resolved the lens and its image;
	// only the reticle is left for this late slot. When no optical image was
	// drawn this frame, the stock image path below shows the PiP image, shadow
	// and reticle at the visible glass pixels instead (scope_glass_fallback).
	const bool stockPipImage = !object_lens_pass && Device.m_SecondViewport.IsSVPActive() && RImplementation.TargetSVP &&
		scope_reference_composited_frame != Device.dwFrame;
	if (scope_glass_deferred_current() && !stockPipImage)
	{
		if (object_lens_pass)
			return;
		SetActive(true);
		u_setrt(rt_Generic_0, nullptr, rt_Position, baseZB);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();
		const bool referenceReticle = scope_reference_current() && scope_reference_composited_frame == Device.dwFrame;
		if (referenceReticle)
		{
			svp_track_disc(Width, Height);
			if (!s_scope_reference_reticle_deferred)
				s_scope_reference_reticle_deferred.create("scope_reference_reticle_deferred");
			// the tone map input gamma lets atlas reticle paint see the image as the image pass wrote it
			draw_scope(s_scope_reference_reticle_deferred,
				[&]
				{
					RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE);
					RCache.set_c("scope_reference_transfer", scope_reference_packet.toneMapInputGamma, 0.f, 0.f, 0.f);
				});
			draw_reticle_emission(s_reticle_emission_reference_deferred, baseZB);
			draw_reference_capture_light();
			u_setrt(rt_Generic_0, nullptr, rt_Position, baseZB);
		}
		// Without this frame's optical image (canted / offset aim beside the glass) the reflex
		// sights in view keep their late draw, as on the other paths.
		if (!referenceReticle)
		{
			u_setrt(rt_Generic_0, nullptr, rt_Position, baseZB);
			RCache.set_CullMode(CULL_CCW);
			RCache.set_Stencil(FALSE);
			RCache.set_ColorWriteEnable();
			draw_reflex();
		}
		draw_scope_overlays(baseZB, referenceReticle, true);
		return;
	}
	PIX_EVENT(PHASE_SCOPE_RETICLE);
	// Deferred glass reaching this path is the missing-image fallback: the glass holds the depth in
	// front of the ocular, so the image, shadow and reticle use the glass-coverage variant.
	const ref_shader& scope_image_shader = scope_glass_deferred_current() ? s_scope_glass_fallback : s_scope_color_write;
	// Hip has no capture preparation; acquire this frame's material packet here.
	if (object_lens_pass && scope_reference_packet.frame != Device.dwFrame)
		scope_reference_refresh();
	// The Reference image replaces the stock image and shadow passes; the reticle and depth passes stay.
	const bool referenceImage = !object_lens_pass && scope_reference_current() && scope_reference_composited_frame == Device.dwFrame;

	// pip take the PiP path only when the active optic can drive the SVP, an optic that
	// captures nothing falls through to the stock render_Reticle
	const bool svp = Device.m_SecondViewport.IsSVPActive() && RImplementation.TargetSVP;
	const bool has_lens = !RImplementation.GMBase.RGraph.mapScopeHUDSorted.empty() && Device.m_SecondViewport.eyepiece.radius > EPS;
	if (object_lens_pass && (!Device.true_pip_on || (!svp && !has_lens)))
		return;

	// pip [SVP-RET] reticle pipeline diag, dumps the capture maps before the draws consume them
	{
		static u32 s_ret_ms = 0;
		if (ps_r__svp_diag && Device.dwTimeGlobal - s_ret_ms > 1000)
		{
			s_ret_ms = Device.dwTimeGlobal;
			auto& G = RImplementation.GMBase.RGraph;
			extern Fvector4 ps_s3ds_param_1;
			extern Fvector4 ps_s3ds_param_3;
			extern Fvector4 ps_shader_scope_params;
			extern float g_pip_scope_magnification;
			extern float g_pip_scope_min_mag;
			extern float g_pip_scope_max_mag;
			extern float g_pip_scope_ratio;
			// the lua triplet stays zero under pip, the pip fields carry the live engine mags
			PipMsg(
				"[SVP-RET] path=%s svp=%d lens=%d scope=%u reflex=%u obj=%u overlay=%u rsize=%.2f rtype=%.0f lua=%.2f/%.2f/%.2f w=%.1f pip=%.2f/%.2f/%.2f ratio=%.2f hudy=%.1f",
				(Device.true_pip_on && (svp || has_lens)) ? "pip" : "stock", (int)svp, (int)has_lens, (u32)G.mapScopeHUDSorted.size(),
				(u32)G.mapReflexHUDSorted.size(), (u32)G.mapScopeHUDObjective.size(), (u32)G.mapScopeHUDOverlay.size(), ps_s3ds_param_1.x,
				ps_s3ds_param_3.y, ps_shader_scope_params.x, ps_shader_scope_params.y, ps_shader_scope_params.z, ps_shader_scope_params.w,
				g_pip_scope_magnification, g_pip_scope_min_mag, g_pip_scope_max_mag, g_pip_scope_ratio,
				g_pGamePersistent ? g_pGamePersistent->m_pGShaderConstants->hud_params.y : 0.f);
			auto dump = [&](const char* tag, auto& map)
			{
				u32 i = 0;
				for (auto& N : map)
				{
					if (!N.pVisual || !N.pMatrix)
					{
						i++;
						continue;
					}
					Fmatrix W = *RImplementation.GMBase.svp_pose_of(N.pMatrix);
					Fvector c;
					N.pVisual->getVisData().box.getcenter(c);
					Fvector wc;
					W.transform_tiny(wc, c);
					auto tx = N.pVisual->GetTexture();
					const LPCSTR shader = N.pVisual->getDebugShaderDef();
					const LPCSTR texture = N.pVisual->getDebugTextureDef();
					PipMsg("[SVP-RET]  %s[%u] shader=%s texture=%s bound=%s pos=(%.2f %.2f %.2f) d=%.1fcm r=%.1fcm", tag, i,
						shader ? shader : "?", texture ? texture : "?", tx ? tx->cName.c_str() : "?", wc.x, wc.y, wc.z,
						wc.distance_to(Device.vCameraPosition) * 100.f, N.pVisual->getVisData().sphere.R * 100.f);
					i++;
				}
			};
			dump("scope", G.mapScopeHUDSorted);
			dump("reflex", G.mapReflexHUDSorted);
			dump("obj", G.mapScopeHUDObjective);
			dump("overlay", G.mapScopeHUDOverlay);
		}
	}

	if (Device.true_pip_on && (svp || has_lens))
	{
		EnsureScopeShaders(); // glue shaders (lazy)
		if (scope_glass_deferred_current() && !s_scope_glass_fallback)
			s_scope_glass_fallback.create("scope_glass_fallback");

		auto M = RImplementation.TargetMain;
		auto S = RImplementation.TargetSVP;

		// a hybrid magnifier drew the holo dot inside the svp already, skip the 1x main-view overlay
		// Suppress the fallback only for the captured optic identity, and only when the image the lens shows
		// carries the capture (the Reference image takes it from its own capture-space target)
		const bool reflex_in_svp = ps_r__svp_reflex_capture && svp && Device.m_SecondViewport.svp_reflex_capture_ok &&
			Device.m_SecondViewport.svp_reflex_capture_epoch == Device.m_SecondViewport.svp_optic_epoch &&
			Device.m_SecondViewport.svp_reflex_capture_session == Device.m_SecondViewport.GetSVPSession() &&
			S->reticle_capture_light_current() == referenceImage;

		// Reticle/depth overlays need a readable position copy. The early object
		// lens pass must not overwrite this scratch target while the main combine
		// pipeline still owns it.
		if (!object_lens_pass)
		{
			svp_copy_begin(SVP_CP_PASS, rt_copy_bytes(rt_Generic_2));
			CopyTextureSurface(rt_Generic_2->pTexture._get(), RImplementation.Target->rt_Position->pTexture._get());
			svp_copy_end(SVP_CP_PASS);
		}

		// The forward lens draw stays in the target domain already owned by the
		// forward renderer. PiP composition runs after resolve on Generic_0.
		if (object_lens_pass && RImplementation.o.dx10_msaa)
			u_setrt(RImplementation.Target->rt_Generic_0_r, RImplementation.Target->rt_Heat, RImplementation.Target->rt_ssfx_motion_vectors,
				RImplementation.Target->rt_MSAADepth->pZRT);
		else if (object_lens_pass)
			u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Heat, RImplementation.Target->rt_ssfx_motion_vectors,
				RImplementation.Target->baseZB);
		else
			u_setrt(RImplementation.Target->rt_Generic_0, nullptr, RImplementation.Target->rt_Position, RImplementation.Target->baseZB);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_Stencil(FALSE);
		RCache.set_ColorWriteEnable();

		if (!object_lens_pass)
			scope_overlay_scene_copy(); // the late-pass overlays blur the scene without the lens
		// the lens composite below draws the reflexes again over the lens, with their atlas reticle light; this
		// pass leaves that light out so it is not added twice
		const bool reflex_redrawn = svp || has_lens;
		if (!object_lens_pass && !reflex_in_svp)
			draw_reflex(false, !reflex_redrawn); // reflex / red dot, both 1x and magnifier

		// composite the captured eyepiece, inactive true PiP keeps clear glass over the main scene
		if (svp || has_lens)
		{
			// JITTERFIX, cancel the TAA jitter in the VS so the lens edge has no ring, cvar 0 skips for the a/b
			if (!object_lens_pass && !referenceImage && svp && ps_r__svp_jitterfix)
			{
				PIX_EVENT(SCOPE_PHASE_JITTERFIX);
				draw_scope(scope_image_shader, []() { RCache.set_c("scope_phase", SCOPE_PHASE_JITTERFIX); });
			}

			if (!object_lens_pass && svp && !referenceImage)
			{
				// point the stock named textures at the active SVP targets
				auto remap = [](LPCSTR name, ref_rt& target)
				{
					ref_texture t;
					t.create(name);
					t->surface_set(target->pSurface);
				};
				remap(r2_RT_secondVP, S->rt_secondVP);
				remap(r2_RT_generic2, S->rt_Position);
				remap(r2_RT_heat, S->rt_Heat);
				{
					ref_texture t;
					t.create("$user$svp_tonemap");
					// Scope combine has already swapped its completed exposure into pool[0].
					// t_LUM_dest is a shared named binding now owned by main combine.
					t->surface_set(S->rt_LUM_pool[0]->pSurface);
				}
				RCache.Invalidate();

				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();

				{
					PIX_EVENT(SCOPE_PHASE_IMAGE);
					draw_scope(scope_image_shader,
						[]()
						{
							RCache.set_c("scope_phase", SCOPE_PHASE_IMAGE);
							auto ts = RImplementation.TargetSVP;
							Fvector4 sr;
							sr.set((float)ts->Width, (float)ts->Height, 1.0f / (float)ts->Width, 1.0f / (float)ts->Height);
							RCache.set_c("screen_res", sr);
							auto tm = RImplementation.TargetMain;
							Fvector4 outr;
							outr.set((float)tm->Width, (float)tm->Height, 1.0f / (float)tm->Width, 1.0f / (float)tm->Height);
							RCache.set_c("output_res", outr);
							Fvector up = { 0, 1, 0 };
							Device.m_SecondViewport.objective.m_W.transform_dir(up);
							Device.mView.transform_dir(up);
							up.z = 0.0f;
							up.normalize_safe();
							float angle = acosf(clampr(up.dotproduct({ 0, 1, 0 }), -1.f, 1.f)) * (up.x > 0 ? 1.0f : -1.0f);
							RCache.set_c("hack_tex_angle", angle);
						});
				}


			}

			if (!object_lens_pass && svp)
				svp_track_disc(M->Width, M->Height);

			if (object_lens_pass)
			{
				{
					PIX_EVENT(SCOPE_PHASE_LENS);
					// Reference glass is drawn after the optical image by the normal skinned material path.
					if (!scope_reference_material_current())
						draw_scope(s_scope_color_write, []() { RCache.set_c("scope_phase", SCOPE_PHASE_LENS); }, true);
				}
			}
			else
			{
				// Restore main-view bindings after sampling the SVP targets. Scope
				// shadow and reticle retain their established late composition order.
				M->SetActive(true);
				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				if (referenceImage)
				{
					PIX_EVENT(SCOPE_PHASE_IMAGE);
					draw_scope(s_scope_reference_tonemap,
						[&]
						{
							RCache.set_CullMode(CULL_NONE);
							RCache.set_c("scope_reference_transfer", scope_reference_packet.toneMapInputGamma, 0.f, 0.f, 0.f);
						});
					RCache.set_CullMode(CULL_CCW);
					// The stock reticle is drawn after tone mapping, limited by the image's pupil alpha.
					if (!s_scope_reference_reticle)
						s_scope_reference_reticle.create("scope_reference_reticle");
					draw_scope(s_scope_reference_reticle,
						[&]
						{
							RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE);
							RCache.set_c("scope_reference_transfer", scope_reference_packet.toneMapInputGamma, 0.f, 0.f, 0.f);
						});
					M->draw_reticle_emission(M->s_reticle_emission_reference, M->baseZB);
					if (reflex_in_svp)
						M->draw_reference_capture_light();
					u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				}
				if (svp && !referenceImage)
				{
					PIX_EVENT(SCOPE_PHASE_SHADOW);
					draw_scope(scope_image_shader, []() { RCache.set_c("scope_phase", SCOPE_PHASE_SHADOW); });
				}
				if (!referenceImage)
				{
					PIX_EVENT(SCOPE_PHASE_RETICLE);
					// Atlas reticle paint reflects the eyepiece capture, which the main bindings above put away
					if (svp)
						M->reticle_atlas_image(S->rt_secondVP);
					draw_scope(scope_image_shader, []() { RCache.set_c("scope_phase", SCOPE_PHASE_RETICLE); });
					if (scope_glass_deferred_current())
						M->draw_reticle_emission(M->s_reticle_emission_glass, M->baseZB);
					else
						M->draw_reticle_emission(M->s_reticle_emission_pip, M->baseZB);
					if (svp)
						M->reticle_atlas_image(M->rt_secondVP);
					u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				}
			}

			// The 3DSS late-pass overlays (transparent housings, cutouts, thermal-overlay
			// reticles) test the scene depth, so they draw before any synthetic depth stamp.
			if (!object_lens_pass)
				draw_scope_overlays(M->baseZB, true, false);

			// Draw the actual surface before the synthetic optical focus-depth stamp.
			// NVG stamps hardware depth zero, which would reject later physical glass.
			if (!object_lens_pass && scope_glass_forward_layer_allowed() && prepare_scope_physical_glass())
			{
				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RImplementation.GMBase.r_dsgraph_render_physical_glass();
			}

			// The focus depth belongs only to a real SVP image and stays with the late overlays.
			if (!object_lens_pass && svp)
			{
				PIX_EVENT(SCOPE_PHASE_CUSTOM_DEPTH);
				u_setrt(RImplementation.Target->rt_Position, 0, 0, 0, RImplementation.Target->baseZB);
				draw_scope(s_scope_depth_write,
					[]()
					{
						RCache.set_c("scope_phase", SCOPE_PHASE_DEPTHWRITE | SCOPE_PHASE_CUSTOM_DEPTH);
						RCache.set_c("scope_depth_value", 1.0f);
					});
			}

			// re-draw the reflex over the composited lens at the main-view position, the hybrid
			// magnifier already has it in the svp image so skip the 1x overlay there
			if (!object_lens_pass && !reflex_in_svp)
			{
				u_setrt(M->rt_Generic_0, nullptr, M->rt_Position, M->baseZB);
				RCache.set_CullMode(CULL_CCW);
				RCache.set_Stencil(FALSE);
				RCache.set_ColorWriteEnable();
				draw_reflex();
			}
		}

		if (object_lens_pass)
		{
			// Restore the forward pass outputs exactly. Leaving the position buffer
			// bound as a color target here corrupts later bloom, fog and post effects.
			if (!RImplementation.o.dx10_msaa)
				u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Heat,
					RImplementation.Target->rt_ssfx_motion_vectors, RImplementation.Target->baseZB);
			else
				u_setrt(RImplementation.Target->rt_Generic_0_r, RImplementation.Target->rt_Heat,
					RImplementation.Target->rt_ssfx_motion_vectors, RImplementation.Target->rt_MSAADepth->pZRT);
			return;
		}

		// The capture maps clear at the main-pass combine tail, past the NVG split
		// and TAA mask stamps that read them after the late overlay pass.
		u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
		return;
	}

	if (object_lens_pass)
		return;

	// legacy 3D-fake / fake-SVP reticle, the stock path when true_pip is off
	render_3DSS_reticles();

	// pip reflexes captured into mapReflexHUDSorted draw here for the fallback optics,
	// the map is empty when true_pip is off
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);
	RCache.set_CullMode(CULL_CCW);
	RCache.set_Stencil(FALSE);
	RCache.set_ColorWriteEnable();
	draw_reflex();

	// any captured lens drew through its own 3DSS shader above and wrote the stencil itself
	draw_scope_overlays(HW.pBaseZB, false, false);
	u_setrt(RImplementation.Target->rt_Generic_0, RImplementation.Target->rt_Position, 0, HW.pBaseZB);

	// the capture maps clear at the main-pass combine tail, past every consumer, the frame-start
	// clear + per-frame rebuild cover staleness on the fallback path too
}
#endif
