-- Port of the Classic parts of src/xrGame/bodycam_settings.h/.cpp: the runtime
-- config, the settings registry (name, default, range) and the presets.
-- Setting names are the original console names without the "bodycam_" prefix,
-- so docs/CLASSIC.md and the shipped presets still describe them.
--
-- Not ported (no Roblox counterpart in this port): Reference mode, regrip,
-- recoil, handling and scope effects settings.

local Simulation = require(script.Parent.Simulation)
local CameraRig = require(script.Parent.CameraRig)
local MovementInertia = require(script.Parent.MovementInertia)

local Settings = {}

-- RuntimeConfig with the shipped defaults.
function Settings.defaultConfig()
	return {
		sim = Simulation.defaultSettings(), -- Sprint sway, lowering, fire impulse, STALKER 2 arm IK.
		camera = CameraRig.defaultCameraSettings(),
		head = CameraRig.defaultHeadSettings(),
		footstep = CameraRig.defaultFootstepSettings(),
		movement_inertia = MovementInertia.defaultSettings(),
	}
end

-- name -> { path, min, max }. Paths index into the config table.
-- stylua: ignore
local FLOATS = {
	{ "footstep_camera_bob", "footstep.bob", 0, 2 },
	{ "footstep_camera_posture", "footstep.posture", 0, 2 },
	{ "footstep_camera_sprint_lunge", "footstep.sprint_lunge", 0, 2 },
	{ "footstep_camera_ads", "footstep.ads", 0, 1 },
	{ "camera_reference_roll", "head.roll", 0, 2 },
	{ "camera_reference_shake", "head.shake", 0, 2 },
	{ "hip_camera_inner_zone_response", "camera.hip.inner_gain", 0, 1 },
	{ "hip_camera_follow_speed", "camera.hip.follow_speed", 0.1, 30 },
	{ "hip_camera_follow_damping", "camera.hip.follow_damping", 0, 3 },
	{ "hip_camera_deadzone_yaw", "camera.hip.deadzone_yaw", 0, 30 },
	{ "hip_camera_deadzone_pitch", "camera.hip.deadzone_pitch", 0, 30 },
	{ "hip_camera_soft_follow_yaw", "camera.hip.softzone_yaw", 0, 45 },
	{ "hip_camera_soft_follow_pitch", "camera.hip.softzone_pitch", 0, 45 },
	{ "hip_camera_max_yaw_offset", "camera.hip.max_yaw", 0, 90 },
	{ "hip_camera_max_pitch_offset", "camera.hip.max_pitch", 0, 90 },
	{ "hip_camera_position_scale", "camera.hip.pos", 0, 0.25 },
	{ "ads_camera_inner_zone_response", "camera.ads.inner_gain", 0, 1 },
	{ "ads_camera_follow_speed", "camera.ads.follow_speed", 0.1, 30 },
	{ "ads_camera_follow_damping", "camera.ads.follow_damping", 0, 3 },
	{ "ads_camera_deadzone_yaw", "camera.ads.deadzone_yaw", 0, 30 },
	{ "ads_camera_deadzone_pitch", "camera.ads.deadzone_pitch", 0, 30 },
	{ "ads_camera_soft_follow_yaw", "camera.ads.softzone_yaw", 0, 45 },
	{ "ads_camera_soft_follow_pitch", "camera.ads.softzone_pitch", 0, 45 },
	{ "ads_camera_max_yaw_offset", "camera.ads.max_yaw", 0, 90 },
	{ "ads_camera_max_pitch_offset", "camera.ads.max_pitch", 0, 90 },
	{ "ads_camera_position_scale", "camera.ads.pos", 0, 0.25 },
	{ "movement_camera_position_scale", "camera.move_pos", 0, 0.25 },
	{ "camera_follow_lag_speed_min", "camera.follow.lag_speed_min", 0.1, 30 },
	{ "camera_follow_lag_speed_max", "camera.follow.lag_speed_max", 0.1, 60 },
	{ "camera_follow_lag_response", "camera.follow.lag_response", 0.1, 30 },
	{ "camera_follow_aim_smoothing", "camera.follow.control_smoothing", 1, 60 },
	{ "mouse_smoothing_speed", "sim.input.mouse_filter", 0.1, 60 },
	{ "movement_smoothing_speed", "sim.input.move_filter", 0.1, 60 },
	{ "movement_acceleration_time", "movement_inertia.accel_time", 0, 2 },
	{ "movement_deceleration_time", "movement_inertia.decel_time", 0, 2 },
	{ "movement_turn_response", "movement_inertia.turn_response", 0, 1 },
	{ "movement_stop_response", "movement_inertia.stop_response", 0, 2 },
	{ "movement_sprint_speed_scale", "movement_inertia.sprint_speed_scale", 0.1, 3 },
	{ "sprint_camera_sway_strength", "sim.sprint.strength", 0, 2 },
	{ "sprint_camera_sway_smoothing", "sim.sprint.smoothness", 0, 1 },
	{ "hip_fire_weapon_impulse", "sim.fire.hip", 0, 5 },
	{ "ads_fire_weapon_impulse", "sim.fire.ads", 0, 5 },
	{ "fire_impulse_ads_scale", "sim.fire.ads_scale", 0, 1 },
	{ "fire_impulse_follow_speed", "sim.fire.follow_speed", 0.1, 30 },
	{ "fire_impulse_damping", "sim.fire.damping", 0, 3 },
	{ "fire_impulse_decay_speed", "sim.fire.decay", 0.1, 60 },
	{ "fire_impulse_max_position_offset", "sim.fire.pos_cap", 0, 0.5 },
	{ "fire_impulse_max_rotation_offset", "sim.fire.rot_cap", 0, 45 },
	{ "fire_impulse_kick_drop", "sim.fire.kick_drop", 0, 0.01 },
	{ "fire_impulse_kick_back", "sim.fire.kick_back", 0, 0.02 },
	{ "fire_impulse_kick_pitch", "sim.fire.kick_pitch", 0, 2 },
	{ "fire_impulse_kick_roll", "sim.fire.kick_roll", 0, 1 },
	{ "vm_lowering_pitch", "sim.lowering.pitch", -45, 45 },
	{ "vm_lowering_yaw", "sim.lowering.yaw", -45, 45 },
	{ "vm_lowering_roll", "sim.lowering.roll", -45, 45 },
	{ "vm_lowering_x", "sim.lowering.x", -1, 1 },
	{ "vm_lowering_y", "sim.lowering.y", -1, 1 },
	{ "vm_lowering_z", "sim.lowering.z", -1, 1 },
	{ "vm_safemode_lowering_y_offset", "sim.lowering.holster_offset", 0, 0.2 },
	{ "vm_lowering_slow_walk_influence", "sim.lowering.slow_walk", 0, 1 },
	{ "vm_lowering_walk_influence", "sim.lowering.walk", 0, 1 },
	{ "vm_lowering_movement_influence", "sim.lowering.move", 0, 1 },
	{ "vm_lowering_enter_speed", "sim.lowering.speed", 0, 1 },
	{ "vm_lowering_return_speed", "sim.lowering.return_speed", 0, 1 },
	{ "vm_lowering_fire_suppression_time", "sim.lowering.fire_timeout", 0, 2 },
	{ "vm_lowering_ads_release_time", "sim.lowering.aim_timeout", 0, 2 },
	{ "vm_lowering_combat_suppression_time", "sim.lowering.combat_timeout", 0, 30 },
	{ "vm_lowering_layer_weight", "sim.lowering.weight", 0, 1 },
	{ "stalker2_arm_ik_strength", "sim.stalker2_arm.strength", 0, 4 },
	{ "stalker2_arm_ik_response", "sim.stalker2_arm.response", 0.1, 40 },
	{ "stalker2_arm_ik_follow_response", "sim.stalker2_arm.arm_follow_response", Simulation.ARM_FOLLOW_RESPONSE_MIN, 40 },
	{ "stalker2_arm_ik_follow_scale", "sim.stalker2_arm.arm_follow_scale", 0, 1 },
	{ "stalker2_arm_ik_ads_scale", "sim.stalker2_arm.ads_scale", 0, 1 },
	{ "stalker2_arm_ik_mouse_strength", "sim.stalker2_arm.mouse_strength", 0, 4 },
	{ "stalker2_arm_ik_mouse_sensitivity", "sim.stalker2_arm.mouse_sensitivity", 0.1, 4 },
	{ "stalker2_arm_ik_mouse_max_yaw", "sim.stalker2_arm.mouse_max_yaw", 0, 30 },
	{ "stalker2_arm_ik_mouse_max_pitch", "sim.stalker2_arm.mouse_max_pitch", 0, 30 },
	{ "stalker2_arm_ik_mouse_max_roll", "sim.stalker2_arm.mouse_max_roll", 0, 45 },
	{ "stalker2_arm_ik_movement_strength", "sim.stalker2_arm.movement_strength", 0, 4 },
	{ "stalker2_arm_ik_movement_response", "sim.stalker2_arm.movement_response", 0.1, 40 },
	{ "stalker2_arm_ik_slow_walk_scale", "sim.stalker2_arm.slow_walk_scale", 0, 1 },
	{ "stalker2_arm_ik_mouse_pitch", "sim.stalker2_arm.mouse_pitch", -20, 20 },
	{ "stalker2_arm_ik_mouse_yaw", "sim.stalker2_arm.mouse_yaw", -20, 20 },
	{ "stalker2_arm_ik_mouse_roll", "sim.stalker2_arm.mouse_roll", -30, 30 },
	{ "stalker2_arm_ik_wrist_scale", "sim.stalker2_arm.wrist_scale", 0, 1 },
	{ "stalker2_arm_ik_layer_weight", "sim.stalker2_arm.weight", 0, 1 },
}

-- stylua: ignore
local BOOLS = {
	{ "camera_decoupling_enable", "camera.enable" },
	{ "camera_continuous_follow_enable", "camera.follow_lag" },
	{ "footstep_camera_enable", "footstep.enable" },
	{ "vm_lowering_enable", "sim.lowering.enable" },
	{ "vm_lowering_disable_in_combat", "sim.lowering.disable_in_combat" },
	{ "stalker2_style_arm_ik_enable", "sim.stalker2_arm.enable" },
	{ "fire_impulse_enable", "sim.fire.enable" },
	{ "sprint_transition_enable", "sim.sprint.enable" },
	{ "movement_inertia_enable", "movement_inertia.enable" },
	{ "movement_inertia_disable_ads", "movement_inertia.disable_ads" },
}

-- Every feature a preset switches: off in the off preset, on in the others.
local PRESET_FEATURES = {
	"camera.enable",
	"sim.lowering.enable",
	"sim.stalker2_arm.enable",
	"sim.fire.enable",
	"sim.sprint.enable",
	"movement_inertia.enable",
	"footstep.enable",
}

-- { path, off, balanced }
-- stylua: ignore
local PRESET_FLOATS = {
	{ "camera.hip.inner_gain", 0, 0.04 },
	{ "camera.hip.follow_speed", 12, 7 },
	{ "camera.hip.deadzone_yaw", 1, 1.5 },
	{ "camera.hip.deadzone_pitch", 0.75, 1 },
	{ "camera.hip.softzone_yaw", 2, 4 },
	{ "camera.hip.softzone_pitch", 1.5, 3 },
	{ "camera.hip.max_yaw", 4, 9 },
	{ "camera.hip.max_pitch", 3, 6 },
	{ "camera.hip.pos", 0, 0.025 },
	{ "camera.move_pos", 0, 0.012 },
	{ "sim.fire.follow_speed", 9, 6 },
	{ "sim.fire.ads_scale", 0, 0.3 },
	{ "sim.fire.hip", 0, 5.0 },
	{ "sim.fire.ads", 0, 1.0 },
	{ "movement_inertia.accel_time", 0.10, 0.30 },
	{ "movement_inertia.decel_time", 0.10, 0.40 },
	{ "movement_inertia.turn_response", 0, 0.45 },
	{ "movement_inertia.stop_response", 0.10, 0.50 },
	{ "movement_inertia.sprint_speed_scale", 1, 1 },
	{ "sim.sprint.strength", 0, 0.85 },
	{ "sim.sprint.smoothness", 0.45, 0.45 },
	-- Both presets share these values.
	{ "camera.hip.follow_damping", 1, 1 },
	{ "sim.fire.damping", 1, 1 },
	{ "sim.stalker2_arm.arm_follow_response", 12, 12 },
	{ "sim.stalker2_arm.ads_scale", 0.55, 0.55 },
	{ "sim.stalker2_arm.mouse_sensitivity", 2, 2 },
	{ "sim.stalker2_arm.mouse_max_yaw", 5.25, 5.25 },
	{ "sim.stalker2_arm.mouse_max_pitch", 13.5, 13.5 },
	{ "sim.stalker2_arm.mouse_max_roll", 18.3, 18.3 },
	{ "sim.stalker2_arm.mouse_yaw", 20, 20 },
}

Settings.Preset = table.freeze({ Off = 0, Balanced = 1, Default = 2 })

local floatIndex, boolIndex = {}, {}
for _, entry in FLOATS do
	floatIndex[entry[1]] = entry
end
for _, entry in BOOLS do
	boolIndex[entry[1]] = entry
end

local function resolve(config, path: string)
	local parent, key = config, nil
	for part in string.gmatch(path, "[^%.]+") do
		if key ~= nil then
			parent = parent[key]
		end
		key = part
	end
	return parent, key
end

local function setPath(config, path: string, value)
	local parent, key = resolve(config, path)
	parent[key] = value
end

local function getPath(config, path: string)
	local parent, key = resolve(config, path)
	return parent[key]
end

-- Returns the value, or nil for an unknown name.
function Settings.get(config, name: string)
	local entry = floatIndex[name] or boolIndex[name]
	return if entry then getPath(config, entry[2]) else nil
end

-- Floats are clamped to their range; non-finite floats are rejected. Returns success.
function Settings.set(config, name: string, value): boolean
	local float = floatIndex[name]
	if float then
		if type(value) ~= "number" or value ~= value or math.abs(value) == math.huge then
			return false
		end
		setPath(config, float[2], math.clamp(value, float[3], float[4]))
		return true
	end
	local bool = boolIndex[name]
	if bool then
		setPath(config, bool[2], value == true)
		return true
	end
	return false
end

-- { name, kind = "float" | "bool", default, min, max } for building an options UI.
function Settings.list()
	local defaults = Settings.defaultConfig()
	local result = {}
	for _, e in FLOATS do
		table.insert(result, { name = e[1], kind = "float", default = getPath(defaults, e[2]), min = e[3], max = e[4] })
	end
	for _, e in BOOLS do
		table.insert(result, { name = e[1], kind = "bool", default = getPath(defaults, e[2]) })
	end
	return result
end

-- 0 off, 1 balanced, 2 the shipped defaults. Replaces the config contents in place.
function Settings.applyPreset(config, preset: number): boolean
	if preset ~= 0 and preset ~= 1 and preset ~= 2 then
		return false
	end
	local fresh = Settings.defaultConfig()
	for key in config do
		config[key] = nil
	end
	for key, value in fresh do
		config[key] = value
	end
	if preset ~= Settings.Preset.Default then
		for _, path in PRESET_FEATURES do
			setPath(config, path, preset ~= Settings.Preset.Off)
		end
		for _, e in PRESET_FLOATS do
			setPath(config, e[1], if preset == Settings.Preset.Off then e[2] else e[3])
		end
	end
	return true
end

function Settings.anyEffectEnabled(config): boolean
	return config.camera.enable
		or config.sim.sprint.enable
		or config.sim.lowering.enable
		or config.sim.fire.enable
		or config.sim.stalker2_arm.enable
		or config.footstep.enable
end

return Settings
