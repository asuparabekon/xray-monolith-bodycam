-- Port of src/xrGame/bodycam_viewmodel_profile.h/.cpp.
-- A viewmodel offset set by scripts (per weapon), blended in and out and faded with ADS.

local MathUtil = require(script.Parent.MathUtil)
local Vec3 = require(script.Parent.Vec3)

local DEFAULT_BLEND_SPEED = 8 -- 1/s
-- Limits on what a script may ask for.
local MIN_BLEND_SPEED = 0.01
local MAX_BLEND_SPEED = 30
local MAX_POSITION_OFFSET = 0.5 -- metres
local MAX_ROTATION_OFFSET = 45 -- degrees
local ACTIVE_EPSILON = 0.00001

local ViewmodelProfile = {}
ViewmodelProfile.DEFAULT_BLEND_SPEED = DEFAULT_BLEND_SPEED

local function sanitize(value: number, limit: number): number
	return if MathUtil.isFinite(value) then MathUtil.clamp(value, -limit, limit) else 0
end

local function sanitizeVec(v, limit: number)
	return Vec3.new(sanitize(v.x, limit), sanitize(v.y, limit), sanitize(v.z, limit))
end

local function sanitizeBlendSpeed(value: number): number
	if not MathUtil.isFinite(value) then
		return DEFAULT_BLEND_SPEED
	end
	return MathUtil.clamp(value, MIN_BLEND_SPEED, MAX_BLEND_SPEED)
end

local function nearlyZero(v): boolean
	return math.abs(v.x) < ACTIVE_EPSILON and math.abs(v.y) < ACTIVE_EPSILON and math.abs(v.z) < ACTIVE_EPSILON
end

function ViewmodelProfile.newState()
	return {
		target_pos = Vec3.new(),
		target_rot = Vec3.new(),
		current_pos = Vec3.new(),
		current_rot = Vec3.new(),
		blend_speed = DEFAULT_BLEND_SPEED,
		enabled = false,
	}
end

-- pos in metres (right, up, forward), rot in degrees (yaw, pitch, roll).
function ViewmodelProfile.set(state, pos, rot, blendSpeed: number?)
	state.target_pos = sanitizeVec(pos, MAX_POSITION_OFFSET)
	state.target_rot = sanitizeVec(rot, MAX_ROTATION_OFFSET)
	state.blend_speed = sanitizeBlendSpeed(blendSpeed or DEFAULT_BLEND_SPEED)
	state.enabled = true
end

function ViewmodelProfile.clear(state, blendSpeed: number?)
	state.target_pos = Vec3.new()
	state.target_rot = Vec3.new()
	state.blend_speed = sanitizeBlendSpeed(blendSpeed or DEFAULT_BLEND_SPEED)
	state.enabled = false
end

-- adsBlend is 0..1. Returns { pos, rot, active }.
function ViewmodelProfile.update(state, dt: number, adsBlend: number)
	MathUtil.smoothVector(state.current_pos, state.target_pos, state.blend_speed, dt)
	MathUtil.smoothVector(state.current_rot, state.target_rot, state.blend_speed, dt)
	local pos = Vec3.copy(state.current_pos)
	local rot = Vec3.copy(state.current_rot)
	Vec3.mul(pos, 1 - adsBlend)
	Vec3.mul(rot, 1 - adsBlend)
	return { pos = pos, rot = rot, active = not nearlyZero(pos) or not nearlyZero(rot) }
end

function ViewmodelProfile.isActive(state): boolean
	return state.enabled or not nearlyZero(state.current_pos) or not nearlyZero(state.current_rot)
end

return ViewmodelProfile
