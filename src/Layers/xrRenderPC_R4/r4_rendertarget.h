#pragma once

#include <functional> // pip draw_scope phase bind callback
#include "../xrRender/ColorMapManager.h"
#include "../xrRender/light_db.h"
#include "SvpDlss.h" // pip SVP DLSS-SR input contract
#include "svp_reference_packet.h"
#include "svp_reference_material_lifetime.h"
#include "svp_reference_sky_capture.h"
class light;
class dxRender_Visual;

// pip SVP scene render extent for the color/depth/MV targets, full display-res (svp_height) at gate 0
// so r__svp_render_scale is inert until the DLSS scaffolding is active (r__svp_dlss != 0)
u32 svp_render_extent();

//#define DU_SPHERE_NUMVERTEX 92
//#define DU_SPHERE_NUMFACES	180
//#define DU_CONE_NUMVERTEX	18
//#define DU_CONE_NUMFACES	32
//	no less than 2
#define	VOLUMETRIC_SLICES	100

class CRenderTarget : public IRender_Target
{
private:
	u32 dwWidth;
	u32 dwHeight;
	u32 dwAccumulatorClearMark;
public:
	enum eStencilOptimizeMode
	{
		SO_Light = 0,
		//	Default
		SO_Combine,
		//	Default
	};

	u32 dwLightMarkerID;
	//
	IBlender* b_occq;
	IBlender* b_accum_mask;
	IBlender* b_accum_direct;
	IBlender* b_accum_point;
	IBlender* b_accum_spot;
	IBlender* b_accum_reflected;
	IBlender* b_bloom;
	IBlender* b_luminance;
	IBlender* b_combine;
	IBlender* b_sunshafts;
	IBlender* b_postprocess_msaa;
	IBlender* b_bloom_msaa;
	IBlender* b_combine_msaa[8];
	IBlender* b_accum_mask_msaa[8];
	IBlender* b_accum_spot_msaa[8];
	IBlender* b_accum_direct_msaa[8];
	IBlender* b_accum_direct_volumetric_msaa[8];
	IBlender* b_accum_direct_volumetric_sun_msaa[8];
	IBlender* b_accum_volumetric_msaa[8];
	IBlender* b_accum_point_msaa[8];
	IBlender* b_accum_reflected_msaa[8];
	IBlender* b_ssao;
	IBlender* b_ssao_msaa[8];

	IBlender* b_hdr10_bloom_downsample;
	IBlender* b_hdr10_bloom_blur;
	IBlender* b_hdr10_bloom_upsample;
	
	IBlender* b_hdr10_lens_flare_downsample;
	IBlender* b_hdr10_lens_flare_fgen;
	IBlender* b_hdr10_lens_flare_blur;
	IBlender* b_hdr10_lens_flare_upsample;

	IBlender* b_blur;
	IBlender* b_dof;
	IBlender* b_pp_bloom;
	IBlender* b_gasmask_drops;
	IBlender* b_gasmask_dudv;
	IBlender* b_nightvision;
	IBlender* b_fakescope; //crookr
	IBlender* b_heatvision; //--DSR-- HeatVision
	IBlender* b_lut;
	IBlender* b_smaa;
	// compute shader for hdao
	IBlender* b_hdao_cs;
	IBlender* b_hdao_msaa_cs;

	// [SSS Stuff]
	IBlender* b_ssfx_fog_scattering;
	IBlender* b_ssfx_motion_blur;
	IBlender* b_ssfx_taa;
	IBlender* b_ssfx_rain;
	IBlender* b_ssfx_water_blur;
	IBlender* b_ssfx_bloom_downsample;
	IBlender* b_ssfx_bloom_upsample;
	IBlender* b_ssfx_bloom;
	IBlender* b_ssfx_bloom_lens;
	IBlender* b_ssfx_sss_ext;
	IBlender* b_ssfx_sss;
	IBlender* b_ssfx_ssr;
	IBlender* b_ssfx_volumetric_blur;
	IBlender* b_ssfx_ao;

#ifdef DEBUG
	struct		dbg_line_t		{
		Fvector	P0,P1;
		u32		color;
	};
	xr_vector<std::pair<Fsphere,Fcolor> >		dbg_spheres;
	xr_vector<dbg_line_t>										dbg_lines;
	xr_vector<Fplane>												dbg_planes;
#endif

	// PiP base RT/ZB, main = HW.secret_pBaseRT, SVP renders into its own rt_baseRT/baseZB
	ID3D11RenderTargetView* baseRT;
	ID3D11DepthStencilView* baseZB;
	ref_rt rt_baseRT;
	ref_rt rt_baseZB;
	// SVP RTs are created $svp-suffixed and remapped over the stock names in SetActive
	xr_vector<std::pair<ref_texture, ref_rt>> RenderTargetRemaps;

	// MRT-path
	ref_rt rt_Depth; // Z-buffer like - initial depth
	ref_rt rt_MSAADepth; // z-buffer for MSAA deferred shading
	ref_rt rt_Generic_0_r; // MRT generic 0
	ref_rt rt_Generic_1_r; // MRT generic 1
	ref_rt rt_Generic;
	ref_rt rt_Position; // 64bit,	fat	(x,y,z,?)				(eye-space)
	ref_rt rt_Color; // 64/32bit,fat	(r,g,b,specular-gloss)	(or decompressed MET-8-8-8-8)

	//--DSR-- HeatVision_start
	ref_rt rt_Heat;
	//--DSR-- HeatVision_end

	//
	ref_rt rt_Accumulator; // 64bit		(r,g,b,specular)
	ref_rt rt_Accumulator_temp; // only for HW which doesn't feature fp16 blend
	ref_rt rt_sunshafts_0; // ss0
	ref_rt rt_sunshafts_1; // ss1
	ref_rt rt_Generic_0; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.
	ref_rt rt_Generic_1; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.

	resptr_core<CRT, resptrcode_crt> rt_Generic_temp;

	ref_rt rt_secondVP;	// 32bit		(r,g,b,a) --//#SM+#-- +SecondVP+
	ID3DTexture2D* m_svp_nb_mip_surf = nullptr; // pip mip-prefiltered svp color for near-blur
	ref_texture m_svp_nb_mip_tex; // pip holds the all-mip srv for GenerateMips


	ref_rt rt_fakescope;	// crookr fakescope

	ref_rt rt_dof;
	ref_rt rt_ui_pda;

	ref_rt rt_blur_h_2;
	ref_rt rt_blur_2;

	ref_rt rt_blur_h_4;
	ref_rt rt_blur_4;

	ref_rt rt_blur_h_8;
	ref_rt rt_blur_8;

	ref_rt rt_pp_bloom;

	ref_rt rt_smaa_edgetex;
	ref_rt rt_smaa_blendtex;

	//	Igor: for volumetric lights
	ref_rt rt_Generic_2; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.
	ref_rt rt_Bloom_1; // 32bit, dim/4	(r,g,b,?)
	ref_rt rt_Bloom_2; // 32bit, dim/4	(r,g,b,?)
	ref_rt rt_LUM_64; // 64bit, 64x64,	log-average in all components
	ref_rt rt_LUM_8; // 64bit, 8x8,		log-average in all components

	ref_rt rt_LUM_pool[2]; // 1xfp32,1x1,		exp-result -> scaler
	ref_texture t_LUM_src; // source
	ref_texture t_LUM_dest; // destination & usage for current frame

	// HDR10
	ref_rt rt_HDR10_HalfRes[2];

	// env
	ref_texture t_envmap_0; // env-0
	ref_texture t_envmap_1; // env-1
	ref_texture t_reticle; // pip per-lens reticle art bound for the scope color shader ($user$reticle)

	// smap
	ref_rt rt_smap_surf; // 32bit,		color
	ref_rt rt_smap_depth; // 24(32) bit,	depth
	ref_rt rt_smap_depth_minmax; //	is used for min/max sm
	//	TODO: DX10: CHeck if we need old-style SMAP
	//	IDirect3DSurface9*			rt_smap_ZB;		//

	// Screen Space Shaders Stuff
	ref_rt rt_ssfx;
	ref_rt rt_ssfx_temp;
	ref_rt rt_ssfx_temp2;
	ref_rt rt_ssfx_temp3;

	ref_rt rt_ssfx_accum;
	//ref_rt rt_ssfx_hud; // DEPRECATED
	ref_rt rt_ssfx_ssr;
	ref_rt rt_ssfx_water;
	ref_rt rt_ssfx_water_waves;
	ref_rt rt_ssfx_ao;
	ref_rt rt_ssfx_il;

	ref_rt rt_ssfx_sss;
	ref_rt rt_ssfx_sss_ext;
	ref_rt rt_ssfx_sss_ext2;
	ref_rt rt_ssfx_sss_tmp;
	ref_rt rt_ssfx_bloom1;
	ref_rt rt_ssfx_bloom_emissive;
	ref_rt rt_ssfx_bloom_lens;
	ref_rt rt_ssfx_rain;
	ref_rt rt_ssfx_volumetric;
	ref_rt rt_ssfx_volumetric_tmp;

	ref_rt rt_ssfx_bloom_tmp2;
	ref_rt rt_ssfx_bloom_tmp4;
	ref_rt rt_ssfx_bloom_tmp8;
	ref_rt rt_ssfx_bloom_tmp16;
	ref_rt rt_ssfx_bloom_tmp32;
	ref_rt rt_ssfx_bloom_tmp64;

	ref_rt rt_ssfx_bloom_tmp32_2;
	ref_rt rt_ssfx_bloom_tmp16_2;
	ref_rt rt_ssfx_bloom_tmp8_2;
	ref_rt rt_ssfx_bloom_tmp4_2;

	ref_rt rt_ssfx_taa;
	ref_rt rt_ssfx_prev_frame;
	ref_rt rt_ssfx_motion_vectors;

	ref_rt rt_ssfx_prevPos;

	ref_shader s_ssfx_water;
	ref_shader s_ssfx_water_blur;
	ref_shader s_ssfx_water_ssr;
	ref_shader s_ssfx_ao;
	//ref_shader s_ssfx_hud[5]; // SSS23: DEPRECATED

	Fmatrix Matrix_previous, Matrix_current;
	//Fmatrix Matrix_HUD_previous, Matrix_HUD_current;
	Fvector3 Position_previous;
	//bool RVelocity;

	ref_rt rt_tempzb; // Redotix99: for 3D Shader Based Scopes

	ref_shader s_ssfx_dumb;

	// pip scope lens glue shaders, created lazily (never at init) so a setup without the PiP mod is unaffected
	ref_shader s_scope_color_write;
    ref_shader s_scope_reference_filter;
    ref_rt rt_scope_reference_filter[4]; // highlights, horizontal blur, vertical blur, dispersion
    ref_texture t_scope_reference_filter_source;
    ref_texture t_scope_reference_composite[3]; // capture, translucent dirt, opaque dirt
    SvpReferencePacket scope_reference_packet;
    ref_texture scope_reference_glass_materials[2];
    ref_texture t_scope_reference_glass_inputs[2];
    ref_texture scope_reference_materials[2]; // keeps the loaded textures when a packet is rejected
    SvpReferenceMaterials::QualifiedSurface<ID3DBaseTexture> scope_reference_material_checks[4]; // materials, then glass materials
    ref_rt rt_scope_reference_raw, rt_scope_reference_sky_raw, rt_scope_reference_ocular, rt_scope_reference_inside;
    ref_texture t_scope_reference_main_meter;
    ref_shader s_scope_reference_glyph;
    ref_shader s_scope_reference_capture, s_scope_reference_mesh, s_scope_reference_tonemap;
    ref_shader s_scope_reference_distortion, s_scope_reference_volumetric;
    ref_shader s_scope_reference_particle_display, s_scope_reference_particle_baseline, s_scope_reference_particle_resolve;
    ref_shader s_scope_reference_fog, s_scope_reference_fog_blur;
    ref_rt rt_scope_reference_fog[2];
    ref_texture t_scope_reference_fog_blur;
    ref_rt rt_scope_reference_scene_temp;
    ref_texture t_scope_reference_scene_source;
    ref_texture t_scope_reference_particle_display;
    ref_shader s_scope_reference_reticle;
    ref_shader s_scope_reference_reticle_deferred;
    SvpReferenceSkyCapture scope_reference_sky;
    u32 scope_reference_sky_frame = u32(-1);
    u32 scope_reference_camera_frame = u32(-1);
    ref_texture t_scope_reference_ocular;
    u32 scope_reference_raw_frame = u32(-1);
    SvpReferenceStamp scope_reference_raw_stamp;
    u32 scope_reference_composited_frame = u32(-1);
    u32 scope_glass_deferred_frame = u32(-1);
    IRenderable* scope_glass_deferred_owner = nullptr; // valid while scope_glass_deferred_current()
    ref_shader s_scope_glass_deferred[6]; // by skinning mode -1..4; -1 is the soft-skinned shader
    bool scope_glass_deferred_current() const { return scope_glass_deferred_frame == Device.dwFrame; }
    void phase_scope_glass_deferred();
    // lens glass composition (svp_optics.cpp, scope_glass_compose.h): the glass's own radiance at its
    // pixels, after the installed combine, or under night vision into its light buffer before it
    ref_shader s_scope_glass_compose, s_scope_glass_compose_nvg;
    void scope_glass_compose_create();
    bool scope_glass_compose_begin(bool night_vision);
    void scope_glass_compose_draw(u32 offset);
    // bind(shader) sets the element and the combine's constants, as for the installed combine.
    template <typename Bind> void phase_scope_glass_combine(Bind&& bind, u32 offset)
    {
        if (!scope_glass_compose_begin(false)) return;
        bind(s_scope_glass_compose);
        scope_glass_compose_draw(offset);
    }
    template <typename Bind> void phase_scope_glass_night_vision(Bind&& bind, u32 offset)
    {
        if (!scope_glass_compose_begin(true)) return;
        u_setrt(rt_Accumulator, nullptr, nullptr, baseZB);
        bind(s_scope_glass_compose_nvg);
        scope_glass_compose_draw(offset);
        u_setrt(rt_Generic_0, rt_Generic_1, rt_Heat, baseZB);
    }
    // The late forward glass layer is RGB; under night vision with deferred glass the combine has
    // already encoded the target and the deferred glass carries the lens.
    bool scope_glass_forward_layer_allowed() const;
    ref_shader s_scope_glass_fallback;
	ref_shader s_scope_depth_write;
	ref_shader s_scope_debug; // pip r__scope_debug on-screen multi-view inspector overlay
	ref_shader s_scope_lensfx; // pip r__svp_lensfx additive lens FX (CA, distortion, exit-pupil dimming)
	ref_shader s_svp_nearblur; // pip near-field defocus on the scope image (svpscope 2)
	ref_shader s_svp_distort_stamp; // pip neutral distort-mask stamp over the composited lens
	ref_shader s_svp_taa_stamp; // pip raw taa skip mask over the composited lens
	ref_shader s_svp_lens_stencil; // pip 3DSS lens stencil (254) stamp under the late-pass overlays
	ref_shader s_svp_lens_stencil_glass; // pip the same stamp where deferred physical glass is the visible lens
	bool m_scope_shaders_ready = false;
	// taa history seeds from the current frame on the next resolve, true at creation and on scope edges
	bool m_taa_seed_history = true;
	bool m_svp_dlss_built = false; // pip this SVP target was built with the DLSS gate on (toggle-recreate key)
	// lean gate one-shot clear, the water reflection buffer gets wiped once on entering the skip
	bool m_lean_water_cleared = false;
	void EnsureScopeShaders();
	void phase_scope_debug(); // pip draw the scope debug overlay (main+SVP views, ssfx, smap), main only

	//	Igor: for async screenshots
	ID3DTexture2D* t_ss_async; //32bit		(r,g,b,a) is situated in the system memory

	// Textures
	ID3DTexture3D* t_material_surf;
	ref_texture t_material;

	ID3DTexture2D* t_noise_surf [TEX_jitter_count];
	ref_texture t_noise [TEX_jitter_count];
	ID3DTexture2D* t_noise_surf_mipped;
	ref_texture t_noise_mipped;
private:
	// OCCq

	ref_shader s_occq;
	ref_shader s_sunshafts;
	// SSAO
	ref_rt rt_ssao_temp;
	ref_rt rt_half_depth;
	ref_shader s_ssao;
	ref_shader s_ssao_msaa[8];
	ref_shader s_hdao_cs;
	ref_shader s_hdao_cs_msaa;



	// Accum
	ref_shader s_accum_mask;
	ref_shader s_accum_direct;
	ref_shader s_accum_direct_volumetric;
	ref_shader s_accum_direct_volumetric_minmax;
	ref_shader s_accum_point;
	ref_shader s_accum_spot;
	ref_shader s_accum_reflected;
	ref_shader s_accum_volume;
	ref_shader s_blur;
	ref_shader s_dof;
	ref_shader s_pp_bloom;
	ref_shader s_gasmask_drops;
	ref_shader s_gasmask_dudv;
	ref_shader s_nightvision;
	ref_shader s_fakescope; //crookr
	ref_shader s_heatvision; //--DSR-- HeatVision
	ref_shader s_smaa;

	ref_shader s_lut;
	//	generate min/max
	ref_shader s_create_minmax_sm;

	//	DX10 Rain
	ref_shader s_rain;

	ref_shader s_rain_msaa[8]; // up to 8 shaders for DX10.0 support
	ref_shader s_accum_direct_volumetric_msaa[8];
	ref_shader s_accum_mask_msaa[8];
	ref_shader s_accum_direct_msaa[8];
	ref_shader s_mark_msaa_edges;
	ref_shader s_accum_point_msaa[8];
	ref_shader s_accum_spot_msaa[8];
	ref_shader s_accum_reflected_msaa[8];
	ref_shader s_accum_volume_msaa[8];

	// Screen Space Shaders Stuff
	ref_shader s_ssfx_fog_scattering;
	ref_shader s_ssfx_motion_blur;
	ref_shader s_ssfx_taa;
	ref_shader s_ssfx_rain;
	ref_shader s_ssfx_bloom;
	ref_shader s_ssfx_bloom_lens;
	ref_shader s_ssfx_bloom_upsample;
	ref_shader s_ssfx_bloom_downsample;
	ref_shader s_ssfx_sss_ext;
	ref_shader s_ssfx_sss;

	ref_shader s_ssfx_ssr;
	ref_shader s_ssfx_volumetric_blur;

	ref_geom g_accum_point;
	ref_geom g_accum_spot;
	ref_geom g_accum_omnipart;
	ref_geom g_accum_volumetric;

	ID3DVertexBuffer* g_accum_point_vb;
	ID3DIndexBuffer* g_accum_point_ib;

	ID3DVertexBuffer* g_accum_omnip_vb;
	ID3DIndexBuffer* g_accum_omnip_ib;

	ID3DVertexBuffer* g_accum_spot_vb;
	ID3DIndexBuffer* g_accum_spot_ib;

	ID3DVertexBuffer* g_accum_volumetric_vb;
	ID3DIndexBuffer* g_accum_volumetric_ib;

	// Bloom
	ref_geom g_bloom_build;
	ref_geom g_bloom_filter;
	ref_shader s_bloom_dbg_1;
	ref_shader s_bloom_dbg_2;
	ref_shader s_bloom;
	ref_shader s_bloom_msaa;
	float f_bloom_factor;

	// HDR10
	ref_shader s_hdr10_bloom_downsample;
	ref_shader s_hdr10_bloom_blur;
	ref_shader s_hdr10_bloom_upsample;
	
	ref_shader s_hdr10_lens_flare_downsample;
	ref_shader s_hdr10_lens_flare_fgen;
	ref_shader s_hdr10_lens_flare_blur;
	ref_shader s_hdr10_lens_flare_upsample;

	// Luminance
	ref_shader s_luminance;
	float f_luminance_adapt;

	// Combine
	ref_geom g_KD;
	ref_geom g_combine;
	ref_geom g_combine_VP; // xy=p,zw=tc
	ref_geom g_combine_2UV;
	ref_geom g_combine_cuboid;
	ref_geom g_aa_blur;
	ref_geom g_aa_AA;
	ref_shader s_combine_dbg_0;
	ref_shader s_combine_dbg_1;
	ref_shader s_combine_dbg_Accumulator;
	ref_shader s_combine;
	ref_shader s_combine_msaa[8];
	ref_shader s_combine_volumetric;
public:
	ref_shader s_postprocess;
	ref_shader s_postprocess_msaa;
	ref_geom g_postprocess;
	ref_shader s_menu;
	ref_geom g_menu;
private:
	float im_noise_time;
	u32 im_noise_shift_w;
	u32 im_noise_shift_h;

	float param_blur;
	float param_gray;
	float param_duality_h;
	float param_duality_v;
	float param_noise;
	float param_noise_scale;
	float param_noise_fps;
	u32 param_color_base;
	u32 param_color_gray;
	Fvector param_color_add;

	//	Color mapping
	float param_color_map_influence;
	float param_color_map_interpolate;
	ColorMapManager color_map_manager;

	//	Igor: used for volumetric lights
	bool m_bHasActiveVolumetric;
	bool m_bHasActiveVolumetric_spot;
public:
	// dims at creation, the SVP target is square so svpCamera reads these for its aspect
	const u32 Width;
	const u32 Height;

	CRenderTarget();
	CRenderTarget(LPCSTR name, u32 width, u32 height);
	~CRenderTarget();
	void SetActive(bool force = false);
	void accum_point_geom_create();
	void accum_point_geom_destroy();
	void accum_omnip_geom_create();
	void accum_omnip_geom_destroy();
	void accum_spot_geom_create();
	void accum_spot_geom_destroy();
	//	Igor: used for volumetric lights
	void accum_volumetric_geom_create();
	void accum_volumetric_geom_destroy();

	void u_stencil_optimize(eStencilOptimizeMode eSOM = SO_Light);
	void u_compute_texgen_screen(Fmatrix& dest);
	void u_compute_texgen_jitter(Fmatrix& dest);
	void u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _4, ID3DDepthStencilView* zb);
	void u_setrt(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb);
	void u_setrt(const ref_rt& _1, const ref_rt& _2, ID3DDepthStencilView* zb);
	void u_setrt(u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3,
	             ID3DDepthStencilView* zb);
	void u_calc_tc_noise(Fvector2& p0, Fvector2& p1);
	void u_calc_tc_duality_ss(Fvector2& r0, Fvector2& r1, Fvector2& l0, Fvector2& l1);
	BOOL u_need_PP();
	bool u_need_CM();
	void phase_sunshafts();
	void phase_blur();
	void phase_pp_bloom();
	void phase_dof();
	void phase_gasmask_drops();
	void phase_gasmask_dudv();
	void phase_nightvision();
	bool svp_nvg_pass();
	void phase_fakescope(); //crookr
	void phase_heatvision(); //--DSR-- HeatVision
	void phase_3DSSReticle(bool object_lens_pass); // PiP object lens early, reticle/depth overlays late
	// 3DSS reticle glow, drawn into its own targets and added after bloom and tone mapping
	// (r4_rendertarget_phase_3dss_reticle.cpp)
	ref_rt rt_reticle_emission, rt_reticle_glow_source; // the lit cores, and the same weighted by glow strength
	ref_rt rt_reticle_image_emission, rt_reticle_image_glow; // both as the lenses copy the frame
	ref_rt rt_reticle_glow_full, rt_reticle_glow_half[2], rt_reticle_glow_quarter[2]; // the glow lobes
	ref_shader s_reticle_glow, s_reticle_glow_final; // their passes, and the tight lobe with the composite
	u32 reticle_glow_frame = u32(-1);
	u32 reticle_glow_composed_frame = u32(-1); // composited before night vision this frame
	bool reticle_glow_dirty = false;
	void reticle_glow_create();
	void reticle_glow_begin();
	void render_3DSS_reticles();
	void reticle_glow_compose();
	void reticle_glow_compose_before_nightvision();
	void reticle_glow_composite(const ref_rt& target, bool night_vision);
	static void reticle_glow_note(dxRender_Visual* visual);
	void phase_svp_capture(); // pip copy the SVP combined color into rt_secondVP for the lens to sample
    void phase_scope_reference_filters(const ref_rt& source, float threshold, float range, float contrast);
    bool scope_reference_submit(const SvpReferencePacket& packet);
    bool prepare_scope_physical_glass();
    void bind_scope_physical_glass(bool enabled);
    void scope_reference_refresh();
    bool scope_reference_prepare_capture();
    bool scope_reference_current() const;
    bool scope_reference_material_current() const;
    bool scope_reference_capture_begin();
    bool phase_scope_reference_sky();
    bool scope_reference_sky_shaders();
    void scope_reference_sky_draw(bool capture);
    void scope_reference_sky_compare();
    bool scope_reference_sky_qualified();
    void scope_reference_capture_end();
    void scope_reference_capture_particles();
    void scope_reference_capture_fog();
    void scope_reference_capture_volumetric(u32 offset);
    void scope_reference_capture_distortion();
    bool phase_scope_reference_ocular();
    bool phase_scope_reference_composite(const ref_rt& destination,ID3DDepthStencilView* depth,
        const Fvector4 (&constants)[SvpReferenceOpticsFrame::ConstantCount]);
	bool svp_nearblur_pass(); // pip near-field defocus dispatch, true when it ran else the caller copies
	void draw_scope(ref_shader se, std::function<void()> bind, bool objective = false); // pip render the ocular or physical objective lens
	void svp_objective_hud_prepare(bool svp_follows); // pip validate one native objective weapon draw
	void svp_objective_hud_report(); // pip report the native objective draw
	void EvalSVP_DLSS(const SvpDlssInputs& in); // pip DLSS-SR eval, bilinear-passthrough stub for now (Task 7)
	u32 draw_reflex(bool svp = false, bool reticle_light = true); // pip render captured reflex materials with their own shaders (reticle_light false: without the atlas reticle light)
	// pip atlas reticle glow of the PiP and Reference lenses and of a reflex in the capture (svp_reticle_emission.cpp)
	ref_shader s_reticle_emission_pip, s_reticle_emission_glass;
	ref_shader s_reticle_emission_reference, s_reticle_emission_reference_deferred;
	ref_rt rt_reticle_capture_emission, rt_reticle_capture_glow; // a hybrid reflex's glow in capture space
	ref_rt rt_reticle_capture_light; // a hybrid reflex in capture space, shown over the Reference image
	u32 reticle_capture_frame = u32(-1);
	u32 reticle_capture_light_frame = u32(-1);
	ref_rt rt_scope_reference_capture_tc; // where the Reference image takes the capture
	ref_shader s_scope_reference_capture_light;
	void draw_reticle_emission(const ref_shader& shader, ID3DDepthStencilView* depth);
	void reticle_capture_begin(bool light);
	bool reticle_capture_light_current() const;
	void draw_reference_capture_light();
	void reticle_atlas_image(const ref_rt& image);
	void draw_scope_overlays(ID3DDepthStencilView* zb, bool stamp_lens, bool refresh_scene); // pip 3DSS non-lens late-pass meshes
	void scope_overlay_scene_copy(); // pip scene copy the late-pass overlays blur
	bool draw_hybrid_reflex(); // pip render an engaged magnifier reflex through the objective camera
	void phase_lut();
	void phase_smaa();
	void phase_scene_prepare();
	void phase_scene_begin();
	void phase_scene_end();
	void phase_occq();
	void phase_ssao();
	void phase_hdao();
	void phase_downsamp();
	void phase_wallmarks();
	void phase_smap_direct(light* L, u32 sub_phase);
	void phase_smap_direct_tsh(light* L, u32 sub_phase);
	void phase_smap_spot_clear();
	void phase_smap_spot(light* L);
	void phase_smap_spot_tsh(light* L);
	void phase_accumulator();
	void phase_vol_accumulator();
	void shadow_direct(light* L, u32 dls_phase);

	// SSS Stuff
	void phase_ssfx_taa();
	void phase_ssfx_motion_blur();
	void phase_ssfx_fog_scattering();
	void phase_ssfx_rain(); // Bloom PP
	void phase_ssfx_bloom(); // Bloom PP
	void phase_ssfx_sss(); // SSS
	void phase_ssfx_sss_ext(light_Package& LP); // SSS Spot lights

	void phase_ssfx_ssr(); // SSR Phase
	void phase_ssfx_volumetric_blur(); // Volumetric Blur
	void phase_ssfx_water_blur(); // Water Blur
	void phase_ssfx_water_waves(); // Water Waves
	void phase_ssfx_ao(); // AO
	void phase_ssfx_il(); // IL
	void set_viewport_size(ID3DDeviceContext* dev, float w, float h);

	// HDR10
	void phase_hdr10_bloom();
	void phase_hdr10_lens_flare();

	//	Generates min/max sm
	void create_minmax_SM();

	void phase_rain();
	void draw_rain(light& RainSetup);

	void mark_msaa_edges();
	void stamp_svp_corner_mask(); // pip zero the geometry stencil in the dead disc corners so the svp lighting skips them

	bool need_to_render_sunshafts();
	bool use_minmax_sm_this_frame();

	BOOL enable_scissor(light* L); // true if intersects near plane
	// conservative screen rect for the light volume, false leaves the draw unscissored
	BOOL compute_light_scissor(light* L, BOOL near_intersect, Irect& R);

	void disable_aniso();

	void draw_volume(light* L);
	void accum_direct(u32 sub_phase);
	void accum_direct_cascade(u32 sub_phase, Fmatrix& xform, Fmatrix& xform_prev, float fBias);
	void accum_direct_f(u32 sub_phase);
	void accum_direct_lum();
	void accum_direct_blend();
	void accum_direct_volumetric(u32 sub_phase, const u32 Offset, const Fmatrix& mShadow);
	void accum_point(light* L);
	void accum_spot(light* L);
	void accum_reflected(light* L);
	//	Igor: for volumetric lights
	void accum_volumetric(light* L);
	void accum_volumetric_lv(light* L);
	void phase_bloom();
	void phase_luminance();
	void phase_combine();
	void phase_combine_volumetric();
	void phase_pp();

	virtual void set_blur(float f) { param_blur = f; }
	virtual void set_gray(float f) { param_gray = f; }
	virtual void set_duality_h(float f) { param_duality_h = _abs(f); }
	virtual void set_duality_v(float f) { param_duality_v = _abs(f); }
	virtual void set_noise(float f) { param_noise = f; }
	virtual void set_noise_scale(float f) { param_noise_scale = f; }
	virtual void set_noise_fps(float f) { param_noise_fps = _abs(f) + EPS_S; }
	virtual void set_color_base(u32 f) { param_color_base = f; }
	virtual void set_color_gray(u32 f) { param_color_gray = f; }
	virtual void set_color_add(const Fvector& f) { param_color_add = f; }

	virtual u32 get_width() { return dwWidth; }
	virtual u32 get_height() { return dwHeight; }

	virtual void set_cm_imfluence(float f) { param_color_map_influence = f; }
	virtual void set_cm_interpolate(float f) { param_color_map_interpolate = f; }

	virtual void set_cm_textures(const shared_str& tex0, const shared_str& tex1)
	{
		color_map_manager.SetTextures(tex0, tex1);
	}

	//	Need to reset stencil only when marker overflows.
	//	Don't clear when render for the first time
	void reset_light_marker(bool bResetStencil = false);
	void increment_light_marker();

	void DoAsyncScreenshot();

#ifdef DEBUG
	IC void						dbg_addline				(Fvector& P0, Fvector& P1, u32 c)					{
		dbg_lines.push_back		(dbg_line_t());
		dbg_lines.back().P0		= P0;
		dbg_lines.back().P1		= P1;
		dbg_lines.back().color	= c;
	}
	IC void						dbg_addplane			(Fplane& P0,  u32 c)								{
		dbg_planes.push_back(P0);
	}
#else
	IC void dbg_addline(Fvector& P0, Fvector& P1, u32 c)
	{
	}

	IC void dbg_addplane(Fplane& P0, u32 c)
	{
	}
#endif
};

// 3DSS reticle glow: the glow targets at slots 2 and 3 for the reticle draws while it lives, and the reflex shaders
// told to put their reticle light there; a draw with light false is repeated later and leaves that light out
// (r4_rendertarget_phase_3dss_reticle.cpp)
class C3DSSReticleGlow
{
public:
	C3DSSReticleGlow(CRenderTarget* target, bool light = true); // the main view's glow targets, none: no change
	C3DSSReticleGlow(const ref_rt& core, const ref_rt& glow); // glow targets of another image
	~C3DSSReticleGlow();
	C3DSSReticleGlow(const C3DSSReticleGlow&) = delete;
	C3DSSReticleGlow& operator=(const C3DSSReticleGlow&) = delete;
	bool active() const { return bound; }
	// the sights drawn now set the frame's glow size
	template <class Items> void note(const Items& items) const
	{
		if (bound)
			for (const auto& item : items)
				CRenderTarget::reticle_glow_note(item.pVisual);
	}
private:
	void hold(ID3DRenderTargetView* core, ID3DRenderTargetView* glow, bool light);
	bool bound = false;   // the glow targets are at slots 2 and 3
	bool changed = false; // the binder flag or the targets differ from the caller's
	float saved_reflex_glow = 0.f;
	ID3DRenderTargetView* saved[4] = {}; // the caller's targets, as RCache holds them (not owned)
	ID3DDepthStencilView* depth = nullptr;
};
