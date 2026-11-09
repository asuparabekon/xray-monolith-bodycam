-- Look lag: the view orientation trails the aim. Pure Luau.
--
-- Each axis (yaw, pitch) follows the aim through a critically damped spring. Small
-- aim changes inside the dead band do not move a resting view; once the gap leaves
-- the band the spring engages and runs until the view has settled on the aim. The
-- gap is held inside a cone; when the aim outruns the spring the view is dragged at
-- the cone edge. Fast turns stiffen the spring.
--
-- Position: while lagging, the view slides toward the side it lags on; moving slides
-- it against the move direction.
--
-- Kicks (recoil, script turns) are not lag: kick() turns the view by the same amount
-- at once.

local BMath = require(script.Parent.Math)

local LookLag = {}
LookLag.__index = LookLag

local RAD = math.pi / 180
local MAX_PITCH = 89 * RAD
local SETTLE_GAP = 2e-5 -- rad
local SETTLE_SPEED = 2e-3 -- rad/s
local MOVE_SHIFT_RATE = 8 -- 1/s smoothing of the move shift

-- config: the Settings `lookLag` group.
function LookLag.new(config)
	local self = setmetatable({}, LookLag)
	self.config = config
	self:reset(0, 0)
	self.initialized = false
	return self
end

-- Snaps the view onto the aim and clears all motion.
function LookLag:reset(aimYaw: number, aimPitch: number)
	self.yaw, self.pitch = aimYaw, aimPitch
	self.vYaw, self.vPitch = 0, 0
	self.engaged = false
	self.shiftX, self.shiftY, self.shiftZ = 0, 0, 0
	self.moveX, self.moveZ = 0, 0
	self.gapYaw, self.gapPitch = 0, 0 -- view minus aim
	self.initialized = true
end

-- An aim kick: the view turns with it at once, the spring state is rebased.
function LookLag:kick(dYaw: number, dPitch: number)
	self.yaw += dYaw
	self.pitch = math.clamp(self.pitch + dPitch, -MAX_PITCH, MAX_PITCH)
end

-- One frame.
--   input.dt, input.yaw, input.pitch   the aim (radians)
--   input.ads                          ADS blend 0..1
--   input.turnRate                     aim angular speed, rad/s, without kicks
--   input.velRight, input.velForward   planar velocity in the aim's yaw frame, studs/s
--   input.walkSpeed                    studs/s
-- Returns self (fields yaw, pitch, shiftX/Y/Z, gapYaw, gapPitch).
function LookLag:update(input)
	local cfg = self.config
	local aimYaw, aimPitch = input.yaw, input.pitch
	if not self.initialized then
		self:reset(aimYaw, aimPitch)
	end
	local dt = input.dt
	if not cfg.enabled then
		self:reset(aimYaw, aimPitch)
		return self
	end
	local ads = BMath.clamp(input.ads or 0, 0, 1)

	-- Keep the view yaw on the same turn as the aim.
	if math.abs(self.yaw - aimYaw) > math.pi then
		self.yaw = aimYaw + BMath.wrapAngle(self.yaw - aimYaw)
	end

	local rate = input.turnRate or 0
	local hz = BMath.lerp(cfg.followHz, cfg.followHzAds, ads)
	hz *= 1 + cfg.turnStiffening * math.min(rate / cfg.stiffenRate, 1)
	local omega = BMath.omega(hz)

	local gy, gp = aimYaw - self.yaw, aimPitch - self.pitch
	local gap = math.sqrt(gy * gy + gp * gp)
	if not self.engaged and gap > cfg.deadBandDeg * RAD then
		self.engaged = true
	end

	if self.engaged and dt > 0 then
		self.yaw, self.vYaw = BMath.criticalSpring(self.yaw, self.vYaw, aimYaw, omega, dt)
		self.pitch, self.vPitch = BMath.criticalSpring(self.pitch, self.vPitch, aimPitch, omega, dt)
		gy, gp = aimYaw - self.yaw, aimPitch - self.pitch
		gap = math.sqrt(gy * gy + gp * gp)
		local speed = math.sqrt(self.vYaw * self.vYaw + self.vPitch * self.vPitch)
		if gap < SETTLE_GAP and speed < SETTLE_SPEED then
			self.yaw, self.pitch = aimYaw, aimPitch
			self.vYaw, self.vPitch = 0, 0
			self.engaged = false
			gy, gp, gap = 0, 0, 0
		end
	elseif not self.engaged then
		self.vYaw, self.vPitch = 0, 0
	end

	-- The cone: drag the view along at its edge.
	local cone = BMath.lerp(cfg.maxLagDeg, cfg.maxLagDegAds, ads) * RAD
	if gap > cone then
		local k = if gap > 0 then cone / gap else 0
		local ny, np = if gap > 0 then gy / gap else 0, if gap > 0 then gp / gap else 0
		gy, gp = gy * k, gp * k
		self.yaw, self.pitch = aimYaw - gy, aimPitch - gp
		gap = cone
		-- Remove velocity that would carry the view further from the aim.
		local toward = self.vYaw * ny + self.vPitch * np
		if toward < 0 then
			self.vYaw -= toward * ny
			self.vPitch -= toward * np
		end
	end
	self.pitch = math.clamp(self.pitch, -MAX_PITCH, MAX_PITCH)
	self.gapYaw, self.gapPitch = -gy, -gp

	-- Slide toward the lagging side (yaw positive = view left of the aim).
	local shift = BMath.lerp(cfg.lagShift, cfg.lagShiftAds, ads)
	local sx, sy = 0, 0
	if cone > 1e-6 then
		sx = -shift * self.gapYaw / cone
		sy = shift * self.gapPitch / cone
	end

	-- Slide against the move direction.
	local walk = math.max(input.walkSpeed or 16, 1e-3)
	local mx = -cfg.moveShift * BMath.clamp((input.velRight or 0) / walk, -1, 1)
	local mz = cfg.moveShiftForward * BMath.clamp((input.velForward or 0) / walk, -1, 1)
	self.moveX = BMath.smooth(self.moveX, mx, MOVE_SHIFT_RATE, dt)
	self.moveZ = BMath.smooth(self.moveZ, mz, MOVE_SHIFT_RATE, dt)

	self.shiftX, self.shiftY, self.shiftZ = sx + self.moveX, sy, self.moveZ
	return self
end

return LookLag
