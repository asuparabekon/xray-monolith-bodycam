#include "stdafx.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"

namespace Bodycam
{
static RuntimeConfig g_bodycam_config;
DebugSnapshot g_bodycam_debug_snapshot;

RuntimeConfig& GetConfig()
{
	return g_bodycam_config;
}

static FloatBinding g_float_bindings[] = {
	// Hip-fire camera response.
	{ "hip_camera_inner_zone_response", "bodycam_hip_camera_inner_zone_response", &g_bodycam_config.camera.hip.inner_gain, 0.f, 1.f },
	{ "hip_camera_follow_speed", "bodycam_hip_camera_follow_speed", &g_bodycam_config.camera.hip.spring_freq, 0.1f, 30.f },
	{ "hip_camera_follow_damping", "bodycam_hip_camera_follow_damping", &g_bodycam_config.camera.hip.spring_damping, 0.f, 3.f },
	{ "hip_camera_deadzone_yaw", "bodycam_hip_camera_deadzone_yaw", &g_bodycam_config.camera.hip.deadzone_yaw, 0.f, 30.f },
	{ "hip_camera_deadzone_pitch", "bodycam_hip_camera_deadzone_pitch", &g_bodycam_config.camera.hip.deadzone_pitch, 0.f, 30.f },
	{ "hip_camera_soft_follow_yaw", "bodycam_hip_camera_soft_follow_yaw", &g_bodycam_config.camera.hip.softzone_yaw, 0.f, 45.f },
	{ "hip_camera_soft_follow_pitch", "bodycam_hip_camera_soft_follow_pitch", &g_bodycam_config.camera.hip.softzone_pitch, 0.f, 45.f },
	{ "hip_camera_max_yaw_offset", "bodycam_hip_camera_max_yaw_offset", &g_bodycam_config.camera.hip.max_yaw, 0.f, 90.f },
	{ "hip_camera_max_pitch_offset", "bodycam_hip_camera_max_pitch_offset", &g_bodycam_config.camera.hip.max_pitch, 0.f, 90.f },
	{ "hip_camera_roll_scale", "bodycam_hip_camera_roll_scale", &g_bodycam_config.camera.hip.roll, 0.f, 20.f },
	{ "hip_camera_position_scale", "bodycam_hip_camera_position_scale", &g_bodycam_config.camera.hip.pos, 0.f, 0.25f },

	// ADS camera response.
	{ "ads_camera_inner_zone_response", "bodycam_ads_camera_inner_zone_response", &g_bodycam_config.camera.ads.inner_gain, 0.f, 1.f },
	{ "ads_camera_follow_speed", "bodycam_ads_camera_follow_speed", &g_bodycam_config.camera.ads.spring_freq, 0.1f, 30.f },
	{ "ads_camera_follow_damping", "bodycam_ads_camera_follow_damping", &g_bodycam_config.camera.ads.spring_damping, 0.f, 3.f },
	{ "ads_camera_deadzone_yaw", "bodycam_ads_camera_deadzone_yaw", &g_bodycam_config.camera.ads.deadzone_yaw, 0.f, 30.f },
	{ "ads_camera_deadzone_pitch", "bodycam_ads_camera_deadzone_pitch", &g_bodycam_config.camera.ads.deadzone_pitch, 0.f, 30.f },
	{ "ads_camera_soft_follow_yaw", "bodycam_ads_camera_soft_follow_yaw", &g_bodycam_config.camera.ads.softzone_yaw, 0.f, 45.f },
	{ "ads_camera_soft_follow_pitch", "bodycam_ads_camera_soft_follow_pitch", &g_bodycam_config.camera.ads.softzone_pitch, 0.f, 45.f },
	{ "ads_camera_max_yaw_offset", "bodycam_ads_camera_max_yaw_offset", &g_bodycam_config.camera.ads.max_yaw, 0.f, 90.f },
	{ "ads_camera_max_pitch_offset", "bodycam_ads_camera_max_pitch_offset", &g_bodycam_config.camera.ads.max_pitch, 0.f, 90.f },
	{ "ads_camera_roll_scale", "bodycam_ads_camera_roll_scale", &g_bodycam_config.camera.ads.roll, 0.f, 20.f },
	{ "ads_camera_position_scale", "bodycam_ads_camera_position_scale", &g_bodycam_config.camera.ads.pos, 0.f, 0.25f },

	// Viewmodel lag and ADS anchoring.
	{ "vm_spring_speed", "bodycam_vm_spring_speed", &g_bodycam_config.viewmodel.follow_speed, 0.1f, 30.f },
	{ "vm_spring_damping", "bodycam_vm_spring_damping", &g_bodycam_config.viewmodel.damping, 0.f, 3.f },
	{ "vm_mouse_position_scale", "bodycam_vm_mouse_position_scale", &g_bodycam_config.viewmodel.mouse_pos, 0.f, 0.25f },
	{ "vm_mouse_rotation_scale", "bodycam_vm_mouse_rotation_scale", &g_bodycam_config.viewmodel.mouse_rot, 0.f, 30.f },
	{ "vm_max_position_offset", "bodycam_vm_max_position_offset", &g_bodycam_config.viewmodel.max_pos, 0.f, 0.35f },
	{ "vm_max_rotation_offset", "bodycam_vm_max_rotation_offset", &g_bodycam_config.viewmodel.max_rot, 0.f, 45.f },
	{ "vm_ads_mouse_scale", "bodycam_vm_ads_mouse_scale", &g_bodycam_config.viewmodel.ads_mouse_mult, 0.f, 1.f },
	{ "vm_ads_movement_scale", "bodycam_vm_ads_movement_scale", &g_bodycam_config.viewmodel.ads_move_mult, 0.f, 1.f },
	{ "vm_ads_impulse_scale", "bodycam_vm_ads_impulse_scale", &g_bodycam_config.viewmodel.ads_impulse_mult, 0.f, 1.f },
	{ "vm_ads_sight_anchor_strength", "bodycam_vm_ads_sight_anchor_strength", &g_bodycam_config.viewmodel.ads_anchor, 0.f, 1.f },
	{ "vm_ads_anchor_position_scale", "bodycam_vm_ads_anchor_position_scale", &g_bodycam_config.viewmodel.ads_anchor_pos, 0.f, 0.25f },
	{ "vm_ads_anchor_rotation_scale", "bodycam_vm_ads_anchor_rotation_scale", &g_bodycam_config.viewmodel.ads_anchor_rot, 0.f, 30.f },

	// Movement sway added on top of mouse response.
	{ "movement_camera_roll_scale", "bodycam_movement_camera_roll_scale", &g_bodycam_config.camera.move_roll, 0.f, 20.f },
	{ "movement_camera_position_scale", "bodycam_movement_camera_position_scale", &g_bodycam_config.camera.move_pos, 0.f, 0.25f },
	{ "vm_movement_position_scale", "bodycam_vm_movement_position_scale", &g_bodycam_config.viewmodel.move_pos, 0.f, 0.25f },
	{ "vm_movement_rotation_scale", "bodycam_vm_movement_rotation_scale", &g_bodycam_config.viewmodel.move_rot, 0.f, 30.f },
	{ "vm_mouse_smoothing_speed", "bodycam_vm_mouse_smoothing_speed", &g_bodycam_config.viewmodel.mouse_filter, 0.1f, 60.f },
	{ "vm_movement_smoothing_speed", "bodycam_vm_movement_smoothing_speed", &g_bodycam_config.viewmodel.move_filter, 0.1f, 60.f },

	// Actor movement response. Speed mods own the target speed; Bodycam owns time-to-target.
	{ "movement_acceleration_time", "bodycam_movement_acceleration_time", &g_bodycam_config.movement.accel_time, 0.f, 2.f },
	{ "movement_deceleration_time", "bodycam_movement_deceleration_time", &g_bodycam_config.movement.decel_time, 0.f, 2.f },
	{ "movement_turn_response", "bodycam_movement_turn_response", &g_bodycam_config.movement.turn_response, 0.f, 1.f },
	{ "movement_stop_response", "bodycam_movement_stop_response", &g_bodycam_config.movement.stop_response, 0.f, 2.f },
	{ "movement_sprint_speed_scale", "bodycam_movement_sprint_speed_scale", &g_bodycam_config.movement.sprint_mult, 0.1f, 3.f },

	// Sprint locomotion layer.
	{ "sprint_transition_vm_blend_scale", "bodycam_sprint_transition_vm_blend_scale", &g_bodycam_config.sprint.strength, 0.f, 2.f },
	{ "sprint_transition_blend_smoothing", "bodycam_sprint_transition_blend_smoothing", &g_bodycam_config.sprint.smoothness, 0.f, 1.f },
	{ "sprint_transition_start_stop_impulse_scale", "bodycam_sprint_transition_start_stop_impulse_scale", &g_bodycam_config.sprint.accent, 0.f, 2.f },
	{ "sprint_transition_vm_pitch", "bodycam_sprint_transition_vm_pitch", &g_bodycam_config.sprint.bridge_pitch, -12.f, 12.f },
	{ "sprint_transition_vm_yaw", "bodycam_sprint_transition_vm_yaw", &g_bodycam_config.sprint.bridge_yaw, -12.f, 12.f },
	{ "sprint_transition_vm_roll", "bodycam_sprint_transition_vm_roll", &g_bodycam_config.sprint.bridge_roll, -12.f, 12.f },
	{ "sprint_transition_vm_position", "bodycam_sprint_transition_vm_position", &g_bodycam_config.sprint.bridge_pos, 0.f, 0.10f },
	{ "sprint_transition_animation_handoff_speed", "bodycam_sprint_transition_animation_handoff_speed", &g_bodycam_config.sprint.bridge_handoff_speed, 0.35f, 1.f },

	// One-shot impulses from gameplay events.
	{ "sprint_transition_impulse", "bodycam_sprint_transition_impulse", &g_bodycam_config.impulse.sprint_impulse, 0.f, 5.f },
	{ "sprint_start_impulse", "bodycam_sprint_start_impulse", &g_bodycam_config.impulse.sprint_start_impulse, 0.f, 5.f },
	{ "sprint_stop_impulse", "bodycam_sprint_stop_impulse", &g_bodycam_config.impulse.sprint_stop_impulse, 0.f, 5.f },
	{ "sprint_impulse_ads_scale", "bodycam_sprint_impulse_ads_scale", &g_bodycam_config.impulse.sprint_ads_mult, 0.f, 1.f },
	{ "sprint_impulse_camera_scale", "bodycam_sprint_impulse_camera_scale", &g_bodycam_config.impulse.sprint_camera_impulse, 0.f, 5.f },
	{ "sprint_impulse_fov_scale", "bodycam_sprint_impulse_fov_scale", &g_bodycam_config.impulse.sprint_fov_impulse, 0.f, 5.f },
	{ "sprint_impulse_fov_return_speed", "bodycam_sprint_impulse_fov_return_speed", &g_bodycam_config.impulse.sprint_fov_speed, 0.05f, 1.f },
	{ "sprint_impulse_motion_return_speed", "bodycam_sprint_impulse_motion_return_speed", &g_bodycam_config.impulse.sprint_impulse_speed, 0.05f, 1.f },
	{ "ads_transition_impulse", "bodycam_ads_transition_impulse", &g_bodycam_config.impulse.ads_impulse, 0.f, 5.f },
	{ "landing_impulse", "bodycam_landing_impulse", &g_bodycam_config.impulse.land_impulse, 0.f, 5.f },
	{ "mouse_flick_impulse", "bodycam_mouse_flick_impulse", &g_bodycam_config.impulse.flick_impulse, 0.f, 5.f },
	{ "hip_fire_weapon_impulse", "bodycam_hip_fire_weapon_impulse", &g_bodycam_config.impulse.fire_impulse, 0.f, 5.f },
	{ "ads_fire_weapon_impulse", "bodycam_ads_fire_weapon_impulse", &g_bodycam_config.impulse.ads_fire_impulse, 0.f, 5.f },
	{ "impulse_decay_speed", "bodycam_impulse_decay_speed", &g_bodycam_config.impulse.decay, 0.1f, 60.f },
	{ "impulse_max_position_offset", "bodycam_impulse_max_position_offset", &g_bodycam_config.impulse.impulse_pos_cap, 0.f, 0.5f },
	{ "impulse_max_rotation_offset", "bodycam_impulse_max_rotation_offset", &g_bodycam_config.impulse.impulse_rot_cap, 0.f, 45.f },

	// Dynamic weapon lowering pose.
	{ "vm_lowering_pitch", "bodycam_vm_lowering_pitch", &g_bodycam_config.lowering.pitch, -45.f, 45.f },
	{ "vm_lowering_yaw", "bodycam_vm_lowering_yaw", &g_bodycam_config.lowering.yaw, -45.f, 45.f },
	{ "vm_lowering_roll", "bodycam_vm_lowering_roll", &g_bodycam_config.lowering.roll, -45.f, 45.f },
	{ "vm_lowering_x", "bodycam_vm_lowering_x", &g_bodycam_config.lowering.x, -1.f, 1.f },
	{ "vm_lowering_y", "bodycam_vm_lowering_y", &g_bodycam_config.lowering.y, -1.f, 1.f },
	{ "vm_lowering_z", "bodycam_vm_lowering_z", &g_bodycam_config.lowering.z, -1.f, 1.f },
	{ "vm_safemode_lowering_y_offset", "bodycam_vm_safemode_lowering_y_offset", &g_bodycam_config.lowering.holster_offset, 0.f, 0.2f },
	{ "vm_lowering_slow_walk_influence", "bodycam_vm_lowering_slow_walk_influence", &g_bodycam_config.lowering.slow_walk, 0.f, 1.f },
	{ "vm_lowering_walk_influence", "bodycam_vm_lowering_walk_influence", &g_bodycam_config.lowering.walk, 0.f, 1.f },
	{ "vm_lowering_movement_influence", "bodycam_vm_lowering_movement_influence", &g_bodycam_config.lowering.move, 0.f, 1.f },
	{ "vm_lowering_enter_speed", "bodycam_vm_lowering_enter_speed", &g_bodycam_config.lowering.speed, 0.f, 1.f },
	{ "vm_lowering_return_speed", "bodycam_vm_lowering_return_speed", &g_bodycam_config.lowering.return_speed, 0.f, 1.f },
	{ "vm_lowering_fire_suppression_time", "bodycam_vm_lowering_fire_suppression_time", &g_bodycam_config.lowering.fire_timeout, 0.f, 2.f },
	{ "vm_lowering_ads_release_time", "bodycam_vm_lowering_ads_release_time", &g_bodycam_config.lowering.aim_timeout, 0.f, 2.f },
	{ "vm_lowering_combat_suppression_time", "bodycam_vm_lowering_combat_suppression_time", &g_bodycam_config.lowering.combat_timeout, 0.f, 30.f },

	// Layer blend weights for testing and Lua control.
	{ "vm_spring_layer_weight", "bodycam_vm_spring_layer_weight", &g_bodycam_config.features.layer_vm_weight, 0.f, 1.f },
	{ "vm_lowering_layer_weight", "bodycam_vm_lowering_layer_weight", &g_bodycam_config.features.layer_lower_weight, 0.f, 1.f },
};

static BoolBinding g_bool_bindings[] = {
	// Feature toggles.
	{ "camera_decoupling_enable", "bodycam_camera_decoupling_enable", &g_bodycam_config.features.camera_enable },
	{ "vm_spring_enable", "bodycam_vm_spring_enable", &g_bodycam_config.features.vm_enable },
	{ "vm_lowering_enable", "bodycam_vm_lowering_enable", &g_bodycam_config.features.lower_enable },
	{ "movement_inertia_enable", "bodycam_movement_inertia_enable", &g_bodycam_config.movement.enable },
	{ "movement_inertia_disable_ads", "bodycam_movement_inertia_disable_ads", &g_bodycam_config.movement.ads_disable },
	{ "vm_lowering_disable_in_combat", "bodycam_vm_lowering_disable_in_combat", &g_bodycam_config.features.lower_disable_in_combat },
	{ "impulse_debug_enable", "bodycam_impulse_debug_enable", &g_bodycam_config.features.impulse_debug },
};

struct PresetFloat
{
	float* value;
	float preset[4];
};

struct PresetBool
{
	BOOL* value;
	BOOL preset[4];
};

static PresetBool g_preset_bools[] = {
	// 0 disables Bodycam. 1-3 progressively increase camera/viewmodel response.
	{ &g_bodycam_config.features.camera_enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.features.vm_enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.features.lower_enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.movement.enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.movement.ads_disable, { TRUE, TRUE, TRUE, TRUE } },
};

static PresetFloat g_preset_floats[] = {
	// Camera/viewmodel preset values. Columns are: off, balanced, strong, cinematic.
	{ &g_bodycam_config.camera.hip.inner_gain, { 0.f, 0.04f, 0.03f, 0.025f } },
	{ &g_bodycam_config.camera.hip.spring_freq, { 12.f, 7.f, 6.f, 5.4f } },
	{ &g_bodycam_config.camera.hip.deadzone_yaw, { 1.f, 1.5f, 3.f, 4.f } },
	{ &g_bodycam_config.camera.hip.deadzone_pitch, { 0.75f, 1.f, 2.f, 2.5f } },
	{ &g_bodycam_config.camera.hip.softzone_yaw, { 2.f, 4.f, 7.f, 9.f } },
	{ &g_bodycam_config.camera.hip.softzone_pitch, { 1.5f, 3.f, 5.f, 6.f } },
	{ &g_bodycam_config.camera.hip.max_yaw, { 4.f, 9.f, 15.f, 18.f } },
	{ &g_bodycam_config.camera.hip.max_pitch, { 3.f, 6.f, 9.f, 11.f } },
	{ &g_bodycam_config.camera.hip.roll, { 0.f, 2.5f, 4.f, 5.5f } },
	{ &g_bodycam_config.camera.hip.pos, { 0.f, 0.025f, 0.040f, 0.052f } },
	{ &g_bodycam_config.viewmodel.follow_speed, { 9.f, 6.f, 7.f, 6.f } },
	{ &g_bodycam_config.viewmodel.mouse_pos, { 0.f, 0.010f, 0.016f, 0.012f } },
	{ &g_bodycam_config.viewmodel.mouse_rot, { 0.f, 1.4f, 2.2f, 1.5f } },
	{ &g_bodycam_config.viewmodel.max_pos, { 0.f, 0.035f, 0.050f, 0.060f } },
	{ &g_bodycam_config.viewmodel.max_rot, { 0.f, 3.5f, 5.0f, 6.0f } },
	{ &g_bodycam_config.viewmodel.ads_mouse_mult, { 0.f, 0.3f, 0.18f, 0.10f } },
	{ &g_bodycam_config.viewmodel.ads_move_mult, { 0.f, 0.3f, 0.18f, 0.28f } },
	{ &g_bodycam_config.viewmodel.ads_impulse_mult, { 0.f, 0.3f, 0.18f, 0.22f } },
	{ &g_bodycam_config.camera.move_roll, { 0.f, 1.8f, 3.0f, 4.5f } },
	{ &g_bodycam_config.camera.move_pos, { 0.f, 0.012f, 0.020f, 0.030f } },
	{ &g_bodycam_config.viewmodel.move_pos, { 0.f, 0.010f, 0.018f, 0.026f } },
	{ &g_bodycam_config.viewmodel.move_rot, { 0.f, 1.0f, 1.8f, 2.6f } },
	{ &g_bodycam_config.viewmodel.ads_anchor, { 0.f, 0.45f, 0.65f, 0.75f } },
	{ &g_bodycam_config.viewmodel.ads_anchor_pos, { 0.f, 0.006f, 0.010f, 0.011f } },
	{ &g_bodycam_config.viewmodel.ads_anchor_rot, { 0.f, 0.7f, 1.2f, 1.4f } },
	{ &g_bodycam_config.movement.accel_time, { 0.10f, 0.30f, 0.28f, 0.34f } },
	{ &g_bodycam_config.movement.decel_time, { 0.10f, 0.40f, 0.38f, 0.46f } },
	{ &g_bodycam_config.movement.turn_response, { 0.f, 0.45f, 0.55f, 0.70f } },
	{ &g_bodycam_config.movement.stop_response, { 0.10f, 0.50f, 0.45f, 0.55f } },
	{ &g_bodycam_config.movement.sprint_mult, { 1.f, 1.f, 1.f, 0.90f } },
	{ &g_bodycam_config.sprint.strength, { 0.f, 0.85f, 1.0f, 1.15f } },
	{ &g_bodycam_config.sprint.smoothness, { 0.45f, 0.45f, 0.45f, 0.55f } },
	{ &g_bodycam_config.sprint.accent, { 0.f, 0.85f, 1.0f, 1.2f } },
	{ &g_bodycam_config.sprint.bridge_pitch, { 0.f, -8.f, -8.f, -9.f } },
	{ &g_bodycam_config.sprint.bridge_yaw, { 0.f, -8.f, -8.f, -9.f } },
	{ &g_bodycam_config.sprint.bridge_roll, { 0.f, -7.7f, -7.7f, -8.5f } },
	{ &g_bodycam_config.sprint.bridge_pos, { 0.f, 0.050f, 0.050f, 0.060f } },
	{ &g_bodycam_config.sprint.bridge_handoff_speed, { 0.90f, 0.90f, 0.90f, 0.92f } },
	{ &g_bodycam_config.impulse.sprint_impulse, { 0.f, 0.55f, 1.0f, 1.25f } },
	{ &g_bodycam_config.impulse.sprint_start_impulse, { 0.f, 0.9f, 1.15f, 1.35f } },
	{ &g_bodycam_config.impulse.sprint_stop_impulse, { 0.f, 0.55f, 0.75f, 0.9f } },
	{ &g_bodycam_config.impulse.sprint_ads_mult, { 0.f, 0.2f, 0.2f, 0.15f } },
	{ &g_bodycam_config.impulse.sprint_camera_impulse, { 0.f, 0.55f, 0.85f, 1.15f } },
	{ &g_bodycam_config.impulse.sprint_fov_impulse, { 0.f, 1.2f, 2.0f, 2.6f } },
	{ &g_bodycam_config.impulse.sprint_fov_speed, { 0.35f, 0.35f, 0.35f, 0.35f } },
	{ &g_bodycam_config.impulse.sprint_impulse_speed, { 0.45f, 0.45f, 0.45f, 0.45f } },
	{ &g_bodycam_config.impulse.ads_impulse, { 0.f, 0.55f, 1.0f, 0.9f } },
	{ &g_bodycam_config.impulse.land_impulse, { 0.f, 0.6f, 1.0f, 1.1f } },
	{ &g_bodycam_config.impulse.flick_impulse, { 0.f, 0.22f, 0.35f, 0.18f } },
	{ &g_bodycam_config.impulse.fire_impulse, { 0.f, 5.0f, 5.0f, 5.0f } },
	{ &g_bodycam_config.impulse.ads_fire_impulse, { 0.f, 1.0f, 1.35f, 1.35f } },
};

static PresetFloat g_preset_common_floats[] = {
	// Shared values are reset by every preset.
	{ &g_bodycam_config.camera.hip.spring_damping, { 1.f, 1.f, 1.f, 1.f } },
	{ &g_bodycam_config.viewmodel.damping, { 1.f, 1.f, 1.f, 1.f } },
	{ &g_bodycam_config.viewmodel.mouse_filter, { 18.f, 18.f, 18.f, 18.f } },
	{ &g_bodycam_config.viewmodel.move_filter, { 8.f, 8.f, 8.f, 8.f } },
	{ &g_bodycam_config.impulse.decay, { 8.f, 8.f, 8.f, 8.f } },
	{ &g_bodycam_config.camera.ads.inner_gain, { 0.0f, 0.0f, 0.0f, 0.0f } },
	{ &g_bodycam_config.camera.ads.spring_freq, { 14.f, 14.f, 14.f, 14.f } },
	{ &g_bodycam_config.camera.ads.spring_damping, { 1.f, 1.f, 1.f, 1.f } },
	{ &g_bodycam_config.camera.ads.deadzone_yaw, { 0.8f, 0.8f, 0.8f, 0.8f } },
	{ &g_bodycam_config.camera.ads.deadzone_pitch, { 0.55f, 0.55f, 0.55f, 0.55f } },
	{ &g_bodycam_config.camera.ads.softzone_yaw, { 1.8f, 1.8f, 1.8f, 1.8f } },
	{ &g_bodycam_config.camera.ads.softzone_pitch, { 1.2f, 1.2f, 1.2f, 1.2f } },
	{ &g_bodycam_config.camera.ads.max_yaw, { 3.0f, 3.0f, 3.0f, 3.0f } },
	{ &g_bodycam_config.camera.ads.max_pitch, { 2.0f, 2.0f, 2.0f, 2.0f } },
	{ &g_bodycam_config.camera.ads.roll, { 0.6f, 0.6f, 0.6f, 0.6f } },
	{ &g_bodycam_config.camera.ads.pos, { 0.006f, 0.006f, 0.006f, 0.006f } },
};

const FloatBinding* GetFloatBindings(u32& count)
{
	count = _countof(g_float_bindings);
	return g_float_bindings;
}

const BoolBinding* GetBoolBindings(u32& count)
{
	count = _countof(g_bool_bindings);
	return g_bool_bindings;
}

void DumpConfigBindings()
{
	Msg("* bodycam config floats=%u bools=%u", static_cast<u32>(_countof(g_float_bindings)), static_cast<u32>(_countof(g_bool_bindings)));
	for (u32 i = 0; i < _countof(g_float_bindings); ++i)
	{
		const FloatBinding& binding = g_float_bindings[i];
		Msg("* bodycam config %s (%s)=%0.4f range[%0.4f %0.4f]", binding.name, binding.console_name, *binding.value, binding.min_value, binding.max_value);
	}
	for (u32 i = 0; i < _countof(g_bool_bindings); ++i)
	{
		const BoolBinding& binding = g_bool_bindings[i];
		Msg("* bodycam config %s (%s)=%d", binding.name, binding.console_name, *binding.value ? 1 : 0);
	}
}

bool CameraEnabled()
{
	return !!g_bodycam_config.features.camera_enable;
}

bool HudSpringEnabled()
{
	return !!g_bodycam_config.features.vm_enable;
}

bool GetFloat(LPCSTR name, float& value)
{
	if (!name)
		return false;

	for (u32 i = 0; i < _countof(g_float_bindings); ++i)
	{
		const FloatBinding& binding = g_float_bindings[i];
		if (xr_strcmp(binding.name, name) == 0)
		{
			value = *binding.value;
			return true;
		}
	}
	return false;
}

bool SetFloat(LPCSTR name, float value)
{
	if (!name)
		return false;

	for (u32 i = 0; i < _countof(g_float_bindings); ++i)
	{
		const FloatBinding& binding = g_float_bindings[i];
		if (xr_strcmp(binding.name, name) == 0)
		{
			*binding.value = clampr(value, binding.min_value, binding.max_value);
			return true;
		}
	}
	return false;
}

bool GetBool(LPCSTR name, bool& value)
{
	if (!name)
		return false;

	for (u32 i = 0; i < _countof(g_bool_bindings); ++i)
	{
		const BoolBinding& binding = g_bool_bindings[i];
		if (xr_strcmp(binding.name, name) == 0)
		{
			value = !!*binding.value;
			return true;
		}
	}
	return false;
}

bool SetBool(LPCSTR name, bool value)
{
	if (!name)
		return false;

	for (u32 i = 0; i < _countof(g_bool_bindings); ++i)
	{
		const BoolBinding& binding = g_bool_bindings[i];
		if (xr_strcmp(binding.name, name) == 0)
		{
			*binding.value = value ? TRUE : FALSE;
			if (binding.value == &g_bodycam_config.features.vm_enable && !value)
				ResetHudOutput();
			return true;
		}
	}
	return false;
}

void SetLayerWeight(LPCSTR layer, float weight)
{
	if (!layer)
		return;

	weight = clampr(weight, 0.f, 1.f);
	if (xr_strcmp(layer, "vm") == 0)
		g_bodycam_config.features.layer_vm_weight = weight;
	else if (xr_strcmp(layer, "vm_lowering") == 0)
		g_bodycam_config.features.layer_lower_weight = weight;
}

float GetLayerWeight(LPCSTR layer)
{
	if (!layer)
		return 0.f;

	if (xr_strcmp(layer, "vm") == 0)
		return g_bodycam_config.features.layer_vm_weight;
	if (xr_strcmp(layer, "vm_lowering") == 0)
		return g_bodycam_config.features.layer_lower_weight;
	return 0.f;
}

SimulationSettings GetSimulationSettings()
{
	SimulationSettings settings;
	settings.features.camera_enable = !!g_bodycam_config.features.camera_enable;
	settings.features.vm_enable = !!g_bodycam_config.features.vm_enable;
	settings.features.lower_enable = !!g_bodycam_config.features.lower_enable;
	settings.features.impulse_debug = !!g_bodycam_config.features.impulse_debug;
	settings.features.lower_disable_in_combat = !!g_bodycam_config.features.lower_disable_in_combat;
	settings.camera = g_bodycam_config.camera;
	settings.viewmodel = g_bodycam_config.viewmodel;
	settings.impulse = g_bodycam_config.impulse;
	settings.sprint = g_bodycam_config.sprint;
	settings.lowering = g_bodycam_config.lowering;
	settings.features.layer_vm_weight = g_bodycam_config.features.layer_vm_weight;
	settings.features.layer_lower_weight = g_bodycam_config.features.layer_lower_weight;
	return settings;
}

void ApplyPreset(int preset)
{
	preset = clampr(preset, 0, 3);
	for (u32 i = 0; i < _countof(g_preset_bools); ++i)
		*g_preset_bools[i].value = g_preset_bools[i].preset[preset];
	for (u32 i = 0; i < _countof(g_preset_floats); ++i)
		*g_preset_floats[i].value = g_preset_floats[i].preset[preset];
	for (u32 i = 0; i < _countof(g_preset_common_floats); ++i)
		*g_preset_common_floats[i].value = g_preset_common_floats[i].preset[preset];
	Msg("* bodycam_preset %d applied", preset);
}
} // namespace Bodycam
