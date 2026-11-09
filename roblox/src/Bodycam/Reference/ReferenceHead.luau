-- Port of ClassicReferenceHead (src/xrGame/bodycam_reference/classic_footstep_camera.h):
-- the display roll (strafe, turn rate, lean at speed) and the camera shake (movement vibration
-- and mouse shake), in degrees. The shake changes the view only.
--
-- input = { yaw, pitch (radians), dt, paused, strafe (-1..1), speedMetres, aiming, leaning }
-- output = { roll, shake = { pitch, yaw, roll } }

local ScalarMath = require(script.Parent.ScalarMath)
local CameraMotion = require(script.Parent.CameraMotion)
local ClassicFollow = require(script.Parent.ClassicFollow)
local CameraShake = require(script.Parent.CameraShake)

local SHAKE_SEED = 0x174e5
local TWO_32 = 4294967296

local ReferenceHead = {}
ReferenceHead.__index = ReferenceHead

function ReferenceHead.new()
	local self = setmetatable({}, ReferenceHead)
	self:reset()
	return self
end

function ReferenceHead:reset()
	self.camera = CameraMotion.new()
	self.previous = CameraMotion.rotation()
	self.seeded = false
	self.out = { roll = 0, shake = CameraMotion.rotation() }
	self.random = SHAKE_SEED
	-- The top 15 bits of a 32-bit linear congruential generator.
	-- random < 2^32, so the product stays below 2^53 and is exact in a double.
	local function draw(): number
		self.random = (self.random * 1664525 + 1013904223) % TWO_32
		return bit32.rshift(self.random, 17)
	end
	self.vibration = CameraShake.MovementVibration.new(draw)
	self.mouse = CameraShake.MouseShakeLayer.new(draw)
	self.effects = CameraShake.CameraEffects.new(self.vibration, self.mouse)
end

function ReferenceHead:output()
	return self.out
end

local ZERO_ROTATION = table.freeze(CameraMotion.rotation())

function ReferenceHead:update(input)
	if input.paused then
		return self.out
	end
	local isFinite = ScalarMath.isFinite
	if
		not isFinite(input.yaw)
		or not isFinite(input.pitch)
		or not isFinite(input.dt)
		or input.dt <= 0
		or input.dt > ClassicFollow.MAX_FRAME_SECONDS
	then
		self:reset()
		return self.out
	end
	local control = ClassicFollow.controlDegrees(input.yaw, input.pitch)
	if not self.seeded then
		self.previous = control
		self.camera:seedControl(control)
		self.seeded = true
		return self.out
	end
	local lookX, lookY = ClassicFollow.lookInput(control, self.previous)
	self.previous = control
	local dt = ScalarMath.clampFrameDelta(input.dt)
	local speedCm = math.max(input.speedMetres or 0, 0) * ScalarMath.CENTIMETRES_PER_METRE
	local state = self.camera:update({
		control = control,
		worldDelta = dt,
		lookX = lookX,
		lookY = lookY,
		moveX = math.clamp(input.strafe or 0, -1, 1),
		speedCm = speedCm,
		aiming = input.aiming or false,
		leaningPressed = input.leaning or false,
		rotationMode = if input.aiming then 2 else 1,
	})
	self.out.roll = state.displayRoll
	-- Open-field shake intensity: speed and look input drive it, not the distance to a wall.
	self.effects:publishIntensity(false, 0)
	self.effects:tick(dt, { speed = speedCm, yawRate = state.yawRate, pitchRate = state.pitchRate, tremor = 0 }, lookX, lookY)
	self.out.shake = self.effects:present(ZERO_ROTATION)
	return self.out
end

return ReferenceHead
