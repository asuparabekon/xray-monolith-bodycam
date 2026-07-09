#pragma once

#include <cstdint>

namespace Bodycam
{
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
	bool vm_enable = true;
	bool lower_enable = true;
	bool impulse_debug = false;
	bool lower_disable_in_combat = true;
	float layer_vm_weight = 1.f;
	float layer_lower_weight = 1.f;
};

struct SimulationCameraModeSettings
{
	float inner_gain = 0.04f;
	float spring_freq = 7.f;
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
	float mouse_pos = 0.018f;
	float mouse_rot = 2.5f;
	float max_pos = 0.035f;
	float max_rot = 5.f;
	float ads_mouse_mult = 0.18f;
	float ads_move_mult = 0.18f;
	float ads_impulse_mult = 0.18f;
	float move_pos = 0.018f;
	float move_rot = 1.8f;
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
	float y = -0.1f;
	float z = 0.1f;
	float holster_offset = 0.015f;
	float slow_walk = 0.5f;
	float walk = 1.f;
	float move = 1.f;
	float fire_timeout = 0.03f;
	float aim_timeout = 0.005f;
	float speed = 0.4f;
	float return_speed = 0.4f;
	float combat_timeout = 2.f;
};

struct SimulationSettings
{
	SimulationFeatureSettings features;
	SimulationCameraSettings camera;
	SimulationViewmodelSettings viewmodel;
	SimulationImpulseSettings impulse;
	SimulationSprintSettings sprint;
	SimulationLoweringSettings lowering;
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
};

struct SimulationCameraState
{
	bool initialized = false;
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	float prev_target_yaw = 0.f;
	float prev_target_pitch = 0.f;
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
	SVec3 move_intent;
	SVec3 impulse_pos;
	SVec3 impulse_rot;
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

struct SimulationState
{
	SimulationCameraState camera;
	SimulationViewmodelState viewmodel;
	SimulationLoweringState lowering;
	SimulationSprintImpulseState sprint_impulse;
	SimulationSprintState sprint;
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
	bool impulse_pos_clamped = false;
	bool impulse_rot_clamped = false;
};

void ResetSimulation(SimulationState& state, float yaw, float pitch, std::uint32_t move_flags, float ads_blend);
void UpdateSimulation(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output);
void AddFireImpulse(const SimulationSettings& settings, SimulationState& state, float power, bool ads);
bool AddNamedImpulse(const SimulationSettings& settings, SimulationState& state, const char* kind, float power, bool ads);
float DegToRad(float value);
float RadToDeg(float value);
} // namespace Bodycam
