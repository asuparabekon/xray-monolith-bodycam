# Classic bodycam

Classic mode adds camera and weapon handling on top of GAMMA's own first-person
animations. It does not replace an authored animation; it moves the camera, adds
offsets to the HUD, and solves the arms around the weapon.

Each feature has its own switch and works with the others off. Camera Decoupling
has two lag models, Deadzone and Reference follow
(`camera_continuous_follow_enable`); its tab shows only the selected model's
settings. Camera Decoupling also carries the head roll and shake, which have no
switch of their own. Sprint Transition is one switch for the sprint start, its
gun bob and the sprint camera sway.

## Features

| Feature (UI tab) | Enable setting | What it does | Code |
| --- | --- | --- | --- |
| Camera Decoupling | `camera_decoupling_enable` | The camera trails the aim, inside a deadzone and soft zone (Deadzone) or continuously with the Reference camera lag (Reference follow); its position shifts with the lag and with movement. Includes Reference head roll and shake. | `bodycam_camera_rig.*`, `bodycam_actor_camera.cpp`; follow lag in `bodycam_reference/classic_follow.h` |
| Footstep Camera | `footstep_camera_enable` | Recorded Reference head motion on the native footstep schedule. Replaces native head bob while it moves the view; freelook keeps native bob. | `bodycam_camera_rig.*`, `bodycam_reference/classic_footstep_camera.h` |
| Sprint Transition | `sprint_transition_enable` | Holds the hand-off to the sprint presentation until full sprint speed, adds the sprint camera sway and the gun step bob (below). | `bodycam_sprint_start.h`, `bodycam_actor_movement.cpp`; sway in `bodycam_simulation.*`; gun bob in `bodycam_reference/classic_sprint_bob.h`, `bodycam_camera.cpp` |
| Movement Inertia | `movement_inertia_enable` | Acceleration and deceleration time, with braking strides. | `bodycam_movement_response.*`, `bodycam_actor_movement.cpp` |
| Viewmodel Lowering | `vm_lowering_enable` | Lowers the weapon while walking. | `bodycam_simulation.*` |
| Fire Impulse (Recoil) | `fire_impulse_enable` | A sprung kick of the weapon per shot; the kick shape per unit of power is set by the `fire_impulse_kick_*` settings. | `bodycam_simulation.*` |
| STALKER 2 Style Arm IK | `stalker2_style_arm_ik_enable` | Weapon and hands turn as one controller with the mouse; adds authored walk motion where the weapon's animation has little. | `bodycam_simulation.*`, `bodycam_hud_arms.*` |
| Reference regrip (Regrip) | `classic_regrip_enable` | Reference regrip clips on the Classic rig. | `bodycam_hud_arms.*` |
| Reference recoil (Recoil) | `classic_recoil_enable` | Reference gun recoil on the Classic rig. | `bodycam_hud_arms.*` |
| Mouse-driven arm settling (Weapon Handling) | `directional_weapon_inertia_enable` | The elbows settle after look movement. | `bodycam_hud_arms.*` |
| Movement-linked handling (Weapon Handling) | `movement_linked_handling_enable` | The weapon reacts to steps and velocity changes. | `classic_handling.h`, `bodycam_hud_arms.*` |

### Sprint Transition gun bob

From the entry decision (about 0.1 s after the press) until the hand-off, a
weapon without its own run-to-sprint clip bobs on the native footstep
schedule. The bob is sized by the Reference step cycle at the current speed:
translation, a depth push, and pitch, yaw and roll about the grip (fixed
ratios in `classic_sprint_bob.h`). It fades in over half a step and out over
one step, and resets on a mode or camera change. A start that completes in
the first 0.1 s gets no bob. A weapon switched in during the start makes its
own entry decision.

## Where features meet

- A shot notifies both lowering (recovery timers) and the fire impulse.
- The camera position spring carries the sprint sway, so the sway uses the camera follow speed.
- The Footstep Camera replaces native head bob on the frames it moves the view and silences the sprint camera sway.
- Reference recoil takes a share of the Fire Impulse (`classic_recoil_fire_impulse_share`).
  The two share the Recoil tab and the shot event, nothing else.
- Regrip, recoil and look settling share one Reference source rig (calibration data only).
  Without the regrip clip bank only regrip stops.
- During a weapon action no new regrip clip starts, and a playing one fades out
  over 0.2 s. Recoil and the handling offset settle through the action instead
  of stopping. Recoil moves both hands, including a hand holding a magazine
  during a reload.
- A script animation resets the handling offset and hides the recoil while it
  plays. The recoil keeps settling underneath and shows again afterwards.
- Braking strides belong to Movement Inertia.
- With only Movement Inertia on, the per-actor simulation does not run.

## Scope eye

With Bodycam scope effects, the lag model also chooses the scope eye
(`bodycam_reference_optics.cpp`, `SelectScopeEyeModel`). Under Deadzone the
eye follows a tracked pupil: the camera's offset from the gun's aimed eye is
eased back to the centre. Under Reference follow the scope shows the
current eye, as Reference mode does, so the camera shift with movement, the
sprint sway and the regrip camera move the exit pupil. Camera Decoupling off
counts as Deadzone. Switching models while aimed hands over without a jump:
leaving Deadzone, the tracked eye returns to the current eye at the tracker's
speed; entering it, tracking starts from the current eye. Scopes drawn without
the scope effects (display sights, thermal overlays, MSAA or HDR10, effects
off) keep the stock eye tracking under both models.

## Diagnostics

`r__svp_cop_diag 2` writes these log lines for as long as it is set.

| Tag | Written |
| --- | --- |
| `[BODYCAM-CAMERA]`, `[BODYCAM-VIEWMODEL]`, `[BODYCAM-ARM]` | every frame the simulation runs |
| `[BODYCAM-FOOTSTEP]` | every frame the Footstep Camera runs |
| `[BODYCAM-SPRINT-START]` | while sprinting, and on the frame a held lowering commits |
| `[BODYCAM-SPRINT-BOB]` | every frame the Sprint Transition gun bob moves the gun |
| `[BODYCAM-BRAKE]` | during a braking stride |
| `[BODYCAM-ADS]` | when the aim blend jumps by more than 0.3 in one frame |
| `[BODYCAM-REGRIP]`, `[BODYCAM-REGRIP-CLIP]` | every frame the arm controller solves; one clip line per playing regrip clip |
| `[BODYCAM-RECOIL]` | while firing or while the recoil pose is active |
| `[BODYCAM-SHOT]` | per shot |
| `[BODYCAM-CONTACT]` | every 60 arm solves: the largest hand-to-grip error since the last line |

The code layout and the order the camera is applied in are in [BODYCAM_PORTING.md](BODYCAM_PORTING.md).
