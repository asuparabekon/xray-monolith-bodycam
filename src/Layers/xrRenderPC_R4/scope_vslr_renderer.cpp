#include "stdafx.h"
#include "scope_vslr_renderer.h"
#include "scope_vslr_capture.h"
#include "scope_vslr_timing.h"
#include "scope_vslr_mirror.h"
#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/Environment.h"
#include "../../xrEngine/irenderable.h"
#include "../../xrEngine/xr_object.h"
#include "../xrRender/dxEnvironmentRender.h"
#include "svp_prefixed_blender.h"
#include "svp_shader_probe.h"
#include "scope_glass_head.h"
#include "../xrRender/svp_console.h"

int ps_scope_vslr = 1;
int ps_scope_vslr_debug = 0;
int ps_scope_vslr_smooth = 1;
int ps_scope_vslr_move_interval = 100;
int ps_scope_vslr_cost = 0;
float ps_scope_vslr_indirect = 6.f;

namespace
{
constexpr LPCSTR colour_texture = "$user$vslr_colour";
constexpr LPCSTR position_texture = "$user$vslr_position";
constexpr LPCSTR albedo_texture = "$user$vslr_albedo";
constexpr LPCSTR distance_texture = "$user$vslr_distance";
constexpr LPCSTR coverage_texture = "$user$vslr_coverage";
constexpr LPCSTR direct_texture = "$user$vslr_direct";
constexpr LPCSTR sky_texture = "$user$vslr_sky";
constexpr LPCSTR lit_texture = "$user$vslr_lit";
constexpr LPCSTR sun_shadow_texture = "$user$vslr_smap";
constexpr LPCSTR previous_colour_texture = "$user$vslr_colour_previous";
constexpr LPCSTR previous_coverage_texture = "$user$vslr_coverage_previous";
// Capturing stops this long after the last consumer request; the captures are kept for the level.
constexpr u32 idle_ms = 10000;
// A new capture starts this long after the last one started, or, with r__scope_vslr_smooth, once the
// camera moved this far and r__scope_vslr_move_interval has passed.
constexpr u32 capture_interval_ms = 250;
constexpr float capture_move_m = .02f;
constexpr float capture_near_m = .05f;
// The debug sphere: anchored this far ahead of the camera when first drawn, this size.
constexpr float sphere_ahead_m = 2.f;
constexpr float sphere_radius_m = .35f;
// A capture fades in from the one before only if that one is this recent and was taken within this
// share of the radius (not across a teleport); the fade spans at most this many frames.
constexpr u32 fade_max_age_ms = 1000;
constexpr float fade_max_shift = .5f;
constexpr u32 fade_max_frames = 120;

Fvector to_fvector(const scope_vslr::Position& p)
{
    return {p.x, p.y, p.z};
}

// The passes of VslrBlender, by element.
enum VslrElement : u32
{
    element_resolve,
    element_sphere,
    element_mirror,
};

class VslrBlender final : public IBlender
{
public:
    BOOL canBeLMAPped() override { return FALSE; }
    LPCSTR getComment() override { return "Scope VSLR"; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        static constexpr LPCSTR pixel_shaders[] = {"scope_vslr_resolve", "scope_vslr_sphere", "scope_vslr_mirror"};
        if (C.iElement > element_mirror) return;
        const bool sphere = C.iElement == element_sphere;
        C.r_Pass("scope_screen_quad", pixel_shaders[C.iElement], FALSE, FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_position", sphere ? r2_RT_P : position_texture);
        C.r_dx10Texture("s_diffuse", sphere ? r2_RT_albedo : albedo_texture);
        C.r_dx10Texture("env_s0", r2_T_envs0);
        C.r_dx10Texture("env_s1", r2_T_envs1);
        if (!sphere)
        {
            C.r_dx10Texture("vslr_sky", sky_texture);
            C.r_dx10Texture("vslr_lit", lit_texture);
        }
        if (C.iElement == element_resolve) C.r_dx10Texture("vslr_direct", direct_texture);
        if (C.iElement == element_mirror) C.r_dx10Texture("vslr_mirror_previous", scope_vslr::mirror_previous_texture);
        // The resolve bounces the published direct light; the sphere traces the capture.
        if (C.iElement != element_mirror) scope_vslr_bind(C);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_End();
    }
};

// The lamps that light the capture: the frame's strongest lights that reach within the capture radius
// of the camera, kept when the frame's lights are gathered (unshadowed in the capture).
struct CaptureLamp
{
    Fvector position, direction, colour;
    float range = 0.f, cone_cosine = -1.f, score = 0.f;
};
constexpr u32 capture_lamp_count = 4;
CaptureLamp capture_lamps[capture_lamp_count];
u32 capture_lamps_frame = u32(-1);

// The installed light model over the capture's G-buffer (scope_vslr_light.ps), with the inputs the
// installed sun accumulation and combine bind.
class VslrLightBlender final : public IBlender
{
public:
    BOOL canBeLMAPped() override { return FALSE; }
    LPCSTR getComment() override { return "Scope VSLR light"; }
    void Compile(CBlender_Compile& C) override
    {
        IBlender::Compile(C);
        if (C.iElement != 0) return;
        C.r_Pass("scope_screen_quad", "scope_vslr_light", FALSE, FALSE, FALSE, FALSE);
        C.r_dx10Texture("s_position", position_texture);
        C.r_dx10Texture("s_diffuse", albedo_texture);
        C.r_dx10Texture("s_material", r2_material);
        C.r_dx10Texture("env_s0", r2_T_envs0);
        C.r_dx10Texture("env_s1", r2_T_envs1);
        C.r_dx10Texture("sky_s0", r2_T_sky0);
        C.r_dx10Texture("sky_s1", r2_T_sky1);
        C.r_dx10Texture("s_vslr_smap", sun_shadow_texture);
        C.r_dx10Sampler("smp_base");
        C.r_dx10Sampler("smp_linear");
        C.r_dx10Sampler("smp_nofilter");
        C.r_dx10Sampler("smp_material");
        C.r_dx10Sampler("smp_rtlinear");
        C.r_dx10Sampler("smp_smap");
        C.r_End();
    }
};

// A wrapper's verdict does not change while the process runs and a compile is a stall: tried once per process.
bool wrapper_compiles(LPCSTR name)
{
    static xr_map<shared_str, bool> verdicts;
    const shared_str key = name;
    const auto found = verdicts.find(key);
    if (found != verdicts.end()) return found->second;
    return verdicts[key] = svp_pixel_shader_compiles(name);
}

void vector_constant(LPCSTR name, const Fvector& value, float w = 0.f)
{
    RCache.set_c(name, value.x, value.y, value.z, w);
}

float elapsed_ms(const CTimerBase& timer)
{
    return timer.GetElapsed_sec() * 1000.f;
}

// Main-view state a capture face replaces: camera, viewport and output bindings. Restored field by
// field: the stored per-viewport matrices lag the live HUD projection when PiP is off, so
// SetActive would not restore it.
class DeviceView
{
public:
    explicit DeviceView(CRenderTarget& target)
        : target(target), target_width(target.get_width()), target_height(target.get_height()),
          view(Device.mView), project(Device.mProject), project_hud(Device.mProjectHud),
          full(Device.mFullTransform), full_hud(Device.mFullTransformHud), inv_view(Device.mInvView),
          inv_project(Device.mInvProject), inv_project_hud(Device.mInvProjectHud),
          inv_full(Device.mInvFullTransform), view_saved(Device.mView_saved),
          project_saved(Device.mProject_saved), full_saved(Device.mFullTransform_saved),
          view_prev(Device.mView_prev), project_prev(Device.mProject_prev), world(RCache.get_xform_world()),
          position(Device.vCameraPosition), direction(Device.vCameraDirection), top(Device.vCameraTop),
          right(Device.vCameraRight), position_saved(Device.vCameraPosition_saved), fov(Device.fFOV),
          aspect(Device.fASPECT), width(Device.dwWidth), height(Device.dwHeight)
    {
        UINT count = 1;
        HW.pContext->RSGetViewports(&count, &viewport);
        if (!count) viewport = {0.f, 0.f, float(width), float(height), 0.f, 1.f};
        for (u32 i = 0; i < 4; ++i)
            outputs[i] = RCache.get_RT(i);
        depth = RCache.get_ZB();
    }

    void restore() const
    {
        Device.mView = view;
        Device.mProject = project;
        Device.mProjectHud = project_hud;
        Device.mFullTransform = full;
        Device.mFullTransformHud = full_hud;
        Device.mInvView = inv_view;
        Device.mInvProject = inv_project;
        Device.mInvProjectHud = inv_project_hud;
        Device.mInvFullTransform = inv_full;
        Device.mView_saved = view_saved;
        Device.mProject_saved = project_saved;
        Device.mFullTransform_saved = full_saved;
        Device.mView_prev = view_prev;
        Device.mProject_prev = project_prev;
        Device.vCameraPosition = position;
        Device.vCameraDirection = direction;
        Device.vCameraTop = top;
        Device.vCameraRight = right;
        Device.vCameraPosition_saved = position_saved;
        Device.fFOV = fov;
        Device.fASPECT = aspect;
        Device.dwWidth = width;
        Device.dwHeight = height;
        RCache.Invalidate();
        Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
        Device.m_pRender->SetCacheXform_prev(Device.mView_prev, Device.mProject_prev);
        RCache.set_xform_world(world);
        target.u_setrt(target_width, target_height, outputs[0], outputs[1], outputs[2], depth);
        RCache.set_RT(outputs[3], 3);
        HW.pContext->RSSetViewports(1, &viewport);
    }

private:
    CRenderTarget& target;
    u32 target_width, target_height;
    Fmatrix view, project, project_hud, full, full_hud, inv_view, inv_project, inv_project_hud, inv_full;
    Fmatrix view_saved, project_saved, full_saved, view_prev, project_prev, world;
    Fvector position, direction, top, right, position_saved;
    float fov, aspect;
    u32 width, height;
    D3D11_VIEWPORT viewport{};
    ID3DRenderTargetView* outputs[4];
    ID3DDepthStencilView* depth;
};

class ScopeVslr
{
public:
    ScopeVslr();
    ~ScopeVslr();
    bool available() const { return available_; }
    void render(CRenderTarget& target, bool sphere, bool timed);
    const Fvector4& published() const { return published_; }
    float radius() const { return capture_.settings().radius; }
    float fade() const;
    // Whether this frame renders capture faces: one is under way, due, or the camera moved.
    bool capture_due() const;
    // Whether this frame begins a capture; its faces all read the sun shadow copied then.
    bool capture_starting() const { return !capture_.capturing() && capture_due(); }
    void keep_sun_shadow(float bias);
    const scope_vslr::Mirror& mirror() const { return mirror_; }
    void consumer_begin() { if (timed_) consumer_timing_.begin(HW.pContext); }
    void consumer_end() { if (timed_) consumer_timing_.end(HW.pContext); }

private:
    void begin_gbuffer(CRenderTarget& target, const Fmatrix& view, const Fmatrix& projection);
    void capture_face(CRenderTarget& target);
    void scene_light();
    void draw_sky(CRenderTarget& target);
    void light_surfaces(CRenderTarget& target, bool sun_shadow);
    void capture_mirror(CRenderTarget& target, IRenderable& viewer, const Fvector& eye, const Fvector& forward,
        const Fvector& up);
    void publish();
    void draw_sphere(CRenderTarget& target);
    void quad();
    void report();
    scope_vslr::Capture capture_;
    scope_vslr::Timing timing_;
    scope_vslr::Span consumer_timing_;
    CDSGraphManager graph_{CDSGraphManager::VQ_OFFSCREEN, 0, {true, false, false, true, false, false, false}};
    ref_rt position_, albedo_, heat_, motion_, depth_, sky_, lit_, sun_shadow_;
    // World to the kept sun cascade's shadow-map coordinates, and the frame it was kept.
    Fmatrix sun_shadow_world_ = Fidentity;
    u32 sun_shadow_frame_ = u32(-1);
    bool capture_shadowed_ = false; // the sun shadow was copied on the frame the capture began
    scope_vslr::Mirror mirror_;
    ref_texture reflection_, distance_, coverage_, direct_, previous_reflection_, previous_coverage_;
    ref_shader resolve_, sky_shader_, light_;
    ref_geom quad_geometry_;
    ID3D11ShaderResourceView* bound_ = nullptr;
    Fvector4 published_{};
    Fvector sphere_position_{};
    CSector* capture_sector_ = nullptr;
    u32 next_capture_ms_ = 0;
    u32 capture_started_ms_ = 0;
    u32 next_report_ms_ = 0;
    // Frame of the last publish and frames since the one before: consumers blend over that interval,
    // so each capture is shown whole when the next one is published.
    u32 publish_frame_ = 0;
    u32 publish_ms_ = 0;
    u32 interval_frames_ = 1;
    bool fading_ = false;
    unsigned publishes_ = 0, fades_ = 0;
    float traverse_ms_ = 0;
    bool anchored_ = false;
    bool available_ = false;
    bool timed_ = false;
};

ScopeVslr::ScopeVslr()
{
    available_ = capture_.create(HW.pDevice, {});
    if (!available_) return;
    timed_ = timing_.create(HW.pDevice) && consumer_timing_.create(HW.pDevice);
    quad_geometry_.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
    const u32 size = capture_.settings().resolution;
    position_.create(position_texture, size, size, D3DFMT_A16B16G16R16F);
    albedo_.create(albedo_texture, size, size, D3DFMT_A8R8G8B8);
    heat_.create("$user$vslr_heat", size, size, D3DFMT_A8R8G8B8);
    motion_.create("$user$vslr_motion", size, size, D3DFMT_A16B16G16R16F);
    depth_.create("$user$vslr_depth", size, size, D3DFMT_D24S8);
    sky_.create(sky_texture, size, size, D3DFMT_A16B16G16R16F);
    lit_.create(lit_texture, size, size, D3DFMT_A16B16G16R16F);
    // The capture is lit by the installed light model or not made at all.
    if (!wrapper_compiles("scope_vslr_light"))
    {
        Msg("! [VSLR] scope_vslr_light.ps does not compile with the installed light model; no scope reflections");
        available_ = false;
        return;
    }
    VslrLightBlender light;
    light_.create(&light, "scope_vslr_light");
    if (!mirror_.create(size))
        Msg("! [VSLR] mirror images not created; the glass shows the player as the sphere figure");
    // The installed sky, wrapped; without it the consumer's environment shows behind the scene.
    if (wrapper_compiles("scope_vslr_sky2"))
    {
        CBlender_skybox skybox;
        SvpPrefixedBlender prefixed(&skybox, "scope_vslr_");
        sky_shader_.create(&prefixed, "scope_vslr_sky");
    }
    else
        Msg("! [VSLR] scope_vslr_sky2.ps does not compile with the installed sky2.ps; captures have no sky");
    reflection_.create(colour_texture);
    distance_.create(distance_texture);
    coverage_.create(coverage_texture);
    direct_.create(direct_texture);
    previous_reflection_.create(previous_colour_texture);
    previous_coverage_.create(previous_coverage_texture);
    VslrBlender blender;
    resolve_.create(&blender, "scope_vslr");
    available_ = position_->pRT && albedo_->pRT && heat_->pRT && motion_->pRT && depth_->pZRT && sky_->pRT && lit_->pRT;
}

// The texture aliases would otherwise keep the released captures alive.
ScopeVslr::~ScopeVslr()
{
    for (ref_texture* texture :
        {&reflection_, &distance_, &coverage_, &direct_, &previous_reflection_, &previous_coverage_})
        if (*texture) (*texture)->surface_set(nullptr);
    mirror_.release();
    RCache.Invalidate();
}

void ScopeVslr::quad()
{
    u32 offset;
    FVF::TL* vertices = (FVF::TL*)RCache.Vertex.Lock(4, quad_geometry_->vb_stride, offset);
    const u32 white = color_rgba(255, 255, 255, 255);
    vertices[0].set(-1, -1, 0, 1, white, 0, 1);
    vertices[1].set(-1, 1, 0, 1, white, 0, 0);
    vertices[2].set(1, -1, 0, 1, white, 1, 1);
    vertices[3].set(1, 1, 0, 1, white, 1, 0);
    RCache.Vertex.Unlock(4, quad_geometry_->vb_stride);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();
    RCache.set_Geometry(quad_geometry_);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
}

// The installed sky box from the face's camera into vslr_sky, alpha one where it was drawn.
void ScopeVslr::draw_sky(CRenderTarget& target)
{
    const float zero[4] = {};
    HW.pContext->ClearRenderTargetView(sky_->pRT, zero);
    if (!sky_shader_) return;
    target.u_setrt(sky_, nullptr, nullptr, nullptr);
    target.set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
    CEnvironment& environment = g_pGamePersistent->Environment();
    u32 vertex_offset, index_offset;
    static_cast<dxEnvironmentRender&>(*environment.m_pRender).PrepareSkyBox(environment, vertex_offset, index_offset);
    RCache.set_Element(sky_shader_->E[0]);
    RCache.set_Textures(&static_cast<dxEnvDescriptorMixerRender&>(*environment.CurrentEnv->m_pDescriptorMixer).sky_r_textures);
    // The installed night vision branch recolours the sky; the capture keeps the plain sky.
    extern Fvector4 ps_dev_param_8;
    RCache.set_c("shader_param_8", 0.f, ps_dev_param_8.y, ps_dev_param_8.z, ps_dev_param_8.w);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();
    RCache.Render(D3DPT_TRIANGLELIST, vertex_offset, 0, 12, index_offset, 20);
}

// The ambient, sky and sun levels the deferred combine lights the scene with
// (r4_rendertarget_phase_combine.cpp), for the captured surfaces.
void ScopeVslr::scene_light()
{
    const CEnvDescriptorMixer& environment = *g_pGamePersistent->Environment().CurrentEnv;
    const float minimum = 0.001f;
    const float ambient = ps_r2_sun_lumscale_amb + g_pGamePersistent->nv_shader_data.lum_factor;
    RCache.set_c("L_ambient", _max(environment.ambient.x * 2, minimum) * ambient,
        _max(environment.ambient.y * 2, minimum) * ambient, _max(environment.ambient.z * 2, minimum) * ambient, 0);
    const float sky = 2 * ps_r2_sun_lumscale_hemi;
    RCache.set_c("env_color", (environment.hemi_color.x * 2 + EPS) * sky, (environment.hemi_color.y * 2 + EPS) * sky,
        (environment.hemi_color.z * 2 + EPS) * sky, environment.weight);
    // The sun as the installed sun accumulation sets it: view-space direction and colour.
    const light* sun = (light*)RImplementation.Lights.sun_adapted._get();
    Fvector direction, colour = {sun->color.r, sun->color.g, sun->color.b};
    Device.mView.transform_dir(direction, sun->direction);
    direction.normalize();
    vector_constant("Ldynamic_dir", direction);
    RCache.set_c("Ldynamic_color", colour.x, colour.y, colour.z, u_diffuse2s(colour));
}

// The captured surfaces through the installed light model into vslr_lit; without sun_shadow the baked
// sky visibility stands in for the sun's shadow map.
void ScopeVslr::light_surfaces(CRenderTarget& target, bool sun_shadow)
{
    target.u_setrt(lit_, nullptr, nullptr, nullptr);
    target.set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
    RCache.set_Element(light_->E[0]);
    scene_light();
    // r__scope_vslr_debug 3, 4 and 5 light the capture with the sun's shadow alone, the ambient alone and
    // the sun alone.
    RCache.set_c("vslr_capture_params", 1.f / Device.dwWidth, capture_.settings().radius,
        ps_scope_vslr_debug >= 3 ? float(ps_scope_vslr_debug) : 0.f, 0.f);
    // The sun's shadow from the widest cascade of the frame the capture began, from the face's view space.
    const bool shadowed = sun_shadow && capture_shadowed_;
    Fmatrix shadow;
    shadow.mul(sun_shadow_world_, Device.mInvView);
    RCache.set_c("vslr_shadow", shadow);
    RCache.set_c("vslr_shadow_params", shadowed ? 1.f : 0.f,
        shadowed ? 1.f / float(RImplementation.o.smapsize) : 0.f, 0.f, 0.f);
    // The kept lamps in the face's view space.
    const bool lamps = capture_lamps_frame == Device.dwFrame;
    for (u32 i = 0; i < capture_lamp_count; ++i)
    {
        const CaptureLamp& lamp = capture_lamps[i];
        const bool on = lamps && lamp.score > 0.f;
        Fvector position, direction;
        Device.mView.transform_tiny(position, lamp.position);
        Device.mView.transform_dir(direction, lamp.direction);
        RCache.set_ca("vslr_lamp_position", i, position.x, position.y, position.z, on ? 1.f / (lamp.range * lamp.range) : 0.f);
        RCache.set_ca("vslr_lamp_colour", i, lamp.colour.x, lamp.colour.y, lamp.colour.z, 0.f);
        RCache.set_ca("vslr_lamp_direction", i, direction.x, direction.y, direction.z, lamp.cone_cosine);
    }
    quad();
}

// Points the camera at a capture view and clears the capture G-buffer for it.
void ScopeVslr::begin_gbuffer(CRenderTarget& target, const Fmatrix& view, const Fmatrix& projection)
{
    RImplementation.SetMatrices(view, projection, projection);
    Device.m_pRender->SetCacheXform(Device.mView, Device.mProject);
    Device.dwWidth = Device.dwHeight = capture_.settings().resolution;
    RCache.Invalidate();
    target.u_setrt(position_, albedo_, heat_, motion_, depth_->pZRT);
    target.set_viewport_size(HW.pContext, float(Device.dwWidth), float(Device.dwHeight));
    const float zero[4] = {};
    for (const ref_rt* rt : {&position_, &albedo_, &heat_, &motion_})
        HW.pContext->ClearRenderTargetView((*rt)->pRT, zero);
    HW.pContext->ClearDepthStencilView(depth_->pZRT, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    RCache.set_Stencil(FALSE);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_ColorWriteEnable();
}

// The player's body model from in front of the eye, looking back, lit like the capture, into the
// mirror image (premultiplied colour, alpha where the body was drawn).
void ScopeVslr::capture_mirror(CRenderTarget& target, IRenderable& viewer, const Fvector& eye,
    const Fvector& forward, const Fvector& up)
{
    Fmatrix& body = mirror_.place_body(viewer, eye, forward, up);
    // The optical image is composited only while aiming.
    const bool aiming = target.scope_reference_composited_frame == Device.dwFrame;
    const scope_vslr::Mirror::Pass pass = mirror_.begin(eye, forward, up, aiming);
    begin_gbuffer(target, pass.view, pass.projection);
    graph_.clear();
    graph_.set_Object(&viewer);
    graph_.add_Dynamic(viewer.renderable.visual, &body);
    graph_.set_Object(nullptr);
    graph_.r_dsgraph_render_dynamic(0);
    RCache.set_RT(nullptr, 3);
    // The moved body is not where the main view's shadow map has it.
    light_surfaces(target, false);
    target.u_setrt(Device.dwWidth, Device.dwHeight, mirror_.target(), nullptr, nullptr, nullptr);
    RCache.set_Element(resolve_->E[element_mirror]);
    RCache.set_c("vslr_capture_params", 1.f / Device.dwWidth, scope_vslr::mirror_far_m, pass.weight, 0.f);
    quad();
    RCache.set_RT(nullptr, 0);
    mirror_.finish();
    if (ps_scope_vslr_debug == 2) mirror_.dump();
}

void ScopeVslr::keep_sun_shadow(float bias)
{
    const auto* main = RImplementation.TargetMain;
    if (!main || !main->rt_smap_depth) return;
    if (!sun_shadow_)
        sun_shadow_.create(sun_shadow_texture, RImplementation.o.smapsize, RImplementation.o.smapsize,
            (D3DFORMAT)RImplementation.o.HW_smap_FORMAT);
    if (!sun_shadow_->pSurface) return;
    HW.pContext->CopyResource(sun_shadow_->pSurface, main->rt_smap_depth->pSurface);
    // As the installed far-cascade accumulation builds m_shadow (accum_direct_cascade), in world space.
    const light* sun = (light*)RImplementation.Lights.sun_adapted._get();
    const Fmatrix adjust = {
        .5f, 0.f, 0.f, 0.f,
        0.f, -.5f, 0.f, 0.f,
        0.f, 0.f, ps_r2_sun_depth_far_scale, 0.f,
        .5f, .5f, bias, 1.f};
    Fmatrix project, offset;
    project.mul(adjust, sun->X.D.combine);
    Fvector shift;
    shift.mul(sun->direction, ps_r2_sun_tsm_bias);
    offset.translate(shift);
    sun_shadow_world_.mul(project, offset);
    sun_shadow_frame_ = Device.dwFrame;
}

void ScopeVslr::capture_face(CRenderTarget& target)
{
    const auto axes = scope_vslr::face_axes[capture_.face()];
    const auto origin = capture_.capture_origin();
    Fvector eye = to_fvector(origin);
    Fmatrix view, projection, full;
    view.build_camera_dir(eye, to_fvector(axes.direction), to_fvector(axes.up));
    projection.build_projection(PI_DIV_2, 1.f, capture_near_m, capture_.settings().radius);
    full.mul(projection, view);
    CFrustum frustum;
    frustum.CreateFromMatrix(full, FRUSTUM_P_LRTB | FRUSTUM_P_NEAR | FRUSTUM_P_FAR);
    begin_gbuffer(target, view, projection);
    CTimerBase traversal;
    traversal.Start();
    graph_.clear();
    graph_.traverse(capture_sector_, frustum, eye, full);
    graph_.r_dsgraph_capture_static();
    traverse_ms_ += elapsed_ms(traversal);
    graph_.r_dsgraph_render_static(0);
    RCache.set_RT(nullptr, 3);
    draw_sky(target);
    light_surfaces(target, true);
    target.u_setrt(Device.dwWidth, Device.dwHeight, capture_.colour_target(), capture_.distance_target(),
        capture_.coverage_target(), nullptr);
    RCache.set_RT(capture_.direct_target(), 3);
    RCache.set_Element(resolve_->E[element_resolve]);
    RCache.set_c("vslr_inv_view", Device.mInvView);
    RCache.set_c("vslr_capture_params", 1.f / Device.dwWidth, capture_.settings().radius, ps_scope_vslr_indirect, 0.f);
    quad();
    RCache.set_RT(nullptr, 3);
    capture_.finish_face();
}

// Points the capture textures at the newest complete capture. Before DeviceView::restore (see render).
void ScopeVslr::publish()
{
    if (!capture_.ready() || capture_.colour() == bound_) return;
    // The last face's targets are still bound; mips are not generated for a bound target. The restore
    // after this rebinds the main view.
    HW.pContext->OMSetRenderTargets(0, nullptr, nullptr);
    Microsoft::WRL::ComPtr<ID3D11Resource> colour, distance, coverage, direct;
    capture_.colour()->GetResource(colour.GetAddressOf());
    capture_.distance()->GetResource(distance.GetAddressOf());
    capture_.coverage()->GetResource(coverage.GetAddressOf());
    capture_.direct()->GetResource(direct.GetAddressOf());
    // Rough reflections sample the colour and coverage mip chains, built once per published capture.
    HW.pContext->GenerateMips(capture_.colour());
    HW.pContext->GenerateMips(capture_.coverage());
    // The next capture's bounce samples a coarse level of the direct light.
    HW.pContext->GenerateMips(capture_.direct());
    reflection_->surface_set(colour.Get());
    distance_->surface_set(distance.Get());
    coverage_->surface_set(coverage.Get());
    direct_->surface_set(direct.Get());
    bound_ = capture_.colour();
    const auto origin = capture_.published_origin();
    published_.set(origin.x, origin.y, origin.z, capture_.settings().radius);
    // Fade from the previous capture unless there is none, it is stale (after an idle spell) or the view
    // moved too far for the two to show the same place (a teleport).
    Microsoft::WRL::ComPtr<ID3D11Resource> previous_colour = colour, previous_coverage = coverage;
    fading_ = false;
    if (capture_.has_previous())
    {
        previous_colour.Reset();
        previous_coverage.Reset();
        capture_.previous_colour()->GetResource(previous_colour.GetAddressOf());
        capture_.previous_coverage()->GetResource(previous_coverage.GetAddressOf());
        const float shift = to_fvector(capture_.previous_origin()).distance_to(to_fvector(origin));
        fading_ = ps_scope_vslr_smooth && Device.dwTimeGlobal - publish_ms_ < fade_max_age_ms
            && shift < fade_max_shift * capture_.settings().radius;
    }
    previous_reflection_->surface_set(previous_colour.Get());
    previous_coverage_->surface_set(previous_coverage.Get());
    interval_frames_ = _min(_max(Device.dwFrame - publish_frame_, 1u), fade_max_frames);
    publish_frame_ = Device.dwFrame;
    publish_ms_ = Device.dwTimeGlobal;
    ++publishes_;
    fades_ += fading_ ? 1 : 0;
}

float ScopeVslr::fade() const
{
    if (!fading_) return 1.f;
    return clampr(float(Device.dwFrame - publish_frame_) / float(interval_frames_), 0.f, 1.f);
}

void ScopeVslr::draw_sphere(CRenderTarget& target)
{
    if (!anchored_)
    {
        sphere_position_.mad(Device.vCameraPosition, Device.vCameraDirection, sphere_ahead_m);
        anchored_ = true;
    }
    if (!bound_) return;
    target.u_setrt(target.rt_Color, nullptr, nullptr, HW.pBaseZB);
    RCache.set_Element(resolve_->E[element_sphere]);
    vector_constant("vslr_sphere", sphere_position_, sphere_radius_m);
    RCache.set_c("vslr_origin", published_.x, published_.y, published_.z, 0);
    vector_constant("vslr_eye", Device.vCameraPosition);
    Fvector right = Device.vCameraRight, up = Device.vCameraTop;
    right.mul(1.f / Device.mProject._11);
    up.mul(1.f / Device.mProject._22);
    vector_constant("vslr_ray_right", right);
    vector_constant("vslr_ray_up", up);
    vector_constant("vslr_ray_forward", Device.vCameraDirection);
    RCache.set_c("vslr_debug_params", 1.f / Device.dwWidth, 1.f / Device.dwHeight, published_.w, 1.f);
    // The sky behind empty capture texels, as the capture lights its surfaces.
    scene_light();
    quad();
}

void ScopeVslr::report()
{
    if (Device.dwTimeGlobal < next_report_ms_) return;
    next_report_ms_ = Device.dwTimeGlobal + 1000;
    const auto totals = timing_.take();
    const auto consumer = consumer_timing_.take(HW.pContext);
    if (!totals.frames && !consumer.passes) return;
    const unsigned publishes = publishes_, fades = fades_;
    publishes_ = fades_ = 0;
    const float per_face = totals.faces ? 1.f / float(totals.faces) : 0.f;
    Msg("[VSLR-COST] frames=%u faces=%u capture_gpu=%.3f ms/face draw_gpu=%.3f ms/frame "
        "traverse_cpu=%.3f ms/face capture_cpu=%.3f ms/face consumer_gpu=%.4f ms/pass passes=%u "
        "publishes=%u fades=%u size=%u radius=%.1f bytes=%u",
        totals.frames, totals.faces, totals.capture_gpu_ms * per_face,
        totals.frames ? totals.draw_gpu_ms / totals.frames : 0.f,
        totals.traverse_cpu_ms * per_face, totals.submit_cpu_ms * per_face,
        consumer.passes ? consumer.gpu_ms / consumer.passes : 0.0, consumer.passes, publishes, fades,
        capture_.settings().resolution,
        capture_.settings().radius, capture_.texture_bytes());
}

bool ScopeVslr::capture_due() const
{
    if (capture_.capturing() || Device.dwTimeGlobal >= next_capture_ms_) return true;
    return ps_scope_vslr_smooth && Device.dwTimeGlobal - capture_started_ms_ >= u32(ps_scope_vslr_move_interval)
        && Device.vCameraPosition.distance_to(to_fvector(capture_.capture_origin())) >= capture_move_m;
}

void ScopeVslr::render(CRenderTarget& target, bool sphere, bool timed)
{
    if (!RImplementation.pLastSector) return;
    timed = timed && timed_;
    if (timed) timing_.begin(HW.pContext);
    if (!capture_.capturing() && capture_due())
    {
        capture_.begin({Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z});
        capture_sector_ = RImplementation.pLastSector;
        capture_started_ms_ = Device.dwTimeGlobal;
        capture_shadowed_ = sun_shadow_frame_ == Device.dwFrame;
        next_capture_ms_ = capture_started_ms_ + capture_interval_ms;
    }
    CTimerBase submit;
    submit.Start();
    traverse_ms_ = 0;
    unsigned faces = 0;
    // The player's body, for the glass drawn this frame when it shows the player (read next frame).
    const bool aiming = target.scope_reference_composited_frame == Device.dwFrame;
    const bool mirror = ps_scope_vslr && mirror_.available() && target.scope_glass_deferred_current()
        && scope_glass_shooter_shown(aiming);
    CObject* viewer_object = mirror && g_pGameLevel ? g_pGameLevel->CurrentViewEntity() : nullptr;
    IRenderable* viewer = viewer_object && !viewer_object->getDestroy() ? viewer_object->dcast_Renderable() : nullptr;
    if (viewer && !viewer->renderable.visual) viewer = nullptr;
    if (capture_.capturing() || viewer)
    {
        const Fvector eye = Device.vCameraPosition, forward = Device.vCameraDirection, up = Device.vCameraTop,
                      right = Device.vCameraRight;
        const DeviceView main_view(target);
        const u32 phase = RImplementation.phase;
        RImplementation.phase = CRender::PHASE_NORMAL;
        for (; faces < capture_.settings().faces_per_frame && capture_.capturing(); ++faces)
            capture_face(target);
        if (viewer)
            capture_mirror(target, *viewer, scope_glass_shooter_eye(eye, right, up, forward), forward, up);
        RImplementation.phase = phase;
        // A capture completes only on a frame that rendered faces. Published before the restore: a bind
        // cache reset after it would leave the next pass (the lens flares) without its targets.
        publish();
        main_view.restore();
    }
    const float submit_ms = elapsed_ms(submit);
    if (timed) timing_.mark(HW.pContext, scope_vslr::Timing::Captured);
    if (sphere) draw_sphere(target);
    if (!timed)
    {
        // Counted from the first timed frame, not from level start.
        publishes_ = fades_ = 0;
        return;
    }
    timing_.mark(HW.pContext, scope_vslr::Timing::Drawn);
    timing_.end(HW.pContext, faces, traverse_ms_, submit_ms);
    report();
}

std::unique_ptr<ScopeVslr> reflections;
u32 last_request_ms = 0;
bool requested_once = false;
}

bool scope_vslr_active()
{
    return ps_scope_vslr && requested_once && Device.dwTimeGlobal - last_request_ms < idle_ms;
}

void scope_vslr_request()
{
    last_request_ms = Device.dwTimeGlobal;
    requested_once = true;
}

void scope_vslr_bind(CBlender_Compile& C)
{
    C.r_dx10Texture("vslr_colour", colour_texture);
    C.r_dx10Texture("vslr_distance", distance_texture);
    C.r_dx10Texture("vslr_coverage", coverage_texture);
    C.r_dx10Texture("vslr_colour_previous", previous_colour_texture);
    C.r_dx10Texture("vslr_coverage_previous", previous_coverage_texture);
    C.r_dx10Texture("vslr_mirror", scope_vslr::mirror_texture);
    u32 stage = C.r_dx10Sampler("vslr_point");
    C.i_dx10Address(stage, D3DTADDRESS_CLAMP);
    C.i_dx10Filter(stage, D3DTEXF_POINT, D3DTEXF_NONE, D3DTEXF_POINT);
    stage = C.r_dx10Sampler("vslr_linear");
    C.i_dx10Address(stage, D3DTADDRESS_CLAMP);
    C.i_dx10Filter(stage, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR);
}

// [VSLR-COST] timing: r__scope_vslr_debug 2 or r__scope_vslr_cost 1.
static bool cost_logged()
{
    return ps_scope_vslr_debug == 2 || ps_scope_vslr_cost;
}

void scope_vslr_consumer_begin()
{
    if (reflections && cost_logged()) reflections->consumer_begin();
}

void scope_vslr_consumer_end()
{
    if (reflections && cost_logged()) reflections->consumer_end();
}

Fvector4 scope_vslr_published()
{
    return reflections && ps_scope_vslr ? reflections->published() : Fvector4{};
}

bool scope_vslr_mirror_view(const Fvector& eye, const Fvector& view_forward, const Fvector& view_up, Fvector4& origin,
    Fvector4& right, Fvector4& up, Fvector4& forward, Fvector4& head)
{
    return reflections && ps_scope_vslr && reflections->mirror().view(eye, view_forward, view_up, origin, right, up, forward, head);
}

void scope_vslr_scene_lights(const light_Package& normal, const light_Package& pending)
{
    if (!reflections || !reflections->available() || !scope_vslr_active()) return;
    for (CaptureLamp& lamp : capture_lamps) lamp = {};
    capture_lamps_frame = Device.dwFrame;
    const Fvector& centre = Device.vCameraPosition;
    const float reach = reflections->radius();
    auto consider = [&](light* source)
    {
        if (source && source->get_type() == IRender_Light::OMNIPART) source = source->omipart_parent;
        if (!source || !source->flags.bActive || source->range <= EPS) return;
        for (const CaptureLamp& kept : capture_lamps)
            if (kept.score > 0.f && kept.position.similar(source->position) && kept.range == source->range) return;
        const float distance = centre.distance_to(source->position);
        if (distance >= source->range + reach) return;
        // Brighter, wider and nearer lamps first.
        const float score = (source->color.r + source->color.g + source->color.b) * source->range / (1.f + distance);
        u32 slot = capture_lamp_count;
        for (u32 i = 0; i < capture_lamp_count; ++i)
            if (score > capture_lamps[i].score) { slot = i; break; }
        if (slot == capture_lamp_count) return;
        for (u32 i = capture_lamp_count - 1; i > slot; --i) capture_lamps[i] = capture_lamps[i - 1];
        CaptureLamp& lamp = capture_lamps[slot];
        lamp.position = source->position;
        lamp.direction = source->direction;
        lamp.colour.set(source->color.r, source->color.g, source->color.b);
        lamp.range = source->range;
        lamp.cone_cosine = source->get_type() == IRender_Light::SPOT ? cosf(source->cone * .5f) : -1.f;
        lamp.score = score;
    };
    for (const light_Package* package : {&normal, &pending})
        for (const auto* sources : {&package->v_point, &package->v_spot, &package->v_shadowed})
            for (light* source : *sources) consider(source);
}

void scope_vslr_sun_cascade(float bias)
{
    // Copied once per capture, on the frame it begins; the mirror image is lit without it.
    if (reflections && reflections->available() && scope_vslr_active() && RImplementation.o.HW_smap
        && RImplementation.Target == RImplementation.TargetMain && reflections->capture_starting())
        reflections->keep_sun_shadow(bias);
}

float scope_vslr_fade()
{
    return reflections ? reflections->fade() : 1.f;
}

void scope_vslr_destroy()
{
    reflections.reset();
    requested_once = false;
}

void scope_vslr_render(CRenderTarget& target)
{
    if (&target != RImplementation.TargetMain) return;
    // The capture renders the static G-buffer without multisampling; switched off it holds nothing.
    if (RImplementation.o.dx10_msaa || (!ps_scope_vslr && !ps_scope_vslr_debug))
    {
        if (!reflections) return;
        // Mid-combine: the destructor resets the bind cache, so the main view is rebound after it.
        const DeviceView main_view(target);
        reflections.reset();
        main_view.restore();
        return;
    }
    if (!reflections)
    {
        // Created on a level's first frame, not on the first aim: creating it compiles shaders and is a
        // stall the aim transition would show.
        CTimerBase creation;
        creation.Start();
        reflections = std::make_unique<ScopeVslr>();
        if (reflections->available())
            Msg("[VSLR] capture created in %.1f ms", elapsed_ms(creation));
    }
    if (!reflections->available() || (!scope_vslr_active() && !ps_scope_vslr_debug)) return;
    // The debug sphere's display mapping is SDR only.
    const bool sphere = ps_scope_vslr_debug && !RImplementation.o.dx11_hdr10;
    reflections->render(target, sphere, cost_logged());
}
