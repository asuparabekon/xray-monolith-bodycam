-- Port of src/xrGame/bodycam_reference/classic_follow.h: the gun-first follow
-- (camera lag from the Reference camera motion) and the look translation it derives.
-- Angles in: the aim's heading and pitch in radians, as the camera rig keeps them.

local ScalarMath = require(script.Parent.ScalarMath)
local CameraMotion = require(script.Parent.CameraMotion)

local wrapDegrees = ScalarMath.wrapDegrees
local isFinite = ScalarMath.isFinite
local DEG = ScalarMath.DEGREES_PER_RADIAN
local CONTROL_DEGREES_PER_LOOK = ScalarMath.CONTROL_DEGREES_PER_LOOK

local ClassicFollow = {}

-- Longer frames reset the Classic producers instead of advancing them.
ClassicFollow.MAX_FRAME_SECONDS = 0.4
local MAX_FRAME_SECONDS = ClassicFollow.MAX_FRAME_SECONDS

-- The heading and pitch in radians as a control rotation in degrees. The
-- library's basis mirrors X, so the yaw is the negated heading.
function ClassicFollow.controlDegrees(yaw: number, pitch: number)
	return CameraMotion.rotation(pitch * DEG, -yaw * DEG, 0)
end
local controlDegrees = ClassicFollow.controlDegrees

-- The look input that turns into this control change.
function ClassicFollow.lookInput(control, previous): (number, number)
	return wrapDegrees(control.yaw - previous.yaw) / CONTROL_DEGREES_PER_LOOK,
		-wrapDegrees(control.pitch - previous.pitch) / CONTROL_DEGREES_PER_LOOK
end
local lookInput = ClassicFollow.lookInput

---------------------------------------------------------------------------
-- Look translation: limit the requested position before smoothing outward
-- movement. During same-side recovery the camera already supplies a smooth
-- angular trajectory: transport the displayed offset along it and discard
-- pending outward travel.
---------------------------------------------------------------------------

local MIN_RESPONSE = 0.01
-- The vertical translation as a share of the horizontal one.
local VERTICAL_SHARE = 0.55

local function newAxis()
	return { error = 0, target = 0, value = 0 }
end

local function updateAxis(axis, nextError: number, limit: number, alpha: number): number
	if limit <= 1e-6 then
		axis.error, axis.target, axis.value = 0, 0, 0
		return 0
	end
	if nextError * axis.error > 0 and math.abs(nextError) < math.abs(axis.error) and axis.value * nextError >= 0 then
		-- A rapid reversal can shrink the error in one sample too.
		-- Bound that step by the same response used for outward travel.
		axis.value *= math.max(nextError / axis.error, 1 - alpha)
		axis.target = axis.value
	else
		axis.target = if nextError * axis.error <= 0
			then math.clamp(nextError / limit, -1, 1)
			else math.clamp(axis.target + (nextError - axis.error) / limit, -1, 1)
		axis.value += (axis.target - axis.value) * alpha
	end
	axis.error = nextError
	return axis.value
end

local LookTranslation = {}
LookTranslation.__index = LookTranslation
ClassicFollow.LookTranslation = LookTranslation

function LookTranslation.new()
	return setmetatable({ yaw = newAxis(), pitch = newAxis() }, LookTranslation)
end

function LookTranslation:reset()
	self.yaw = newAxis()
	self.pitch = newAxis()
end

-- Returns x, y, z (camera-local metres).
function LookTranslation:update(
	yaw: number,
	pitch: number,
	cameraYaw: number,
	cameraPitch: number,
	maxYawDegrees: number,
	maxPitchDegrees: number,
	amount: number,
	dt: number,
	response: number
): (number, number, number)
	if not isFinite(dt) or dt <= 0 or dt > MAX_FRAME_SECONDS then
		self:reset()
		return 0, 0, 0
	end
	local alpha = 1 - math.exp(-math.max(response, MIN_RESPONSE) * dt)
	local x = updateAxis(self.yaw, wrapDegrees((yaw - cameraYaw) * DEG), maxYawDegrees, alpha)
	local y = updateAxis(self.pitch, wrapDegrees((pitch - cameraPitch) * DEG), maxPitchDegrees, alpha)
	return -x * amount, y * amount * VERTICAL_SHARE, 0
end

---------------------------------------------------------------------------
-- The follow.
---------------------------------------------------------------------------

local Follow = {}
Follow.__index = Follow
ClassicFollow.Follow = Follow

function Follow.new()
	return setmetatable({ camera = CameraMotion.new(), previous = CameraMotion.rotation(), seeded = false }, Follow)
end

function Follow:reset()
	self.camera:reset()
	self.previous = CameraMotion.rotation()
	self.seeded = false
end

-- Starts following from the displayed camera angles while control is observed
-- from the target angles. Passing the target angles for both starts without lag.
function Follow:seed(targetYaw: number, targetPitch: number, displayedYaw: number, displayedPitch: number)
	self.previous = controlDegrees(targetYaw, targetPitch)
	self.camera:seedControl(controlDegrees(displayedYaw, displayedPitch))
	self.seeded = true
end

function ClassicFollow.newOutput()
	return {
		cameraYaw = 0,
		cameraPitch = 0,
		-- The arms endpoint of the previous frame minus the control, radians.
		armsYawOffset = 0,
		armsPitchOffset = 0,
		lookX = 0,
		lookY = 0,
		yawRate = 0,
		pitchRate = 0,
		ownsFollow = false,
	}
end

-- input = { yaw, pitch, dt, strafe, speedMetres, enabled, aiming, leaningPressed, reset, tuning }
-- tuning = CameraMotion.defaultTuning() shape.
function Follow:update(input)
	local out = ClassicFollow.newOutput()
	out.cameraYaw = input.yaw
	out.cameraPitch = input.pitch
	if
		not input.enabled
		or not isFinite(input.yaw)
		or not isFinite(input.pitch)
		or not isFinite(input.dt)
		or input.dt <= 0
		or input.dt > MAX_FRAME_SECONDS
		or not isFinite(input.strafe or 0)
		or not isFinite(input.speedMetres or 0)
	then
		self:reset()
		return out
	end
	if input.reset then
		self:reset()
	end
	local control = controlDegrees(input.yaw, input.pitch)
	if not self.seeded then
		self:seed(input.yaw, input.pitch, input.yaw, input.pitch)
		out.ownsFollow = true
		return out
	end
	out.lookX, out.lookY = lookInput(control, self.previous)
	self.previous = control
	if input.tuning then
		self.camera:setTuning(input.tuning)
	end
	local state = self.camera:update({
		control = control,
		worldDelta = ScalarMath.clampFrameDelta(input.dt),
		lookX = out.lookX,
		lookY = out.lookY,
		moveX = input.strafe or 0,
		speedCm = (input.speedMetres or 0) * ScalarMath.CENTIMETRES_PER_METRE,
		aiming = input.aiming or false,
		leaningPressed = input.leaningPressed or false,
		rotationMode = if input.aiming then 2 else 1,
	})
	out.cameraYaw = -state.yaw
	out.cameraPitch = state.pitch
	out.armsYawOffset = -wrapDegrees(state.finalYaw * DEG - control.yaw) / DEG
	out.armsPitchOffset = wrapDegrees(state.finalPitch * DEG - control.pitch) / DEG
	out.yawRate = state.yawRate
	out.pitchRate = state.pitchRate
	out.ownsFollow = true
	return out
end

return ClassicFollow
