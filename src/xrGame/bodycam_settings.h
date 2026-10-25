#pragma once

#include "bodycam_camera_rig.h"
#include "bodycam_simulation.h"
#include "bodycam_reference/motion_mode.h"

namespace Bodycam
{
// Each feature owns one settings struct with its own enable flag. The
// simulation features live in SimulationSettings (bodycam_simulation.h), the
// camera features in bodycam_camera_rig.h.

struct RegripSettings
{
	bool enable = true;
	float intensity[4] = { .54f, .6f, .54f, .57f };
};

struct RecoilSettings
{
	bool enable = true;
	float hip = .39f;
	float ads = 1.f;
	// Fire Impulse strength kept while Reference recoil owns the gun kick.
	float fire_impulse_share = .5f;
};

// Two independent arm handling features.
struct HandlingSettings
{
	bool directional_weapon_inertia = true; // The elbows settle after mouse movement.
	bool movement_linked_handling = true; // The weapon moves with acceleration, stops and footsteps.
};

// The gun's step bob through the sprint start; the enable flag is the
// Sprint Transition's (sim.sprint.enable).
struct SprintStartSettings
{
	float gun_bob = 0.15f; // 0..1; 1 is the strong end.
};

struct MovementInertiaSettings
{
	bool enable = true;
	bool disable_ads = true;
	float accel_time = 0.42f; // Seconds to reach the target speed.
	float decel_time = 0.38f;
	float turn_response = 0.55f; // Extra time for a reversal, as a share of the time.
	float stop_response = 0.45f; // Scale on the time of a full stop.
	float sprint_speed_scale = 1.f; // Divides the acceleration time while sprinting.
};

struct ReferenceFeatureSettings
{
	bool movement = true, camera_shake = true, weapon_sway = true;
	bool stress = true, sight_regrip = true;
};

struct RuntimeConfig
{
	ReferenceFeatureSettings reference;
	bool scope_effects_enable = true;
	// Classic features.
	SimulationSettings sim; // Sprint sway, lowering, fire impulse, STALKER 2 arm IK.
	CameraDecouplingSettings camera;
	HeadMotionSettings head;
	FootstepCameraSettings footstep;
	RegripSettings regrip;
	RecoilSettings recoil;
	HandlingSettings handling;
	MovementInertiaSettings movement_inertia;
	SprintStartSettings sprint_start;
};

struct FloatBinding
{
	LPCSTR name;
	LPCSTR console_name;
	float* value;
	float default_value;
	float min_value;
	float max_value;
};

struct BoolBinding
{
	LPCSTR name;
	LPCSTR console_name;
	bool* value;
	bool default_value;
	// The engine's console reads integers; this mirrors value for it.
	int console_value;
};

// Authoritative neutral Reference weapon positions in camera-relative metres.
// +X right, +Y up, +Z forward. Scope variants keep their own section keys.
struct ReferenceWeaponPosition
{
	Fvector hip{}, ads{};
};
const ReferenceWeaponPosition& GetReferenceWeaponPosition(LPCSTR section);
// Sets the baseline a weapon's Reference binding supplies. Saved positions and
// manual previews win over it. Until a baseline exists only the saved endpoints
// are set and the others read as zero: check HasReferenceWeaponPositionBaseline first.
bool SeedReferenceWeaponPosition(LPCSTR section, const ReferenceWeaponPosition& baseline);
const ReferenceWeaponPosition& GetReferenceWeaponPositionBaseline(LPCSTR section);
bool HasReferenceWeaponPositionBaseline(LPCSTR section);
bool RestoreReferenceWeaponPosition(LPCSTR section); // Preview only; Save persists.
bool SetReferenceWeaponPosition(LPCSTR section, bool ads, const Fvector& metres);
bool SaveReferenceWeaponPosition(LPCSTR section);

RuntimeConfig& GetConfig();
BodycamReference::MotionSelection& GetMotionSelection();
// Requests a motion mode (0 Classic, 1 Reference) and keeps bodycam_motion_mode's value equal to it;
// request modes through here rather than GetMotionSelection().Request.
bool RequestMotionMode(int mode);
int* MotionModeConsoleValue();
// The motion mode in effect this frame.
bool ClassicActive();
bool ReferenceActive();
// Reference is active and moves the actor at its own gait speeds.
bool ReferenceMovementActive();
const FloatBinding* GetFloatBindings(u32& count);
BoolBinding* GetBoolBindings(u32& count);
void DumpConfigBindings();
bool FootstepCameraEnabled();
// One toggle: the sprint start presentation and the sprint camera sway.
bool SprintTransitionEnabled();
// The Sprint Transition's gun step bob.
bool SprintBobEnabled();
bool AnyEffectEnabled();
// 0 off, 1 balanced, 2 the shipped defaults.
constexpr int kDefaultPreset = 2;
void ApplyPreset(int preset);
bool GetFloat(LPCSTR name, float& value);
bool SetFloat(LPCSTR name, float value);
bool GetBool(LPCSTR name, bool& value);
bool SetBool(LPCSTR name, bool value);
// The Reticle Editor's inspect zoom (r__reticle_inspect): the HUD drawn this many times closer, 1 = off.
float ReticleInspectZoom();
} // namespace Bodycam
