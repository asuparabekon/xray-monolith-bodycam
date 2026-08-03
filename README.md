# X-Ray Monolith Bodycam MT

This fork combines the Bodycam camera and viewmodel systems with objective-camera
picture-in-picture scopes for S.T.A.L.K.E.R. Anomaly / GAMMA.

## **WHAT'S NEW**

1. Merged the latest X-Ray Monolith MT and objective-camera PiP development builds.
2. Weapon-relative true PiP alignment:
   - The scope reticle and tunneling follow the live weapon and sight axes while the gun moves inside the Bodycam deadzone.
   - Projectile zero, reticle alignment, and visible weapon geometry remain coherent from 1x through high magnification.
   - Current-frame weapon and sight snapshots prevent delayed reticle movement, stale alignment, and cross-weapon state leakage.
3. Physical scope behavior:
   - Dynamic exit-pupil vignetting is derived from eye alignment rather than a fixed overlay.
   - Tunneling, image transmission, field of view, eye relief, exit pupil, objective diameter, and controller response are modeled separately.
   - Typed per-optic profiles contain measured specifications for supported LPVOs, magnifiers, prism sights, Eastern optics, night-vision optics, and integrated weapon sights.
   - Authored scope interiors remain supported while generated tunneling covers optics without a suitable interior mask.
   - Optical sights use physical eye coupling; digital and thermal displays remain rigid.
4. Updated PiP renderer integration:
   - Linear magnification stepping, hybrid-optic capture, objective-aware near-plane handling, local exposure, and scope-specific post-processing.
   - Stable PiP ownership during ADS transitions, reloads, weapon changes, and high-magnification recentering.
   - Per-weapon swing envelopes drive scope-shadow response without leaking state between weapons or viewport sessions.
5. Expanded Bodycam simulation:
   - Consistent mouse response across sensitivity settings.
   - Coordinated hip-fire, ADS, PiP camera, viewmodel, and ballistic direction.
   - Selectable Bodycam Style and STALKER 2 Style arm IK layers.
   - Correct left-hand device attachment while procedural arm IK is active.
   - Per-weapon viewmodel profiles with smooth, ADS-safe positioning for hip-fire, walking, and sprint states.
6. Reworked Bodycam and PiP ImGui menus:
   - Feature-oriented pages, descriptive tooltips, and right-click reset to default.
   - Default, Balanced, Disable, No ADS, Red Orchestra, and Red Orchestra (ADS only) Bodycam presets.
   - Named user presets can be created, updated, renamed, and deleted in-game.
   - User presets are isolated in `bodycam_user.ltx` and never modify shipped defaults.
7. Stable integration surfaces for other mods:
   - Typed PiP optic API version 3 with exact schema negotiation, descriptors, structured validation, and profile provenance.
   - Lua-facing Bodycam state and viewmodel controls.
   - Additive actor-camera deltas for recoil mods without actor-direction rebakes.

## Bodycam MT Version

This version keeps the upstream MT branch as its base and adds a modular engine-level Bodycam
layer that coordinates the camera, viewmodel, procedural arms, weapon trajectory, and PiP scope
without making those systems share hidden state. It provides:

* camera decoupling with separate hip-fire and ADS response;
* viewmodel spring with stable weapon direction and ADS sight alignment;
* movement inertia for acceleration, deceleration, turning, and stopping;
* dynamic viewmodel lowering and per-weapon viewmodel positioning outside ADS;
* procedural sprint acceleration and transition before the authored sprint animation takes over;
* sprint start/stop camera, FOV, braking footsteps, and impulse feedback;
* independently configurable firing, ADS transition, landing, and mouse-flick impulses;
* selectable Bodycam Style and STALKER 2 Style procedural arm IK;
* left-hand device attachment that remains coherent with procedural arm movement;
* objective-camera PiP ownership that does not leak scope FOV or optic state across weapons and transitions;
* weapon-relative PiP reticle, tunneling, and projectile alignment across deadzone movement and magnification changes;
* current-frame weapon and sight snapshots for stable scope projection;
* per-weapon scope-shadow swing state that resets across weapon and viewport changes;
* additive actor-camera deltas for recoil scripts without forcing actor-direction rebakes;
* Peak Volumetrics shader support integrated into the DX11 renderer path;
* console variables, Modded Exes settings, feature-oriented ImGui controls, and Lua access to Bodycam state and viewmodel profiles;
* built-in Bodycam presets plus user-created presets that can be managed entirely in-game;
* isolated `bodycam_user.ltx` storage for user-created Bodycam presets without modifying shipped defaults.

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
git clone --recursive https://github.com/asuparabekon/xray-monolith-bodycam.git
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

### Keep each value in the correct DLTX root

DLTX selects the root file from the mod-file name. Give every file a unique author or mod suffix:

| Addon file | Extends | Put these values here |
| --- | --- | --- |
| `mod_system_<author-or-mod>_pip.ltx` | `system.ltx` | Runtime scope, weapon, or upgrade fields such as objective geometry, PiP magnification, and hybrid state |
| `mod_pip_optic_physical_specs_<author-or-mod>.ltx` | `pip_optic_physical_specs.ltx` | Canonical real-world measurements and aliases from runtime sections to those records |
| `mod_pip_optic_profiles_<author-or-mod>.ltx` | `pip_optic_profiles.ltx` | Optional controller response and zeroing values |
| `mod_pip_hybrid_optics_<author-or-mod>.ltx` | `pip_hybrid_optics.ltx` | Fallback hybrid-state registry entries for a standalone compatibility patch |

The section operator is relative to that root:

- use `![existing_section]` to add keys to a section that already exists in that root;
- use `[new_section]` to create a section that does not exist in that root;
- never repeat an existing section as `[existing_section]`; DLTX treats that as an unmarked
  duplicate rather than an override.

For example, an installed scope already exists in `system.ltx`, so its runtime patch uses
`![my_pack_scope]`. Its alias usually does not yet exist in `pip_optic_physical_specs.ltx`, so the
alias uses `[my_pack_scope]` in that separate root.

Do not ship replacement copies of the root files. A replacement can hide every record supplied by
the engine and other addons. `pip_objective_mm.ltx` is a built-in legacy lookup table rather than an
addon merge target; new physical data belongs in `pip_optic_physical_specs.ltx`. The
`pip_optic_external_aliases.ltx` file is a schema-checked, single-owner contract for the official
MAS/Pizza compatibility package, not a general extension point.

### Patch an existing scope

First identify the section that is active in game. Inventory names, icons, and HUD section names are
not reliable identities. Enable diagnostics, aim through the optic, and inspect the resolved
`weapon`, `scope`, `diagnostic_scope`, and `binding_section`:

```text
r__svp_diag 1
svp_dump_optic
```

Patch the reported runtime scope or weapon section and add only the PiP-specific lines:

```ini
; gamedata/configs/mod_system_my_pack_pip.ltx
![my_pack_scope]
; Replace this tuple with values measured from this HUD mesh
scope_objective_lens_offset = 0, 0, 13.3, 0.73
; Add only when the standard magnification fields are absent or incorrect
svp_magnifications = 1-6
```

Do not copy the whole original section into the patch. Preserve its `hud`, legacy
`scope_zoom_factor`, `scope_dynamic_zoom`, controller, upgrade, and 3DSS fields so the same addon
continues to work on engines without true PiP.

The resolver recognizes these runtime additions:

| Key | Purpose and preferred use |
| --- | --- |
| `scope_objective_lens_offset` | Places and sizes the objective relative to the eyepiece |
| `svp_magnifications` | Optionally overrides ambiguous or incorrect legacy magnification data |
| `s3ds_objective_mm` | Legacy per-section objective-diameter fallback when no canonical physical record can supply `objective_mm` |
| `scope_zero_m` | Per-section zero distance; use the profile root when it is controller tuning shared by the optic |
| `s3ds_middle_grey`, `s3ds_adapt_speed` | Existing 3DSS exposure controls that PiP preserves, accepted at `0`-`2` and `0`-`20` respectively |
| `svp_hybrid_reflex` | State marker for an engaged or inactive reflex-plus-magnifier assembly |

`scope_objective_lens_offset` is `x, y, z, w` in eyepiece-radius units. `x` and `y` are the
objective's lateral offset, `z` is its forward distance, and `w` is its clear radius. The tuple is
specific to the HUD mesh, not merely the optic model. Two weapon variants may share a physical
profile but still require different tuples if their HUD meshes differ. If the field is absent, the
current engine can fall back to live mesh detection; treat that as a compatibility fallback, not as
authored proof. Use `r__scope_debug 2` and the `[PIP-OPTIC-GEOM]` line to verify the objective disc
and whether the winning source is the section or `engine_detection`.

An authored tuple must contain four finite numbers. The typed route accepts `x` and `y` from `-8`
through `64`; `z` and `w` must be greater than zero and no greater than `64`. Invalid geometry
rejects the complete typed snapshot rather than partially applying it.

`w` is a visual radius in eyepiece-radius units, not a real-world diameter. Objective millimetres
resolve in this order:

1. `objective_mm` from the canonical physical record;
2. `s3ds_objective_mm` on the active weapon, scope, or permanent-scope hint;
3. the built-in legacy `pip_objective_mm.ltx` table;
4. live engine detection;
5. a renderer fallback derived from mesh-relative `w`.

`svp_magnifications` accepts:

| Form | Meaning |
| --- | --- |
| `4` | Fixed 4x optic |
| `1-6` | Smooth variable 1x through 6x |
| `1,6` | Two-position 1x/6x switch |

Endpoints must be ordered from low to high and currently fall within `0.5x` through `100x`.
Use the PiP field only when the existing standard 3DSS `magnifications` or `magnification` field is
absent, ambiguous, or wrong for the modeled optic. `svp_magnifications` wins when present; otherwise
the engine consumes those standard fields and then retains the legacy zoom-factor route. A
malformed authored value stays on the legacy route and emits `[SVP-MAGS]` with the section, value,
and winning DLTX file.

For a detachable or modular optic, put optic-specific records on the attachment or active
state section reported as `scope`. The attached scope is resolved before the permanent-scope hint
and weapon fallback, so a base-weapon profile cannot mask a recognized attachment. For a permanent
or integrated optic, the weapon section is normally the correct owner.

### Structure a new scope additively

True PiP does not replace the attachment framework or 3DSS definition. Start with a scope that
already works through the normal engine, then layer PiP data onto it:

| Layer | Responsibility |
| --- | --- |
| Attachment framework | Register the scope, inheritance, compatible weapon groups, upgrades, and state transitions |
| Runtime scope section | Keep `hud`, legacy zoom fields, 3DSS fields, objective geometry, and any PiP magnification override |
| HUD section and model | Supply `item_visual` plus real lens and reticle geometry/materials that 3DSS can classify |
| Physical-spec DLTX root | Map the runtime identity to one sourced canonical optic record |
| Profile DLTX root | Optionally tune tracking response and zeroing without rewriting physical measurements |
| Hybrid state | Mark each engaged and inactive runtime section when a reflex and magnifier share the optical path |

MAS-specific tables, addon registries, scope groups, and base-section names remain MAS concerns;
traditional indexed attachments should keep their own `scopes`, `scope_name`, and derived weapon
sections. PiP consumes the final runtime identity and visible HUD geometry; it does not manufacture
either from an LTX name.

Before publishing, verify that each runtime state has a valid `hud` section, that the HUD has an
`item_visual`, and that aiming produces a real scope-lens draw. A correct LTX profile cannot recover
a model with no classified scope lens, or a hybrid with no reflex mesh.

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

`data_grade`, `optic_class`, and FOV fields retain provenance and review metadata. Neither FOV
fields nor the canonical `magnification_min` and `magnification_max` endpoints set gameplay zoom;
the runtime section still needs its legacy zoom data or a valid `svp_magnifications` override.
Physical `geometric` or `display` values take precedence over matching controller-tuning values.

A physical record does not set gameplay zoom or locate the objective in a particular HUD mesh.
Those remain in the runtime `system.ltx` patch described above. Multiple runtime aliases may share
one canonical physical record while retaining their own mesh tuples.

Physical identity lookup is deterministic:

1. attached scope section;
2. permanent-scope hint;
3. active weapon section.

Controller tuning uses the same identity order and then falls back to `[default]`.

Within each identity, built-in aliases are checked before the optional compatibility alias
contract. `pip_optic_external_aliases.ltx` is a single-owner, schema-checked compatibility file;
ordinary weapon packs should extend `pip_optic_physical_specs.ltx` through DLTX instead of
overwriting that contract. A DLTX-added alias is part of the built-in root for this lookup.

An alias section claims its runtime identity even if its `spec` is missing or invalid. Do not add
placeholder aliases such as `spec = unknown`; they terminate lookup at that identity without
providing a usable physical profile. `runtime_model` must resolve to `geometric` or `display`
before the canonical record can drive the runtime.

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
Resolvable physical values from a bound record remain authoritative. Missing eye-relief,
exit-pupil, parity, or geometric twilight inputs may still fall through to controller tuning;
transmission, pupil-field scale, and geometric tunneling receive physical-model defaults.
Controller profiles are best kept to tracking response and zero distance unless a value is
genuinely a nonphysical fallback.

Accepted controller bounds are:

| Profile key | Range |
| --- | ---: |
| `s3ds_tunneling_parallax` | `0`-`0.15` |
| `s3ds_tunneling_min`, `s3ds_tunneling_max` | `0`-`1` |
| `s3ds_tunneling_softness` | `0`-`0.1` |
| `s3ds_eye_tracking_speed` | `0.1`-`30` |
| `s3ds_eye_tracking_accel_mm_s2` | `1`-`500` |
| `s3ds_eye_tracking_limit_mm` | `0`-`20` |
| `s3ds_eye_relief_low_mm`, `s3ds_eye_relief_high_mm` | `20`-`150` |
| `s3ds_exit_pupil_low_mm`, `s3ds_exit_pupil_high_mm` | `0`-`100` |
| `s3ds_pupil_parity` | `-1`-`1` |
| `s3ds_pupil_field_low`, `s3ds_pupil_field_high` | `0`-`6` |
| `s3ds_transmission`, `s3ds_twilight_strength` | `0`-`1` |
| `scope_zero_m` | `0`-`1000` |
| `eye_coupling` | `0` or `1` |

An out-of-range value rejects the complete typed profile, so do not depend on silent clamping.

`eye_coupling` classifies the viewing model: `1` for an eyepiece image that follows the eye and
`0` for a rigid digital panel such as a thermal or sensor display. The bound physical record's
`optic_class` selects the default, and a runtime-section line overrides it. A rigid panel keeps
eyepiece parallax and tunneling off while its overlay owns the image; a true ocular in front of a
tube, such as the 1PN23, should carry an authored `eye_coupling = 1`. An optic that resolves no
record and no authored line falls back to the `[default]` profile, so a display-type optic from a
weapon pack needs either a `display` physical record or its own `eye_coupling = 0` line to stop
its panel swimming with the eye.

The classification follows the optic the eye actually looks through, not the sensor. A clip-on
thermal mounted ahead of a host scope projects its image through the host's objective and
eyepiece, so the assembly keeps `eye_coupling = 1` and its parallax shadow; only a sight whose
display is viewed directly with no ocular behind it is a rigid panel. Do not author
`eye_coupling = 0` on a clip-on.

### Hybrid magnifiers

A hybrid is a reflex or holographic sight viewed through a flip-to-side magnifier. When the
magnifier is engaged, true PiP renders the magnified world and then draws the actual reflex mesh
into the finished second viewport through the same objective camera. A successful capture keeps
the reticle sharp inside the magnified view and suppresses its duplicate one-power overlay. If the
capture cannot be proven current, the ordinary overlay remains as the safe fallback.

`svp_hybrid_reflex` describes the current runtime state. It does not perform the flip, select zoom,
hide a lens, create missing scope or reflex meshes, or turn an ordinary dot into a hybrid. Mark
every state explicitly:

```ini
; gamedata/configs/mod_system_my_pack_pip_hybrids.ltx
![my_dot_magnifier_engaged]
svp_magnifications = 3
svp_hybrid_reflex = true

![my_dot_magnifier_off]
svp_hybrid_reflex = false

![my_dot_magnifier_flipped]
svp_hybrid_reflex = false
```

The explicit values mean:

| Resolved value | Behavior |
| --- | --- |
| `true` | The magnifier is on the optical axis and reflex capture is eligible |
| `false` | The state is explicitly off, flipped, or otherwise non-hybrid |
| field absent with reticle type 12 | Legacy magnifier classification |
| field absent with another reticle type | Normal non-hybrid optic |

Explicit `false` matters. An inactive state that retains 3DSS reticle type 12 but omits the field
can still enter the legacy hybrid classifier. Do not place `true` on a shared reflex parent or on
both sides of a flip transition.

For fully authored physical behavior, the engaged state should also provide:

- the real fixed or variable magnification;
- a mesh-specific objective offset, or a verified engine-detection fallback;
- a physical-profile alias for the magnifier actually modeled;
- working scope-lens and reflex meshes classified by the existing 3DSS material path.

For example, only an attachment that really models an EOTECH G33 should reuse its shipped record:

```ini
; gamedata/configs/mod_pip_optic_physical_specs_my_pack.ltx
[my_dot_magnifier_engaged]
spec = optic_eotech_g33_3x
```

Otherwise add a new sourced `optic_...` record for the actual magnifier. The inactive reflex state
does not inherit the magnifier's physical profile merely because it belongs to the same assembly.

Put the Boolean on the exact live state section or installed-upgrade section. Hybrid resolution
checks an installed upgrade first, then a direct field on a non-modular weapon, the attached scope,
a permanent-scope hint, and finally the exact-name registry. Direct weapon fields are skipped on a
modular host, so its attachment or upgrade should own the state. The fallback registry can still
match a weapon name; never register a generic modular host as a hybrid identity.

If a flip controller keeps one section and changes only a bone animation, static LTX cannot express
both states. Expose distinct runtime states, or deliberately own and republish the complete typed
optic snapshot and lifecycle. The typed API is not a Boolean setter, and a dynamic publisher must
not compete with the bundled publisher.

For a standalone compatibility patch that should not touch the original runtime sections, extend
the fallback registry:

```ini
; gamedata/configs/mod_pip_hybrid_optics_my_pack.ltx
[my_dot_magnifier_engaged]
svp_hybrid_reflex = true

[my_dot_magnifier_flipped]
svp_hybrid_reflex = false
```

Direct runtime fields win over the registry. Do not replace `pip_hybrid_optics.ltx`, and do not
define both routes unless their values agree.

The shipped 3DSS magnifier contract uses reticle type 12, but explicit hybrid capability is not
intrinsically tied to that number. Keep the reticle type required by the optic's working shader
contract. In particular, do not remap ARC type 10 merely to trigger the type-12 legacy fallback.

With `r__svp_diag 1`, an engaged state should report
`[PIP-OPTIC-GEOM] ... hybrid=true authored=true` followed by
`[SVP-HYBRID] state=drawn`. An off or flipped state should report
`hybrid=false authored=true` and must not produce a successful hybrid draw. Depending on whether
that state still activates an objective viewport, its renderer state may be `not_hybrid`,
`inactive`, or `wrong_domain`. A live thermal display reports `thermal`: the reflex capture is
skipped by design while the sensor overlay owns the image. `empty` means the scope or reflex
capture map has no candidates.
`no_draw` means candidates existed but none passed the visibility, ownership, relation, projection,
or draw checks. `stale_identity` or `stale_type` points to a runtime identity or reticle-state
mismatch. Use `svp_dump_optic` to inspect the accepted scope, reticle type, physical profile, and
hybrid presence/value. `r__svp_reflex_capture 0` keeps the one-power fallback for an A/B test.

### Typed Lua API

The engine API and the published profile table have separate versions:

- typed API version `3`;
- profile table schema version `3`.

The API and schema are one exact contract. Consumers must verify the supported pair and schema hash
before reading descriptors or publishing a profile. An incompatible consumer stays inert instead
of attempting to interpret a different table layout.

Probe global functions with `rawget`, require an exact API version, and use `pcall`. Never infer
support from a console variable or call fork-only exports unconditionally.

```lua
local API_VERSION = 3
local SCHEMA_VERSION = 3

local function connect_true_pip()
    local version_fn = rawget(_G, "svp_optic_api_version")
    local info_fn = rawget(_G, "svp_optic_api_info")
    local connect_fn = rawget(_G, "svp_optic_api_connect")
    if type(version_fn) ~= "function" or type(info_fn) ~= "function"
        or type(connect_fn) ~= "function" then
        return false
    end

    local ok, version = pcall(version_fn)
    if not ok or version ~= API_VERSION then
        return false
    end

    local info_ok, info = pcall(info_fn)
    if not info_ok or type(info) ~= "table" then
        return false
    end

    local connected_ok, connected = pcall(connect_fn, API_VERSION, SCHEMA_VERSION)
    return connected_ok and type(connected) == "table" and connected.ok == true
        and connected.schema_hash == info.schema_hash
end
```

The public functions are:

| Function | Purpose |
| --- | --- |
| `svp_optic_api_version()` | Return the engine API version |
| `svp_optic_api_info()` | Return supported API/schema pairs, schema hash, and capability map |
| `svp_optic_api_connect(api, schema)` | Activate the typed route after an exact-pair handshake and return a structured result |
| `svp_optic_api_describe()` | Return schema field descriptors, types, bounds, and constraints |
| `svp_optic_api_has_capability(name)` | Query an optional capability by name |
| `svp_validate_optic_fields(table)` | Validate an authored profile fragment without publishing it |
| `svp_validate_optic_profile(table)` | Validate a complete profile without publishing it |
| `svp_optic_route_epoch()` | Detect an engine-side route reset that requires republishing |
| `svp_begin_optic_context(context, weapon, weapon_id, scope, zoom_type, identity_source, diagnostic_scope)` | Begin one weapon and optic identity and receive a nonzero token |
| `svp_apply_optic_profile(token, table)` | Atomically validate and publish a complete schema-3 snapshot, returning a structured result |
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

A direct publisher must begin a context, submit the full schema-3 table, clear its token on zoom
out or ownership change, and republish after the route epoch changes. Unknown table keys, missing
fields, out-of-range values, stale tokens, and changed identity fields are rejected as one unit.
Use `svp_optic_api_describe()` rather than duplicating field limits in another script. Optional
features must still be capability-probed before their fields are authored.
Use the `typed_table` builder in
[`zzz_extra_scope_features.script`](gamedata/scripts/zzz_extra_scope_features.script) as the
canonical field list and the validator in
[`svp_optic_config_script.cpp`](src/xrGame/svp_optic_config_script.cpp) as the authoritative
types and ranges. A complete read-only consumer demonstrating the handshake, capability probing,
schema introspection, fragment validation, and provenance reads ships as
[`pip_optic_api_example.script`](gamedata/scripts/pip_optic_api_example.script); it is inert on
engines without the API and silent unless `r__svp_diag` is enabled.

### Version-safe extension rules

1. Preserve legacy weapon, zoom, 3DSS, and Mark Switch fields; PiP fields are additive.
2. Namespace new filenames and canonical sections with the author or mod identity.
3. Use `[new_section]` for a new section and `![existing_section]` only when patching a section
   already defined in the same LTX root.
4. Never ship a replacement `pip_optic_physical_specs.ltx`,
   `pip_optic_profiles.ltx`, `pip_hybrid_optics.ltx`, or `zzz_extra_scope_features.script` for a
   data-only addon.
5. Treat API and table versions as exact contracts. Feature-probe optional capabilities.
6. Increment a compatibility patch's own version whenever aliases, required sections, or runtime
   mappings change, and keep its manifest, metadata, and LTX control version synchronized.
7. Keep detachable-optic data on the attachment identity; reserve weapon-level profiles for
   permanent or integrated glass.
8. Mark both engaged and inactive hybrid states. Missing is not equivalent to explicit `false`.
9. Test fixed, variable, and switched endpoints after attach, detach, reload, and state changes.
10. Test both `r__svpscope 0` and `2`. A missing API must leave the old route functional.

Enable `print_dltx_warnings 1` while authoring. Then use `r__svp_diag 1` and `svp_dump_optic` to
confirm the active identity, canonical spec, resolved values, and the winning DLTX source. The
runtime also writes `[PIP-OPTIC]`, `[PIP-OPTIC-ZOOM]`, `[PIP-OPTIC-GEOM]`, and `[SVP-CONFIG]`
lines for concrete bug reports.

### Feature status

Everything above describes the current published build. The table below marks the edges of the
implementation so an integration does not depend on behavior that is still moving. In development
means the work exists in the development branch and ships with a future build; designed means the
behavior is specified but not yet built.

| Area | Status |
| --- | --- |
| DLTX roots, physical records, aliases, controller tuning, hybrid states | Stable and documented above |
| Typed Lua API version `3`, profile table schema `3` | Current contract: exact-pair negotiation, schema introspection, structured validation and apply results, two-phase publication, profile provenance, and physical magnification ladders owned by the optic context |
| Ring-dial magnification stops | In development: a variable optic with a rotating ring dial steps on integer magnifications so the needle lands on the engraved digits |
| Scopeless optics on the typed route | In development: binoculars and other section-only identities resolve through the weapon section to the `[default]` profile |
| In-game profile editor | In development: a schema-driven live editor that writes a loose user override file instead of mod data |
| `r__svp_roll_stabilize` | Reserved and currently inert; reticle cant leveling is the planned behavior |
| Lens-physics effects | Designed, not implemented: field-dependent distortion, eyepiece sun glare, and per-scope field-curvature authoring |
| Activation without a typed config | Known limitation: on an API-enabled build an optic that never resolves a typed config does not activate the second viewport; give every optic a resolvable identity, and note the `[default]` tier covers section-only optics |

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
