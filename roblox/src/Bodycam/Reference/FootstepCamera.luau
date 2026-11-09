-- Port of ClassicFootstepCamera (src/xrGame/bodycam_reference/classic_footstep_camera.h)
-- and gait_phase.h: recorded Reference head motion replayed on the host's footstep schedule.
--
-- input = {
--   dt, paused,
--   footPhase = number? -- phase of the step schedule: left contact 0, right contact .5; nil when not striding
--   speed,               -- planar metres/second (on Reference's gait speeds)
--   forward, right,      -- planar velocity in the camera yaw frame
--   sprinting, grounded,
--   bobScale, postureScale, lungeScale,
-- }
-- output.offset = { right, up, forward } in metres, camera yaw frame.

local Data = require(script.Parent.FootstepCameraData)

local BINS = Data.BINS
local CYCLES = Data.CYCLES
local SPRINT_CYCLE = CYCLES[5]
local LUNGE = Data.SPRINT_LUNGE

local MAX_STEP_SECONDS = 0.1
local ACCEL_FOLLOW_RATE, GROUND_FOLLOW_RATE = 10, 10
-- The travel direction is updated only above these speeds.
local MIN_PLANAR_VELOCITY, MIN_DIRECTION_SPEED = 1e-3, 0.05
-- Reference sprint-entry acceleration (125->325 cm/s in about .33 s). The lunge plays at the
-- ratio of the actual acceleration to it, within these limits.
local REFERENCE_SPRINT_ACCEL, LUNGE_MIN_ACCEL, LUNGE_MIN_RATE, LUNGE_MAX_RATE = 6, 0.5, 0.4, 1.25
local RESEEK_FADE_RATE = 6
-- A schedule step larger than this is a jump, not a rate.
local MAX_RATE_STEP = 0.25
local RATE_FOLLOW_RATE = 8
-- The schedule is followed exactly for this long after locking on.
local LOCK_SETTLE_SECONDS = 0.5
-- A larger phase error snaps to the schedule; a smaller one is corrected by steps at most
-- DRIFT_CORRECTION quicker or slower.
local RESEEK_ERROR, DRIFT_CORRECTION, MIN_CORRECTION_RATE, ERROR_FOLLOW_RATE = 0.1, 0.6, 0.25, 15
-- While the body still moves faster than this after the leg cycle ended, the stride continues
-- for at most MAX_BRAKE_STEPS contacts.
local BRAKE_MIN_SPEED = 0.3
local MAX_BRAKE_STEPS = 2
local COAST_DECAY_RATE = 4

local function wrap(phase: number): number
	return phase - math.floor(phase)
end

local function wrapSigned(value: number): number
	return value - math.floor(value + 0.5)
end

local function clamp(v: number, lo: number, hi: number): number
	return if v < lo then lo elseif v > hi then hi else v
end

local function footContactCrossed(previous: number, current: number, contact: number): boolean
	local delta = wrap(current - previous)
	local distance = wrap(contact - previous)
	-- No catch-up bursts on discontinuous reseeks or very long stalls.
	return delta > 0 and delta <= 0.5 and distance > 0 and distance <= delta
end

local function sample(curve, phase: number): number
	local x = wrap(phase) * BINS
	local i = math.floor(x) % BINS
	local t = x - math.floor(x)
	return curve[i + 1] * (1 - t) + curve[(i + 1) % BINS + 1] * t
end

local FootstepCamera = {}
FootstepCamera.__index = FootstepCamera

FootstepCamera.wrapSignedFootPhase = wrapSigned
FootstepCamera.footContactCrossed = footContactCrossed

-- Foot phase (left contact at 0, right at .5) -> clip phase; left and right are the clip's contact phases.
function FootstepCamera.legPhase(foot: number, left: number, right: number): number
	foot, left, right = wrap(foot), wrap(left), wrap(right)
	local span = wrap(right - left)
	return if foot < 0.5 then wrap(left + 2 * foot * span) else wrap(right + 2 * (foot - 0.5) * (1 - span))
end

-- Inverse of legPhase.
function FootstepCamera.footPhase(leg: number, left: number, right: number): number
	leg, left, right = wrap(leg), wrap(left), wrap(right)
	local span = wrap(right - left)
	if span <= 0 or span >= 1 then
		return wrap(leg - left)
	end
	local fromLeft = wrap(leg - left)
	return if fromLeft < span then 0.5 * fromLeft / span else wrap(0.5 + 0.5 * (fromLeft - span) / (1 - span))
end

local function newOutput()
	return { offset = { 0, 0, 0 }, phase = 0, locked = false, brakeContacts = 0, speed = 0 }
end

function FootstepCamera.new()
	local self = setmetatable({}, FootstepCamera)
	self:reset()
	return self
end

function FootstepCamera:reset()
	self.out = newOutput()
	self.posture = { 0, 0, 0 }
	self.phase = 0
	self.rate = 0
	self.lastEnginePhase = 0
	self.gait = 0
	self.ground = 1
	self.lunge = -1
	self.forward = 1
	self.right = 0
	self.accel = 0
	self.lastSpeed = 0
	self.strideRate = 0
	self.lockAge = 0
	self.crossfade = { 0, 0, 0 }
	self.reseek = false
	self.brakeSteps = 0
	self.locked = false
	self.sprinting = false
end

function FootstepCamera:output()
	return self.out
end

-- Follows the host's schedule closely without popping when the clip changes.
function FootstepCamera:_trackPhase(input, dt: number, speed: number)
	local out = self.out
	out.brakeContacts = 0
	local footPhase = input.footPhase
	if footPhase ~= nil and footPhase == footPhase and math.abs(footPhase) ~= math.huge then
		local enginePhase = wrap(footPhase)
		if not self.locked then
			self.phase = enginePhase
			self.rate = 0
			self.locked = true
			self.lockAge = 0
		else
			self.lockAge += dt
			local step = wrapSigned(enginePhase - self.lastEnginePhase)
			if math.abs(step) < MAX_RATE_STEP then
				self.rate += (step / dt - self.rate) * math.min(1, dt * RATE_FOLLOW_RATE)
			end
			local predicted = self.phase + self.rate * dt
			-- A new stride, or a clip change that restarts the step schedule, snaps to the
			-- schedule and update crossfades the resulting offset.
			local err = wrapSigned(enginePhase - predicted)
			local bound = DRIFT_CORRECTION * math.max(self.rate, MIN_CORRECTION_RATE) * dt
			if self.lockAge < LOCK_SETTLE_SECONDS then
				self.phase = enginePhase
			elseif math.abs(err) > RESEEK_ERROR then
				self.phase = enginePhase
				self.reseek = true
			else
				self.phase = predicted + clamp(err * math.min(1, dt * ERROR_FOLLOW_RATE), -bound, bound)
			end
		end
		self.lastEnginePhase = enginePhase
		self.strideRate = self.rate
		self.brakeSteps = 0
	else
		self.locked = false
		local previous = wrap(self.phase)
		if input.grounded and speed > BRAKE_MIN_SPEED and self.strideRate > 0 and self.brakeSteps < MAX_BRAKE_STEPS then
			-- The body is still braking: keep the last cadence (braking shortens steps, not
			-- their rhythm) and report each contact reached.
			self.rate = self.strideRate
			self.phase += self.rate * dt
			local from, to = previous, wrap(self.phase)
			if footContactCrossed(from, to, 0) then
				out.brakeContacts = bit32.bor(out.brakeContacts, 1)
				self.brakeSteps += 1
			end
			if self.brakeSteps < MAX_BRAKE_STEPS and footContactCrossed(from, to, 0.5) then
				out.brakeContacts = bit32.bor(out.brakeContacts, 2)
				self.brakeSteps += 1
			end
		else
			self.phase += self.rate * dt
			self.rate *= math.exp(-COAST_DECAY_RATE * dt)
			if speed <= BRAKE_MIN_SPEED then
				self.strideRate = 0
			end
		end
	end
	self.phase = wrap(self.phase)
	out.locked = self.locked
end

-- WalkForward, WalkBackward, WalkRight, WalkLeft, as the generator blends.
function FootstepCamera:_directionWeights(): (number, number, number, number)
	local f, r = self.forward, self.right
	local sum = math.abs(f) + math.abs(r)
	if sum <= 1e-9 then
		return 1, 0, 0, 0
	end
	return math.max(f, 0) / sum, math.max(-f, 0) / sum, math.max(r, 0) / sum, math.max(-r, 0) / sum
end

function FootstepCamera:update(input)
	local out = self.out
	if input.paused or not (input.dt > 0) then
		return out
	end
	local dt = math.min(input.dt, MAX_STEP_SECONDS)
	local p1, p2, p3 = out.offset[1], out.offset[2], out.offset[3]
	self.reseek = false
	local speed = math.max(input.speed or 0, 0)
	self.accel += ((speed - self.lastSpeed) / dt - self.accel) * math.min(1, dt * ACCEL_FOLLOW_RATE)
	self.lastSpeed = speed
	self:_trackPhase(input, dt, speed)
	self.gait += ((if input.sprinting then 1 else 0) - self.gait) * (1 - math.exp(-Data.GAIT_RATE * dt))
	local grounded = input.grounded ~= false
	self.ground += ((if grounded then 1 else 0) - self.ground) * (1 - math.exp(-GROUND_FOLLOW_RATE * dt))
	if input.sprinting and not self.sprinting then
		self.lunge = 0
	end
	self.sprinting = input.sprinting or false
	local forward, right = input.forward or 0, input.right or 0
	local planar = math.sqrt(forward * forward + right * right)
	if planar > MIN_PLANAR_VELOCITY and (input.speed or 0) > MIN_DIRECTION_SPEED then
		self.forward = forward / planar
		self.right = right / planar
	end
	local walkW = math.min(speed / Data.WALK_SPEED, 1)
	local sprintW = clamp((speed - Data.WALK_SPEED) / (Data.SPRINT_SPEED - Data.WALK_SPEED), 0, 1)
	local w1, w2, w3, w4 = self:_directionWeights()
	local weights = { w1, w2, w3, w4 }
	local phase = self.phase
	local walkPosture = { 0, 0, 0 }
	local walkCycle = { 0, 0, 0 }
	local sprintCycle = { 0, 0, 0 }
	for c = 1, 4 do
		local w = weights[c]
		if w > 0 then
			local cycle = CYCLES[c]
			for a = 1, 3 do
				walkPosture[a] += cycle.posture[a] * w
				walkCycle[a] += sample(cycle.axes[a], phase) * w
			end
		end
	end
	for a = 1, 3 do
		sprintCycle[a] = sample(SPRINT_CYCLE.axes[a], phase)
	end
	local postureFollow = 1 - math.exp(-Data.POSTURE_RATE * dt)
	local lunge = { 0, 0, 0 }
	if self.lunge >= 0 then
		local index = self.lunge * Data.LUNGE_RATE
		if index < Data.LUNGE_SAMPLES - 1 then
			local i = math.floor(index)
			local t = index - i
			for a = 1, 3 do
				lunge[a] = LUNGE[a][i + 1] * (1 - t) + LUNGE[a][i + 2] * t
			end
			-- Progress with the actual acceleration: a slower ramp stretches the dive,
			-- a plateau plays it at recorded speed.
			local rate = if input.sprinting and self.accel > LUNGE_MIN_ACCEL
				then clamp(self.accel / REFERENCE_SPRINT_ACCEL, LUNGE_MIN_RATE, LUNGE_MAX_RATE)
				else 1
			self.lunge += dt * rate
		else
			self.lunge = -1
		end
	end
	local bobScale = input.bobScale or 1
	local postureScale = input.postureScale or 1
	local lungeScale = input.lungeScale or 1
	local gait = self.gait
	local ground = self.ground
	for a = 1, 3 do
		local target = (walkPosture[a] * (1 - gait) + SPRINT_CYCLE.posture[a] * gait) * walkW * ground
		self.posture[a] += (target - self.posture[a]) * postureFollow
		local cycle = (walkCycle[a] * (1 - gait) * walkW + sprintCycle[a] * gait * sprintW) * ground
		out.offset[a] = self.posture[a] * postureScale + cycle * bobScale + lunge[a] * lungeScale
	end
	-- Crossfade a schedule reseek instead of popping the camera.
	local fade = math.exp(-RESEEK_FADE_RATE * dt)
	local previous = { p1, p2, p3 }
	for a = 1, 3 do
		if self.reseek then
			self.crossfade[a] = previous[a] - out.offset[a]
		else
			self.crossfade[a] *= fade
		end
		out.offset[a] += self.crossfade[a]
	end
	out.phase = self.phase
	out.speed = speed
	return out
end

return FootstepCamera
