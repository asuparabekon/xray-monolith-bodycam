#pragma once

// 3DSS reticle atlas settings and shader constants, shared by the main viewport and
// PiP capture. The console names keep their r__reticle_msdf_ prefix: players' saved settings and the MCM use them.
// Glow passes live in r4_rendertarget_phase_3dss_reticle.cpp.

extern int ps_r__reticle_bloom_exclusion; // the glow has its own targets, added after bloom and tone mapping
extern float ps_r__reticle_brightness; // scale on each atlas's authored illumination luminance
extern Fvector ps_r__reticle_glow_size; // glow lobe sigmas (tight, mid, wide), px at 1080p, times the atlas's glow size
extern Fvector ps_r__reticle_glow_weight; // glow lobe weights against the core
extern float ps_r__reticle_glow_exposure; // power of the glow's exposure on the lobes
extern float ps_r__reticle_glow_lowlum; // the glow's exposure floor, in place of the game's r2_tonemap_lowlum
extern float ps_r__reticle_glow_shoulder; // how fast light above display white turns the core white
extern float ps_r__reticle_glow_halo; // the display level the lobes approach, below white
extern float ps_r__reticle_glow_tint; // how much the glow covers the image behind it
extern float ps_r__reticle_holo_level; // scale on each holographic atlas's authored level
extern float ps_r__reticle_grain; // scale on each holographic atlas's grain mask depth
extern float ps_r__reticle_line_weight; // thin line weight, 0 = true darkness, 1 = bold
extern int ps_r__reticle_debug; // atlases that fail validation show a magenta tint

// reticle_atlas_reflex_glow: x 1 while the glow targets are bound at slots 2 and 3, -1 while a reflex draw leaves
// its reticle light out; y 1 while $user$reticle_image_emission and _glow hold the frame's reflex glow for the lenses
extern float g_reticle_reflex_glow;
extern float g_reticle_image_glow;

bool reticle_glow_wanted(); // the glow goes into its own targets this frame

void reticle_3dss_console_init();
class CBlender_Compile;
void RegisterReticle3DSSConstants(CBlender_Compile& dst);
