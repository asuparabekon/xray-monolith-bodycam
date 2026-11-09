-- Weapon motion: the viewmodel's camera-local offset. Pure Luau.
--
--   walk lowering   walking forward drops, pulls back and tilts the weapon down
--   holster         the game reports the weapon lowered/safe
--   fire kick       per-shot impulses into a position and a rotation spring
--   wrist roll      fast mouse turns roll/pitch the weapon (a separate rotation about
--                   the grip)
--   profile         a per-weapon hip offset that fades out with ADS
--
-- Offsets: x right, y up, z back (studs). Rotations: pitch (positive muzzle up), yaw
-- (positive left), roll (CFrame Z angle, positive rolls left). Radians.

local BMath = require(script.Parent.Math)

local WeaponMotion = {}
WeaponMotion.__index = WeaponMotion

local RAD = math.pi / 180

-- config: the Settings `weapon` group. random: function() -> [0, 1) (default
-- math.random), for the kick's roll sign.
function WeaponMotion.new(config, random: (() -> number)?)
	local self = setmetatable({}, WeaponMotion)
	self.config = config
	self.random = random or math.random
	self.kickPos = BMath.spring3() -- x, y, z
	self.kickRot = BMath.spring3() -- pitch, yaw, roll
	self.profileTarget = { 0, 0, 0, 0, 0, 0 } -- x, y, z studs; pitch, yaw, roll rad
	self.profile = { 0, 0, 0, 0, 0, 0 }
	self.profileRate = nil
	self:reset()
	return self
end

-- Clears all motion (the profile keeps its target and jumps to it).
function WeaponMotion:reset()
	self.lower = 0
	self.holster = 0
	self.sinceShot = math.huge
	self.sinceAds = math.huge
	BMath.resetSpring3(self.kickPos)
	BMath.resetSpring3(self.kickRot)
	self.yawRate, self.pitchRate = 0, 0
	self.wristPitch, self.wristPitchV = 0, 0
	self.wristRoll, self.wristRollV = 0, 0
	if self.profile then
		for i = 1, 6 do
			self.profile[i] = self.profileTarget[i]
		end
	end
	self.x, self.y, self.z = 0, 0, 0
	self.pitch, self.yaw, self.roll = 0, 0, 0
end

-- A shot. power scales the kick (default 1); ads is the current ADS blend.
function WeaponMotion:notifyShot(power: number?, ads: number?)
	local cfg = self.config
	self.sinceShot = 0
	if not (cfg.enabled and cfg.kickEnabled) then
		return
	end
	local scale = (power or 1) * BMath.lerp(1, cfg.kickAds, BMath.clamp(ads or 0, 0, 1))
	local omega = BMath.omega(cfg.kickHz)
	local unit = BMath.impulseFor(1, omega, cfg.kickDamping)
	local sign = if self.random() < 0.5 then -1 else 1
	local p, r = self.kickPos.v, self.kickRot.v
	p[2] += cfg.kickUp * scale * unit
	p[3] += cfg.kickBack * scale * unit
	r[1] += cfg.kickPitchDeg * RAD * scale * unit
	r[3] += cfg.kickRollDeg * RAD * sign * scale * unit
end

-- Per-weapon hip offset: pos {x, y, z} studs, rot {x, y, z} degrees (pitch, yaw,
-- roll), blended in at blendSpeed (1/s, default profileRate).
function WeaponMotion:setProfile(pos, rot, blendSpeed: number?)
	pos = pos or {}
	rot = rot or {}
	local t = self.profileTarget
	t[1], t[2], t[3] = pos.x or pos[1] or 0, pos.y or pos[2] or 0, pos.z or pos[3] or 0
	t[4] = (rot.x or rot[1] or 0) * RAD
	t[5] = (rot.y or rot[2] or 0) * RAD
	t[6] = (rot.z or rot[3] or 0) * RAD
	self.profileRate = blendSpeed
end

function WeaponMotion:clearProfile(blendSpeed: number?)
	self:setProfile(nil, nil, blendSpeed)
end

-- The walk lowering wanted this frame, 0..1.
function WeaponMotion:_lowerTarget(input): number
	local cfg = self.config
	if not cfg.lowerEnabled or input.sprinting or input.leaning or input.grounded == false then
		return 0
	end
	if input.aiming or (input.ads or 0) > 0.01 or self.sinceAds < cfg.lowerHoldAfterAds then
		return 0
	end
	if self.sinceShot < cfg.lowerHoldAfterShot then
		return 0
	end
	local speed = input.speed or 0
	if speed < 0.5 then
		return 0
	end
	-- Forward 1, strafe 0.5, backward 0.
	local forward = BMath.clamp((input.velForward or 0) / speed, -1, 1)
	local direction = 0.5 * (1 + forward)
	local walk = math.max(input.walkSpeed or 16, 1e-3)
	return direction * math.min(speed / walk, 1)
end

-- One frame.
--   dt, ads (0..1), aiming (bool), sprinting, grounded, leaning, lowered (holster)
--   speed (planar studs/s), velRight, velForward (aim yaw frame), walkSpeed
--   yawRate, pitchRate (rad/s without kicks; positive = left / up)
-- Returns self: x, y, z, pitch, yaw, roll (weapon offset) and wristPitch, wristRoll.
function WeaponMotion:update(input)
	local cfg = self.config
	local dt = input.dt
	local ads = BMath.clamp(input.ads or 0, 0, 1)
	self.sinceShot += dt
	if input.aiming or ads > 0.01 then
		self.sinceAds = 0
	else
		self.sinceAds += dt
	end
	if not cfg.enabled then
		local sinceShot, sinceAds = self.sinceShot, self.sinceAds
		self:reset()
		self.sinceShot, self.sinceAds = sinceShot, sinceAds
		return self
	end
	local x, y, z, pitch, yaw, roll = 0, 0, 0, 0, 0, 0

	-- Walk lowering.
	local target = self:_lowerTarget(input)
	local rate = if target > self.lower then BMath.rateForTime(cfg.lowerInTime) else BMath.rateForTime(cfg.lowerOutTime)
	self.lower = BMath.smooth(self.lower, target, rate, dt)
	y -= cfg.lowerDrop * self.lower
	z += cfg.lowerBack * self.lower
	pitch -= cfg.lowerPitchDeg * RAD * self.lower

	-- Holster (safe) pose.
	local holster = if cfg.holsterEnabled and input.lowered then 1 else 0
	self.holster = BMath.smooth(self.holster, holster, BMath.rateForTime(cfg.holsterTime), dt)
	y -= cfg.holsterDrop * self.holster
	pitch -= cfg.holsterPitchDeg * RAD * self.holster

	-- Fire kick.
	if cfg.kickEnabled then
		local omega = BMath.omega(cfg.kickHz)
		BMath.stepSpring3(self.kickPos, nil, omega, cfg.kickDamping, dt)
		BMath.stepSpring3(self.kickRot, nil, omega, cfg.kickDamping, dt)
		BMath.capAxis(self.kickPos, 3, -cfg.kickMaxBack, cfg.kickMaxBack)
		local maxPitch = cfg.kickMaxPitchDeg * RAD
		BMath.capAxis(self.kickRot, 1, -maxPitch, maxPitch)
		local p, r = self.kickPos.x, self.kickRot.x
		x += p[1]
		y += p[2]
		z += p[3]
		pitch += r[1]
		yaw += r[2]
		roll += r[3]
	else
		BMath.resetSpring3(self.kickPos)
		BMath.resetSpring3(self.kickRot)
	end

	-- Wrist roll and pitch from smoothed mouse turn rates.
	if cfg.wristEnabled then
		local a = BMath.alpha(BMath.omega(cfg.rateHz), dt)
		self.yawRate += ((input.yawRate or 0) - self.yawRate) * a
		self.pitchRate += ((input.pitchRate or 0) - self.pitchRate) * a
		local scale = BMath.lerp(1, cfg.wristAds, ads)
		-- Against a horizontal turn (turning left rolls right), with a vertical one.
		local rollTarget = -cfg.wristRollDeg * RAD * scale * BMath.softSaturate(self.yawRate / cfg.wristFullRate)
		local pitchTarget = cfg.wristPitchDeg * RAD * scale * BMath.softSaturate(self.pitchRate / cfg.wristFullRate)
		local omega = BMath.omega(cfg.wristHz)
		self.wristRoll, self.wristRollV = BMath.criticalSpring(self.wristRoll, self.wristRollV, rollTarget, omega, dt)
		self.wristPitch, self.wristPitchV = BMath.criticalSpring(self.wristPitch, self.wristPitchV, pitchTarget, omega, dt)
	else
		self.yawRate, self.pitchRate = 0, 0
		self.wristRoll, self.wristRollV, self.wristPitch, self.wristPitchV = 0, 0, 0, 0
	end

	-- Weapon profile, faded out by ADS.
	if cfg.profileEnabled then
		local pa = BMath.alpha(self.profileRate or cfg.profileRate, dt)
		local hip = 1 - ads
		for i = 1, 6 do
			self.profile[i] += (self.profileTarget[i] - self.profile[i]) * pa
		end
		local pr = self.profile
		x += pr[1] * hip
		y += pr[2] * hip
		z += pr[3] * hip
		pitch += pr[4] * hip
		yaw += pr[5] * hip
		roll += pr[6] * hip
	end

	self.x, self.y, self.z = x, y, z
	self.pitch, self.yaw, self.roll = pitch, yaw, roll
	return self
end

return WeaponMotion
