# X-Ray Monolith Bodycam MT

This fork combines the Bodycam camera and viewmodel systems with objective-camera
picture-in-picture scopes for S.T.A.L.K.E.R. Anomaly / GAMMA.

## **WHAT'S NEW**

1. Merged the latest MT and objective-camera PiP development builds.
2. Physical PiP scope simulation:
   - Dynamic exit-pupil vignetting derived from live eye alignment.
   - Separate models for tunneling, image transmission, field of view, eye relief, exit pupil, and objective diameter.
   - Magnification-aware optical behavior instead of a uniformly scaled shader effect.
   - Typed per-optic profiles with measured specifications for supported LPVOs, magnifiers, prism sights, Eastern optics, night-vision optics, and integrated weapon sights.
   - Corrected exit-pupil recentering at high magnification and stable PiP FOV ownership during ADS transitions and weapon changes.
3. Expanded Bodycam simulation:
   - Consistent mouse response across sensitivity settings.
   - Coordinated hip-fire, ADS, PiP camera, and viewmodel behavior.
   - Selectable Bodycam Style and STALKER 2 Style arm IK layers.
   - Correct left-hand device attachment while procedural arm IK is active.
4. Per-weapon viewmodel profiles with smooth, ADS-safe transitions for hip-fire, walking, and sprint states.
5. Reworked ImGui menus for Bodycam and Picture in Picture, including readable feature pages, presets, automatically saved custom settings, tooltips, and right-click reset to default.
6. Lua-facing Bodycam controls and an additive actor-camera delta API for recoil mods.

## Bodycam MT Version

This version keeps the upstream MT test build as the base and adds an engine-level Bodycam module with:

* camera decoupling with separate hip-fire and ADS response;
* viewmodel spring, stable firing direction, and ADS sight anchoring;
* movement inertia for acceleration, deceleration, turning, and stopping;
* dynamic viewmodel lowering and per-weapon viewmodel positioning outside ADS;
* procedural sprint acceleration and transition before the authored sprint animation takes over;
* sprint start/stop camera, FOV, braking footsteps, and impulse feedback;
* independently configurable firing, ADS transition, landing, and mouse-flick impulses;
* selectable Bodycam Style and STALKER 2 Style procedural arm IK;
* left-hand device attachment that remains coherent with procedural arm movement;
* objective-camera PiP coordination without leaking scope FOV across weapons or transitions;
* additive actor-camera deltas for recoil scripts without forcing actor-direction rebakes;
* Peak Volumetrics shader support integrated into the DX11 renderer path;
* console variables, Modded Exes settings, ImGui controls, presets, and Lua access to Bodycam state and viewmodel profiles.

## Upstream MT Version

The Modded Exes come with standard and MT versions. This fork is based on the MT test branch.

> [!IMPORTANT]
> This project is under active development. The engine, bundled `gamedata`, and external
> compatibility patches must come from the same revision. Mixing versions can make scopes
> fall back to the legacy shader path or produce incorrect optic behavior.

## What true PiP means

Traditional shader scopes magnify or distort the main camera image. This project renders a
second view of the world through the physical objective of the optic.

The current implementation provides:

- one entrance-pupil camera for the scoped world and eligible weapon geometry;
- a synchronized weapon pose across the main view and scope view;
- continuous barrel and handguard geometry through the objective;
- authored or measured objective diameter, eye relief, exit pupil, and magnification;
- physical field-stop, pupil, twilight, tunneling, and near-field behavior;
- off-axis deferred reconstruction from the active scope projection;
- separate scope exposure, light capture, bloom, emissive, grass, and LOD controls;
- a hard TAA exclusion stamp for the lens image and reticle;
- objective-only NVG processing without copying the wearer's mask into the scope;
- hybrid magnifier support with reflex-element capture;
- typed Lua-to-engine optic profiles with legacy LTX and script fallbacks;
- diagnostic overlays and structured runtime logging.

`r__svpscope 0` keeps the original non-PiP path. Any positive value selects objective true
PiP; the retired classic mode is not a separate rendering path.

## Compatibility

The engine is designed to preserve the existing Anomaly and Modded Exes ecosystem. Legacy
weapon sections, upgrades, DLTX patches, 3DSS parameters, Mark Switch data, and script-driven
zoom controllers remain supported.

Tested integration targets include:

- 3D Shader Scopes for GAMMA;
- ARC 3DSS content;
- Modular Attachment System;
- Modular Attachment System for GAMMA;
- Pizza's Ultimate Sight Selection;
- hybrid reflex and magnifier combinations.

MAS, MASG, and Pizza are optional. A regular 3DSS setup does not require them.

Third-party mods and their assets are not included in this repository. Shader compatibility
files derived from 3DSS are distributed separately and should be installed as their own MO2
patch above the scope mods they extend.

## Installing a matched build

There is no public release archive yet. For development builds:

1. Install the required 3DSS and weapon-mod dependencies through MO2.
2. Build either `DX11-AVX | x64` or `DX11 | x64`.
3. Copy the matching executable and PDB from `_build/_game/bin_dbg` into the Anomaly `bin`
   directory.
4. Install this repository's `gamedata` as a separate MO2 mod, or package it while preserving
   its directory layout.
5. Install the matching 3DSS PiP compatibility patch at higher MO2 priority than 3DSS.
6. Clear `appdata/shaders_cache` before the first launch after shader changes.

Do not install only the executable. The scripts, configs, and shaders in `gamedata` are part
of the runtime contract.

## Building

Requirements:

- Windows 10 or newer;
- Visual Studio 2022;
- Desktop development with C++;
- current MSVC, Windows SDK, MFC, and ATL components;
- Git submodules initialized recursively.

```powershell
git clone --recursive https://github.com/TheLostInPlace/xray-monolith-pip.git
```

Open `src/engine-vs2022.sln`, select an x64 DX11 configuration, and build the solution. The
current PiP implementation targets DX11.

## Configuration

The normal Modded Exes options page exposes:

- Picture in Picture;
- smooth variable zoom;
- analog zoom input.

The in-game ImGui menu contains the supported optical, rendering, and performance controls.
Experimental invariants are intentionally fixed internally rather than exposed as player
settings.

Important console commands:

| Command | Purpose |
| --- | --- |
| `r__svpscope 0` | Disable PiP and retain the original rendering path |
| `r__svpscope 2` | Enable objective true PiP |
| `r__svp_diag 1` | Enable throttled pass and activation logging |
| `r__svp_cop_diag 1` | Log optic camera and HUD-pose state |
| `r__svp_cop_diag 2` | Add detailed per-frame alignment data |
| `r__svp_report 1` | Emit a one-shot configuration and file report |
| `r__scope_debug 1` to `4` | Show camera, buffer, mask, and geometry diagnostics |
| `svp_dump_optic` | Dump the resolved typed optic profile and provenance |

## Modder integration

Existing weapon and scope definitions remain valid. New physical data can be authored without
replacing the legacy fields used by older engines.

### Choose the smallest integration surface

| Need | Recommended route |
| --- | --- |
| Give a normal weapon-pack optic physical data | Add a DLTX profile and runtime alias |
| Tune eye tracking or zeroing without claiming physical measurements | Add a controller-tuning DLTX section |
| Mark an engaged or flipped hybrid magnifier | Add `svp_hybrid_reflex` to the existing runtime section |
| Inspect the profile selected by the engine | Use `svp_dump_optic` or the read-only Lua inspector |
| Publish an optic assembled dynamically at runtime | Use the typed Lua API |

Most mods should use the data route. The bundled
[`zzz_extra_scope_features.script`](gamedata/scripts/zzz_extra_scope_features.script) resolves
the active weapon and scope, publishes one complete profile, and falls back to the legacy console
route on an older executable. A second publisher should not compete with it unless the mod
deliberately owns the entire optic lifecycle.

### Add a physical profile with DLTX

Do not replace `pip_optic_physical_specs.ltx`. Add a namespaced file matching the root:

```text
gamedata/configs/mod_pip_optic_physical_specs_<author-or-mod>.ltx
```

To reuse an existing canonical record, add only an alias whose section name exactly matches the
runtime scope or weapon section:

```ini
; mod_pip_optic_physical_specs_my_pack.ltx
[my_pack_vudu]
spec = optic_eotech_vudu_1_6x24_ffp
```

For an optic not yet represented, add a uniquely named canonical record and its alias in the same
file. This is the shape of the sourced Vudu record that already ships with the project:

```ini
[optic_eotech_vudu_1_6x24_ffp]
data_grade = manufacturer_family
optic_class = lpvo
runtime_model = geometric
magnification_min = 1
magnification_max = 6
objective_mm = 24
eye_relief_low_min_mm = 83
eye_relief_low_max_mm = 100
eye_relief_high_min_mm = 82
eye_relief_high_max_mm = 100
fov_low_ft_100yd = 102.4
fov_high_ft_100yd = 16.7
```

Do not redefine that section. For another optic, give the canonical section a real manufacturer
and model identity, replace every value with data from its manufacturer or manual, and omit unknown
values instead of copying a similar optic.

The physical reader understands:

| Key | Meaning |
| --- | --- |
| `runtime_model` | `geometric` for a direct-view optic or `display` for a sensor display |
| `magnification_min`, `magnification_max` | Physical endpoints used by pupil calculations and validation |
| `objective_mm` | Clear objective diameter in millimetres |
| `eye_relief_mm` | One eye-relief value for both endpoints |
| `eye_relief_low_mm`, `eye_relief_high_mm` | Endpoint-specific eye relief |
| `eye_relief_*_min_mm`, `eye_relief_*_max_mm` | Manufacturer-published endpoint ranges, averaged by the current runtime |
| `exit_pupil_low_mm`, `exit_pupil_high_mm` | Explicit endpoint exit pupils; otherwise derived from objective and magnification |
| `pupil_parity`, `pupil_field_low`, `pupil_field_high` | Advanced pupil mapping; omit unless measured or calibrated |
| `transmission` | Measured light transmission from 0 to 1; omission uses neutral transmission |

`data_grade`, `optic_class`, and FOV fields retain provenance and review metadata. FOV fields do
not set gameplay zoom. Physical `geometric` or `display` values take precedence over matching
controller-tuning values.

Keep mesh geometry and gameplay magnification on the existing runtime item section. Add fields
with the weapon pack's normal `mod_system_*.ltx` DLTX file so older engines retain all legacy
zoom and 3DSS data:

```ini
![my_pack_scope]
scope_objective_lens_offset = 0, 0, 13.3, 0.73
svp_magnifications = 1-6
```

`scope_objective_lens_offset` is `x, y, z, radius` in eyepiece-radius units. It is mesh-specific;
do not copy another scope's tuple. `svp_magnifications` accepts `4` for fixed 4x, `1-6` for smooth
1x to 6x, or `1,6` for a stepped switch. If it is absent or malformed, the engine keeps the
existing `magnifications`, `magnification`, and legacy zoom-factor route.

Profile lookup is deterministic:

1. active weapon section;
2. attached scope section;
3. permanent-scope hint;
4. default controller tuning.

Within each identity, built-in aliases are checked before the optional compatibility alias
contract. `pip_optic_external_aliases.ltx` is a single-owner, schema-checked compatibility file;
ordinary weapon packs should extend `pip_optic_physical_specs.ltx` through DLTX instead of
overwriting that contract.

### Add controller tuning

Physical measurements belong in `pip_optic_physical_specs.ltx`. Game-feel controls belong in a
separate namespaced DLTX file matching `pip_optic_profiles.ltx`:

```ini
; mod_pip_optic_profiles_my_pack.ltx
[my_pack_scope]
s3ds_eye_tracking_speed = 5.5
s3ds_eye_tracking_accel_mm_s2 = 90
s3ds_eye_tracking_limit_mm = 7.5
scope_zero_m = 100
```

Use this layer for controller behavior, not as a substitute for known objective, eye-relief, or
exit-pupil measurements. See
[`pip_optic_profiles.ltx`](gamedata/configs/pip_optic_profiles.ltx) for the supported tuning keys.

For a hybrid optic, explicitly describe both states in its normal runtime sections:

```ini
![my_magnifier_engaged]
svp_hybrid_reflex = true

![my_magnifier_flipped]
svp_hybrid_reflex = false
```

### Typed Lua API

The engine API and the published profile table have separate versions:

- typed API version `2`;
- profile table schema version `1`.

Probe global functions with `rawget`, require an exact API version, and use `pcall`. Never infer
support from a console variable or call fork-only exports unconditionally.

```lua
local API_VERSION = 2

local function connect_true_pip()
    local version_fn = rawget(_G, "svp_optic_api_version")
    local connect_fn = rawget(_G, "svp_optic_api_connect")
    if type(version_fn) ~= "function" or type(connect_fn) ~= "function" then
        return false
    end

    local ok, version = pcall(version_fn)
    if not ok or version ~= API_VERSION then
        return false
    end

    local connected_ok, connected = pcall(connect_fn, API_VERSION)
    return connected_ok and connected == true
end
```

The public functions are:

| Function | Purpose |
| --- | --- |
| `svp_optic_api_version()` | Return the engine API version |
| `svp_optic_api_connect(version)` | Activate the typed route after an exact-version handshake |
| `svp_optic_api_has_capability(name)` | Query optional `hybrid_reflex` or `profile_inspector` support |
| `svp_optic_route_epoch()` | Detect an engine-side route reset that requires republishing |
| `svp_begin_optic_context(context, weapon, weapon_id, scope, zoom_type, identity_source, diagnostic_scope)` | Begin one weapon and optic identity and receive a nonzero token |
| `svp_apply_optic_profile(token, table)` | Atomically validate and publish a complete schema-1 snapshot |
| `svp_clear_optic_profile(token)` | Clear the current snapshot if the token still owns it |
| `svp_current_optic_profile()` | Return the accepted profile and its provenance for inspection |

Read-only inspection is safe for other mods:

```lua
local capability_fn = rawget(_G, "svp_optic_api_has_capability")
local current_fn = rawget(_G, "svp_current_optic_profile")
local has_inspector = false

if type(capability_fn) == "function" then
    local ok, supported = pcall(capability_fn, "profile_inspector")
    has_inspector = ok and supported == true
end

if has_inspector and type(current_fn) == "function" then
    local ok, profile = pcall(current_fn)
    if ok and profile.valid then
        printf("active PiP spec %s from %s", profile.spec, profile.binding_section)
    end
end
```

A direct publisher must begin a context, submit the full schema-1 table, clear its token on zoom
out or ownership change, and republish after the route epoch changes. Unknown table keys, missing
fields, out-of-range values, stale tokens, and changed identity fields are rejected as one unit.
Every field in the canonical builder is required except `hybrid_reflex`, which must be omitted
unless the capability probe succeeds.
Use the `typed_table` builder in
[`zzz_extra_scope_features.script`](gamedata/scripts/zzz_extra_scope_features.script) as the
canonical field list and the validator in
[`svp_optic_config_script.cpp`](src/xrGame/svp_optic_config_script.cpp) as the authoritative
types and ranges.

### Version-safe extension rules

1. Preserve legacy weapon, zoom, 3DSS, and Mark Switch fields; PiP fields are additive.
2. Namespace new filenames and canonical sections with the author or mod identity.
3. Use `[new_section]` for a new section and `![existing_section]` only when patching a section
   already defined in the same LTX root.
4. Never ship a replacement `pip_optic_physical_specs.ltx`,
   `pip_optic_profiles.ltx`, or `zzz_extra_scope_features.script` for a data-only addon.
5. Treat API and table versions as exact contracts. Feature-probe optional capabilities.
6. Increment a compatibility patch's own version whenever aliases, required sections, or runtime
   mappings change, and keep its manifest, metadata, and LTX control version synchronized.
7. Test both `r__svpscope 0` and `2`. A missing API must leave the old route functional.

Enable `print_dltx_warnings 1` while authoring. Then use `r__svp_diag 1` and `svp_dump_optic` to
confirm the active identity, canonical spec, resolved values, and the winning DLTX source. The
runtime also writes `[PIP-OPTIC]`, `[PIP-OPTIC-ZOOM]`, `[PIP-OPTIC-GEOM]`, and `[SVP-CONFIG]`
lines for concrete bug reports.

## Reporting problems

Include:

- the exact weapon and optic section names;
- whether the issue occurs with `r__svpscope 0` or `2`;
- `appdata/logs/pip.log`;
- the current `appdata/logs/xray_*.log`;
- the relevant portion of the MO2 mod list;
- a short video when the problem is motion-dependent.

For activation or zoom problems, test once with `r__svp_diag 1`. For camera or continuity
problems, also enable `r__svp_cop_diag 2`.

## Credits

- [X-Ray Monolith](https://github.com/themrdemonized/xray-monolith) and its contributors;
- the original PiP work in the [gc64 fork](https://github.com/CnRJay/xray-monolith-gc64);
- the authors and maintainers of 3DSS, ARC, MAS, MASG, and the supported weapon packs;
- the OpenXRay, IX-Ray, Call of Chernobyl, and wider Anomaly modding communities.

## License

This repository inherits the upstream X-Ray engine licensing terms. See
[License.txt](License.txt). The original S.T.A.L.K.E.R. engine code is available for
non-commercial use under its applicable terms.
