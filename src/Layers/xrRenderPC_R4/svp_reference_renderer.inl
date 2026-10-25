// Part of svp_optics.cpp.
#include "svp_reference_anchor.h"
#include "svp_reference_material_lifetime.h"
extern xr_task_group textures_load_tasks;
// Defined in r__dsgraph_render.cpp.
extern bool g_svp_reference_particle_replay;
namespace
{
// Side of the square target that holds the lens mesh in its UV chart.
const u32 svp_reference_chart_size = 2048;
// constants[36].z blends the kernel's ghost images; r__svp_reference_ghosts scales it.
const unsigned svp_reference_ghost_constant = 36;

class SvpReferenceCaptureBlender final : public IBlender
{
public:
    LPCSTR getComment() override { return "Reference deferred capture"; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        if (C.iElement != 0) return;
        // The shader mixes fog and sky in display space and writes raw
        // radiance, so the pass has no hardware blend.
        C.r_Pass("combine_1", "scope_reference_capture", FALSE, FALSE, FALSE, FALSE);
        C.r_Stencil(TRUE, D3DCMP_LESSEQUAL, 0xff, 0x00);
        C.r_StencilRef(0x01);
        C.r_dx10Texture("s_position", r2_RT_P);
        C.r_dx10Texture("s_diffuse", r2_RT_albedo);
        C.r_dx10Texture("s_accumulator", r2_RT_accum);
        C.r_dx10Texture("s_depth", r2_RT_depth);
        C.r_dx10Texture("s_tonemap", r2_RT_luminance_cur);
        C.r_dx10Texture("scope_reference_sky_raw", "$user$svp_reference_sky_raw");
        C.r_dx10Texture("scope_reference_main_meter", "$user$svp_reference_main_meter");
        C.r_dx10Texture("s_material", r2_material);
        C.r_dx10Texture("env_s0", r2_T_envs0);
        C.r_dx10Texture("env_s1", r2_T_envs1);
        C.r_dx10Texture("sky_s0", r2_T_sky0);
        C.r_dx10Texture("sky_s1", r2_T_sky1);
        C.r_dx10Texture("s_occ", r2_RT_ssao_temp);
        C.r_dx10Texture("s_half_depth", r2_RT_half_depth);
        C.r_dx10Texture("ssfx_ao", r2_RT_ssfx_temp);
        C.r_dx10Texture("ssfx_il", r2_RT_ssfx_temp2);
        C.r_dx10Texture("s_motion_vectors", r2_RT_ssfx_motion_vectors);
        C.r_dx10Texture("s_ssfx_sss", r2_RT_ssfx_sss);
        jitter(C);
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_material");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
    }
};

// Clears every shader-resource slot and the render targets, so that a target
// of one pass can be sampled by the next. Also unbinds the geometry and
// tessellation stages, which RCache does not track and particle draws leave set.
void svp_reference_unbind()
{
    ID3D11ShaderResourceView* empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    HW.pContext->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
    HW.pContext->OMSetRenderTargets(0, nullptr, nullptr);
    HW.pContext->GSSetShader(nullptr, nullptr, 0);
    HW.pContext->HSSetShader(nullptr, nullptr, 0);
    HW.pContext->DSSetShader(nullptr, nullptr, 0);
    RCache.Invalidate();
}

// Creates the FP16 target, or recreates it at a new size.
void svp_reference_target(ref_rt& target, LPCSTR name, u32 width, u32 height)
{
    if (target && (target->dwWidth != width || target->dwHeight != height)) target.destroy();
    if (!target) target.create(name, width, height, D3DFMT_A16B16G16R16F);
}

// Draws the bound element over the whole target with the combine quad.
void svp_reference_fullscreen(ref_geom& quad)
{
    RCache.set_Scissor(nullptr);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);
    RCache.set_Z(FALSE);
    RCache.set_ColorWriteEnable();
    u32 offset;
    auto* vertices = static_cast<FVF::TL*>(RCache.Vertex.Lock(4, quad->vb_stride, offset));
    const u32 color = color_rgba(255, 255, 255, 255);
    vertices[0].set(-1.f, -1.f, 0.f, 1.f, color, 0.f, 1.f);
    vertices[1].set(-1.f, 1.f, 0.f, 1.f, color, 0.f, 0.f);
    vertices[2].set(1.f, -1.f, 0.f, 1.f, color, 1.f, 1.f);
    vertices[3].set(1.f, 1.f, 0.f, 1.f, color, 1.f, 0.f);
    RCache.Vertex.Unlock(4, quad->vb_stride);
    RCache.set_Geometry(quad);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

// A 2D, single-sample texture that can be bound as a shader resource.
bool svp_reference_surface_usable(ID3DBaseTexture* surface)
{
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(surface->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) return false;
    D3D11_TEXTURE2D_DESC desc = {};
    texture->GetDesc(&desc);
    texture->Release();
    return desc.ArraySize == 1 && desc.SampleDesc.Count == 1
        && !(desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        && (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE);
}

bool svp_reference_material_usable(SvpReferenceMaterials::QualifiedSurface<ID3DBaseTexture>& check, const ref_texture& material)
{
    return check.Qualify(material ? material->surface_get() : nullptr, svp_reference_surface_usable);
}
}

bool CRenderTarget::scope_reference_current() const
{
    // Night vision and thermal sights show an electronic image (3DSS tube/sensor processing,
    // battery); the optical image model describes glass, so a live display keeps the 3DSS image.
    if (svp_electronic_image_active()) return false;
    return Device.m_SecondViewport.IsSVPActive() && scope_reference_material_current()
        && scope_reference_packet.imageAnchorQualified;
}

// True when the packet was published this frame for the optic in use. The
// glass material needs only this; the optical image needs scope_reference_current.
bool CRenderTarget::scope_reference_material_current() const
{
    if (ps_r__svp_reference_native_compare) return false;
    const auto& p = scope_reference_packet;
    const auto& vp = Device.m_SecondViewport;
    // SCREEN and FLAT_SCREEN reticle modes are rectangular displays, which the
    // round pupil model does not describe. They keep the stock image.
    extern Fvector4 ps_s3ds_param_3;
    const int reticleMode = int(ps_s3ds_param_3.y);
    if (reticleMode == 6 || reticleMode == 8) return false;
    return this == RImplementation.TargetMain && p.enabled && p.allowDeferredCapture && Device.true_pip_on
        && !RImplementation.o.dx10_msaa && !ps_r4_hdr10_on
        && p.frame == Device.dwFrame && p.session == vp.GetSVPSession()
        && p.opticEpoch == vp.svp_optic_epoch && p.routeEpoch == vp.GetOpticRouteEpoch()
        && p.generation == vp.RenderOpticConfig().generation;
}

bool CRenderTarget::scope_reference_submit(const SvpReferencePacket& packet)
{
    scope_reference_packet = {};
    scope_reference_camera_frame = u32(-1);
    if (this != RImplementation.TargetMain || !packet.enabled
        || !packet.sourcePackage[0] || !packet.sourceVariant[0]
        || !std::isfinite(packet.sourceRadius) || packet.sourceRadius<=EPS
        || !_valid(packet.sourceBaselineEye) || packet.sourceBaselineEye.z>=-EPS
        || !_valid(packet.ocularBaselineEye) || packet.ocularBaselineEye.z>=-EPS
        || !_valid(packet.sourceSocket) || !_valid(packet.sourceTangent) || !_valid(packet.sourceBitangent)
        || std::abs(packet.sourceTangent.magnitude()-1.f)>.01f || std::abs(packet.sourceBitangent.magnitude()-1.f)>.01f
        || std::abs(packet.sourceTangent.dotproduct(packet.sourceBitangent))>.01f
        || !std::isfinite(packet.threshold) || !std::isfinite(packet.range) || packet.range <= 0.f
        || !std::isfinite(packet.contrast)
        || (packet.toneMapInputGamma != 1.f && packet.toneMapInputGamma != 2.2f))
        return false;
    static_assert(std::extent_v<decltype(SvpReferencePacket::materials)> + std::extent_v<decltype(SvpReferencePacket::glassMaterials)> ==
        std::extent_v<decltype(CRenderTarget::scope_reference_material_checks)>, "one material check per packet material");
    auto* check = scope_reference_material_checks;
    for (const auto& material : packet.materials)
        if (!svp_reference_material_usable(*check++, material)) return false;
    for (const auto& material : packet.glassMaterials)
        if (!svp_reference_material_usable(*check++, material)) return false;
    for (const auto& c : packet.constants) if (!_valid(c)) return false;
    for (const auto& c : packet.glassConstants) if (!_valid(c)) return false;
    scope_reference_packet = packet;
    if (scope_reference_material_current()) return true;
    scope_reference_packet = {};
    return false;
}

void CRenderTarget::scope_reference_refresh()
{
    SvpReferencePacket p;
    const bool read = svp_reference_optics_read(p);
    s_reference_pupil.Refresh(read, p.classicPupilTracking, p.classicFollowEye, p.enabled, Device.m_SecondViewport.IsSVPActive());
    if (!read) { scope_reference_packet = {}; return; }
    if (p.ocularBaselineFromWeapon)
    {
        // The anchor needs the ocular drawn this frame. At the hip there is
        // none: the packet still serves the glass material, and
        // scope_reference_current() withholds the image.
        const auto& vp = Device.m_SecondViewport;
        const auto& ocular = vp.eyepiece;
        Fvector anchor;
        p.imageAnchorQualified = SvpReferenceAnchor::FreshOcular(vp.svp_lens_frame, Device.dwFrame,
                vp.svp_lens_root != nullptr, vp.svp_lens_visual != nullptr)
            && SvpReferenceAnchor::Resolve(p.weaponEyeWorld, ocular.m_W.c,
                ocular.m_W.i, ocular.m_W.j, ocular.m_W.k, ocular.radius, anchor);
        if (p.imageAnchorQualified) p.ocularBaselineEye = anchor;
    }
    const auto create = [](ref_texture& material, const char* name) {
        if (!material || xr_strcmp(material->cName.c_str(), name)) material.create(name);
    };
    const auto drain = [] { textures_load_tasks.wait(); };
    if (!SvpReferenceMaterials::Acquire(scope_reference_glass_materials, p.glassMaterials, p.glassMaterialNames, create, drain)
        || !SvpReferenceMaterials::Acquire(scope_reference_materials, p.materials, p.materialNames, create, drain))
    { scope_reference_packet = {}; return; }
    scope_reference_submit(p);
}

#include "svp_reference_sky.inl"

bool CRenderTarget::scope_reference_prepare_capture()
{
    scope_reference_camera_frame=u32(-1);
    scope_reference_refresh();
    // The capture is taken before NVG; the main combine applies NVG once, to the composited image.
    if(!scope_reference_current() || svp_thermal_overlay_active() || !RImplementation.TargetSVP
        || !RImplementation.TargetSVP->scope_reference_sky_qualified()) return false;
    const auto& nodes=RImplementation.GMBase.RGraph.mapScopeHUDSorted;
    if(std::none_of(nodes.begin(),nodes.end(),[](const auto& n){return n.pVisual && n.pMatrix;})) return false;
    scope_reference_camera_frame=Device.dwFrame;
    return true;
}

bool CRenderTarget::scope_reference_capture_begin()
{
    auto* main = RImplementation.TargetMain;
    if (this != RImplementation.TargetSVP || !main || !main->scope_reference_current()
        || scope_reference_sky_frame != Device.dwFrame || !rt_scope_reference_raw
        || !rt_scope_reference_sky_raw || !t_scope_reference_main_meter
        || scope_reference_raw_stamp != main->scope_reference_packet.Stamp())
        return false;
    if (!s_scope_reference_capture)
    {
        SvpReferenceCaptureBlender blender;
        s_scope_reference_capture.create(&blender, "scope_reference_capture");
    }
    u_setrt(rt_scope_reference_raw, nullptr, nullptr, baseZB);
    return true;
}

#include "svp_reference_world_effects.inl"

void CRenderTarget::scope_reference_capture_end()
{
    const auto& p = RImplementation.TargetMain->scope_reference_packet;
    scope_reference_raw_frame = Device.dwFrame;
    scope_reference_raw_stamp = p.Stamp();
    phase_scope_reference_filters(rt_scope_reference_raw, p.threshold, p.range, p.contrast);
    svp_reference_unbind();
    SetActive(true);
    u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, baseZB);
}

bool CRenderTarget::phase_scope_reference_ocular()
{
    scope_reference_composited_frame = u32(-1);
    if (!scope_reference_current()) return false;
    const auto* capture = RImplementation.TargetSVP;
    const auto& p = scope_reference_packet;
    const auto& vp = Device.m_SecondViewport;
    if (!capture || capture->scope_reference_raw_frame != Device.dwFrame
        || capture->scope_reference_raw_stamp != p.Stamp()
        || vp.svp_camera_frame != Device.dwFrame || vp.svp_camera_session != p.session
        || vp.svp_lens_frame != Device.dwFrame) return false;
    EnsureScopeShaders();

    svp_reference_unbind();
    svp_reference_target(rt_scope_reference_ocular, "$user$svp_reference_ocular", Width, Height);
    const float transparent[4]={0,0,0,0};
    HW.pContext->ClearRenderTargetView(rt_scope_reference_ocular->pRT,transparent);
    svp_reference_target(rt_scope_reference_inside, "$user$svp_reference_inside", svp_reference_chart_size, svp_reference_chart_size);
    if (capture->reticle_capture_light_current())
    {
        svp_reference_target(rt_scope_reference_capture_tc, "$user$svp_reference_capture_tc", Width, Height);
        HW.pContext->ClearRenderTargetView(rt_scope_reference_capture_tc->pRT, transparent);
    }
    if(!s_scope_reference_glyph)s_scope_reference_glyph.create("scope_reference_glyph");
    Fvector4 constants[SvpReferencePacket::ConstantCount];
    CopyMemory(constants,p.constants,sizeof(constants));
    constants[svp_reference_ghost_constant].z*=ps_r__svp_reference_ghosts;
    bool drawn=false;
    // Each lens mesh gets its inside layer drawn into its UV chart and then
    // the optical image drawn on the mesh itself.
    for(const auto& node:RImplementation.GMBase.RGraph.mapScopeHUDSorted)
    {
        if(!node.pVisual || !node.pMatrix) continue;
        scope_reference_packet.ocularRoot=node.pMatrix;
        scope_reference_packet.ocularVisual=node.pVisual;
        svp_reference_unbind();
        u_setrt(rt_scope_reference_inside,nullptr,nullptr,nullptr);
        set_viewport_size(HW.pContext,float(svp_reference_chart_size),float(svp_reference_chart_size));
        HW.pContext->ClearRenderTargetView(rt_scope_reference_inside->pRT,transparent);
        draw_scope(s_scope_reference_glyph,[&]{
            RCache.set_c("scope_phase",SCOPE_PHASE_RETICLE);
            // The reticle shader converts derivatives to pixels of the main
            // viewport, whatever the size of this target. The stock image
            // pass, which normally sets output_res, does not run here.
            const auto mainTarget=RImplementation.TargetMain;
            const float mainWidth=float(mainTarget->Width),mainHeight=float(mainTarget->Height);
            RCache.set_c("output_res",mainWidth,mainHeight,1.f/mainWidth,1.f/mainHeight);
            RCache.set_CullMode(CULL_NONE);RCache.set_Stencil(FALSE);RCache.set_Z(FALSE);
        });
        svp_reference_unbind();
        drawn=phase_scope_reference_composite(rt_scope_reference_ocular,scope_glass_deferred_current() ? nullptr : baseZB,
            constants)||drawn;
    }
    scope_reference_packet.ocularRoot=nullptr;
    scope_reference_packet.ocularVisual=nullptr;
    set_viewport_size(HW.pContext,float(Width),float(Height));
    svp_reference_unbind();
    SetActive(true);
    u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, baseZB);
    if (!drawn) return false;

    // phase_3DSSReticle presents the image in the stock late image slot, after
    // the forward scope geometry.
    if (!s_scope_reference_tonemap) s_scope_reference_tonemap.create("scope_reference_tonemap");
    if (!t_scope_reference_ocular) t_scope_reference_ocular.create("$user$svp_reference_ocular_input");
    t_scope_reference_ocular->surface_set(rt_scope_reference_ocular->pSurface);
    scope_reference_composited_frame = Device.dwFrame;
    return true;
}
