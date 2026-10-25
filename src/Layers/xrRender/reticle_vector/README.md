# Vector reticles from SVG

The engine builds a 3DSS reticle straight from its drawing. Put `<texture>.svg` and `<texture>.reticle.ini`
(and a grain mask PNG, if the settings name one) at the path of the reticle texture they replace: in
`gamedata/textures`, or in `appdata/reticles` for a player's own copy. When the game loads that texture it
builds a format 12 reticle atlas, the one the GAMMA patch's `reticle_vector.h` draws. The atlas holds the
drawing's curves, exact at any zoom, in a few KB to a few hundred KB.

`tools/reticle_vector/svg_to_vector` uses the same builder to write a `.dds`.

## The drawing

Filled shapes (paths, rectangles, circles, ellipses, polygons) in named Inkscape layers. The layers nest in any
order; any other group or layer only holds shapes.

| Layer | Its shapes |
| --- | --- |
| `illum` | etched lines that light up (black while the illumination is off) |
| `paint` | a surface lit by the scene, in its fill colour (black for etched lines) |
| `glow` | paint that also lights up |
| `light` | light only, unseen while the illumination is off (a fibre dot) |
| `mark N`, `mark 1, 3` | show only with those Mark Switch marks (1 to 15); everything else shows with every mark |
| `sfp` | always keep their size when the sight zooms; for dual focal plane reticles. Everything else follows the sight's 3DSS focal plane with `legacy_span`, and grows with the zoom without it |
| `dial` | keep their size and turn about the centre with the zoom (`reticle_dial_angle`), like the 3DSS 1P59's magnification dial |
| `center` | one mark whose middle is the bore centre, not drawn; without it the centre is the middle of the viewBox |

Every drawn shape needs one of the four role layers. Fills are solid and opaque with the nonzero rule; strokes
(convert with Path > Stroke to Path), even-odd fills, gradients, masks, clip paths, filters and stylesheets are
errors. Holes are fine. Contours that overlap inside one path (an Inkscape Combine) are kept and their overlap
draws as covered; a contour lying wholly inside another of the same path draws nothing and the build warns about
it. Path > Union gives the exact outline.

## The settings (`<texture>.reticle.ini`)

Only these keys; anything else is an error.

| Key | Default | Meaning |
| --- | --- | --- |
| `[reticle] scale` | required | SVG units per reticle unit |
| `[reticle] units` | `moa` | `moa` or `mil` |
| `[reticle] legacy_span` | none | reticle units across the bitmap the atlas replaces: the reticle then shows at that bitmap's size and follows the 3DSS coordinates (the shipped GAMMA reticles); without it, true angles |
| `[reticle] calibrated_magnification` | 1 | the magnification at which `sfp` parts have their true angular size; used only without `legacy_span` (with it, `sfp` parts match the rest at the lowest zoom) |
| `[illumination] colour` | `sight` | `sight`: the sight's Mark Switch colour lights the reticle (black is off); `drawing`: each lit shape's own fill |
| `[illumination] luminance` | 8 | the lit core's scene luminance at full brightness (battery light) |
| `[illumination] brightness_steps` | none | up to 8 levels 0..1, one per Mark Switch mark; without them the Mark Switch colour's magnitude (or 1 with `colour = drawing`) |
| `[illumination] fibre`, `tritium` | none | always-on daylight and tritium light, like the 3DSS ACOG. Core luminance = `fibre` × sky luminance + `tritium`. Ranges: `fibre` 0..7.9, `tritium` 0..255. Hue follows `colour`; `sight` gives white on a sight without Mark Switch colours. Not combined with `luminance` or `brightness_steps` |
| `[illumination] glow_strength`, `glow_size` | 1, 1 | the glow's energy against the core and the scale on its lobes |
| `[holo]` | | a holographic sight: `softness` (1..4), `level` (0.05..2.5), `ghost_level` (0..0.5), `ghost_offset` (x, y units), `fringe` (units), `flicker` (0..1) |
| `[mask]` | | a hologram's grain: `image` (grey PNG, power of two from 2 to 1024 each way, beside the settings), `depth` (0..1), `size` (units per repeat), `eye_shift` (units per mm of eye movement), `animate` (changes per second, 0..15.9: the grain cross-fades to a new random place in the image; 0 = still) |

The mask multiplies the light by the image over its mean, so the average brightness is kept; keep the mean low
(dark image, bright grains). Grain a screen pixel cannot resolve fades out (the shader takes a grain to be about
4 mask pixels). `r__reticle_msdf_speckle` scales the depth.

## Files

| File | Role |
| --- | --- |
| `reticle_vector.h` | API: SVG text + settings text + a file reader in, RGBA8 texels + glow size + diagnostics out. Engine-free, reentrant. |
| `reticle_vector_svg.*` | The drawing: layers, styles, transforms, path data, arcs, shapes. pugixml parses the XML. |
| `reticle_vector_build.cpp` | Settings, shapes in reticle units, contour orientation, per-cell pieces, the shape grid and the layout; the mask PNG through stb_image (PNG only). |
| `../../xrRenderPC_R4/r4_reticle_vector.*` | Glue: file lookup, the D3D11 texture, `r__reticle_svg`. Hook: `CTexture::Load` (dx10SH_Texture.cpp), normal-texture branch. |
| `tools/reticle_vector/svg_to_vector.cpp` | The command-line builder (`build-linux.sh` builds it with g++); see below |

`svg_to_vector <drawing.svg> <out.dds>` builds one atlas. `svg_to_vector --all <reticles dir> <textures dir>`
builds every `<texture>.svg` under a folder laid out like `gamedata/textures`, such as the GAMMA patch's.
`--check` draws every Mark Switch pattern from the built texture and compares it with the exact shape
coverage of each pixel, over the whole reticle and at 20x on the centre. It fails above an error of 0.01
plus the coordinate step in pixels.

## Which file wins

1. The player's own reticle, `appdata/reticles/<texture>.svg` + `.reticle.ini` (the GAMMA patch's Reticle Editor
   writes them), ahead of everything. The folder is listed at the first texture load and again on each
   `r__reticle_edit` / `r__reticle_reload`; files added by hand while the game runs count after a reload.
   A grain image is looked for beside it first, then beside the shipped drawing.
2. A `.dds` in `$level$` or `$game_saves$`: as for every texture.
3. `r__reticle_svg 0` (default): a `.dds` in `gamedata/textures`, then the `.svg`. MO2 merges mods into one
   folder, so the engine cannot tell which mod a file came from; shipped `.dds` files keep priority.
4. `r__reticle_svg 1` (authoring): the `.svg` before the `.dds`.

An `.svg` without `.reticle.ini`, or one that does not build, logs `! [RETICLE-SVG]` with the reasons and the
texture loads as usual. A built one logs `* [RETICLE-SVG] <texture>: N shapes in M pieces, W x H RGBA8`.

Format 12 is the only reticle atlas drawn. An older one at a reticle path (the MSDF formats 2-8, vector formats 9-11)
draws nothing and logs `! [RETICLE] <texture>.dds is an older reticle atlas format` once; rebuild it from its SVG.

## Limits

- At most 4095 pieces (a shape crossing several grid cells is one piece per cell), 65535 curves and 65535 grid
  list entries; a shape in several marks counts once per mark.
- The settings can be edited live (`r__reticle_edit <texture> <settings file>`); `r__reticle_reload <texture>`
  loads a reticle again from its sources (after the player's own was written or deleted); `r__reticle_edit_status`
  reports the last result. The GAMMA patch's Reticle Editor drives them.

## Engine side of the reticle glow

The glow is a 3DSS feature; the GAMMA patch's `docs/RETICLE_ATLASES.md` explains how it looks and works.

| Shaders (core gamedata) | Role |
| --- | --- |
| `reticle_glow.h`, `reticle_glow_{down,blur}.ps`, `reticle_emission_combine.ps` | The glow passes and the composite. |
| `*_emission.{s,ps}`, `reticle_emission_out.h` | PiP: emission-only variants of the PiP (also over deferred glass) and Reference lens shaders; they evaluate only the reticle. |
| `scope_reference_capture_light.{s,ps}` | PiP: a hybrid magnifier's red dot and its glow over the Reference image, at the capture coordinate `scope_reference_image.ps` writes. |

| File | Hook |
| --- | --- |
| `xrRenderPC_R4/r4_rendertarget_phase_3dss_reticle.cpp` | Creates the glow targets and passes, draws the stock 3DSS reticles (`render_3DSS_reticles`) and composites the glow (`reticle_glow_begin`, `C3DSSReticleGlow`, `reticle_glow_note`, `reticle_glow_compose`) |
| `xrRenderPC_R4/r4_3dss_reticle.cpp/.h` | the `r__reticle_msdf*` settings and the constants `reticle_atlas_params`, `reticle_atlas_control`, `reticle_holo_eye`, `reticle_atlas_reflex_glow` |
| `xrRender/xrRender_console.cpp`, `xrRender/Blender_Recorder_StandartBinding.cpp` | one call each registering the above |
| `xrRenderPC_R4/r4_rendertarget.cpp` | `reticle_glow_create()` in the main target's constructor |
| `xrRenderPC_R4/r4_rendertarget_phase_combine.cpp` | `reticle_glow_begin()` before `render_forward()`; `reticle_glow_compose()` after combine_2, the HDR10 bloom and lens flare |
| `xrRender/r__dsgraph_build.cpp`, `r__dsgraph_types.h`, `r__dsgraph_render.cpp`, `r__dsgraph_manager.h` | `scopelense(10)` HUD meshes go to `mapScopeReflexHUD` while 3DSS is on (`r__3Dfakescope`); `r_dsgraph_render_ReflexSorted` draws them |
| `xrRenderPC_R4/svp_optics.cpp` (PiP) | `phase_3DSSReticle` runs `render_3DSS_reticles` when PiP takes no part; `draw_reflex` and the hybrid magnifier hold the glow targets with `C3DSSReticleGlow`; `draw_scope` publishes `svp_reticle_projection` and keeps a lens texture loaded; one emission draw after each PiP or Reference lens reticle; the emission shaders build with the lens glue in `EnsureScopeShaders` |
| `xrRenderPC_R4/r4_R_render.cpp` (PiP) | `CRender::Render` builds the lens glue (`EnsureScopeShaders`) on the level's first frame instead of the first aim |
| `xrRenderPC_R4/r4_shader_warmup.cpp/.h` | the shader warm-up: while the level loads, worker threads compile the pixel shaders of the 3DSS sight materials and, with PiP, of the lens glue and the Reference passes from sources read ahead on the loader thread; nothing else is touched off the render thread |
| `xrRenderPC_R4/r4_loader.cpp`, `xrRenderPC_R4/r4.cpp` | `shader_warmup_start` at level load, `shader_warmup_stop` at unload; `shader_compile` builds its defines and target with `shader_compile_options` / `shader_compile_target` (shared with the warm-up) and takes the warm-up's bytecode on a cache miss |
| `xrRenderPC_R4/svp_reticle_emission.cpp` (PiP) | `draw_reticle_emission`, `reticle_capture_begin` (capture-space targets of a hybrid reflex), `draw_reference_capture_light` (a hybrid reflex over the Reference image), `reticle_atlas_image` (PiP reticle paint sees the eyepiece capture) |
| `xrRenderDX10/dx10SH_Texture.cpp`, `xrRenderPC_R4/r4_reticle_vector.cpp/.h`, `xrRender/reticle_vector/` | `CTexture::Load` builds a texture with `<name>.svg` + `<name>.reticle.ini` into a vector reticle (`r__reticle_svg`), and `reticle_glow_begin` applies the editor's live rebuilds; `reticle_atlas.h` / `r4_reticle_atlas.cpp` read the glow size and log older atlases |
