-- Port of src/xrGame/bodycam_reference/camera.h/.cpp: the displayed camera's lag,
-- turn and strafe roll, and the control smoothing of the recorded Reference camera.
-- Rotations are tables { pitch, yaw, roll } in degrees.

local ScalarMath = require(script.Parent.ScalarMath)

local wrapDegrees = ScalarMath.wrapDegrees
local mapRangeClamped = ScalarMath.mapRangeClamped
local fInterpTo = ScalarMath.fInterpTo
local clamp01 = ScalarMath.clamp01
local DEG = ScalarMath.DEGREES_PER_RADIAN
local PI = math.pi

-- Look input per second is divided by this to give the yaw and pitch rates.
local LOOK_RATE_SCALE = 50
-- A yaw rate of TURN_ROLL_RATE rolls the view by TURN_ROLL, full strafe input by STRAFE_ROLL.
local TURN_ROLL_RATE, TURN_ROLL, STRAFE_ROLL = 3, 8, 5
-- Above this speed the lean roll is added to the turn roll.
local LEAN_ROLL_SPEED_CM = 250
-- While aiming or leaning the roll target is the lean roll scaled by speed.
local LEAN_SCALE_SLOW_CM, LEAN_SCALE_FAST_CM, LEAN_SCALE_SLOW, LEAN_SCALE_FAST = 120, 200, 2, 1.5
local ROLL_SPEED = 8
-- The lean target follows the yaw rate when aiming and the summed rates otherwise.
local HIP_LEAN_RATE, HIP_LEAN = 2, 0.4
local LEAN_TARGET_SLOW_RATE, LEAN_TARGET_FAST_RATE, LEAN_TARGET_SLOW_SPEED, LEAN_TARGET_FAST_SPEED = 0.5, 2, 3, 10
local LEAN_ALPHA_SPEED = 12
local MAX_LEAN_ROLL = 15
-- The arms lag speed returns to ARMS_LAG_SPEED at a rate that grows with the pitch rate in rotation mode 2.
local ARMS_LAG_SPEED, ARMS_LAG_SLOW_RATE, ARMS_LAG_FAST_RATE, ARMS_LAG_PITCH_RATE = 20, 0.5, 15, 0.3
-- The summed absolute rates over which the camera lag speed rises.
local CAMERA_LAG_SLOW_RATE, CAMERA_LAG_FAST_RATE = 1, 2
local MAX_LAG_STEP_SECONDS = 0.033333
-- Components and angles within this tolerance count as equal.
local ROTATION_TOLERANCE = 0.0001
-- |x z - w y| above this is a pitch of +-90 degrees.
local POLE_TEST = 0.4999995
-- Quaternions closer than this (by dot product) blend linearly instead of spherically.
local SLERP_LINEAR_DOT = 0.9999

local CameraMotion = {}
CameraMotion.__index = CameraMotion

function CameraMotion.rotation(pitch: number?, yaw: number?, roll: number?)
	return { pitch = pitch or 0, yaw = yaw or 0, roll = roll or 0 }
end
local rot = CameraMotion.rotation

function CameraMotion.defaultTuning()
	-- The camera lag speed rises from cameraLagSlowSpeed to cameraLagFastSpeed with the summed absolute rates.
	return { cameraLagSlowSpeed = 2, cameraLagFastSpeed = 16, cameraLagRate = 5, controlSmoothSpeed = 25 }
end

local function newCameraState()
	return {
		smoothControl = rot(),
		armsEndpoint = rot(),
		cameraEndpoint = rot(),
		yaw = 0,
		pitch = 0,
		displayRoll = 0,
		finalYaw = 0,
		finalPitch = 0,
		finalRoll = 0,
		currentLeanRoll = 0,
		roll = 0,
		leanTarget = 0,
		leanAlpha = 0,
		yawRate = 0,
		pitchRate = 0,
		armsLagSpeed = 20,
		lagSpeed = 1,
	}
end

function CameraMotion.interpolateControlRotation(current, target, delta: number, speed: number)
	local dt, rate = delta, speed
	if dt == 0 or (current.pitch == target.pitch and current.yaw == target.yaw and current.roll == target.roll) then
		return current
	end
	if rate <= 0 then
		return target
	end
	local dp = wrapDegrees(target.pitch - current.pitch)
	local dy = wrapDegrees(target.yaw - current.yaw)
	local dr = wrapDegrees(target.roll - current.roll)
	if
		math.abs(wrapDegrees(dp)) <= ROTATION_TOLERANCE
		and math.abs(wrapDegrees(dy)) <= ROTATION_TOLERANCE
		and math.abs(wrapDegrees(dr)) <= ROTATION_TOLERANCE
	then
		return target
	end
	local a = clamp01(dt * rate)
	return rot(wrapDegrees(current.pitch + dp * a), wrapDegrees(current.yaw + dy * a), wrapDegrees(current.roll + dr * a))
end

local function toQuaternion(r)
	local h = PI / 360
	local p, y, l = r.pitch * h, r.yaw * h, r.roll * h
	local cp, sp, cy, sy, cr, sr = math.cos(p), math.sin(p), math.cos(y), math.sin(y), math.cos(l), math.sin(l)
	return {
		cr * sp * sy - sr * cp * cy,
		-cr * sp * cy - sr * cp * sy,
		cr * cp * sy - sr * sp * cy,
		cr * cp * cy + sr * sp * sy,
	}
end

local function toRotation(q)
	local x, y, z, w = q[1], q[2], q[3], q[4]
	local test = x * z - w * y
	local yaw = math.atan2(2 * (x * y + w * z), 1 - 2 * (y * y + z * z)) * DEG
	if test < -POLE_TEST then
		return rot(-90, yaw, wrapDegrees(-yaw - 2 * math.atan2(x, w) * DEG))
	end
	if test > POLE_TEST then
		return rot(90, yaw, wrapDegrees(yaw - 2 * math.atan2(x, w) * DEG))
	end
	return rot(math.asin(2 * test) * DEG, yaw, math.atan2(-2 * (x * w + y * z), 1 - 2 * (x * x + y * y)) * DEG)
end

local function interpQuaternion(current, target, dt: number, speed: number)
	local same, opposite = true, true
	for i = 1, 4 do
		same = same and math.abs(current[i] - target[i]) <= ROTATION_TOLERANCE
		opposite = opposite and math.abs(current[i] + target[i]) <= ROTATION_TOLERANCE
	end
	if speed <= 0 or same or opposite then
		return target
	end
	local alpha = clamp01(dt * speed)
	local dot = (current[4] * target[4] + current[3] * target[3]) + (current[2] * target[2] + current[1] * target[1])
	local absolute = math.abs(dot)
	local a, b = 1 - alpha, if dot < 0 then -alpha else alpha
	if absolute < SLERP_LINEAR_DOT then
		local theta = math.acos(absolute)
		local inverse = 1 / math.sin(theta)
		a = math.sin(theta * a) * inverse
		b = math.sin(theta * b) * inverse
	end
	local q = {}
	for i = 1, 4 do
		q[i] = current[i] * a + target[i] * b
	end
	local squared = (q[1] * q[1] + q[3] * q[3]) + (q[2] * q[2] + q[4] * q[4])
	if squared < 1e-8 then
		return { 0, 0, 0, 1 }
	end
	local inverse = 1 / math.sqrt(squared)
	for i = 1, 4 do
		q[i] *= inverse
	end
	return q
end

local function lag(current, target, dt: number, speed: number)
	return toRotation(interpQuaternion(toQuaternion(current), toQuaternion(target), math.min(dt, MAX_LAG_STEP_SECONDS), speed))
end

function CameraMotion.new()
	return setmetatable({ state = newCameraState(), tuning = CameraMotion.defaultTuning() }, CameraMotion)
end

function CameraMotion:reset()
	self.state = newCameraState()
end

function CameraMotion:setTuning(tuning)
	self.tuning = tuning
end

function CameraMotion:seedControl(control)
	local s = newCameraState()
	s.smoothControl = rot(control.pitch, control.yaw, control.roll)
	s.armsEndpoint = rot(control.pitch, control.yaw, control.roll)
	s.cameraEndpoint = rot(control.pitch, control.yaw, control.roll)
	s.yaw = control.yaw * PI / 180
	s.pitch = control.pitch * PI / 180
	s.finalYaw = s.yaw
	s.finalPitch = s.pitch
	s.roll = control.roll
	s.displayRoll = control.roll
	s.finalRoll = control.roll
	self.state = s
end

-- input = { control, worldDelta, lookX, lookY, moveX, speedCm, aiming, leaningPressed, rotationMode }
function CameraMotion:update(input)
	local s = self.state
	local tuning = self.tuning
	local dt = input.worldDelta
	local inverse = if dt == 0 then 0 else 1 / dt
	local yawRate = (input.lookX * inverse) / LOOK_RATE_SCALE
	local pitchRate = (input.lookY * inverse) / LOOK_RATE_SCALE
	local priorLeanRoll = s.currentLeanRoll
	local target = rot(input.control.pitch, input.control.yaw, input.control.roll + priorLeanRoll)
	s.smoothControl = CameraMotion.interpolateControlRotation(s.smoothControl, target, dt, tuning.controlSmoothSpeed)
	local normalRoll = mapRangeClamped(yawRate, -TURN_ROLL_RATE, TURN_ROLL_RATE, TURN_ROLL, -TURN_ROLL)
		+ mapRangeClamped(input.moveX, -1, 1, -STRAFE_ROLL, STRAFE_ROLL)
		+ (if input.speedCm > LEAN_ROLL_SPEED_CM then priorLeanRoll else 0)
	local leanScale = mapRangeClamped(input.speedCm, LEAN_SCALE_SLOW_CM, LEAN_SCALE_FAST_CM, LEAN_SCALE_SLOW, LEAN_SCALE_FAST)
	local rollTarget = if input.aiming or input.leaningPressed then priorLeanRoll * leanScale else normalRoll
	s.roll = fInterpTo(s.roll, rollTarget, dt, ROLL_SPEED)
	local sum = yawRate + pitchRate
	s.leanTarget = fInterpTo(
		s.leanTarget,
		if input.aiming
			then mapRangeClamped(yawRate, -1, 1, 1, -1)
			else mapRangeClamped(sum, -HIP_LEAN_RATE, HIP_LEAN_RATE, HIP_LEAN, -HIP_LEAN),
		dt,
		mapRangeClamped(sum, LEAN_TARGET_SLOW_RATE, LEAN_TARGET_FAST_RATE, LEAN_TARGET_SLOW_SPEED, LEAN_TARGET_FAST_SPEED)
	)
	s.leanAlpha = fInterpTo(s.leanAlpha, s.leanTarget, dt, LEAN_ALPHA_SPEED)
	local armsAdjust = if input.rotationMode == 2
		then ARMS_LAG_SLOW_RATE + (ARMS_LAG_FAST_RATE - ARMS_LAG_SLOW_RATE) * (math.abs(pitchRate) / ARMS_LAG_PITCH_RATE)
		else ARMS_LAG_SLOW_RATE
	s.armsLagSpeed = fInterpTo(s.armsLagSpeed, ARMS_LAG_SPEED, dt, armsAdjust)
	s.lagSpeed = fInterpTo(
		s.lagSpeed,
		mapRangeClamped(
			math.abs(pitchRate) + math.abs(yawRate),
			CAMERA_LAG_SLOW_RATE,
			CAMERA_LAG_FAST_RATE,
			tuning.cameraLagSlowSpeed,
			tuning.cameraLagFastSpeed
		),
		dt,
		tuning.cameraLagRate
	)
	-- The arms use the endpoint of the previous frame; the displayed camera uses this frame's.
	s.finalYaw = s.armsEndpoint.yaw * PI / 180
	s.finalPitch = s.armsEndpoint.pitch * PI / 180
	s.finalRoll = s.armsEndpoint.roll
	s.armsEndpoint = lag(s.armsEndpoint, s.smoothControl, dt, s.armsLagSpeed)
	s.cameraEndpoint = lag(s.cameraEndpoint, rot(s.smoothControl.pitch, s.smoothControl.yaw, s.roll), dt, s.lagSpeed)
	s.yaw = s.cameraEndpoint.yaw * PI / 180
	s.pitch = s.cameraEndpoint.pitch * PI / 180
	s.displayRoll = s.cameraEndpoint.roll
	if s.leanAlpha ~= 0 then
		s.currentLeanRoll = MAX_LEAN_ROLL * s.leanAlpha
	end
	s.yawRate = yawRate
	s.pitchRate = pitchRate
	return s
end

return CameraMotion
