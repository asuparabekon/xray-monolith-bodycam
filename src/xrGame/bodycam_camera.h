#pragma once

class CActor;
class CCameraBase;
struct lua_State;

#include "bodycam_simulation.h"
#include "bodycam_mouse_input.h"
#include "bodycam_pip_adapter.h"
#include "bodycam_viewmodel_profile.h"

namespace Bodycam
{
struct UpdateInput
{
	float target_yaw = 0.f;
	float target_pitch = 0.f;
	float dt = 0.f;
	u32 mstate = 0;
	bool ads = false;
	float ads_blend = 0.f;
	bool weapon_lowered = false;
	bool combat = false;
	bool firearm_equipped = true;
	float actor_speed_fraction = 0.f;
};

struct VisualOutput
{
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	Fvector pos = { 0.f, 0.f, 0.f };
	float fov_offset = 0.f;
};

struct ArmPose
{
	bool active = false;
	bool stalker2_active = false;
	Fvector clavicle = { 0.f, 0.f, 0.f };
	Fvector upperarm = { 0.f, 0.f, 0.f };
	Fvector forearm = { 0.f, 0.f, 0.f };
	Fvector twist = { 0.f, 0.f, 0.f };
	Fvector hand = { 0.f, 0.f, 0.f };
	Fvector left_clavicle = { 0.f, 0.f, 0.f };
	Fvector left_upperarm = { 0.f, 0.f, 0.f };
	Fvector left_forearm = { 0.f, 0.f, 0.f };
	Fvector left_twist = { 0.f, 0.f, 0.f };
	Fvector left_hand = { 0.f, 0.f, 0.f };
	Fvector stalker2_wrist_rot = { 0.f, 0.f, 0.f };
	Fvector stalker2_arm_follow_rot = { 0.f, 0.f, 0.f };
	float stalker2_movement_weight = 0.f;
	float stalker2_movement_response = kDefaultStalker2MovementResponse;
};

struct DebugSnapshot
{
	bool active = false;
	bool camera_enabled = false;
	bool vm_enabled = false;
	bool lower_enabled = false;
	bool arm_enabled = false;
	bool ads = false;
	u32 mstate = 0;
	float camera_yaw = 0.f;
	float camera_pitch = 0.f;
	float camera_roll = 0.f;
	Fvector camera_pos = { 0.f, 0.f, 0.f };
	Fvector vm_pos = { 0.f, 0.f, 0.f };
	Fvector vm_rot = { 0.f, 0.f, 0.f };
	float movement_target_speed = 0.f;
	float movement_actual_speed = 0.f;
	float movement_speed_fraction = 0.f;
	float lower_target = 0.f;
	float lower_amount = 0.f;
	float lower_holster = 0.f;
	Fvector arm_clavicle = { 0.f, 0.f, 0.f };
	Fvector arm_upperarm = { 0.f, 0.f, 0.f };
	Fvector arm_forearm = { 0.f, 0.f, 0.f };
	Fvector arm_twist = { 0.f, 0.f, 0.f };
	Fvector arm_hand = { 0.f, 0.f, 0.f };
};

class CBodycam
{
public:
	bool CameraEnabled() const;
	bool HudEnabled() const;
	bool AnyFeatureEnabled() const;
	void Reset(const CCameraBase* camera, u32 mstate, float ads_blend);
	void AddMouseLookDelta(float yaw_delta, float pitch_delta);
	void RebaseLookDelta(float yaw_delta, float pitch_delta);
	void Update(const UpdateInput& input, VisualOutput& output);
	PipView UpdatePipView(const PipInput& input);
	void AddFireImpulse(float power, bool ads);
	void AddImpulse(LPCSTR kind, float power, bool ads);
	void SetViewmodelProfile(const Fvector& pos, const Fvector& rot, float blend_speed);
	void ClearViewmodelProfile(float blend_speed);
	void Dump(bool ads, u32 mstate) const;
	bool GetHudOffset(Fvector& pos, Fvector& rot) const;
	bool GetArmPose(ArmPose& pose) const;
	void SetMovementDebug(float target_speed, float actual_speed, float speed_fraction);
	void CaptureDebugSnapshot(bool ads, float ads_blend, u32 mstate) const;

private:
	SimulationState m_state;
	PipAdapter m_pip_adapter;
	MouseAimState m_mouse_aim;
	BOOL m_viewmodel_active = FALSE;
	Fvector m_viewmodel_pos = { 0.f, 0.f, 0.f };
	Fvector m_viewmodel_rot = { 0.f, 0.f, 0.f };
	ArmPose m_arm_pose;
	float m_movement_target_speed = 0.f;
	float m_movement_actual_speed = 0.f;
	float m_movement_speed_fraction = 0.f;
	ViewmodelProfileState m_viewmodel_profile;

	void ClearViewmodelOutput();
	void ClearHudOutput();
	void ApplyViewmodelProfile(float dt, float ads_blend);
};

bool GetDebugSnapshot(DebugSnapshot& snapshot);
void RegisterConsoleCommands();
void script_register(lua_State* L);
} // namespace Bodycam
