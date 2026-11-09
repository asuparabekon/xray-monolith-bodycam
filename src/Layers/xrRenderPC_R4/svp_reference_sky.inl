#include "../xrRender/dxEnvironmentRender.h"
#include "svp_shader_probe.h"

// The Reference capture's sky: the installed sky shaders drawn again into the capture target,
// compared with the installed sky in the PiP view at a few pixels, then converted to raw radiance.
namespace
{
// Whether the installed sky can be captured does not change during a session.
SvpReferenceSkyVerdict& svp_reference_sky_verdict()
{
    static SvpReferenceSkyVerdict verdict;
    return verdict;
}

void svp_reference_sky_reject(LPCSTR reason)
{
    if (svp_reference_sky_verdict().reject())
        Msg("[SVP-REF-SKY] Reference scope image off for this session: %s", reason);
}

dxEnvironmentRender& svp_environment_render(CEnvironment& env)
{
    return static_cast<dxEnvironmentRender&>(*env.m_pRender);
}

dxEnvDescriptorMixerRender& svp_environment_mixer(CEnvironment& env)
{
    return static_cast<dxEnvDescriptorMixerRender&>(*env.CurrentEnv->m_pDescriptorMixer);
}

// The capture runs inside the engine's sky draw of the PiP view, after the sky box and before the
// sun, so the PiP colour target holds exactly the installed sky box when it is compared.
void svp_reference_sky_after_sky_box()
{
    if (Device.m_SecondViewport.m_render_pass_is_svp && RImplementation.TargetSVP)
        RImplementation.TargetSVP->phase_scope_reference_sky();
}
[[maybe_unused]] const bool svp_reference_sky_hooked = (dxEnvironmentRender::after_sky_box = svp_reference_sky_after_sky_box, true);
}

// Creates the sky shaders once; false (and the capture rejected) when the installed sky2.ps cannot
// be wrapped.
bool CRenderTarget::scope_reference_sky_shaders()
{
    auto& sky = scope_reference_sky;
    if (sky.installed) return true;
    if (!svp_pixel_shader_compiles("scope_reference_sky2"))
    {
        svp_reference_sky_reject("scope_reference_sky2.ps does not compile with the installed sky2.ps");
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (auto*& staging : sky.staging)
        if (!staging && FAILED(HW.pDevice->CreateTexture2D(&desc, nullptr, &staging)))
        {
            svp_reference_sky_reject("the comparison readback textures cannot be created");
            return false;
        }
    CBlender_skybox skybox;
    SvpPrefixedBlender prefixed(&skybox, "scope_reference_");
    sky.installed.create(&prefixed, "scope_reference_sky_installed");
    sky.raw.create("scope_reference_sky");
    sky.check.create("scope_reference_sky_check");
    sky.display.create("$user$svp_reference_sky_display");
    sky.scene.create("$user$svp_reference_sky_scene");
    sky.checkResult.create("$user$svp_reference_sky_check", 1, 1, D3DFMT_R32F);
    return true;
}

// Draws the installed sky into the raw target, used as scratch for display values. For the
// comparison the constants are the installed sky's own; the capture needs the night vision branch
// off, which takes a second draw only while night vision is on.
void CRenderTarget::scope_reference_sky_draw(bool capture)
{
    extern Fvector4 ps_dev_param_8;
    if (capture && ps_dev_param_8.x < 1.f) return;
    auto& env = g_pGamePersistent->Environment();
    const float clear[4] = {0, 0, 0, 0};
    HW.pContext->ClearRenderTargetView(rt_scope_reference_raw->pRT, clear);
    u_setrt(rt_scope_reference_raw, nullptr, nullptr, nullptr);
    set_viewport_size(HW.pContext, float(Width), float(Height));
    u32 vertexOffset, indexOffset;
    svp_environment_render(env).PrepareSkyBox(env, vertexOffset, indexOffset);
    RCache.set_Element(scope_reference_sky.installed->E[0]);
    RCache.set_Textures(&svp_environment_mixer(env).sky_r_textures);
    if (capture)
        RCache.set_c("shader_param_8", 0.f, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();
    RCache.Render(D3DPT_TRIANGLELIST, vertexOffset, 0, 12, indexOffset, 20);
    svp_reference_unbind();
}

// Compares the drawn sky with the installed sky box in the PiP view's colour target and queues the
// 1x1 result for a later non-blocking read.
void CRenderTarget::scope_reference_sky_compare()
{
    auto& sky = scope_reference_sky;
    const int slot = sky.readback.acquire();
    if (slot < 0) return;
    sky.display->surface_set(rt_scope_reference_raw->pSurface);
    sky.scene->surface_set(rt_Generic_0->pSurface);
    u_setrt(sky.checkResult, nullptr, nullptr, nullptr);
    set_viewport_size(HW.pContext, 1.f, 1.f);
    RCache.set_Element(sky.check->E[0]);
    // The sample grid moves every frame, so a pixel that differs for another reason is not always sampled.
    const float phase = float(Device.dwFrame % 16);
    RCache.set_c("scope_reference_sky_phase", fmodf(phase * .25f, 1.f), fmodf(phase * .0625f, 1.f), 0.f, 0.f);
    svp_reference_fullscreen(g_combine);
    svp_reference_unbind();
    HW.pContext->CopyResource(sky.staging[slot], sky.checkResult->pSurface);
    sky.readback.submitted();
}

// Reads the finished comparisons; true while the capture is admitted. Called before the PiP camera
// is chosen, so a rejection applies from this frame on.
bool CRenderTarget::scope_reference_sky_qualified()
{
    auto& verdict = svp_reference_sky_verdict();
    auto& sky = scope_reference_sky;
    using Read = decltype(sky.readback)::Read;
    sky.readback.poll([&](unsigned slot, bool& passed) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = HW.pContext->Map(sky.staging[slot], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING) return Read::Pending;
        if (FAILED(result))
        {
            static bool logged = false;
            if (!logged) Msg("! [SVP-REF-SKY] comparison read failed hr=0x%08x", result);
            logged = true;
            return Read::Failed;
        }
        passed = *static_cast<const float*>(mapped.pData) > .5f;
        HW.pContext->Unmap(sky.staging[slot], 0);
        return Read::Ready;
    }, [&](bool passed) {
        if (verdict.sample(passed))
            svp_reference_sky_reject("the captured sky differs from the installed sky in the PiP view");
    });
    return verdict.verified();
}

// Runs in the PiP view between the installed sky box and the sun. The sky is drawn and compared
// whenever a Reference packet is current, so the capture can be qualified before the Reference
// camera is admitted; the raw sky is produced when the capture runs this frame.
bool CRenderTarget::phase_scope_reference_sky()
{
    scope_reference_sky_frame = scope_reference_raw_frame = u32(-1);
    auto* main = RImplementation.TargetMain;
    if (this != RImplementation.TargetSVP || !main || !main->scope_reference_current()
        || svp_thermal_overlay_active() || svp_reference_sky_verdict().rejected()
        || !scope_reference_sky_shaders()) return false;
    const bool capture = main->scope_reference_camera_frame == Device.dwFrame;

    const Fmatrix world = RCache.get_xform_world();
    D3D11_VIEWPORT saved[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    HW.pContext->RSGetViewports(&count, saved);
    svp_reference_unbind();
    svp_reference_target(rt_scope_reference_raw, "$user$svp_reference_raw", Width, Height);
    svp_reference_target(rt_scope_reference_sky_raw, "$user$svp_reference_sky_raw", Width, Height);

    scope_reference_sky_draw(false);
    scope_reference_sky_compare();
    if (capture)
    {
        scope_reference_sky_draw(true);
        // The sun source (the moon at night) is drawn after this hook, so the raw sky takes its own copy
        u_setrt(rt_scope_reference_raw, nullptr, nullptr, baseZB);
        set_viewport_size(HW.pContext, float(Width), float(Height));
        svp_environment_render(g_pGamePersistent->Environment()).RenderSun(g_pGamePersistent->Environment());
        svp_reference_unbind();
        if (!t_scope_reference_main_meter)
            t_scope_reference_main_meter.create("$user$svp_reference_main_meter");
        t_scope_reference_main_meter->surface_set(main->rt_LUM_pool[1]->pSurface);
        scope_reference_sky.display->surface_set(rt_scope_reference_raw->pSurface);
        u_setrt(rt_scope_reference_sky_raw, nullptr, nullptr, nullptr);
        set_viewport_size(HW.pContext, float(Width), float(Height));
        RCache.set_Element(scope_reference_sky.raw->E[0]);
        RCache.set_c("scope_reference_transfer", main->scope_reference_packet.toneMapInputGamma, 0.f, 0.f, 0.f);
        svp_reference_fullscreen(g_combine);
        svp_reference_unbind();
        // The raw target starts as the sky for pixels the deferred capture leaves without geometry.
        HW.pContext->CopyResource(rt_scope_reference_raw->pSurface, rt_scope_reference_sky_raw->pSurface);
        scope_reference_sky_frame = Device.dwFrame;
        scope_reference_raw_stamp = main->scope_reference_packet.Stamp();
    }
    RCache.set_xform_world(world);
    HW.pContext->RSSetViewports(count, saved);
    u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, baseZB);
    return capture;
}
