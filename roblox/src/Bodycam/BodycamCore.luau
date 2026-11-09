-- BodycamCore: runs the pure modules for one character, one frame at a time.
-- Pure Luau: takes plain numbers, returns plain numbers. Controller.luau turns them
-- into CFrames.
--
--   local core = BodycamCore.new(Settings.default())
--   core:reset(yaw, pitch)
--   core:kick(dYaw, dPitch)          -- aim kick (not lag)
--   core:notifyShot(power, ads)
--   local out = core:update(input)   -- see update()

local BMath = require(script.Parent.Math)
local Settings = require(script.Parent.Settings)
local LookLag = require(script.Parent.LookLag)
local HeadMotion = require(script.Parent.HeadMotion)
local WeaponMotion = require(script.Parent.WeaponMotion)
local MovementInertia = require(script.Parent.MovementInertia)
local StrideClock = require(script.Parent.StrideClock)

local BodycamCore = {}
BodycamCore.__index = BodycamCore

local MAX_DT = 0.25

-- config: a Settings table (default Settings.default()).
-- options.random: function() -> [0, 1) for the kick roll sign.
function BodycamCore.new(config, options)
	options = options or {}
	config = config or Settings.default()
	local self = setmetatable({}, BodycamCore)
	self.config = config
	self.lookLag = LookLag.new(config.lookLag)
	self.head = HeadMotion.new(config.head)
	self.weapon = WeaponMotion.new(config.weapon, options.random)
	self.inertia = MovementInertia.new(config.inertia)
	self.stride = StrideClock.new(config.stride)
	self.output = {}
	self:reset(0, 0)
	self.initialized = false
	return self
end

-- Clears all motion and snaps the view onto the aim.
function BodycamCore:reset(yaw: number, pitch: number)
	self.lookLag:reset(yaw, pitch)
	self.head:reset()
	self.weapon:reset()
	self.inertia:reset()
	self.lastYaw, self.lastPitch = yaw, pitch
	self.yawRate, self.pitchRate = 0, 0
	self.lastPhase = nil
	self.wasGrounded = true
	self.minVelY = 0
	self.initialized = true
end

-- An aim kick (recoil, script turn), radians. The view turns with it at once and it
-- does not count as mouse speed.
function BodycamCore:kick(dYaw: number, dPitch: number)
	self.lookLag:kick(dYaw, dPitch)
	self.lastYaw += dYaw
	self.lastPitch += dPitch
end

function BodycamCore:notifyShot(power: number?, ads: number?)
	self.weapon:notifyShot(power, ads)
end

function BodycamCore:setWeaponProfile(pos, rot, blendSpeed: number?)
	self.weapon:setProfile(pos, rot, blendSpeed)
end

function BodycamCore:clearWeaponProfile(blendSpeed: number?)
	self.weapon:clearProfile(blendSpeed)
end

-- Which side's contact the phase passed going from `from` to `to`, if any.
function BodycamCore.footContact(from: number?, to: number?): string?
	if from == nil or to == nil then
		return nil
	end
	local d = (to - from) % 1
	if d <= 0 or d >= 0.5 then
		return nil -- standing still, or too far to tell the direction
	end
	from = from % 1
	local reach = from + d
	if from < 0.5 and reach >= 0.5 then
		return "Right"
	elseif reach >= 1 then
		return "Left"
	end
	return nil
end

-- One frame.
-- input:
--   dt                       seconds
--   yaw, pitch               the aim (radians; yaw positive = left)
--   ads, aiming              ADS blend 0..1, aim key state
--   grounded, sprinting, crouching, climbing, lowered, leaning   booleans
--   velX, velY, velZ         character velocity, world studs/s
--   moveX, moveZ             commanded planar velocity, world studs/s (for inertia)
--   walkSpeed, sprintSpeed   studs/s
--   footPhase                the game's foot phase, or nil to use the stride clock
--   clearLeft, clearRight    free space beside the head (studs), optional
-- Returns a table (reused each frame):
--   viewYaw, viewPitch, lagX, lagY, lagZ           look lag
--   headX, headY, headZ, headPitch, headRoll       head motion
--   weaponX, weaponY, weaponZ, weaponPitch, weaponYaw, weaponRoll
--   wristPitch, wristRoll
--   footPhase, footstep ("Left" | "Right" | nil), planarSpeed, velRight, velForward,
--   yawRate, pitchRate, landingSpeed, moveX, moveZ, speedFraction, inertiaBypassed
function BodycamCore:update(input)
	local dt = BMath.clamp(input.dt or 0, 0, MAX_DT)
	local yaw, pitch = input.yaw, input.pitch
	if not self.initialized then
		self:reset(yaw, pitch)
	end
	local out = self.output

	-- Aim rates without kicks.
	local dYaw = BMath.wrapAngle(yaw - self.lastYaw)
	local dPitch = pitch - self.lastPitch
	self.lastYaw, self.lastPitch = yaw, pitch
	if dt > 0 then
		self.yawRate, self.pitchRate = dYaw / dt, dPitch / dt
	else
		self.yawRate, self.pitchRate = 0, 0
	end
	local turnRate = math.sqrt(self.yawRate * self.yawRate + self.pitchRate * self.pitchRate)

	-- Velocity in the aim's yaw frame.
	local c, s = math.cos(yaw), math.sin(yaw)
	local vx, vy, vz = input.velX or 0, input.velY or 0, input.velZ or 0
	local velRight = vx * c - vz * s
	local velForward = -vx * s - vz * c
	local planarSpeed = math.sqrt(vx * vx + vz * vz)

	local walkSpeed = input.walkSpeed or 16
	local sprintSpeed = input.sprintSpeed or walkSpeed * 1.6
	local grounded = input.grounded ~= false

	-- Landing: the fastest fall speed of the airtime.
	local landingSpeed = nil
	if not grounded then
		self.minVelY = math.min(self.minVelY, vy)
	elseif not self.wasGrounded then
		landingSpeed = math.max(-self.minVelY, 0)
		self.minVelY = 0
	end
	if grounded then
		self.minVelY = 0
	end
	self.wasGrounded = grounded

	-- Movement inertia.
	local state = { crouching = input.crouching, climbing = input.climbing, grounded = grounded, aiming = input.aiming }
	local mx, mz, fraction = self.inertia:update(dt, input.moveX or 0, input.moveZ or 0, state)

	-- Foot phase and contacts.
	local phase = input.footPhase
	if phase == nil then
		phase = self.stride:update(dt, planarSpeed, grounded, walkSpeed, sprintSpeed)
	else
		phase %= 1
	end
	local footstep = BodycamCore.footContact(self.lastPhase, phase)
	self.lastPhase = phase

	local ads = BMath.clamp(input.ads or 0, 0, 1)

	local lag = self.lookLag:update({
		dt = dt,
		yaw = yaw,
		pitch = pitch,
		ads = ads,
		turnRate = turnRate,
		velRight = velRight,
		velForward = velForward,
		walkSpeed = walkSpeed,
	})

	local head = self.head:update({
		dt = dt,
		phase = phase,
		speed = planarSpeed,
		walkSpeed = walkSpeed,
		sprintSpeed = sprintSpeed,
		grounded = grounded,
		sprinting = input.sprinting,
		leaning = input.leaning,
		ads = ads,
		velRight = velRight,
		yawRate = self.yawRate,
		landingSpeed = landingSpeed,
		clearLeft = input.clearLeft,
		clearRight = input.clearRight,
	})

	local weapon = self.weapon:update({
		dt = dt,
		ads = ads,
		aiming = input.aiming,
		sprinting = input.sprinting,
		grounded = grounded,
		leaning = input.leaning,
		lowered = input.lowered,
		speed = planarSpeed,
		velRight = velRight,
		velForward = velForward,
		walkSpeed = walkSpeed,
		yawRate = self.yawRate,
		pitchRate = self.pitchRate,
	})

	out.viewYaw, out.viewPitch = lag.yaw, lag.pitch
	out.lagX, out.lagY, out.lagZ = lag.shiftX, lag.shiftY, lag.shiftZ
	out.headX, out.headY, out.headZ = head.x, head.y, head.z
	out.headPitch, out.headRoll = head.pitch, head.rollOut
	out.weaponX, out.weaponY, out.weaponZ = weapon.x, weapon.y, weapon.z
	out.weaponPitch, out.weaponYaw, out.weaponRoll = weapon.pitch, weapon.yaw, weapon.roll
	out.wristPitch, out.wristRoll = weapon.wristPitch, weapon.wristRoll
	out.footPhase, out.footstep = phase, footstep
	out.planarSpeed, out.velRight, out.velForward = planarSpeed, velRight, velForward
	out.yawRate, out.pitchRate = self.yawRate, self.pitchRate
	out.landingSpeed = landingSpeed
	out.moveX, out.moveZ, out.speedFraction = mx, mz, fraction
	out.inertiaBypassed = self.inertia:bypassed(state)
	return out
end

return BodycamCore
