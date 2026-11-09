-- Head motion: stride bob, strafe/turn roll, sprint lean and landing dip. Pure Luau.
--
-- Output is a camera-local offset (x right, y up, z back, studs) and a rotation
-- (pitch: positive up; roll: the CFrame Z angle, positive rolls the view left).
--
-- Foot phase: 0 = left contact, 0.5 = right contact, one unit per full stride.

local BMath = require(script.Parent.Math)

local HeadMotion = {}
HeadMotion.__index = HeadMotion

local RAD = math.pi / 180
local TAU = 2 * math.pi
local BOB_RISE_RATE = 10 -- 1/s: bob amplitude following a faster gait

-- config: the Settings `head` group.
function HeadMotion.new(config)
	local self = setmetatable({}, HeadMotion)
	self.config = config
	self:reset()
	return self
end

function HeadMotion:reset()
	self.phase = 0
	self.bobAmp = 0
	self.roll, self.rollV = 0, 0
	self.sprint = 0
	self.wasSprinting = false
	self.lungeTime = nil
	self.land, self.landV = 0, 0
	self.x, self.y, self.z = 0, 0, 0
	self.pitch, self.rollOut = 0, 0
	self.bobX, self.bobY, self.bobPitch = 0, 0, 0
end

-- Bob amplitude scale for a gait: speed / walk up to walk, then rising to
-- sprintBobScale at sprint speed.
function HeadMotion.gaitScale(config, speed: number, walkSpeed: number, sprintSpeed: number): number
	walkSpeed = math.max(walkSpeed, 1e-3)
	if speed <= walkSpeed then
		return math.max(speed, 0) / walkSpeed
	end
	local span = sprintSpeed - walkSpeed
	local t = if span > 1e-3 then BMath.clamp((speed - walkSpeed) / span, 0, 1) else 1
	return BMath.lerp(1, config.sprintBobScale, t)
end

-- The vertical bob shape: 1 at each contact, 0 between (two dips per stride).
function HeadMotion.dipShape(phase: number): number
	return 0.5 + 0.5 * math.cos(2 * TAU * phase)
end

-- The lateral bob shape: -1 at left contact, +1 at right contact (once per stride).
function HeadMotion.swayShape(phase: number): number
	return -math.cos(TAU * phase)
end

-- Envelope of the sprint lunge at time t: up to 1 at `peak`, 0 from `total` on.
local function lungeEnvelope(t: number, peak: number, total: number): number
	if t <= 0 or t >= total then
		return 0
	elseif t < peak then
		return math.sin(0.5 * math.pi * t / peak)
	end
	return 0.5 + 0.5 * math.cos(math.pi * (t - peak) / math.max(total - peak, 1e-3))
end

-- One frame.
--   dt, phase (number? foot phase), speed (planar studs/s), walkSpeed, sprintSpeed
--   grounded, sprinting, leaning, ads (0..1)
--   velRight (studs/s in the aim's yaw frame), yawRate (rad/s, positive = left)
--   landingSpeed (number?: fall speed on the frame the character lands)
--   clearLeft, clearRight (studs of free space beside the head; default unlimited)
-- Returns self (x, y, z, pitch, rollOut).
function HeadMotion:update(input)
	local cfg = self.config
	local dt = input.dt
	if not cfg.enabled then
		self:reset()
		return self
	end
	local ads = BMath.clamp(input.ads or 0, 0, 1)
	local x, y, z, pitch, roll = 0, 0, 0, 0, 0

	-- Stride bob.
	if cfg.bobEnabled then
		local phase = input.phase
		local target = 0
		if phase ~= nil and input.grounded then
			self.phase = phase % 1
			target = HeadMotion.gaitScale(cfg, input.speed or 0, input.walkSpeed or 16, input.sprintSpeed or 25.6)
			target *= BMath.lerp(1, cfg.bobAds, ads)
		end
		local rate = if target > self.bobAmp then BOB_RISE_RATE else BMath.rateForTime(cfg.bobRestTime)
		self.bobAmp = BMath.smooth(self.bobAmp, target, rate, dt)
		if self.bobAmp < 1e-5 and target == 0 then
			self.bobAmp = 0
		end
		local a = self.bobAmp
		local dip = HeadMotion.dipShape(self.phase)
		local side = cfg.bobSide * a * HeadMotion.swayShape(self.phase)
		-- Keep clear of walls beside the head.
		if side > 0 then
			side = math.min(side, math.max((input.clearRight or math.huge) - cfg.wallMargin, 0))
		elseif side < 0 then
			side = -math.min(-side, math.max((input.clearLeft or math.huge) - cfg.wallMargin, 0))
		end
		self.bobX, self.bobY, self.bobPitch = side, -cfg.bobDown * a * dip, -cfg.bobNodDeg * RAD * a * dip
		x += self.bobX
		y += self.bobY
		pitch += self.bobPitch
	else
		self.bobAmp = 0
		self.bobX, self.bobY, self.bobPitch = 0, 0, 0
	end

	-- Strafe and turn roll through one spring.
	if cfg.rollEnabled then
		local walk = math.max(input.walkSpeed or 16, 1e-3)
		local strafe = if input.leaning then 0 else BMath.clamp((input.velRight or 0) / walk, -1, 1)
		local target = -cfg.strafeRollDeg * RAD * strafe -- strafing right rolls right
		target += cfg.turnRollDeg * RAD * BMath.softSaturate((input.yawRate or 0) / cfg.turnRollRate)
		self.roll, self.rollV = BMath.criticalSpring(self.roll, self.rollV, target, BMath.omega(cfg.rollHz), dt)
		roll += self.roll
	else
		self.roll, self.rollV = 0, 0
	end

	-- Sprint lean and the one-shot lunge on entering a sprint.
	local sprinting = input.sprinting and input.grounded ~= false
	if cfg.sprintLeanEnabled then
		self.sprint = BMath.smooth(self.sprint, if sprinting then 1 else 0, BMath.rateForTime(cfg.sprintEaseTime), dt)
		pitch -= cfg.sprintPitchDeg * RAD * self.sprint
		y -= cfg.sprintDrop * self.sprint
		if sprinting and not self.wasSprinting then
			self.lungeTime = 0
		end
		if self.lungeTime then
			self.lungeTime += dt
			local e = lungeEnvelope(self.lungeTime, cfg.sprintLungePeak, cfg.sprintLungeTime)
			z -= cfg.sprintLunge * e
			y -= cfg.sprintLungeDown * e
			if self.lungeTime >= cfg.sprintLungeTime then
				self.lungeTime = nil
			end
		end
	else
		self.sprint = 0
		self.lungeTime = nil
	end
	self.wasSprinting = sprinting

	-- Landing dip: an impulse into a slightly underdamped spring.
	if cfg.landEnabled then
		local omega = BMath.omega(cfg.landHz)
		local fall = input.landingSpeed
		if fall and fall > 0 then
			local scale = math.min(fall / cfg.landRefSpeed, cfg.landMaxScale)
			self.landV -= BMath.impulseFor(cfg.landDrop * scale, omega, cfg.landDamping)
		end
		self.land, self.landV = BMath.spring(self.land, self.landV, 0, omega, cfg.landDamping, dt)
		y += self.land
	else
		self.land, self.landV = 0, 0
	end

	self.x, self.y, self.z, self.pitch, self.rollOut = x, y, z, pitch, roll
	return self
end

-- The largest sideways bob this config can produce (for sizing wall checks).
function HeadMotion.maxSide(config): number
	return config.bobSide * math.max(config.sprintBobScale, 1)
end

return HeadMotion
