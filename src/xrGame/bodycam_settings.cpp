#include "stdafx.h"
#include "bodycam_settings.h"
#include "bodycam_reference/weapon_position.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>

namespace Bodycam
{
namespace
{
const RuntimeConfig kDefaults;
RuntimeConfig g_config = kDefaults;
BodycamReference::MotionSelection g_motion_selection;
int g_motion_mode_console = 0;
} // namespace

BodycamReference::MotionSelection& GetMotionSelection()
{
	return g_motion_selection;
}

bool RequestMotionMode(int mode)
{
	if (mode < 0 || !g_motion_selection.Request(static_cast<unsigned>(mode)))
		return false;
	g_motion_mode_console = mode;
	return true;
}

int* MotionModeConsoleValue()
{
	return &g_motion_mode_console;
}

bool ClassicActive()
{
	return g_motion_selection.Active() == BodycamReference::MotionMode::Classic;
}

bool ReferenceActive()
{
	return g_motion_selection.Active() == BodycamReference::MotionMode::Reference;
}

bool ReferenceMovementActive()
{
	return ReferenceActive() && g_config.reference.movement;
}

namespace
{
struct PositionEntry
{
	ReferenceWeaponPosition value, baseline;
	bool has_baseline = false;
	bool hip_override = false, ads_override = false;
};
std::map<std::string, PositionEntry> g_weapon_positions;
std::unique_ptr<CInifile> g_position_file;
const ReferenceWeaponPosition kNoPosition;

void PositionPath(string_path& path)
{
	FS.update_path(path, "$app_data_root$", "bodycam_weapon_positions.ltx");
}
bool LoadPosition(const CInifile& file, LPCSTR section, LPCSTR key, Fvector& output)
{
	if (!file.section_exist(section) || !file.line_exist(section, key))
		return false;
	std::array<float, 3> value;
	if (BodycamReference::ParseWeaponPosition(file.r_string(section, key), value))
	{
		output.set(value[0], value[1], value[2]);
		return true;
	}
	Msg("! Reference weapon position ignored invalid %s.%s", section, key);
	return false;
}
bool ValidPosition(const Fvector& value)
{
	return BodycamReference::ValidWeaponPosition({ value.x, value.y, value.z });
}
void BindPositionEndpoint(Fvector& value, const Fvector& baseline, bool user_override)
{
	if (!user_override)
		value = baseline;
}
// The entry of a weapon section, read from the position file on first use.
// Null for a name that is not a weapon section.
PositionEntry* FindOrLoadEntry(LPCSTR section)
{
	if (!BodycamReference::WeaponPositionKey(section))
		return nullptr;
	const auto found = g_weapon_positions.find(section);
	if (found != g_weapon_positions.end())
		return &found->second;
	if (!g_position_file)
	{
		string_path path;
		PositionPath(path);
		g_position_file = std::make_unique<CInifile>(path, TRUE, TRUE, FALSE);
	}
	PositionEntry entry;
	entry.hip_override = LoadPosition(*g_position_file, section, "hip_position_m", entry.value.hip);
	entry.ads_override = LoadPosition(*g_position_file, section, "ads_position_m", entry.value.ads);
	return &g_weapon_positions.emplace(section, entry).first->second;
}
}
const ReferenceWeaponPosition& GetReferenceWeaponPosition(LPCSTR section)
{
	const PositionEntry* entry = FindOrLoadEntry(section);
	return entry ? entry->value : kNoPosition;
}
bool SeedReferenceWeaponPosition(LPCSTR section, const ReferenceWeaponPosition& baseline)
{
	if (!ValidPosition(baseline.hip) || !ValidPosition(baseline.ads))
		return false;
	PositionEntry* entry = FindOrLoadEntry(section);
	if (!entry)
		return false;
	entry->baseline = baseline;
	entry->has_baseline = true;
	BindPositionEndpoint(entry->value.hip, baseline.hip, entry->hip_override);
	BindPositionEndpoint(entry->value.ads, baseline.ads, entry->ads_override);
	return true;
}
const ReferenceWeaponPosition& GetReferenceWeaponPositionBaseline(LPCSTR section)
{
	const PositionEntry* entry = FindOrLoadEntry(section);
	return entry ? entry->baseline : kNoPosition;
}
bool HasReferenceWeaponPositionBaseline(LPCSTR section)
{
	const PositionEntry* entry = FindOrLoadEntry(section);
	return entry && entry->has_baseline;
}
bool RestoreReferenceWeaponPosition(LPCSTR section)
{
	PositionEntry* entry = FindOrLoadEntry(section);
	if (!entry || !entry->has_baseline)
		return false;
	entry->value = entry->baseline;
	entry->hip_override = entry->ads_override = false;
	return true;
}
bool SetReferenceWeaponPosition(LPCSTR section, bool ads, const Fvector& metres)
{
	PositionEntry* entry = FindOrLoadEntry(section);
	if (!entry || !entry->has_baseline || !ValidPosition(metres))
		return false;
	(ads ? entry->value.ads : entry->value.hip) = metres;
	(ads ? entry->ads_override : entry->hip_override) = true;
	return true;
}
bool SaveReferenceWeaponPosition(LPCSTR section)
{
	const PositionEntry* entry = FindOrLoadEntry(section);
	if (!entry || !entry->has_baseline)
		return false;
	string_path path;
	PositionPath(path);
	// Other weapons and the other keys of this section are kept. Only edited
	// endpoints are written; the others keep following the binding baseline.
	CInifile saved(path, FALSE, TRUE, FALSE);
	saved.set_override_names(TRUE);
	const auto write = [&](LPCSTR key, const Fvector& value, bool edited)
	{
		if (edited)
			saved.w_fvector3(section, key, value);
		else
			saved.remove_line(section, key);
	};
	write("hip_position_m", entry->value.hip, entry->hip_override);
	write("ads_position_m", entry->value.ads, entry->ads_override);
	const bool ok = saved.save_as(path);
	Msg("[Reference weapon position] save section=%s result=%s", section, ok ? "ok" : "failed");
	return ok;
}
RuntimeConfig& GetConfig()
{
	return g_config;
}

#define BODYCAM_FLOAT(name, member, minimum, maximum) { #name, "bodycam_" #name, &g_config.member, kDefaults.member, minimum, maximum }
#define BODYCAM_BOOL(name, member) { #name, "bodycam_" #name, &g_config.member, kDefaults.member, kDefaults.member }

namespace
{
// The one definition of every setting: Lua name, console name, storage,
// default and range. The UI scripts refer to settings by name only.
const FloatBinding g_float_bindings[] = {
	// Reference regrip and recoil on the Classic rig.
	BODYCAM_FLOAT(classic_regrip_1_intensity, regrip.intensity[0], 0.f, 1.f),
	BODYCAM_FLOAT(classic_regrip_2_intensity, regrip.intensity[1], 0.f, 1.f),
	BODYCAM_FLOAT(classic_regrip_3_intensity, regrip.intensity[2], 0.f, 1.f),
	BODYCAM_FLOAT(classic_regrip_4_intensity, regrip.intensity[3], 0.f, 1.f),
	BODYCAM_FLOAT(classic_recoil_hip_intensity, recoil.hip, 0.f, 2.f),
	BODYCAM_FLOAT(classic_recoil_ads_intensity, recoil.ads, 0.f, 2.f),
	BODYCAM_FLOAT(classic_recoil_fire_impulse_share, recoil.fire_impulse_share, 0.f, 1.f),

	// Footstep camera.
	BODYCAM_FLOAT(footstep_camera_bob, footstep.bob, 0.f, 2.f),
	BODYCAM_FLOAT(footstep_camera_posture, footstep.posture, 0.f, 2.f),
	BODYCAM_FLOAT(footstep_camera_sprint_lunge, footstep.sprint_lunge, 0.f, 2.f),
	BODYCAM_FLOAT(footstep_camera_ads, footstep.ads, 0.f, 1.f),

	// Camera decoupling: head roll and shake, hip and ADS response, the follow lag.
	BODYCAM_FLOAT(camera_reference_roll, head.roll, 0.f, 2.f),
	BODYCAM_FLOAT(camera_reference_shake, head.shake, 0.f, 2.f),
	BODYCAM_FLOAT(hip_camera_inner_zone_response, camera.hip.inner_gain, 0.f, 1.f),
	BODYCAM_FLOAT(hip_camera_follow_speed, camera.hip.follow_speed, 0.1f, 30.f),
	BODYCAM_FLOAT(hip_camera_follow_damping, camera.hip.follow_damping, 0.f, 3.f),
	BODYCAM_FLOAT(hip_camera_deadzone_yaw, camera.hip.deadzone_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(hip_camera_deadzone_pitch, camera.hip.deadzone_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(hip_camera_soft_follow_yaw, camera.hip.softzone_yaw, 0.f, 45.f),
	BODYCAM_FLOAT(hip_camera_soft_follow_pitch, camera.hip.softzone_pitch, 0.f, 45.f),
	BODYCAM_FLOAT(hip_camera_max_yaw_offset, camera.hip.max_yaw, 0.f, 90.f),
	BODYCAM_FLOAT(hip_camera_max_pitch_offset, camera.hip.max_pitch, 0.f, 90.f),
	BODYCAM_FLOAT(hip_camera_position_scale, camera.hip.pos, 0.f, 0.25f),
	BODYCAM_FLOAT(ads_camera_inner_zone_response, camera.ads.inner_gain, 0.f, 1.f),
	BODYCAM_FLOAT(ads_camera_follow_speed, camera.ads.follow_speed, 0.1f, 30.f),
	BODYCAM_FLOAT(ads_camera_follow_damping, camera.ads.follow_damping, 0.f, 3.f),
	BODYCAM_FLOAT(ads_camera_deadzone_yaw, camera.ads.deadzone_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(ads_camera_deadzone_pitch, camera.ads.deadzone_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(ads_camera_soft_follow_yaw, camera.ads.softzone_yaw, 0.f, 45.f),
	BODYCAM_FLOAT(ads_camera_soft_follow_pitch, camera.ads.softzone_pitch, 0.f, 45.f),
	BODYCAM_FLOAT(ads_camera_max_yaw_offset, camera.ads.max_yaw, 0.f, 90.f),
	BODYCAM_FLOAT(ads_camera_max_pitch_offset, camera.ads.max_pitch, 0.f, 90.f),
	BODYCAM_FLOAT(ads_camera_position_scale, camera.ads.pos, 0.f, 0.25f),
	BODYCAM_FLOAT(movement_camera_position_scale, camera.move_pos, 0.f, 0.25f),
	BODYCAM_FLOAT(camera_follow_lag_speed_min, camera.follow.lag_speed_min, 0.1f, 30.f),
	BODYCAM_FLOAT(camera_follow_lag_speed_max, camera.follow.lag_speed_max, 0.1f, 60.f),
	BODYCAM_FLOAT(camera_follow_lag_response, camera.follow.lag_response, 0.1f, 30.f),
	BODYCAM_FLOAT(camera_follow_aim_smoothing, camera.follow.control_smoothing, 1.f, 60.f),

	// Shared input smoothing: mouse speed and movement intent.
	BODYCAM_FLOAT(mouse_smoothing_speed, sim.input.mouse_filter, 0.1f, 60.f),
	BODYCAM_FLOAT(movement_smoothing_speed, sim.input.move_filter, 0.1f, 60.f),

	// Movement inertia. Speed mods own the target speed; bodycam owns time-to-target.
	BODYCAM_FLOAT(movement_acceleration_time, movement_inertia.accel_time, 0.f, 2.f),
	BODYCAM_FLOAT(movement_deceleration_time, movement_inertia.decel_time, 0.f, 2.f),
	BODYCAM_FLOAT(movement_turn_response, movement_inertia.turn_response, 0.f, 1.f),
	BODYCAM_FLOAT(movement_stop_response, movement_inertia.stop_response, 0.f, 2.f),
	BODYCAM_FLOAT(movement_sprint_speed_scale, movement_inertia.sprint_speed_scale, 0.1f, 3.f),

	// Sprint transition camera sway.
	BODYCAM_FLOAT(sprint_camera_sway_strength, sim.sprint.strength, 0.f, 2.f),
	BODYCAM_FLOAT(sprint_camera_sway_smoothing, sim.sprint.smoothness, 0.f, 1.f),
	BODYCAM_FLOAT(sprint_transition_gun_bob, sprint_start.gun_bob, 0.f, 1.f),

	// Fire impulse.
	BODYCAM_FLOAT(hip_fire_weapon_impulse, sim.fire.hip, 0.f, 5.f),
	BODYCAM_FLOAT(ads_fire_weapon_impulse, sim.fire.ads, 0.f, 5.f),
	BODYCAM_FLOAT(fire_impulse_ads_scale, sim.fire.ads_scale, 0.f, 1.f),
	BODYCAM_FLOAT(fire_impulse_follow_speed, sim.fire.follow_speed, 0.1f, 30.f),
	BODYCAM_FLOAT(fire_impulse_damping, sim.fire.damping, 0.f, 3.f),
	BODYCAM_FLOAT(fire_impulse_decay_speed, sim.fire.decay, 0.1f, 60.f),
	BODYCAM_FLOAT(fire_impulse_max_position_offset, sim.fire.pos_cap, 0.f, 0.5f),
	BODYCAM_FLOAT(fire_impulse_max_rotation_offset, sim.fire.rot_cap, 0.f, 45.f),
	BODYCAM_FLOAT(fire_impulse_kick_drop, sim.fire.kick_drop, 0.f, 0.01f),
	BODYCAM_FLOAT(fire_impulse_kick_back, sim.fire.kick_back, 0.f, 0.02f),
	BODYCAM_FLOAT(fire_impulse_kick_pitch, sim.fire.kick_pitch, 0.f, 2.f),
	BODYCAM_FLOAT(fire_impulse_kick_roll, sim.fire.kick_roll, 0.f, 1.f),

	// Viewmodel lowering.
	BODYCAM_FLOAT(vm_lowering_pitch, sim.lowering.pitch, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_yaw, sim.lowering.yaw, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_roll, sim.lowering.roll, -45.f, 45.f),
	BODYCAM_FLOAT(vm_lowering_x, sim.lowering.x, -1.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_y, sim.lowering.y, -1.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_z, sim.lowering.z, -1.f, 1.f),
	BODYCAM_FLOAT(vm_safemode_lowering_y_offset, sim.lowering.holster_offset, 0.f, 0.2f),
	BODYCAM_FLOAT(vm_lowering_slow_walk_influence, sim.lowering.slow_walk, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_walk_influence, sim.lowering.walk, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_movement_influence, sim.lowering.move, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_enter_speed, sim.lowering.speed, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_return_speed, sim.lowering.return_speed, 0.f, 1.f),
	BODYCAM_FLOAT(vm_lowering_fire_suppression_time, sim.lowering.fire_timeout, 0.f, 2.f),
	BODYCAM_FLOAT(vm_lowering_ads_release_time, sim.lowering.aim_timeout, 0.f, 2.f),
	BODYCAM_FLOAT(vm_lowering_combat_suppression_time, sim.lowering.combat_timeout, 0.f, 30.f),
	BODYCAM_FLOAT(vm_lowering_layer_weight, sim.lowering.weight, 0.f, 1.f),

	// STALKER 2 style arm IK.
	BODYCAM_FLOAT(stalker2_arm_ik_strength, sim.stalker2_arm.strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_response, sim.stalker2_arm.response, 0.1f, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_follow_response, sim.stalker2_arm.arm_follow_response, kArmFollowResponseMin, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_follow_scale, sim.stalker2_arm.arm_follow_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_ads_scale, sim.stalker2_arm.ads_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_strength, sim.stalker2_arm.mouse_strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_sensitivity, sim.stalker2_arm.mouse_sensitivity, 0.1f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_yaw, sim.stalker2_arm.mouse_max_yaw, 0.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_pitch, sim.stalker2_arm.mouse_max_pitch, 0.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_max_roll, sim.stalker2_arm.mouse_max_roll, 0.f, 45.f),
	BODYCAM_FLOAT(stalker2_arm_ik_movement_strength, sim.stalker2_arm.movement_strength, 0.f, 4.f),
	BODYCAM_FLOAT(stalker2_arm_ik_movement_response, sim.stalker2_arm.movement_response, 0.1f, 40.f),
	BODYCAM_FLOAT(stalker2_arm_ik_slow_walk_scale, sim.stalker2_arm.slow_walk_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_pitch, sim.stalker2_arm.mouse_pitch, -20.f, 20.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_yaw, sim.stalker2_arm.mouse_yaw, -20.f, 20.f),
	BODYCAM_FLOAT(stalker2_arm_ik_mouse_roll, sim.stalker2_arm.mouse_roll, -30.f, 30.f),
	BODYCAM_FLOAT(stalker2_arm_ik_wrist_scale, sim.stalker2_arm.wrist_scale, 0.f, 1.f),
	BODYCAM_FLOAT(stalker2_arm_ik_layer_weight, sim.stalker2_arm.weight, 0.f, 1.f),
};

BoolBinding g_bool_bindings[] = {
	// Reference mode.
	BODYCAM_BOOL(reference_movement_enable, reference.movement),
	BODYCAM_BOOL(reference_camera_shake_enable, reference.camera_shake),
	BODYCAM_BOOL(reference_weapon_sway_enable, reference.weapon_sway),
	BODYCAM_BOOL(reference_stress_enable, reference.stress),
	BODYCAM_BOOL(reference_sight_regrip_enable, reference.sight_regrip),
	BODYCAM_BOOL(scope_effects_enable, scope_effects_enable),
	// Classic features: one enable flag each.
	BODYCAM_BOOL(camera_decoupling_enable, camera.enable),
	BODYCAM_BOOL(camera_continuous_follow_enable, camera.follow_lag),
	BODYCAM_BOOL(footstep_camera_enable, footstep.enable),
	BODYCAM_BOOL(classic_regrip_enable, regrip.enable),
	BODYCAM_BOOL(classic_recoil_enable, recoil.enable),
	BODYCAM_BOOL(directional_weapon_inertia_enable, handling.directional_weapon_inertia),
	BODYCAM_BOOL(movement_linked_handling_enable, handling.movement_linked_handling),
	BODYCAM_BOOL(vm_lowering_enable, sim.lowering.enable),
	BODYCAM_BOOL(vm_lowering_disable_in_combat, sim.lowering.disable_in_combat),
	BODYCAM_BOOL(stalker2_style_arm_ik_enable, sim.stalker2_arm.enable),
	BODYCAM_BOOL(fire_impulse_enable, sim.fire.enable),
	BODYCAM_BOOL(sprint_transition_enable, sim.sprint.enable),
	BODYCAM_BOOL(movement_inertia_enable, movement_inertia.enable),
	BODYCAM_BOOL(movement_inertia_disable_ads, movement_inertia.disable_ads),
};

#undef BODYCAM_BOOL
#undef BODYCAM_FLOAT

constexpr int kPresetOff = 0;

// Every feature a preset switches: off in the off preset, on in the others.
bool* const g_preset_features[] = {
	&g_config.camera.enable,
	&g_config.sim.lowering.enable,
	&g_config.sim.stalker2_arm.enable,
	&g_config.sim.fire.enable,
	&g_config.sim.sprint.enable,
	&g_config.movement_inertia.enable,
	&g_config.footstep.enable,
	&g_config.regrip.enable,
	&g_config.recoil.enable,
	&g_config.handling.directional_weapon_inertia,
	&g_config.handling.movement_linked_handling,
};

struct PresetFloat
{
	float* value;
	float off, balanced;
};

const PresetFloat g_preset_floats[] = {
	{ &g_config.camera.hip.inner_gain, 0.f, 0.04f },
	{ &g_config.camera.hip.follow_speed, 12.f, 7.f },
	{ &g_config.camera.hip.deadzone_yaw, 1.f, 1.5f },
	{ &g_config.camera.hip.deadzone_pitch, 0.75f, 1.f },
	{ &g_config.camera.hip.softzone_yaw, 2.f, 4.f },
	{ &g_config.camera.hip.softzone_pitch, 1.5f, 3.f },
	{ &g_config.camera.hip.max_yaw, 4.f, 9.f },
	{ &g_config.camera.hip.max_pitch, 3.f, 6.f },
	{ &g_config.camera.hip.pos, 0.f, 0.025f },
	{ &g_config.camera.move_pos, 0.f, 0.012f },
	{ &g_config.sim.fire.follow_speed, 9.f, 6.f },
	{ &g_config.sim.fire.ads_scale, 0.f, 0.3f },
	{ &g_config.sim.fire.hip, 0.f, 5.0f },
	{ &g_config.sim.fire.ads, 0.f, 1.0f },
	{ &g_config.movement_inertia.accel_time, 0.10f, 0.30f },
	{ &g_config.movement_inertia.decel_time, 0.10f, 0.40f },
	{ &g_config.movement_inertia.turn_response, 0.f, 0.45f },
	{ &g_config.movement_inertia.stop_response, 0.10f, 0.50f },
	{ &g_config.movement_inertia.sprint_speed_scale, 1.f, 1.f },
	{ &g_config.sim.sprint.strength, 0.f, 0.85f },
	{ &g_config.sim.sprint.smoothness, 0.45f, 0.45f },
	// Both presets share these values.
	{ &g_config.camera.hip.follow_damping, 1.f, 1.f },
	{ &g_config.sim.fire.damping, 1.f, 1.f },
	{ &g_config.sim.stalker2_arm.arm_follow_response, 12.f, 12.f },
	{ &g_config.sim.stalker2_arm.ads_scale, 0.55f, 0.55f },
	{ &g_config.sim.stalker2_arm.mouse_sensitivity, 2.f, 2.f },
	{ &g_config.sim.stalker2_arm.mouse_max_yaw, 5.25f, 5.25f },
	{ &g_config.sim.stalker2_arm.mouse_max_pitch, 13.5f, 13.5f },
	{ &g_config.sim.stalker2_arm.mouse_max_roll, 18.3f, 18.3f },
	{ &g_config.sim.stalker2_arm.mouse_yaw, 20.f, 20.f },
};

void SyncConsoleMirrors()
{
	for (BoolBinding& binding : g_bool_bindings)
		binding.console_value = *binding.value;
}

const FloatBinding* FindFloat(LPCSTR name)
{
	if (name)
		for (const FloatBinding& binding : g_float_bindings)
			if (xr_strcmp(binding.name, name) == 0)
				return &binding;
	return nullptr;
}

BoolBinding* FindBool(LPCSTR name)
{
	if (name)
		for (BoolBinding& binding : g_bool_bindings)
			if (xr_strcmp(binding.name, name) == 0)
				return &binding;
	return nullptr;
}
} // namespace

const FloatBinding* GetFloatBindings(u32& count)
{
	count = _countof(g_float_bindings);
	return g_float_bindings;
}

BoolBinding* GetBoolBindings(u32& count)
{
	count = _countof(g_bool_bindings);
	return g_bool_bindings;
}

void DumpConfigBindings()
{
	Msg("* bodycam config floats=%u bools=%u", static_cast<u32>(_countof(g_float_bindings)), static_cast<u32>(_countof(g_bool_bindings)));
	for (const FloatBinding& binding : g_float_bindings)
		Msg("* bodycam config %s (%s)=%0.4f default=%0.4f range[%0.4f %0.4f]", binding.name, binding.console_name, *binding.value,
			binding.default_value, binding.min_value, binding.max_value);
	for (const BoolBinding& binding : g_bool_bindings)
		Msg("* bodycam config %s (%s)=%d default=%d", binding.name, binding.console_name, *binding.value ? 1 : 0,
			binding.default_value ? 1 : 0);
}

bool FootstepCameraEnabled()
{
	return g_config.footstep.enable;
}

bool SprintTransitionEnabled()
{
	return g_config.sim.sprint.enable;
}

bool SprintBobEnabled()
{
	return g_config.sim.sprint.enable && g_config.sprint_start.gun_bob > 0.f;
}

bool AnyEffectEnabled()
{
	const RuntimeConfig& config = g_config;
	return config.camera.enable || config.sim.sprint.enable || config.sim.lowering.enable || config.sim.fire.enable ||
		config.sim.stalker2_arm.enable || config.footstep.enable || config.regrip.enable || config.recoil.enable ||
		config.handling.directional_weapon_inertia || config.handling.movement_linked_handling;
}

bool GetFloat(LPCSTR name, float& value)
{
	const FloatBinding* binding = FindFloat(name);
	if (binding)
		value = *binding->value;
	return !!binding;
}

bool SetFloat(LPCSTR name, float value)
{
	if (!std::isfinite(value))
		return false;
	const FloatBinding* binding = FindFloat(name);
	if (binding)
		*binding->value = std::clamp(value, binding->min_value, binding->max_value);
	return !!binding;
}

bool GetBool(LPCSTR name, bool& value)
{
	const BoolBinding* binding = FindBool(name);
	if (binding)
		value = *binding->value;
	return !!binding;
}

bool SetBool(LPCSTR name, bool value)
{
	BoolBinding* binding = FindBool(name);
	if (binding)
	{
		*binding->value = value;
		binding->console_value = value;
	}
	return !!binding;
}

void ApplyPreset(int preset)
{
	if (preset < kPresetOff || preset > kDefaultPreset)
	{
		Msg("! bodycam_preset %d rejected: valid presets are %d..%d", preset, kPresetOff, kDefaultPreset);
		return;
	}
	// Classic presets leave the motion mode, Reference choices and the scope effects alone.
	const ReferenceFeatureSettings reference = g_config.reference;
	const bool scope_effects = g_config.scope_effects_enable;
	g_config = kDefaults;
	g_config.scope_effects_enable = scope_effects;
	g_config.reference = reference;

	if (preset != kDefaultPreset)
	{
		for (bool* enable : g_preset_features)
			*enable = preset != kPresetOff;
		for (const PresetFloat& entry : g_preset_floats)
			*entry.value = preset == kPresetOff ? entry.off : entry.balanced;
	}
	SyncConsoleMirrors();
	Msg("* bodycam_preset %d applied", preset);
}
} // namespace Bodycam
