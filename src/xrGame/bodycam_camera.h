#pragma once

class CActor;
class CCameraBase;
struct lua_State;

#include "bodycam_camera_rig.h"
#include "bodycam_simulation.h"
#include "bodycam_mouse_input.h"
#include "bodycam_pip_adapter.h"
#include "bodycam_viewmodel_profile.h"
#include "bodycam_reference/classic_sprint_bob.h"

namespace Bodycam
{
// STALKER 2 style arm controller for the HUD rig, radians.
struct ArmPose
{
	bool active = false;
	Fvector wrist_rot = { 0.f, 0.f, 0.f };
	Fvector arm_follow_rot = { 0.f, 0.f, 0.f };
	float movement_weight = 0.f;
	float movement_response = kDefaultStalker2MovementResponse;
};

// The actor's state for one frame, read once from the engine: the camera rig's
// input and what the simulation needs besides it.
struct ActorFrame
{
	CameraRigInput camera; // Without the movement inertia speed, which CBodycam keeps.
	std::uint32_t move_flags = 0; // ESimMoveFlags
	bool accelerated = false;
	bool weapon_lowered = false;
	bool combat = false;
	bool firearm_equipped = true;
	// The Sprint Transition's gun step bob; gathered only while it is held or fading.
	BodycamReference::SprintBobInput sprint_bob;
};

// The Classic simulation and camera rig of one actor, and what they publish to
// the camera and the HUD.
class CBodycam
{
public:
	bool AnyFeatureEnabled() const;
	void Reset(const CCameraBase* camera, float ads_blend);
	// Deltas of the camera's yaw and pitch (CCameraBase angles, radians).
	void AddMouseLookDelta(float yaw_delta, float pitch_delta);
	void RebaseLookDelta(float yaw_delta, float pitch_delta);
	const CameraComposition& Update(const ActorFrame& frame, CameraRigHost& host);
	const ClassicCameraRig& CameraRig() const { return m_camera_rig; }
	PipView UpdatePipView(const PipInput& input);
	void NotifyShot(float power, bool ads);
	void SetViewmodelProfile(const Fvector& pos, const Fvector& rot, float blend_speed);
	void ClearViewmodelProfile(float blend_speed);
	void Dump(bool ads, u32 mstate) const;
	// lowering_weight 0..1 is the share of the lowering offset that is kept.
	bool GetHudOffset(Fvector& pos, Fvector& rot, float lowering_weight) const;
	bool GetArmPose(ArmPose& pose) const;
	// Speeds in m/s and the filtered speed over its target, from Movement Inertia.
	void SetMovementSpeed(float target_speed, float filtered_speed, float speed_fraction);
	bool SprintBobActive() const { return m_sprint_bob.Output().active; }

private:
	void ClearViewmodelOutput();
	void ClearHudOutput();
	void ApplyViewmodelProfile(float dt, float ads_blend);
	void ApplySprintBob(BodycamReference::SprintBobInput input);

	SimulationState m_state;
	ClassicCameraRig m_camera_rig;
	PipAdapter m_pip_adapter;
	MouseAimState m_mouse_aim;
	bool m_viewmodel_active = false;
	Fvector m_viewmodel_pos = { 0.f, 0.f, 0.f };
	Fvector m_viewmodel_rot = { 0.f, 0.f, 0.f };
	Fvector m_lowering_pos = { 0.f, 0.f, 0.f };
	Fvector m_lowering_rot = { 0.f, 0.f, 0.f };
	ArmPose m_arm_pose;
	float m_movement_target_speed = 0.f;
	float m_movement_filtered_speed = 0.f;
	float m_movement_speed_fraction = 0.f;
	ViewmodelProfileState m_viewmodel_profile;
	BodycamReference::ClassicSprintBob m_sprint_bob;
};

// The level's current entity when it is an actor; null otherwise and outside a level.
CActor* CurrentActor();
// Logs the bodycam state of the current actor.
void DumpState();
void RegisterConsoleCommands();
void script_register(lua_State* L);
} // namespace Bodycam
