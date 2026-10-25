# X-Ray Monolith Bodycam

A fork of the [X-Ray Monolith](https://github.com/themrdemonized/xray-monolith) MT engine for
S.T.A.L.K.E.R. Anomaly and G.A.M.M.A. It adds a bodycam camera and weapon-handling layer,
true picture-in-picture scopes, vector reticles and faster grass. DX11 only.

## What's new in v0.3.1

- **Vector reticles.** Reticles are drawn from SVG, so they stay sharp at any zoom. They can have parts
  that scale with zoom, a turning magnification dial, and fibre or tritium lighting. You can put your own
  reticles in `appdata\reticles` and edit them in game with the Reticle Editor (Bodycam GAMMA Patch).
- **Faster grass.** TheLostInPlace's fps-clean grass renderer replaces the old instancing. Hidden grass is
  skipped on the GPU, and scopes reuse the main view's grass work. Needs Bodycam Grass Optimisation 5.0.0.
- **Weapon feel.**
  - Hip fire kicks the muzzle up instead of twisting the gun sideways.
  - Full-auto and burst fire push the gun back.
  - Arm and wrist sway follows the mouse correctly.
  - Look shake no longer depends on framerate.
  - Coming back from third person or a cutscene no longer swings the view.
  - Head bob works during freelook.
  - The Disabled preset turns everything off.
- **Scopes.**
  - Lens reflections stay visible while aiming.
  - Swapping optics while aimed resizes the scope image.
  - Variable-zoom and clip-on magnifiers now place the near plane correctly.
  - 1x thermal and night-vision sights get their scope image after loading a save.
  - PiP turns off under MSAA, which it never supported.
- **Fixes.**
  - Several crashes in the scope renderer are fixed.
  - The scope-effects setting keeps your choice.
  - Saving in the optic workbench no longer upsets MO2.
- **For modders.**
  - GAMMA-specific Reference tuning moved to `[bodycam_reference_tuning]` in
    `mod_system_bodycam_reference.ltx`.
  - The profile key `copied` is now `idle`.
  - The optic inspector returns `has_owner` instead of `context_token`.
  - Retired console commands are removed; old `user.ltx` lines log "Unknown command" once.

Update the core, Bodycam GAMMA Patch 2.0.0 and Bodycam Grass Optimisation 5.0.0 together. Reticle files
made for v0.3 do not load.

## Features

**Bodycam.** Two motion modes, picked with `bodycam_motion_mode`:

- Classic (`0`) works on top of the game's own first-person animations. The camera trails your aim,
  the weapon has inertia and lowers while you walk, sprint starts ease in, footsteps move the
  camera, shots kick the weapon, and the arms solve around the gun (Bodycam or STALKER 2 style IK).
  Each feature has its own switch and works with the others off. See [docs/CLASSIC.md](docs/CLASSIC.md).
- Reference (`1`) drives the character from recorded reference rig data, shipped under
  `gamedata/bodycam_reference/character/`.

Settings live in the in-game ImGui menu and the Modded Exes options page. Six built-in presets ship,
and you can save your own; they go to `bodycam_user.ltx` and never touch the shipped defaults.

**Scopes.** `r__svpscope 2` renders the scope as a second camera at the objective lens instead of
magnifying the main view. Each optic gets its real objective size, eye relief, exit pupil and
magnification, so the tunnel and vignette come from where your eye sits. Hybrid magnifiers render
the red dot through the magnified view. Night vision is applied inside the scope, and the scope
glass reflects the scene around you. `r__svpscope 0` keeps the stock path.

**Reticles.** Scope and red-dot reticles are drawn from SVG drawings, so they stay sharp at any zoom,
with first and second focal plane parts, Mark Switch colours and a glow that adapts to the light.
This is a 3D Shader Scopes (3DSS) feature and also works with PiP off. The reticles and the in-game editor
ship in the Bodycam GAMMA Patch.

**Grass.** TheLostInPlace's grass renderer is built in: it batches grass draws, keeps grass data on
the GPU and skips grass that is hidden. Install Bodycam Grass Optimisation to turn it on.

## Install

Install the core and, on G.A.M.M.A. or with 3D Shader Scopes, the Bodycam GAMMA Patch from the same
release. Bodycam Grass Optimisation is optional. Packages from different releases break scopes.

1. **Core**: copy `bin\` into `Anomaly\bin` and `db\mods\00_modded_exes_gamedata.db0` into
   `Anomaly\db\mods`. Both are needed; the exe alone is not enough. The core runs on plain Anomaly.
2. **Bodycam GAMMA Patch**: install its zip in MO2 and let it win the file conflicts listed in its
   README.
3. **Bodycam Grass Optimisation** (optional): install its zip in MO2 and let it win conflicts against
   Screen Space Shaders 23, Enhanced Shaders (KennShade) and Grass Tweaks.

Clear `appdata\shaders_cache` after installing or updating. [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)
lists what each package contains.

## Build

Windows 10 or newer and Visual Studio 2022 with the C++ desktop workload, MSVC, the Windows SDK,
MFC and ATL.

```powershell
git clone --recursive https://github.com/asuparabekon/xray-monolith-bodycam.git
```

Open `src/engine-vs2022.sln`, pick an x64 DX11 configuration and build.

## Console

| Command | Effect |
| --- | --- |
| `bodycam_motion_mode 0` / `1` | Classic or Reference motion |
| `r__svpscope 0` / `2` | Stock scopes or true PiP |
| `r__svp_diag 1` | Log scope activation and passes |
| `r__svp_cop_diag 1` / `2` | Log optic camera and HUD pose, `2` adds per-frame alignment |
| `r__svp_report 1` | Write a one-off configuration report |
| `r__scope_debug 1`-`4` | Show camera, buffer, mask and geometry overlays |
| `svp_dump_optic` | Print the optic profile the engine resolved and where it came from |

## For modders

- [docs/PIP_OPTICS.md](docs/PIP_OPTICS.md): optic data, hybrid magnifiers and the typed Lua optic API.
- [docs/FIRST_PERSON_LUA_API.md](docs/FIRST_PERSON_LUA_API.md): camera, HUD and weapon transforms for scripts.
- [docs/BODYCAM_HUD_ANIMATION_POLICY.md](docs/BODYCAM_HUD_ANIMATION_POLICY.md): turning off weapon lowering during HUD animations.
- [docs/BODYCAM_PORTING.md](docs/BODYCAM_PORTING.md): moving the bodycam module to another X-Ray build.

Tested with 3D Shader Scopes for GAMMA, ARC 3DSS, the Modular Attachment System (MAS), MAS for GAMMA,
Pizza's Ultimate Sight Selection and hybrid reflex/magnifier combinations. MAS and Pizza are optional.

## Reporting problems

Include:

- the weapon and optic section names;
- whether it happens with `r__svpscope 0`, `2` or both;
- `appdata/logs/pip.log`;
- the current `appdata/logs/xray_*.log`;
- the relevant portion of the MO2 mod list;
- a short video if it depends on movement.

For scope activation or zoom problems, reproduce once with `r__svp_diag 1`; for camera problems,
also with `r__svp_cop_diag 2`.

## Credits

- [X-Ray Monolith](https://github.com/themrdemonized/xray-monolith) and its contributors;
- the original PiP work in the [gc64 fork](https://github.com/CnRJay/xray-monolith-gc64);
- TheLostInPlace (m22) for the PiP scopes and the fps-clean grass pipeline;
- the authors and maintainers of 3DSS, ARC, MAS, MASG, and the supported weapon packs;
- the OpenXRay, IX-Ray, Call of Chernobyl, and wider Anomaly modding communities.

## License

This repository inherits the upstream X-Ray engine licensing terms. See
[License.txt](License.txt). The original S.T.A.L.K.E.R. engine code is available for
non-commercial use under its applicable terms.
