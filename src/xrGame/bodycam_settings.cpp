#include "stdafx.h"
#include "bodycam_settings.h"

namespace Bodycam
{
namespace
{
constexpr float kFeatureEpsilon = 1.0e-6f;
}

static const RuntimeConfig g_default_bodycam_config;
static RuntimeConfig g_bodycam_config = g_default_bodycam_config;

RuntimeConfig& GetConfig()
{
	return g_bodycam_config;
}

#define BODYCAM_FLOAT(name, member, minimum, maximum) \
	{ #name, "bodycam_" #name, &g_bodycam_config.member, minimum, maximum }
#define BODYCAM_BOOL(name, member) \
	{ #name, "bodycam_" #name, &g_bodycam_config.member }
// This public command already contains the bodycam_ prefix.
#define BODYCAM_BOOL_EXACT(name, member) \
	{ #name, #name, &g_bodycam_config.member }

static FloatBinding g_float_bindings[] = {
	// Hip-fire camera response.
	BODYCAM_FLOAT(hip_camera_inner_zone_response, camera.hip.inner_gain, 0.f, 1.f),
	BODYCAM_FLOAT(hip_camera_follow_speed, camera.hip.spring_freq, 0.1f, 30.f),
	BODYCAM_FLOAT(hip_camera_follow_damping, camera.hip.spring_damping, 0.f, 3.f),
	BODYCAM_FLOAT(hip_camera_deadzone_yaw, camera.hip.deadzone_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(hip_camera_deadzone_pitch, camera.hip.deadzone_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(hip_camera_soft_follow_yaw, camera.hip.softzone_yaw, 0.f, 45.f),
	BODYCAM_FLOAT(hip_camera_soft_follow_pitch, camera.hip.softzone_pitch, 0.f, 45.f),
	BODYCAM_FLOAT(hip_camera_max_yaw_offset, camera.hip.max_yaw, 0.f, 90.f),
	BODYCAM_FLOAT(hip_camera_max_pitch_offset, camera.hip.max_pitch, 0.f, 90.f),
	BODYCAM_FLOAT(hip_camera_roll_scale, camera.hip.roll, 0.f, 20.f),
	BODYCAM_FLOAT(hip_camera_position_scale, camera.hip.pos, 0.f, 0.25f),

	// ADS camera response.
	BODYCAM_FLOAT(ads_camera_inner_zone_response, camera.ads.inner_gain, 0.f, 1.f),
	BODYCAM_FLOAT(ads_camera_follow_speed, camera.ads.spring_freq, 0.1f, 30.f),
	BODYCAM_FLOAT(ads_camera_follow_damping, camera.ads.spring_damping, 0.f, 3.f),
	BODYCAM_FLOAT(ads_camera_deadzone_yaw, camera.ads.deadzone_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(ads_camera_deadzone_pitch, camera.ads.deadzone_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(ads_camera_soft_follow_yaw, camera.ads.softzone_yaw, 0.f, 45.f),
	BODYCAM_FLOAT(ads_camera_soft_follow_pitch, camera.ads.softzone_pitch, 0.f, 45.f),
	BODYCAM_FLOAT(ads_camera_max_yaw_offset, camera.ads.max_yaw, 0.f, 90.f),
	BODYCAM_FLOAT(ads_camera_max_pitch_offset, camera.ads.max_pitch, 0.f, 90.f),
	BODYCAM_FLOAT(ads_camera_roll_scale, camera.ads.roll, 0.f, 20.f),
	BODYCAM_FLOAT(ads_camera_position_scale, camera.ads.pos, 0.f, 0.25f),

	// Viewmodel lag and ADS anchoring.
	BODYCAM_FLOAT(vm_spring_speed, viewmodel.follow_speed, 0.1f, 30.f),
	BODYCAM_FLOAT(vm_spring_damping, viewmodel.damping, 0.f, 3.f),
	BODYCAM_FLOAT(vm_mouse_position_scale, viewmodel.mouse_pos, 0.f, 0.25f),
	BODYCAM_FLOAT(vm_mouse_rotation_scale, viewmodel.mouse_rot, 0.f, 30.f),
	BODYCAM_FLOAT(vm_max_position_offset, viewmodel.max_pos, 0.f, 0.35f),
	BODYCAM_FLOAT(vm_max_rotation_offset, viewmodel.max_rot, 0.f, 45.f),
	BODYCAM_FLOAT(vm_ads_mouse_scale, viewmodel.ads_mouse_mult, 0.f, 1.f),
	BODYCAM_FLOAT(vm_ads_impulse_scale, viewmodel.ads_impulse_mult, 0.f, 1.f),
	BODYCAM_FLOAT(vm_ads_sight_anchor_strength, viewmodel.ads_anchor, 0.f, 1.f),
	BODYCAM_FLOAT(vm_ads_anchor_position_scale, viewmodel.ads_anchor_pos, 0.f, 0.25f),
	BODYCAM_FLOAT(vm_ads_anchor_rotation_scale, viewmodel.ads_anchor_rot, 0.f, 30.f),

	// Camera movement response and shared movement filtering.
	BODYCAM_FLOAT(movement_camera_roll_scale, camera.move_roll, 0.f, 20.f),
	BODYCAM_FLOAT(movement_camera_position_scale, camera.move_pos, 0.f, 0.25f),
	BODYCAM_FLOAT(vm_mouse_smoothing_speed, viewmodel.mouse_filter, 0.1f, 60.f),
	BODYCAM_FLOAT(vm_movement_smoothing_speed, viewmodel.move_filter, 0.1f, 60.f),

	// Actor movement response. Speed mods own the target speed; Bodycam owns time-to-target.
	BODYCAM_FLOAT(movement_acceleration_time, movement.accel_time, 0.f, 2.f),
	BODYCAM_FLOAT(movement_deceleration_time, movement.decel_time, 0.f, 2.f),
	BODYCAM_FLOAT(movement_turn_response, movement.turn_response, 0.f, 1.f),
	BODYCAM_FLOAT(movement_stop_response, movement.stop_response, 0.f, 2.f),
	BODYCAM_FLOAT(movement_sprint_speed_scale, movement.sprint_mult, 0.1f, 3.f),

	// Sprint locomotion layer.
	BODYCAM_FLOAT(sprint_transition_vm_blend_scale, sprint.strength, 0.f, 2.f),
	BODYCAM_FLOAT(sprint_transition_blend_smoothing, sprint.smoothness, 0.f, 1.f),
	BODYCAM_FLOAT(sprint_transition_start_stop_impulse_scale, sprint.accent, 0.f, 2.f),
	BODYCAM_FLOAT(sprint_transition_vm_pitch, sprint.bridge_pitch, -12.f, 12.f),
	BODYCAM_FLOAT(sprint_transition_vm_yaw, sprint.bridge_yaw, -12.f, 12.f),
	BODYCAM_FLOAT(sprint_transition_vm_roll, sprint.bridge_roll, -12.f, 12.f),
	BODYCAM_FLOAT(sprint_transition_vm_position, sprint.bridge_pos, 0.f, 0.10f),
	BODYCAM_FLOAT(sprint_transition_animation_handoff_speed, sprint.bridge_handoff_speed, 0.35f, 1.f),

	// One-shot impulses from gameplay events.
	BODYCAM_FLOAT(sprint_transition_impulse, impulse.sprint_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(sprint_start_impulse, impulse.sprint_start_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(sprint_stop_impulse, impulse.sprint_stop_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(sprint_impulse_ads_scale, impulse.sprint_ads_mult, 0.f, 1.f),
	BODYCAM_FLOAT(sprint_impulse_camera_scale, impulse.sprint_camera_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(sprint_impulse_fov_scale, impulse.sprint_fov_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(sprint_impulse_fov_return_speed, impulse.sprint_fov_speed, 0.05f, 1.f),
	BODYCAM_FLOAT(sprint_impulse_motion_return_speed, impulse.sprint_impulse_speed, 0.05f, 1.f),
	BODYCAM_FLOAT(ads_transition_impulse, impulse.ads_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(landing_impulse, impulse.land_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(mouse_flick_impulse, impulse.flick_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(hip_fire_weapon_impulse, impulse.fire_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(ads_fire_weapon_impulse, impulse.ads_fire_impulse, 0.f, 5.f),
	BODYCAM_FLOAT(impulse_decay_speed, impulse.decay, 0.1f, 60.f),
	BODYCAM_FLOAT(impulse_max_position_offset, impulse.impulse_pos_cap, 0.f, 0.5f),
	BODYCAM_FLOAT(impulse_max_rotation_offset, impulse.impulse_rot_cap, 0.f, 45.f),

	// Dynamic weapon lowering pose.
	BODYCAM_FLOAT(vm_lowering_pitch, lowering.pitch, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_yaw, lowering.yaw, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_roll, lowering.roll, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_x, lowering.x, -1.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_y, lowering.y, -1.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_z, lowering.z, -1.f, 1.f),
	BODYCAM_FLOAT(vm_safemode_lowering_y_offset, lowering.holster_offset, 0.f, 0.2f),
	BODYCAM_FLOAT(vm_lowering_slow_walk_influence, lowering.slow_walk, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_walk_influence, lowering.walk, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_movement_influence, lowering.move, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_enter_speed, lowering.speed, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_return_speed, lowering.return_speed, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_fire_suppression_time, lowering.fire_timeout, 0.f, 2.f),
	BODYCAM_FLOAT(vm_lowering_ads_release_time, lowering.aim_timeout, 0.f, 2.f),
	BODYCAM_FLOAT(vm_lowering_combat_suppression_time, lowering.combat_timeout, 0.f, 30.f),

	// Additive first-person arm compliance.
	BODYCAM_FLOAT(arm_compliance_strength, bodycam_arm.strength, 0.f, 8.f),
	BODYCAM_FLOAT(arm_compliance_response, bodycam_arm.response, 0.1f, 40.f),
	BODYCAM_FLOAT(arm_compliance_ads_scale, bodycam_arm.ads_scale, 0.f, 1.f),
	BODYCAM_FLOAT(arm_compliance_mouse_pitch, bodycam_arm.mouse_pitch, -30.f, 30.f),
	BODYCAM_FLOAT(arm_compliance_mouse_yaw, bodycam_arm.mouse_yaw, -30.f, 30.f),
	BODYCAM_FLOAT(arm_compliance_mouse_roll, bodycam_arm.mouse_roll, -45.f, 45.f),
	BODYCAM_FLOAT(arm_compliance_secondary_roll, bodycam_arm.secondary_roll, -30.f, 30.f),
	BODYCAM_FLOAT(arm_compliance_hand_scale, bodycam_arm.hand_scale, 0.f, 1.f),
	BODYCAM_FLOAT(arm_compliance_upperarm_scale, bodycam_arm.upperarm_scale, 0.f, 1.f),
	BODYCAM_FLOAT(arm_compliance_forearm_scale, bodycam_arm.forearm_scale, 0.f, 1.5f),
	BODYCAM_FLOAT(arm_compliance_twist_scale, bodycam_arm.twist_scale, 0.f, 2.f),

	// STALKER 2-style mouse response and authored-animation arm follow.
	BODYCAM_FLOAT(stalker2_arm_ik_strength, stalker2_arm.strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_response, stalker2_arm.response, 0.1f, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_follow_response, stalker2_arm.arm_follow_response, 0.1f, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_follow_scale, stalker2_arm.arm_follow_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_ads_scale, stalker2_arm.ads_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_strength, stalker2_arm.mouse_strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_sensitivity, stalker2_arm.mouse_sensitivity, 0.1f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_yaw, stalker2_arm.mouse_max_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_pitch, stalker2_arm.mouse_max_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_roll, stalker2_arm.mouse_max_roll, 0.f, 45.f),
	BODYCAM_FLOAT(stalker2_arm_ik_movement_strength, stalker2_arm.movement_strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_movement_response, stalker2_arm.movement_response, 0.1f, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_slow_walk_scale, stalker2_arm.slow_walk_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_pitch, stalker2_arm.mouse_pitch, -20.f, 20.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_yaw, stalker2_arm.mouse_yaw, -20.f, 20.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_roll, stalker2_arm.mouse_roll, -30.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_wrist_scale, stalker2_arm.wrist_scale, 0.f, 1.f),

	// Layer blend weights for testing and Lua control.
	BODYCAM_FLOAT(vm_spring_layer_weight, features.layer_vm_weight, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_layer_weight, features.layer_lower_weight, 0.f, 1.f),
	BODYCAM_FLOAT(arm_compliance_layer_weight, features.layer_arm_weight, 0.f, 1.f),
};

static BoolBinding g_bool_bindings[] = {
	// Feature toggles.
	BODYCAM_BOOL(camera_decoupling_enable, features.camera_enable),
	BODYCAM_BOOL(vm_spring_enable, features.vm_enable),
	BODYCAM_BOOL(vm_lowering_enable, features.lower_enable),
	BODYCAM_BOOL_EXACT(bodycam_style_arm_ik_enable, features.bodycam_arm_enable),
	BODYCAM_BOOL(stalker2_style_arm_ik_enable, features.stalker2_arm_enable),
	BODYCAM_BOOL(movement_inertia_enable, movement.enable),
	BODYCAM_BOOL(movement_inertia_disable_ads, movement.ads_disable),
	BODYCAM_BOOL(vm_lowering_disable_in_combat, features.lower_disable_in_combat),
	BODYCAM_BOOL(impulse_debug_enable, features.impulse_debug),
};

#undef BODYCAM_BOOL_EXACT
#undef BODYCAM_BOOL
#undef BODYCAM_FLOAT

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
	{ &g_bodycam_config.features.vm_enable, { FALSE, TRUE, FALSE, TRUE } },
	{ &g_bodycam_config.features.lower_enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.features.bodycam_arm_enable, { FALSE, FALSE, FALSE, FALSE } },
	{ &g_bodycam_config.features.stalker2_arm_enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.movement.enable, { FALSE, TRUE, TRUE, TRUE } },
	{ &g_bodycam_config.movement.ads_disable, { TRUE, TRUE, TRUE, TRUE } },
};

static PresetFloat g_preset_floats[] = {
	// Camera/viewmodel preset values. Columns are: off, balanced, strong, cinematic.
	{ &g_bodycam_config.camera.hip.inner_gain, { 0.f, 0.04f, 0.f, 0.025f } },
	{ &g_bodycam_config.camera.hip.spring_freq, { 12.f, 7.f, 8.f, 5.4f } },
	{ &g_bodycam_config.camera.hip.deadzone_yaw, { 1.f, 1.5f, 1.5f, 4.f } },
	{ &g_bodycam_config.camera.hip.deadzone_pitch, { 0.75f, 1.f, 1.f, 2.5f } },
	{ &g_bodycam_config.camera.hip.softzone_yaw, { 2.f, 4.f, 4.f, 9.f } },
	{ &g_bodycam_config.camera.hip.softzone_pitch, { 1.5f, 3.f, 3.f, 6.f } },
	{ &g_bodycam_config.camera.hip.max_yaw, { 4.f, 9.f, 19.f, 18.f } },
	{ &g_bodycam_config.camera.hip.max_pitch, { 3.f, 6.f, 19.f, 11.f } },
	{ &g_bodycam_config.camera.hip.roll, { 0.f, 2.5f, 2.5f, 5.5f } },
	{ &g_bodycam_config.camera.hip.pos, { 0.f, 0.025f, 0.025f, 0.052f } },
	{ &g_bodycam_config.viewmodel.follow_speed, { 9.f, 6.f, 9.f, 6.f } },
	{ &g_bodycam_config.viewmodel.mouse_pos, { 0.f, 0.020f, 0.030f, 0.038f } },
	{ &g_bodycam_config.viewmodel.mouse_rot, { 0.f, 3.2f, 5.0f, 6.4f } },
	{ &g_bodycam_config.viewmodel.max_pos, { 0.f, 0.045f, 0.035f, 0.075f } },
	{ &g_bodycam_config.viewmodel.max_rot, { 0.f, 6.0f, 5.0f, 11.0f } },
	{ &g_bodycam_config.viewmodel.ads_mouse_mult, { 0.f, 0.3f, 0.18f, 0.10f } },
	{ &g_bodycam_config.viewmodel.ads_impulse_mult, { 0.f, 0.3f, 0.18f, 0.22f } },
	{ &g_bodycam_config.camera.move_roll, { 0.f, 1.8f, 3.0f, 4.5f } },
	{ &g_bodycam_config.camera.move_pos, { 0.f, 0.012f, 0.020f, 0.030f } },
	{ &g_bodycam_config.viewmodel.ads_anchor, { 0.f, 0.45f, 0.65f, 0.75f } },
	{ &g_bodycam_config.viewmodel.ads_anchor_pos, { 0.f, 0.006f, 0.010f, 0.011f } },
	{ &g_bodycam_config.viewmodel.ads_anchor_rot, { 0.f, 0.7f, 1.2f, 1.4f } },
	{ &g_bodycam_config.movement.accel_time, { 0.10f, 0.30f, 0.28f, 0.34f } },
	{ &g_bodycam_config.movement.decel_time, { 0.10f, 0.40f, 0.38f, 0.46f } },
	{ &g_bodycam_config.movement.turn_response, { 0.f, 0.45f, 0.55f, 0.70f } },
	{ &g_bodycam_config.movement.stop_response, { 0.10f, 0.50f, 0.45f, 0.55f } },
	{ &g_bodycam_config.movement.sprint_mult, { 1.f, 1.f, 1.f, 0.90f } },
	{ &g_bodycam_config.sprint.strength, { 0.f, 0.85f, 1.5f, 1.15f } },
	{ &g_bodycam_config.sprint.smoothness, { 0.45f, 0.45f, 1.f, 0.55f } },
	{ &g_bodycam_config.sprint.accent, { 0.f, 0.85f, 2.f, 1.2f } },
	{ &g_bodycam_config.sprint.bridge_pitch, { 0.f, -8.f, -8.f, -9.f } },
	{ &g_bodycam_config.sprint.bridge_yaw, { 0.f, -8.f, -8.f, -9.f } },
	{ &g_bodycam_config.sprint.bridge_roll, { 0.f, -7.7f, -7.7f, -8.5f } },
	{ &g_bodycam_config.sprint.bridge_pos, { 0.f, 0.050f, 0.050f, 0.060f } },
	{ &g_bodycam_config.sprint.bridge_handoff_speed, { 0.90f, 0.90f, 0.90f, 0.92f } },
	{ &g_bodycam_config.impulse.sprint_impulse, { 0.f, 0.55f, 1.0f, 1.25f } },
	{ &g_bodycam_config.impulse.sprint_start_impulse, { 0.f, 0.9f, 5.f, 1.35f } },
	{ &g_bodycam_config.impulse.sprint_stop_impulse, { 0.f, 0.55f, 5.f, 0.9f } },
	{ &g_bodycam_config.impulse.sprint_ads_mult, { 0.f, 0.2f, 0.2f, 0.15f } },
	{ &g_bodycam_config.impulse.sprint_camera_impulse, { 0.f, 0.55f, 5.f, 1.15f } },
	{ &g_bodycam_config.impulse.sprint_fov_impulse, { 0.f, 1.2f, 5.f, 2.6f } },
	{ &g_bodycam_config.impulse.sprint_fov_speed, { 0.35f, 0.35f, 0.5f, 0.35f } },
	{ &g_bodycam_config.impulse.sprint_impulse_speed, { 0.45f, 0.45f, 0.2f, 0.45f } },
	{ &g_bodycam_config.impulse.ads_impulse, { 0.f, 0.55f, 1.0f, 0.9f } },
	{ &g_bodycam_config.impulse.land_impulse, { 0.f, 0.6f, 1.0f, 1.1f } },
	{ &g_bodycam_config.impulse.flick_impulse, { 0.f, 0.22f, 0.f, 0.18f } },
	{ &g_bodycam_config.impulse.fire_impulse, { 0.f, 5.0f, 5.0f, 5.0f } },
	{ &g_bodycam_config.impulse.ads_fire_impulse, { 0.f, 1.0f, 1.35f, 1.35f } },
	{ &g_bodycam_config.bodycam_arm.strength, { 0.f, 3.f, 3.f, 3.f } },
	{ &g_bodycam_config.bodycam_arm.ads_scale, { 0.f, 1.f, 1.f, 1.f } },
};

static PresetFloat g_preset_common_floats[] = {
	// Shared values are reset by every preset.
	{ &g_bodycam_config.camera.hip.spring_damping, { 1.f, 1.f, 0.85f, 1.f } },
	{ &g_bodycam_config.viewmodel.damping, { 1.f, 1.f, 0.78f, 1.f } },
	{ &g_bodycam_config.viewmodel.mouse_filter, { 18.f, 18.f, 18.f, 18.f } },
	{ &g_bodycam_config.viewmodel.move_filter, { 8.f, 8.f, 8.f, 8.f } },
	{ &g_bodycam_config.impulse.decay, { 8.f, 8.f, 8.f, 8.f } },
	{ &g_bodycam_config.bodycam_arm.response, { 30.f, 30.f, 30.f, 30.f } },
	{ &g_bodycam_config.bodycam_arm.mouse_pitch, { 30.0f, 30.0f, 30.0f, 30.0f } },
	{ &g_bodycam_config.bodycam_arm.mouse_yaw, { 30.0f, 30.0f, 30.0f, 30.0f } },
	{ &g_bodycam_config.bodycam_arm.mouse_roll, { 45.0f, 45.0f, 45.0f, 45.0f } },
	{ &g_bodycam_config.bodycam_arm.secondary_roll, { 20.0f, 20.0f, 20.0f, 20.0f } },
	{ &g_bodycam_config.bodycam_arm.hand_scale, { 0.f, 0.f, 0.f, 0.f } },
	{ &g_bodycam_config.bodycam_arm.upperarm_scale, { 0.05f, 0.05f, 0.05f, 0.05f } },
	{ &g_bodycam_config.bodycam_arm.forearm_scale, { 0.1f, 0.1f, 0.1f, 0.1f } },
	{ &g_bodycam_config.bodycam_arm.twist_scale, { 2.f, 2.f, 2.f, 2.f } },
	{ &g_bodycam_config.stalker2_arm.strength, { 1.5f, 1.5f, 1.5f, 1.5f } },
	{ &g_bodycam_config.stalker2_arm.response, { 20.f, 20.f, 20.f, 20.f } },
	{ &g_bodycam_config.stalker2_arm.arm_follow_response, { 12.f, 12.f, 0.1f, 12.f } },
	{ &g_bodycam_config.stalker2_arm.arm_follow_scale, { 0.f, 0.f, 0.f, 0.f } },
	{ &g_bodycam_config.stalker2_arm.ads_scale, { 0.55f, 0.55f, 0.1f, 0.55f } },
	{ &g_bodycam_config.stalker2_arm.mouse_strength, { 1.f, 1.f, 1.f, 1.f } },
	{ &g_bodycam_config.stalker2_arm.mouse_sensitivity, { 2.f, 2.f, 2.f, 2.f } },
	{ &g_bodycam_config.stalker2_arm.mouse_max_yaw, { 5.25f, 5.25f, 5.25f, 5.25f } },
	{ &g_bodycam_config.stalker2_arm.mouse_max_pitch, { 13.5f, 13.5f, 13.5f, 13.5f } },
	{ &g_bodycam_config.stalker2_arm.mouse_max_roll, { 18.3f, 18.3f, 15.5f, 18.3f } },
	{ &g_bodycam_config.stalker2_arm.movement_strength, { 1.f, 1.f, 1.f, 1.f } },
	{ &g_bodycam_config.stalker2_arm.movement_response, { kDefaultStalker2MovementResponse, kDefaultStalker2MovementResponse,
		kDefaultStalker2MovementResponse, kDefaultStalker2MovementResponse } },
	{ &g_bodycam_config.stalker2_arm.slow_walk_scale, { 0.5f, 0.5f, 0.5f, 0.5f } },
	{ &g_bodycam_config.stalker2_arm.mouse_pitch, { 18.f, 18.f, 18.f, 18.f } },
	{ &g_bodycam_config.stalker2_arm.mouse_yaw, { 20.f, 20.f, 20.f, 20.f } },
	{ &g_bodycam_config.stalker2_arm.mouse_roll, { 28.f, 28.f, 28.f, 28.f } },
	{ &g_bodycam_config.stalker2_arm.wrist_scale, { 1.f, 1.f, 1.f, 1.f } },
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

bool HudEffectsEnabled()
{
	const RuntimeFeatureSettings& features = g_bodycam_config.features;
	const bool sprint_bridge_enabled = g_bodycam_config.movement.enable &&
		features.layer_vm_weight > kFeatureEpsilon && g_bodycam_config.sprint.strength > kFeatureEpsilon;

	return !!features.vm_enable || !!features.lower_enable ||
		!!features.bodycam_arm_enable || !!features.stalker2_arm_enable ||
		sprint_bridge_enabled;
}

bool AnyEffectEnabled()
{
	return CameraEnabled() || HudEffectsEnabled();
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
	else if (xr_strcmp(layer, "arm_compliance") == 0)
		g_bodycam_config.features.layer_arm_weight = weight;
}

float GetLayerWeight(LPCSTR layer)
{
	if (!layer)
		return 0.f;

	if (xr_strcmp(layer, "vm") == 0)
		return g_bodycam_config.features.layer_vm_weight;
	if (xr_strcmp(layer, "vm_lowering") == 0)
		return g_bodycam_config.features.layer_lower_weight;
	if (xr_strcmp(layer, "arm_compliance") == 0)
		return g_bodycam_config.features.layer_arm_weight;
	return 0.f;
}

SimulationSettings GetSimulationSettings()
{
	SimulationSettings settings;
	settings.features.camera_enable = !!g_bodycam_config.features.camera_enable;
	settings.features.vm_enable = !!g_bodycam_config.features.vm_enable;
	settings.features.lower_enable = !!g_bodycam_config.features.lower_enable;
	settings.features.bodycam_arm_enable = !!g_bodycam_config.features.bodycam_arm_enable;
	settings.features.stalker2_arm_enable = !!g_bodycam_config.features.stalker2_arm_enable;
	settings.features.sprint_bridge_enable = !!g_bodycam_config.movement.enable;
	settings.features.impulse_debug = !!g_bodycam_config.features.impulse_debug;
	settings.features.lower_disable_in_combat = !!g_bodycam_config.features.lower_disable_in_combat;
	settings.camera = g_bodycam_config.camera;
	settings.viewmodel = g_bodycam_config.viewmodel;
	settings.impulse = g_bodycam_config.impulse;
	settings.sprint = g_bodycam_config.sprint;
	settings.lowering = g_bodycam_config.lowering;
	settings.bodycam_arm = g_bodycam_config.bodycam_arm;
	settings.stalker2_arm = g_bodycam_config.stalker2_arm;
	settings.features.layer_vm_weight = g_bodycam_config.features.layer_vm_weight;
	settings.features.layer_lower_weight = g_bodycam_config.features.layer_lower_weight;
	settings.features.layer_arm_weight = g_bodycam_config.features.layer_arm_weight;
	return settings;
}

void ApplyPreset(int preset)
{
	preset = clampr(preset, 0, 3);
	g_bodycam_config = g_default_bodycam_config;

	// Preset 2 is the shipped default. Persisted settings only override it.
	if (preset == 2)
	{
		Msg("* bodycam default preset applied");
		return;
	}

	for (u32 i = 0; i < _countof(g_preset_bools); ++i)
		*g_preset_bools[i].value = g_preset_bools[i].preset[preset];
	for (u32 i = 0; i < _countof(g_preset_floats); ++i)
		*g_preset_floats[i].value = g_preset_floats[i].preset[preset];
	for (u32 i = 0; i < _countof(g_preset_common_floats); ++i)
		*g_preset_common_floats[i].value = g_preset_common_floats[i].preset[preset];
	Msg("* bodycam_preset %d applied", preset);
}
} // namespace Bodycam
