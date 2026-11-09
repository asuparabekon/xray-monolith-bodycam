-- Port of Bodycam::CBodycam (src/xrGame/bodycam_camera.h/.cpp): the Classic
-- simulation and camera rig of one character, and what they publish to the
-- camera and the viewmodel. No Roblox types: the Roblox adapter
-- (Roblox/BodycamController.luau) gathers a frame and applies the result.
--
-- Not ported: the PiP scope adapter, the sprint-start gun bob and diagnostics dumps.

local Vec3 = require(script.Parent.Vec3)
local Simulation = require(script.Parent.Simulation)
local CameraRig = require(script.Parent.CameraRig)
local MouseAim = require(script.Parent.MouseAim)
local ViewmodelProfile = require(script.Parent.ViewmodelProfile)
local MovementInertia = require(script.Parent.MovementInertia)
local Settings = require(script.Parent.Settings)

local BodycamCore = {}
BodycamCore.__index = BodycamCore

-- config: Settings.defaultConfig() (shared and editable at runtime).
function BodycamCore.new(config)
	local self = setmetatable({}, BodycamCore)
	self.config = config or Settings.defaultConfig()
	self.state = Simulation.newState()
	self.simOutput = Simulation.newOutput()
	self.rig = CameraRig.new()
	self.mouseAim = MouseAim.newState()
	self.viewmodelProfile = ViewmodelProfile.newState()
	self.inertia = MovementInertia.newState()
	self.movement = { target_speed = 0, filtered_speed = 0, speed_fraction = 0 }
	self:_clearHudOutput()
	return self
end

function BodycamCore:_clearViewmodelOutput()
	self.viewmodel = {
		active = false,
		pos = Vec3.new(), -- metres: right, up, forward
		rot = Vec3.new(), -- degrees: yaw, pitch, roll
		lowering_pos = Vec3.new(),
		lowering_rot = Vec3.new(),
	}
end

function BodycamCore:_clearHudOutput()
	self:_clearViewmodelOutput()
	self.armPose = {
		active = false,
		wrist_rot = Vec3.new(), -- radians: yaw, pitch, roll
		arm_follow_rot = Vec3.new(),
		movement_weight = 0,
		movement_response = Simulation.DEFAULT_STALKER2_MOVEMENT_RESPONSE,
	}
end

function BodycamCore:anyFeatureEnabled(): boolean
	return Settings.anyEffectEnabled(self.config) or ViewmodelProfile.isActive(self.viewmodelProfile)
end

-- Starts from the given aim (radians); without one, from the first frame's aim.
function BodycamCore:reset(yaw: number?, pitch: number?, adsBlend: number?)
	self:_clearHudOutput()
	self.inertia = MovementInertia.newState()
	if yaw == nil or pitch == nil then
		self.mouseAim = MouseAim.newState()
		self.state = Simulation.newState()
		self.rig:reset()
		return
	end
	MouseAim.reset(self.mouseAim, yaw, pitch)
	Simulation.resetState(self.state, yaw, pitch, adsBlend or 0)
	self.rig:reset(yaw, pitch)
end

-- Mouse deltas in the aim's yaw/pitch (radians), as handed to the camera this frame.
function BodycamCore:addMouseLookDelta(yawDelta: number, pitchDelta: number)
	MouseAim.addDelta(self.mouseAim, yawDelta, pitchDelta)
end

-- A script turned the camera by these deltas (radians).
function BodycamCore:rebaseLookDelta(yawDelta: number, pitchDelta: number)
	MouseAim.rebase(self.mouseAim, yawDelta, pitchDelta)
	Simulation.rebaseLook(self.state, yawDelta, pitchDelta)
	self.rig:rebase(yawDelta, pitchDelta)
end

-- Per-weapon hip offset set by game scripts. pos metres, rot degrees.
function BodycamCore:setViewmodelProfile(pos, rot, blendSpeed: number?)
	ViewmodelProfile.set(self.viewmodelProfile, pos, rot, blendSpeed)
end

function BodycamCore:clearViewmodelProfile(blendSpeed: number?)
	ViewmodelProfile.clear(self.viewmodelProfile, blendSpeed)
end

-- Movement Inertia on the host's commanded planar velocity (m/s, engine axes).
-- Returns the eased velocity the host should move with. Updates the speeds the
-- simulation reads, so call it before update() each frame.
function BodycamCore:updateMovement(dt: number, moveFlags: number, ads: boolean, targetVelocity)
	local out = MovementInertia.update(self.config.movement_inertia, self.inertia, {
		dt = dt,
		move_flags = moveFlags,
		ads = ads,
		target_velocity = targetVelocity,
	})
	self:setMovementSpeed(out.target_speed, out.filtered_speed, out.speed_fraction)
	return out.velocity
end

-- Speeds in m/s and the filtered speed over its target, from Movement Inertia.
function BodycamCore:setMovementSpeed(targetSpeed: number, filteredSpeed: number, speedFraction: number)
	self.movement.target_speed = targetSpeed
	self.movement.filtered_speed = filteredSpeed
	self.movement.speed_fraction = speedFraction
end

function BodycamCore:notifyShot(power: number, ads: boolean)
	Simulation.notifyShot(self.config.sim, self.state, power, ads)
end

-- frame = {
--   camera = CameraRig.newInput() fields (aim_yaw, aim_pitch, dt, ads, ...),
--   move_flags, accelerated, weapon_lowered, combat, firearm_equipped,
-- }
-- host = { horizontalClearance = function(right, forward, maxDistance) -> number? }
-- Returns the camera composition (CameraRig).
function BodycamCore:update(frame, host)
	local config = self.config
	local cameraSettings = {
		camera = config.camera,
		head = config.head,
		footstep = config.footstep,
		sprint_sway = config.sim.sprint.enable,
	}
	local followLag = CameraRig.usesFollowLag(config.camera)
	local camera = frame.camera
	camera.inertia_speed = self.movement.filtered_speed
	local aimYaw, aimPitch = MouseAim.resolve(self.mouseAim, camera.aim_yaw, camera.aim_pitch)

	local input = self.simInput or Simulation.newInput()
	self.simInput = input
	input.dt = camera.dt
	input.move_flags = frame.move_flags or 0
	input.ads = camera.ads.active
	input.ads_blend = camera.ads.blend
	input.weapon_lowered = frame.weapon_lowered or false
	input.combat = frame.combat or false
	input.accelerated = frame.accelerated or false
	input.firearm_equipped = frame.firearm_equipped ~= false
	input.actor_speed_fraction = self.movement.speed_fraction
	input.gun_first_follow = followLag
	input.sprint_camera_owned_elsewhere = config.footstep.enable
	input.mouse_aim_yaw = aimYaw
	input.mouse_aim_pitch = aimPitch

	local out = Simulation.update(config.sim, self.state, input, self.simOutput)

	local vm = self.viewmodel
	if out.viewmodel_active then
		vm.active = true
		Vec3.assign(vm.pos, out.viewmodel_pos)
		Vec3.assign(vm.rot, out.viewmodel_rot)
		Vec3.assign(vm.lowering_pos, out.lowering_pos)
		Vec3.assign(vm.lowering_rot, out.lowering_rot)
	else
		self:_clearViewmodelOutput()
		vm = self.viewmodel
	end
	local profile = ViewmodelProfile.update(self.viewmodelProfile, input.dt, input.ads_blend)
	if profile.active then
		vm.active = true
		Vec3.add(vm.pos, profile.pos)
		Vec3.add(vm.rot, profile.rot)
	end

	local arm = self.armPose
	arm.active = out.stalker2_arm_active
	Vec3.assign(arm.wrist_rot, out.stalker2_wrist_rot)
	Vec3.assign(arm.arm_follow_rot, out.stalker2_arm_follow_rot)
	arm.movement_weight = out.stalker2_movement_weight
	arm.movement_response = out.stalker2_movement_response

	return self.rig:compose(cameraSettings, camera, self.state.signals, out.sprint_sway, host)
end

-- loweringWeight 0..1 is the share of the lowering offset that is kept.
-- Returns pos (metres), rot (degrees: yaw, pitch, roll), or nil when inactive.
function BodycamCore:getHudOffset(loweringWeight: number?)
	local vm = self.viewmodel
	if not vm.active then
		return nil
	end
	local keep = (loweringWeight or 1) - 1
	local pos = Vec3.copy(vm.pos)
	local rot = Vec3.copy(vm.rot)
	Vec3.addXYZ(pos, vm.lowering_pos.x * keep, vm.lowering_pos.y * keep, vm.lowering_pos.z * keep)
	Vec3.addXYZ(rot, vm.lowering_rot.x * keep, vm.lowering_rot.y * keep, vm.lowering_rot.z * keep)
	return pos, rot
end

-- The STALKER 2 style arm pose (radians), or nil when inactive.
function BodycamCore:getArmPose()
	return if self.armPose.active then self.armPose else nil
end

return BodycamCore
