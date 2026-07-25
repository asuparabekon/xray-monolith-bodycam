#pragma once

#include <cstdint>

namespace Bodycam
{
constexpr float kDefaultStalker2MovementResponse = 5.f;

struct SVec3
{
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;

	void Set(float nx, float ny, float nz);
	void Add(const SVec3& value);
	void Add(float nx, float ny, float nz);
	void Sub(const SVec3& value);
	void Mul(float value);
	float Magnitude() const;
	void NormalizeSafe();
};

float ClampSprintBridgeHandoffSpeed(float value);

enum ESimMoveFlags : std::uint32_t
{
	smfForward = 1u << 0,
	smfBack = 1u << 1,
	smfLeft = 1u << 2,
	smfRight = 1u << 3,
	smfCrouch = 1u << 4,
	smfSprint = 1u << 5,
	smfFall = 1u << 6,
	smfJump = 1u << 7,
	smfLanding = 1u << 8,
	smfAnyMove = smfForward | smfBack | smfLeft | smfRight,
};

struct SimulationFeatureSettings
{
	bool camera_enable = true;
	bool vm_enable = false;
	bool lower_enable = true;
	bool bodycam_arm_enable = false;
	bool stalker2_arm_enable = true;
	bool sprint_transition_enable = true;
	bool fire_impulse_enable = true;
	bool impulse_debug = false;
	bool lower_disable_in_combat = true;
	float layer_vm_weight = 1.f;
	float layer_lower_weight = 1.f;
	float layer_arm_weight = 1.f;
};

struct AuthoredMotionMetrics
{
	float lead_rotation = 0.f;
	float lead_translation = 0.f;
	float wrist_rotation = 0.f;
	float forearm_rotation = 0.f;
	float upperarm_rotation = 0.f;
};

struct AuthoredMotionGains
{
	float controller = 0.f;
	float wrist = 0.f;
	float arm = 0.f;
};

struct SimulationCameraModeSettings
{
	float inner_gain = 0.f;
	float spring_freq = 8.f;
	float spring_damping = 0.85f;
	float deadzone_yaw = 1.5f;
	float deadzone_pitch = 1.f;
	float softzone_yaw = 4.f;
	float softzone_pitch = 3.f;
	float max_yaw = 19.f;
	float max_pitch = 19.f;
	float roll = 2.5f;
	float pos = 0.025f;
};

struct SimulationCameraSettings
{
	SimulationCameraModeSettings hip;
	SimulationCameraModeSettings ads = { 0.f, 14.f, 1.f, 0.8f, 0.55f, 1.8f, 1.2f, 3.f, 2.f, 0.6f, 0.006f };
	float move_roll = 3.f;
	float move_pos = 0.020f;
};

struct SimulationViewmodelSettings
{
	float follow_speed = 9.f;
	float damping = 0.78f;
	float mouse_pos = 0.030f;
	float mouse_rot = 5.0f;
	float max_pos = 0.035f;
	float max_rot = 5.f;
	float ads_mouse_mult = 0.18f;
	float ads_impulse_mult = 0.18f;
	float mouse_filter = 18.f;
	float move_filter = 8.f;
	float ads_anchor = 0.65f;
	float ads_anchor_pos = 0.010f;
	float ads_anchor_rot = 1.2f;
};

struct SimulationImpulseSettings
{
	float decay = 8.f;
	float sprint_impulse = 1.f;
	float sprint_start_impulse = 5.f;
	float sprint_stop_impulse = 5.f;
	float sprint_ads_mult = 0.2f;
	float sprint_camera_impulse = 5.f;
	float sprint_fov_impulse = 5.f;
	float sprint_fov_speed = 0.5f;
	float sprint_impulse_speed = 0.2f;
	float ads_impulse = 1.f;
	float land_impulse = 1.f;
	float flick_impulse = 0.f;
	float fire_impulse = 5.f;
	float ads_fire_impulse = 1.35f;
	float impulse_pos_cap = 0.08f;
	float impulse_rot_cap = 8.f;
};

struct SimulationSprintSettings
{
	float strength = 1.5f;
	float smoothness = 1.f;
	float enter_time = 0.32f;
	float exit_time = 0.48f;
	float camera_pitch = -0.25f;
	float camera_roll = 0.25f;
	float camera_pos = 0.004f;
	float bridge_pitch = -8.f;
	float bridge_yaw = -8.f;
	float bridge_roll = -7.7f;
	float bridge_pos = 0.050f;
	float bridge_handoff_speed = 0.90f;
	float accent = 2.f;
};

struct SimulationLoweringSettings
{
	float pitch = 0.f;
	float yaw = 0.f;
	float roll = 0.f;
	float x = 0.f;
	float y = -0.08f;
	float z = 0.1f;
	float holster_offset = 0.015f;
	float slow_walk = 0.5f;
	float walk = 1.f;
	float move = 1.f;
	float fire_timeout = 0.03f;
	float aim_timeout = 0.005f;
	float speed = 0.05f;
	float return_speed = 0.4f;
	float combat_timeout = 2.f;
};

struct SimulationArmSettings
{
	float strength = 3.f;
	float response = 30.f;
	float ads_scale = 1.f;
	float mouse_pitch = 30.f;
	float mouse_yaw = 30.f;
	float mouse_roll = 45.f;
	float secondary_roll = 20.f;
	float hand_scale = 0.f;
	float upperarm_scale = 0.05f;
	float forearm_scale = 0.1f;
	float twist_scale = 2.f;
};

struct SimulationStalker2ArmSettings
{
	float strength = 1.5f;
	float response = 20.f;
	float arm_follow_response = 0.1f;
	float arm_follow_scale = 0.f;
	float ads_scale = 0.2f;
	float mouse_strength = 1.f;
	float mouse_sensitivity = 1.5f;
	float mouse_max_yaw = 0.f;
	float mouse_max_pitch = 18.f;
	float mouse_max_roll = 15.5f;
	float movement_strength = 1.f;
	float movement_response = kDefaultStalker2MovementResponse;
	float slow_walk_scale = 0.5f;
	float mouse_pitch = 18.f;
	float mouse_yaw = 0.f;
	float mouse_roll = 28.f;
	float wrist_scale = 1.f;
};

struct SimulationSettings
{
	SimulationFeatureSettings features;
	SimulationCameraSettings camera;
	SimulationViewmodelSettings viewmodel;
	SimulationImpulseSettings impulse;
	SimulationSprintSettings sprint;
	SimulationLoweringSettings lowering;
	SimulationArmSettings bodycam_arm;
	SimulationStalker2ArmSettings stalker2_arm;
};

struct SimulationInput
{
	float target_yaw = 0.f;
	float target_pitch = 0.f;
	float dt = 0.f;
	std::uint32_t move_flags = 0;
	bool ads = false;
	float ads_blend = 0.f;
	bool weapon_lowered = false;
	bool combat = false;
	bool accelerated = false;
	bool firearm_equipped = true;
	float actor_speed_fraction = 0.f;
	bool visual_aim_available = false;
	float visual_aim_yaw = 0.f;
	float visual_aim_pitch = 0.f;
};

struct SimulationCameraState
{
	bool initialized = false;
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	SVec3 pos;
	SVec3 impulse_pos;
	float impulse_roll = 0.f;
	float impulse_fov = 0.f;
	float impulse_fov_target = 0.f;
};

struct SimulationViewmodelState
{
	SVec3 pos;
	SVec3 rot;
	SVec3 mouse_speed;
	SVec3 prev_mouse_speed;
	SVec3 mouse_accel;
	float mouse_aim_yaw = 0.f;
	float mouse_aim_pitch = 0.f;
	bool mouse_aim_initialized = false;
	SVec3 move_intent;
	SVec3 impulse_pos;
	SVec3 impulse_rot;
	SVec3 fire_impulse_pos;
	SVec3 fire_impulse_rot;
	SVec3 fire_pos;
	SVec3 fire_rot;
	float airborne_time = 0.f;
};

struct SimulationLoweringState
{
	float amount = 0.f;
	float target = 0.f;
	float holster = 0.f;
	float fire_recovery = 0.f;
	float ads_recovery = 0.f;
	float combat_timer = 0.f;
	SVec3 pos;
	SVec3 rot;
};

struct SimulationSprintImpulseState
{
	SVec3 camera_pos;
	float camera_roll = 0.f;
};

struct SimulationSprintState
{
	float amount = 0.f;
	float target = 0.f;
	float viewmodel_amount = 0.f;
	float settle = 0.f;
	float phase = 0.f;
	float prev_amount = 0.f;
};

struct SimulationArmState
{
	SVec3 clavicle;
	SVec3 upperarm;
	SVec3 forearm;
	SVec3 twist;
	SVec3 hand;
	SVec3 left_clavicle;
	SVec3 left_upperarm;
	SVec3 left_forearm;
	SVec3 left_twist;
	SVec3 left_hand;
	SVec3 controller;
	SVec3 prev_controller;
	SVec3 settle;
	SVec3 clavicle_vel;
	SVec3 upperarm_vel;
	SVec3 forearm_vel;
	SVec3 twist_vel;
	SVec3 hand_vel;
	SVec3 left_clavicle_vel;
	SVec3 left_upperarm_vel;
	SVec3 left_forearm_vel;
	SVec3 left_twist_vel;
	SVec3 left_hand_vel;
	float weight = 0.f;
	float motion_weight = 0.f;
};

struct SimulationStalker2ArmState
{
	SVec3 wrist_rot;
	SVec3 wrist_rot_vel;
	SVec3 arm_follow_rot;
	SVec3 arm_follow_rot_vel;
	float weight = 0.f;
	float movement_weight = 0.f;
};

struct AdsState
{
	bool active = false;
	float blend = 0.f;
};

struct SimulationState
{
	SimulationCameraState camera;
	SimulationViewmodelState viewmodel;
	SimulationLoweringState lowering;
	SimulationSprintImpulseState sprint_impulse;
	SimulationSprintState sprint;
	SimulationArmState arm;
	SimulationStalker2ArmState stalker2_arm;
	float ads_blend = 0.f;
	std::uint32_t prev_move_flags = 0;
	bool prev_ads = false;
};

struct SimulationOutput
{
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	SVec3 camera_pos;
	float fov_offset = 0.f;
	float lower_amount = 0.f;
	bool viewmodel_active = false;
	SVec3 viewmodel_pos;
	SVec3 viewmodel_rot;
	bool arm_active = false;
	SVec3 arm_clavicle;
	SVec3 arm_upperarm;
	SVec3 arm_forearm;
	SVec3 arm_twist;
	SVec3 arm_hand;
	SVec3 arm_left_clavicle;
	SVec3 arm_left_upperarm;
	SVec3 arm_left_forearm;
	SVec3 arm_left_twist;
	SVec3 arm_left_hand;
	bool stalker2_arm_active = false;
	SVec3 stalker2_wrist_rot;
	SVec3 stalker2_arm_follow_rot;
	float stalker2_movement_weight = 0.f;
	float stalker2_movement_response = kDefaultStalker2MovementResponse;
	bool impulse_pos_clamped = false;
	bool impulse_rot_clamped = false;
};

void ResetSimulation(SimulationState& state, float yaw, float pitch, std::uint32_t move_flags, float ads_blend);
void RebaseSimulationLook(SimulationState& state, float yaw_delta, float pitch_delta);
AdsState ResolveAdsState(bool weapon_zoomed, float weapon_blend);

void UpdateSimulation(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output);
void AddFireImpulse(const SimulationSettings& settings, SimulationState& state, float power, bool ads);
bool AddNamedImpulse(const SimulationSettings& settings, SimulationState& state, const char* kind, float power, bool ads);
float ArmCorrectionAngle(float dot, float cross_magnitude);
SVec3 SolveArmMidpoint(const SVec3& start, const SVec3& end, const SVec3& current_mid, const SVec3& desired_mid);
AuthoredMotionGains CalculateAuthoredMotionGains(const AuthoredMotionMetrics& metrics);
SVec3 CalculateAuthoredWalkRotation(float phase, float weight, const AuthoredMotionGains& gains);
SVec3 CalculateAuthoredWalkTranslation(float phase, float weight, const AuthoredMotionGains& gains);
float CalculateAuthoredArmFollow(float arm_gain);
SVec3 CalculateStalker2MouseControllerRotation(float yaw_throw, float yaw_scale, float roll_scale, float weight);
SVec3 CalculateStalker2VerticalArmFollow(float pitch_throw, float pitch_scale, float weight);
float CalculateMouseThrow(float angular_speed, float full_scale_speed, float sensitivity);
float SoftLimitMouseResponse(float value, float limit);
SVec3 ClampStalker2MouseRotation(const SVec3& rotation, float max_yaw, float max_pitch, float max_roll);
float CalculateStalker2MovementAmount(float move_intent, float speed_fraction, bool accelerated, float slow_walk_scale);
float DegToRad(float value);
float RadToDeg(float value);
} // namespace Bodycam
