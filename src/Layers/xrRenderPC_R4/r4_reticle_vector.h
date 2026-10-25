#pragma once

// Vector reticles from SVG: a reticle texture `name` is built in memory (src/Layers/xrRender/reticle_vector) as the
// atlas the GAMMA patch's shaders draw when it has a drawing and its settings:
//   1. the user's own reticle, appdata\reticles\<name>.svg + <name>.reticle.ini (the GAMMA patch's Reticle Editor
//      puts them there), ahead of everything;
//   2. else gamedata\textures\<name>.svg + <name>.reticle.ini: a level or save .dds wins as always, then a .dds in
//      gamedata\textures unless r__reticle_svg is 1 (authoring); an .svg with no .dds is always built.
// Called from CTexture::Load on its loading thread.
//
// In-game editing: r__reticle_edit rebuilds a loaded reticle from its drawing with settings from another file and
// swaps it in live; r__reticle_reload loads one again from its sources (after the user's reticle was saved or
// removed). r__reticle_edit_status reports the last result.

extern int ps_r__reticle_svg; // 1 = <name>.svg before <name>.dds (authoring), 0 = only when there is no .dds

// The built texture (RGBA8, one mip, immutable) and its glow size, or nullptr to load the texture as usual.
ID3DBaseTexture* reticle_vector_texture_load(LPCSTR name, u32& memory, float& glow_size);

// Registers r__reticle_edit, r__reticle_reload and r__reticle_edit_status.
void reticle_vector_console_init();

// Applies the queued edits; the render thread calls it once per frame (reticle_glow_begin).
void reticle_vector_edit_apply();
