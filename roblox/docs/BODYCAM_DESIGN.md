# Bodycam package: design specification

This document specifies the `Bodycam` package (`roblox/src/Bodycam`) for a clean
reimplementation. It describes **behaviour and interfaces only**. It was written so
the package could be rebuilt without the X-Ray Monolith Bodycam source, whose license
forbids commercial use. The implementation must not use that source, its constants,
its recorded animation data, its names or its structure. Features are described
the way a game designer would describe them; every default value here was chosen for
this design.

## Goals

A first-person "body-worn camera" feel for Roblox:

- The weapon points where the player aims. The view trails slightly behind it, as if
  the camera were strapped to the chest and the head had to catch up.
- The head moves with the body: steps bob it, strafing and turning roll it, sprinting
  leans it forward.
- The weapon moves with the hands: it dips when walking, kicks on every shot, and the
  wrists roll a little when the mouse moves fast.
- Starting and stopping take a moment (movement inertia).

## Conventions

- Roblox native, right-handed. Distances are **studs**; angles are **radians** unless a
  setting name ends in `Deg`.
- Aim angles: `yaw` is the rotation about world Y with positive turning left (as
  `CFrame.fromOrientation(pitch, yaw, 0)`), `pitch` positive looks up.
- Camera-local offsets are Roblox camera space: +X right, +Y up, −Z forward.
- `studsPerMetre` (default `1 / 0.28`) exists only so other packages can convert to SI
  units; the Bodycam package itself authors everything in studs.

## Module layout

All under `roblox/src/Bodycam/`. `init.luau` returns the public table.

| Module | Roblox API? | Role |
| --- | --- | --- |
| `Math.luau` | no | Shared helpers: clamp, lerp, wrapAngle, exponential smoothing, critically damped spring (scalar and 3-vector), soft limit. |
| `Aim.luau` | `CFrame`/`Vector3` only | `aimFromLook(look) -> yaw, pitch`, `aimRotation(yaw, pitch) -> CFrame`, `yawFrame(yaw) -> right, forward` (unit world vectors of the yaw-only frame). |
| `Settings.luau` | no | Settings registry and presets (below). |
| `LookLag.luau` | no | View trailing the aim. |
| `HeadMotion.luau` | no | Stride bob, strafe/turn roll, sprint lean, landing dip. |
| `WeaponMotion.luau` | no | Walk lowering, holster offset, fire kick, wrist roll, weapon profile. |
| `MovementInertia.luau` | no | Eased commanded velocity. |
| `StrideClock.luau` | no | Foot phase from speed when the game gives none. |
| `BodycamCore.luau` | no | Runs the pure modules for one character per frame. |
| `Controller.luau` | yes | Roblox adapter: camera, humanoid, viewmodel, hooks. |

Pure modules take plain numbers/tables and must run in any Luau VM (tests run outside
Roblox). `Controller.luau` is the only file that touches services.

## Features

Every feature has an `enabled` switch and works with the others off.

### 1. Look lag (view trails the aim)

- The view orientation follows the aim through a critically damped spring per axis
  (yaw, pitch). Natural frequency: hip `followHz = 5`, ADS `followHzAds = 11`, blended
  by the ADS amount.
- A small dead band: aim changes within `deadBandDeg = 0.35` of the view do not move the
  view.
- The angular gap is limited to a cone: `maxLagDeg = 7` hip, `maxLagDegAds = 1.5`.
  When the aim turns faster than the spring, the view is dragged along at the edge.
- Turning fast makes the lag spring stiffer: frequency × `1 + turnStiffening *
  min(turnRate / 6 rad/s, 1)` with `turnStiffening = 0.6`, so flicks do not leave the
  view far behind for long.
- Position shift: while lagging, the view slides toward the side it lags on, up to
  `lagShift = 0.06` studs at the cone edge (hip), `0.015` at full ADS. Moving shifts the
  view against the move direction by up to `moveShift = 0.05` studs (sideways) and
  `0.03` (forward/back).
- Aim kicks (recoil, script turns) are not lag: a kick turns the view at once by the
  same amount (the spring's state is rebased), and it is not counted as mouse speed.

### 2. Head motion

All head motion is a camera-local position offset plus a rotation.

- **Stride bob** from the foot phase (0 = left contact, 0.5 = right contact, one unit
  per full stride):
  - vertical: down at each contact, `bobDown = 0.07` studs at walk; twice per stride.
  - lateral: sways toward the planted foot, `bobSide = 0.05` studs; once per stride.
  - nod: small pitch down at each contact, `bobNodDeg = 0.35`.
  - Amplitudes scale with gait: × speed/walkSpeed up to walk, rising to `sprintBobScale =
    2.2` at sprint speed. ADS scales them by `bobAds = 0.4`. Airborne fades them out.
  - When the foot phase stops (the character stops), the bob eases to rest over about
    0.25 s instead of snapping.
  - Horizontal bob offsets are shortened before walls: the controller casts sideways,
    and the bob keeps `wallMargin = 0.25` studs of clearance.
- **Strafe roll:** strafing right rolls the view right, `strafeRollDeg = 1.8`.
- **Turn roll:** turning rolls the view into the turn, `turnRollDeg = 2.5` at 4 rad/s and
  above, soft-limited. Both rolls go through one spring (`rollHz = 3`).
- **Sprint lean:** sprinting pitches the view down `sprintPitchDeg = 2` and lowers it
  `sprintDrop = 0.12` studs, eased in over ~0.35 s. Entering a sprint adds a one-shot
  forward dip (`sprintLunge = 0.18` studs forward and 0.06 down, peaking at 0.15 s and
  gone by 0.6 s).
- **Landing dip:** on landing, the view drops by `landDrop = 0.12` × (fall speed / 50
  studs/s, at most 2) and springs back (`landHz = 4`, slightly underdamped, ζ = 0.6).
- **Idle micro-motion:** none; that belongs to the Presentation package (handheld shake
  and sway).

### 3. Weapon motion (viewmodel offset)

The output is a camera-local CFrame offset for the viewmodel plus a separate wrist
rotation (applied about the grip). The view is not affected.

- **Walk lowering:** walking forward (not sprinting, not aiming) lowers the weapon by
  `lowerDrop = 0.12` studs, pulls it back `lowerBack = 0.08`, and tilts the muzzle down
  `lowerPitchDeg = 7`, eased in over ~0.6 s and out over ~0.25 s. Strafing gives half,
  walking backward none. Firing suppresses it for `lowerHoldAfterShot = 0.8` s; aiming
  suppresses it while aiming and for 0.2 s after.
- **Holster (safe) offset:** when the game reports the weapon as lowered/safe, drop
  `holsterDrop = 0.25` and pitch down `holsterPitchDeg = 25`.
- **Fire kick:** each shot adds an impulse to a 3-D position spring and a 3-D rotation
  spring (`kickHz = 7`, ζ = 0.75): back `kickBack = 0.05`, up `kickUp = 0.015`, muzzle up
  `kickPitchDeg = 2.2`, roll `kickRollDeg = 0.8` with a random sign. The shot's power
  scales it; ADS multiplies by `kickAds = 0.35`. Offsets are capped (`kickMaxBack =
  0.25`, `kickMaxPitchDeg = 10`).
- **Wrist roll:** fast horizontal mouse turns roll the weapon against the turn up to
  `wristRollDeg = 9`; vertical turns pitch it with the turn up to `wristPitchDeg = 5`.
  Rates are smoothed (`rateHz = 12`), mapped with a soft limit (full at 6 rad/s), and
  driven through a spring (`wristHz = 4`). ADS scales by `wristAds = 0.3`.
- **Weapon profile:** games can set a per-weapon hip offset (position studs, rotation
  degrees) that blends in at `profileRate = 8`/s and fades out with ADS.

### 4. Movement inertia

- Input: target planar velocity (studs/s) from the controls. Output: eased velocity.
- Speeding up takes `accelTime = 0.22` s to reach about 95 %, slowing down
  `decelTime = 0.16` s, a full stop `stopTime = 0.12` s. Reversing direction adds up to
  `turnPenalty = 0.5` × the time.
- The eased speed never exceeds the target speed. Bypassed while crouched, climbing,
  in the air, or (optionally) aiming.
- Also reports `speedFraction` (eased / target, 0..1).

### 5. Stride clock

- Advances a foot phase from planar speed: one full stride every `strideLength = 4.6`
  studs at walk speed, lengthening to `6.8` studs at sprint speed (linear in between).
  Returns `nil` when not grounded or slower than 0.5 studs/s.
- Games with footstep animation markers can supply their own phase instead.

## Settings

`Settings.default()` returns a nested table: `lookLag`, `head`, `weapon`, `inertia`,
`stride`, each with `enabled` and the values above (names as written above, camelCase).
`Settings.set(config, "group.name", value)` validates the type, clamps numbers to the
registered range, and returns success. `Settings.list()` returns `{ path, kind, default,
min, max }` for building an options menu.

Presets via `Settings.applyPreset(config, name)`:

| Preset | Effect |
| --- | --- |
| `"Off"` | every feature disabled |
| `"Subtle"` | all on, amplitudes × 0.5, lag cone × 0.5 |
| `"Default"` | the defaults above |
| `"Strong"` | amplitudes × 1.5, lag cone × 1.5 (still clamped to ranges) |

## Controller (Roblox adapter) contract

Other packages (FPSys, FPSController, Presentation, the example client) rely on exactly
this interface. Names and meanings must match.

```text
Bodycam.controller() -> Controller          (module, loaded on demand)
Bodycam.aim() -> Aim                         (helper module)
Controller.new(options) -> controller
  options: viewmodel (Model with PrimaryPart, optional), viewmodelOffset (CFrame),
           gripPivot (Vector3 studs, camera-local), walkSpeed, sprintSpeed (studs/s),
           isSprinting(), isCrouching(), isLowered(), isLeaning() (functions -> bool),
           footPhase() (function -> number?), applyMovementInertia (bool),
           config (Settings table), studsPerMetre, rayParams
controller:start() / controller:stop()
controller:setAiming(active, blendSpeed?) / controller:setAdsBlend(blend)
controller.ads -> { active: bool, blend: number }
controller:notifyShot(power?)
controller:setWeaponProfile(pos {x,y,z studs}, rot {x,y,z degrees}, blendSpeed?)
controller:clearWeaponProfile(blendSpeed?)
controller:applyPreset(name)
controller:kickAim(yaw, pitch)               -- radians; applied before the camera scripts
controller:getAimCFrame() / getViewCFrame() / getHudCFrame()
controller:addBodyModifier(fn(info) -> CFrame?) -> remover
controller:addViewModifier(fn(view, info) -> CFrame) -> remover
controller:addViewmodelModifier(fn(localCFrame, info) -> CFrame) -> remover
controller:onFootstep(fn(side "Left"|"Right", info)) -> disconnect
controller:featureEnabled(name) -> bool      -- "LookLag", "HeadBob", "HeadRoll", ...
controller.config, controller.studsPerMetre, controller.options, controller.frameInfo
controller.core                              -- BodycamCore (for advanced users)
```

`info` (per frame, also `controller.frameInfo`): `dt, aim (CFrame), yaw, pitch,
humanoid, rootPart, character, grounded, sprinting, crouching, planarSpeed (studs/s),
footPhase, ads (blend), aiming (bool), studsPerMetre, state (HumanoidStateType)`.

### Frame order

1. `RenderPriority.Camera - 1`: restore the camera to last frame's **aim** CFrame (apply
   any pending `kickAim` first and rebase the look lag), so Roblox's camera scripts never
   read the bodycam view back.
2. Camera scripts run (priority Camera).
3. `RenderPriority.Camera + 1`: read the new aim, run the core, then:
   - `view = aim` turned by the look lag, offset by the lag shift and head motion.
   - `hud = aim` offset by head motion position and head roll only (not the lag), so
     the weapon stays on the aim while the view trails.
   - body modifiers: `offset = fn(info)`; `view = view * offset`; `hud = hud * offset`.
   - view modifiers: `view = fn(view, info)`.
   - set `Camera.CFrame = view` when it differs from the aim.
   - viewmodel: `local = weaponOffset * wrist turn about gripPivot * viewmodelOffset`,
     then each viewmodel modifier, then `viewmodel:PivotTo(hud * local)`.
   - footstep events: when the foot phase passes 0 (Left) or 0.5 (Right).
4. Respawn or a camera jump of more than 50 studs resets all motion.

Shots must use `getAimCFrame()` (or a weapon system's own barrel), never the view.

## Tests (required)

Outside Roblox with `roblox/tools/luau-runner` and the mocks in `roblox/tests/mocks`:

- Look lag: holds within the dead band; bounded by the cone after a snap; settles to
  the aim; ADS tightens it; a kick moves the view by the kick at once.
- Head motion: bob period matches the stride (two vertical dips per phase unit), scales
  with speed, rests when stopped; roll signs; sprint lean eases in; landing dip returns.
- Weapon motion: lowering only when walking forward; suppressed after a shot and while
  aiming; kick direction and settling; wrist roll sign against the turn; caps hold.
- Inertia: eases in, never overshoots, bypass rules.
- Settings: clamping, unknown names, presets.
- Controller on the mocked client: the aim is restored before the camera scripts; the
  view differs from the aim while turning; the viewmodel hangs from the HUD camera;
  footstep events fire while walking; `kickAim` reaches the camera scripts without
  counting as lag.
