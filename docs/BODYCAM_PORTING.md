# Porting bodycam to another X-Ray build

Bodycam is split into files a port copies unchanged and hooks a port re-creates
in its own engine files. The hook table at the end is generated from the
source by name matching: it lists call sites and declarations that mention
bodycam names. Engine APIs the module depends on don't show up there; they are
under "Engine prerequisites".

## What to copy

| Files | Contents |
| --- | --- |
| `src/xrGame/bodycam_reference/` | Portable Reference library (C++17, own `CMakeLists.txt`, no engine dependency). Includes the `classic_*.h` and `native_*.h` policy headers and the generated data headers. |
| `src/xrGame/bodycam_simulation.*`, `bodycam_camera_rig.*`, `bodycam_smoothing.h`, `bodycam_camera.*`, `bodycam_math.*`, `bodycam_mouse_input.*`, `bodycam_movement_response.*`, `bodycam_viewmodel_profile.*`, `classic_handling.h` | Classic simulation, camera, movement and handling. See [Classic code layout](#classic-code-layout). |
| `src/xrGame/bodycam_settings.*`, `bodycam_console.cpp`, `bodycam_script.cpp` | Settings registry, console commands, Lua bindings. The registry is the one definition of a setting; Lua and XML refer to it by name. |
| `src/xrGame/bodycam_diagnostics.*` | Log diagnostics switch. Reads the scope renderer's `r__svp_cop_diag` console level. |
| `src/xrGame/bodycam_actor_camera.cpp` | `CActor` camera members: the camera rig's engine side (its frame input, `CameraRigHost`, applying the composed view, the HUD share), HUD offset, regrip publication. |
| `src/xrGame/bodycam_sprint_start.h` | Engine-free movement presentation: sprint start, run-loop pacing, held sprint lowering, braking stride. Talks to the engine through `SprintStartHost` and plain input structs. |
| `src/xrGame/bodycam_actor_movement.cpp` | Adapter for the above: gathers inputs from `CActor`, implements `SprintStartHost`, applies inertia. Also the Reference gait and speed rules that `g_cl_CheckControls` calls. This is the file a port edits. |
| `src/xrGame/bodycam_hud_arms.*` | Classic arm IK, regrip, recoil and handling on the HUD rig. |
| `src/xrGame/bodycam_hud_playback.cpp` | `CHudItem` and `player_hud` members: sprint-entry clip, one-shot scrub, loop pacing, script-only movement notification. |
| `src/xrGame/bodycam_steps.cpp` | `CStepManager` members: stride time, foot phase, locomotion-phase sync. |
| `src/xrGame/bodycam_reference_*.cpp/.h` | Reference mode hosts: frame adapter, arms, skeleton, optics, asset loading, HUD bindings, action playback, the weapon's action aim and fire. |
| `src/xrGame/bodycam_pip_*`, `first_person_transform.*` | PiP adapter and the first-person transform API scripts read. |
| `gamedata/scripts/bodycam_imgui.script`, `options_modded_exes_bodycam.script`, `gamedata/configs/text/eng/st_bodycam_mcm.xml` | ImGui window, MCM options, MCM strings. |
| `gamedata/scripts/bodycam_weapon_hud_profiles.script`, `bodycam_physical_glass.script` | Per-weapon hip viewmodel profiles; visibility of converted scope glass. |
| `gamedata/configs/mod_system_bodycam_reference.ltx` | Reference config: `bodycam_reference_character` names the rig, regrip and recoil packets (installed under `bodycam_reference\character\`); `bodycam_reference_tuning` holds the host game's tuning (extra reload aliases, pistol hip offset, recoil budgets per family, script layers Reference skips), so set it for another game; `bodycam_reference_effects` is the scope effects profile. |
| `gamedata/bodycam_reference/` | The scope glass constants (`optics/source_glass_cb.bin`, `optics/selected_cb.bin`). The Reference character packets are not in the repository. |

The scope glass and PiP renderer (`svp_*` under `src/Layers` and `src/xrEngine`,
shaders under `gamedata/shaders/r3`) is a separate module with its own upstream;
this guide does not cover it. The bodycam module drives it through the
published optics frame (`src/xrEngine/svp_reference_optics.h`), written by
`bodycam_reference_optics.cpp`; its scope eye flags (`classicPupilTracking`,
`classicFollowEye`) are read by `SvpReferencePupil::Eye`
(`svp_reference_pupil_tracking.h`).

For a build without the engine, define `BODYCAM_STANDALONE` when compiling
`bodycam_simulation.cpp`, `bodycam_camera_rig.cpp` and `bodycam_mouse_input.cpp`.
The camera rig also needs the xrCore math headers.

## How the seam works

The implementations are members of the engine classes (`CActor`, `CHudItem`,
`player_hud`, `CStepManager`). They are defined in bodycam-owned files, and
the engine files keep only the calls. A port therefore needs:

1. The files above added to the game project.
2. The member declarations in the engine headers: the `(declarations)` rows
   of the `.h` files in the hook inventory. They are grouped under
   `Bodycam` / `cam_Bodycam` / `Reference` names.
3. The call sites listed in the hook inventory.

Movement presentation has no engine types at all: its logic is in
`bodycam_sprint_start.h`, and `bodycam_actor_movement.cpp` is only the
adapter. A port to a build with different member names rewrites the adapter
and leaves the logic alone.
The Classic camera works the same way: `bodycam_camera_rig.*` holds the
camera logic with no engine state, and `bodycam_actor_camera.cpp` reads the
actor once per frame (`ActorFrame`), implements `CameraRigHost` and applies the
composed view.

## Engine prerequisites

These are engine changes the module relies on. They are not hooks and the
table does not list them.

- **Grounded planar physics** (Reference movement): `GetGroundedPlanarObservation`,
  `ClearGroundedPlanarVelocity` and `EnableGroundedPlanarMotor` on the movement
  control, implemented through `PHCharacter.h`, `PHSimpleCharacter.*` and
  `xrPhysics/PHGroundedPlanarMotor.h`.
- **Camera manager and projection**: the HUD projection and camera effector
  changes in `xrEngine/CameraManager.cpp` and `xrEngine/device.cpp`.
- **Script callbacks**: `_G.CHudItem__PlayHUDMotion`,
  `_g.player_hud__OnMovementChanged` and `_G.CActor__FootstepCallback` must
  exist with the same meaning.
- **Weapon lowering**: `CActor::set_safemode`, `kSAFEMODE` and the lowered HUD
  offset slot (index 4).

## Hooks that are more than one call

- **Camera and HUD (`CActor::cam_Finalize`, `CActor::UpdateCL`)**: the visual
  camera override keeps the native fallback branch, and the HUD camera receives
  the footstep offset before the HUD solve. After the HUD solve,
  `cam_BodycamApplyRegrip` applies the view again, turned by the regrip camera,
  and republishes; it must stay after `g_player_hud->update` (see
  [Camera frame](#camera-frame)).
- **Movement (`CActor::g_cl_CheckControls`)**: `BodycamUpdateMovement` rewrites
  the control acceleration, so it must run where the native code has computed
  it and before physics consumes it.
- **Legs (`CActor::g_SetAnimation`)**: the braking stride selects the leg clip
  inline, and the phase carry-over condition includes braking.
- **Idle clip selection (`CHudItem::TryPlayAnimIdle`, `CHudItem::PlayHUDMotion`)**:
  the sprint gate and the sprint-entry probe sit inside the native selection.
- **Lowered pose (`CWeapon::UpdateHudAdditional`)**: the HUD offset target is
  blended toward the lowered offset before the native interpolation.
- **Script lowering (`actor_set_safemode` in `level_script.cpp`)**: a lowering
  requested during the sprint start is held before any native side effect.

## Classic code layout

All under `src/xrGame`. The four simulation features each have a settings
struct with an enable flag, a state struct and an update function in
`bodycam_simulation.*`. Camera Decoupling and the Footstep Camera keep their
settings and state in the camera rig; the other features keep their settings in
`bodycam_settings.h`. Mouse-driven arm settling and Movement-linked handling
share one struct with two flags. The sprint gun bob's strength
(`sprint_transition_gun_bob`) is in `RuntimeConfig::sprint_start`; its switch
is the Sprint Transition's (`sprint_transition_enable`).

- `bodycam_simulation.*`: sprint sway, lowering, fire impulse and STALKER 2
  arm IK. Plain data in and out, no engine types.
- `bodycam_camera_rig.*`: the Classic camera: look lag, position shift, head
  roll and shake, footstep motion, the HUD offsets and the regrip turn. Both
  lag models update every frame and the selected one supplies the angles. Only
  xrCore math types, no engine state; `bodycam_actor_camera.cpp` gathers
  `CameraRigInput`, answers `CameraRigHost` (the footstep wall check) and
  applies the view.
- `bodycam_smoothing.h`: smoothing steps the simulation and the rig share.
- `bodycam_sprint_start.h`: sprint start, run-loop pacing, held lowering,
  braking stride. No engine types; the engine side is `SprintStartHost`,
  implemented in `bodycam_actor_movement.cpp`.
- `bodycam_settings.*`: the registry of settings (name, console name,
  storage, default, range). UI scripts refer to settings by name only.
- `bodycam_camera.*`: `CBodycam`, the per-actor host of the simulation and
  the camera rig. The actor reads the engine once per frame into an
  `ActorFrame`; `CBodycam::Update` derives the simulation input and the rig
  input from it.
- `bodycam_actor_camera.cpp`, `bodycam_actor_movement.cpp`,
  `bodycam_hud_playback.cpp`, `bodycam_steps.cpp`: members of engine classes,
  defined in bodycam-owned files.
- `bodycam_hud_arms.*`: the arm controller on the HUD rig.
- `bodycam_reference/`: the portable Reference library the Classic features
  borrow from (regrip, recoil, footstep camera, head motion, follow).

### Camera frame

In `CActor::cam_Finalize`, after the level camera manager has applied the raw
camera (`ActorCameras.cpp`), `cam_BodycamVisualUpdate` runs the rig and
applies its view, or the native camera when the rig does not compose; a held
PiP surround applies nothing. `FirstPerson::PublishCamera` publishes that
camera. The HUD update then solves the arms (regrip included). Last,
`cam_BodycamApplyRegrip` (`Actor.cpp`) applies the view a second time, turned
by the regrip camera, only when regrip is on and this frame's HUD solve gave
a regrip camera; it then publishes the camera and the weapon's scope snapshot
again. Readers inside the HUD update therefore see the first apply.

## Limits

- Except for movement presentation, the seam is member functions of engine
  classes, so a build with different member names needs edits inside the
  camera, arms, steps and HUD playback files.
- Setting defaults and ranges shown by the MCM page are written by hand in
  `options_modded_exes_bodycam.script`; only the names are checked against
  the registry.
- The scope glass and PiP renderer is not separated from its upstream.

## Hook inventory

<!-- hooks:begin (generated by tests/reference/bodycam-hooks.py) -->

30 engine files, 278 hook lines.

| Engine file | Hook lines | Functions that contain hooks |
| --- | ---: | --- |
| `src/xrEngine/bodycam_reference_projection.h` | 1 | `(declarations)` |
| `src/xrEngine/CameraManager.cpp` | 6 | `(declarations)`, `CCameraManager::ApplyDevice` |
| `src/xrEngine/device.cpp` | 6 | `(declarations)`, `CRenderDevice::on_idle` |
| `src/xrEngine/device.h` | 3 | `(declarations)` |
| `src/xrGame/Actor.cpp` | 5 | `(declarations)`, `CActor::Load`, `CActor::UpdateCL` |
| `src/xrGame/Actor.h` | 53 | `(declarations)` |
| `src/xrGame/Actor_Movement.cpp` | 14 | `(declarations)`, `CActor::g_cl_CheckControls`, `CActor::CanSprint` |
| `src/xrGame/Actor_Network.cpp` | 1 | `CActor::net_Destroy` |
| `src/xrGame/Actor_Weapon.cpp` | 5 | `CActor::on_weapon_shot_start`, `CActor::on_weapon_shot_remove`, `CActor::on_weapon_shot_stop`, `CActor::on_weapon_hide` |
| `src/xrGame/ActorAnimation.cpp` | 9 | `CActor::g_SetAnimation` |
| `src/xrGame/ActorCameras.cpp` | 15 | `(declarations)`, `CActor::cam_ApplyScriptCameraDeltas`, `CActor::cam_Prepare`, `CActor::cam_Finalize` |
| `src/xrGame/ActorEffector.cpp` | 4 | `CActorCameraManager::UpdateReference`, `CActorCameraManager::ProcessCameraEffector` |
| `src/xrGame/ActorEffector.h` | 2 | `(declarations)` |
| `src/xrGame/ActorInput.cpp` | 6 | `CActor::IR_OnMouseMove` |
| `src/xrGame/console_commands.cpp` | 2 | `(declarations)`, `CCC_RegisterCommands` |
| `src/xrGame/EffectorBobbing.cpp` | 2 | `(declarations)`, `CEffectorBobbing::ProcessCam` |
| `src/xrGame/HudItem.cpp` | 6 | `CHudItem::PlayHUDMotion`, `CHudItem::TryPlayAnimIdle` |
| `src/xrGame/HudItem.h` | 6 | `(declarations)` |
| `src/xrGame/Level.cpp` | 1 | `CLevel::~CLevel` |
| `src/xrGame/Level_input.cpp` | 1 | `CLevel::IR_OnKeyboardPress` |
| `src/xrGame/level_script.cpp` | 6 | `PlayHudMotionBodycam`, `PlayBlendAnmBodycam`, `actor_set_safemode`, `CLevel::script_register` |
| `src/xrGame/player_hud.cpp` | 44 | `(declarations)`, `NativeHudLayerInfluence`, `attachable_hud_item::anim_play`, `player_hud::player_hud`, `player_hud::~player_hud`, `player_hud::load`, `player_hud::update`, `player_hud::prepare_hud_pose`, `player_hud::finalize_hud_pose`, `player_hud::updateMovementLayerState`, `player_hud::PlayBlendAnm`, `player_hud::anim_play`, `player_hud::script_anim_play`, `player_hud::attach_item`, `player_hud::detach_item_idx`, `player_hud::OnMovementChanged`, `player_hud::OnFrame` |
| `src/xrGame/player_hud.h` | 48 | `(declarations)` |
| `src/xrGame/step_manager.cpp` | 3 | `CStepManager::on_animation_start`, `CStepManager::update` |
| `src/xrGame/step_manager.h` | 8 | `(declarations)` |
| `src/xrGame/Weapon.cpp` | 7 | `CWeapon::OnH_B_Independent`, `CWeapon::UpdateCL`, `CWeapon::signal_HideComplete`, `CWeapon::Action`, `CWeapon::Reload`, `CWeapon::OnZoomIn`, `CWeapon::UpdateHudAdditional` |
| `src/xrGame/Weapon.h` | 8 | `(declarations)` |
| `src/xrGame/WeaponBinoculars.cpp` | 1 | `CWeaponBinoculars::OnZoomIn` |
| `src/xrGame/WeaponMagazined.cpp` | 2 | `CWeaponMagazined::OnZoomIn`, `CWeaponMagazined::OnZoomOut` |
| `src/xrServerEntities/script_engine_export.cpp` | 3 | `(declarations)`, `export_classes` |

<!-- hooks:end -->
