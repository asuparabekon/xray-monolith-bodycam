-- Port of src/xrGame/bodycam_reference/movement_vibration.*, mouse_shake.* and camera_effects.*:
-- the Reference camera shake (movement vibration and mouse-started shakes) in degrees.
-- Only the rotation is ported; the Classic rig never reads the shake's location.

local ScalarMath = require(script.Parent.ScalarMath)

local mapRangeClamped = ScalarMath.mapRangeClamped
local fInterpTo = ScalarMath.fInterpTo
local wrapDegrees = ScalarMath.wrapDegrees
local atan2 = ScalarMath.atan2
local f32 = ScalarMath.f32
local DEG = ScalarMath.DEGREES_PER_RADIAN
-- The source's single-precision degree factor.
local RAD_F = 0.01745329238474369

local CameraShake = {}

-- Hex float constants of the source, built from their mantissas.
local function hexFloat(mantissa: number, exponent: number): number
	return (1 + mantissa / 2 ^ 24) * 2 ^ exponent
end
local VIBRATION_PHASE_SCALE = hexFloat(0xFE03FC, -8) -- float(255 / 32767)
local RANDOM_PHASE_SCALE = hexFloat(0x9222DA, -13) -- float(2 pi / 32767)
local LOOK_ACTUATION_SQUARED = hexFloat(0x5798EC, -27)

-- Perlin permutation (257 entries) and gradients.
-- stylua: ignore
local PERMUTATION = {
	63, 9, 212, 205, 31, 128, 72, 59, 137, 203, 195, 170, 181, 115, 165, 40, 116, 139, 175, 225, 132, 99, 222, 2, 41,
	15, 197, 93, 169, 90, 228, 43, 221, 38, 206, 204, 73, 17, 97, 10, 96, 47, 32, 138, 136, 30, 219, 78, 224, 13, 193,
	88, 134, 211, 7, 112, 176, 19, 106, 83, 75, 217, 85, 0, 98, 140, 229, 80, 118, 151, 117, 251, 103, 242, 81, 238,
	172, 82, 110, 4, 227, 77, 243, 46, 12, 189, 34, 188, 200, 161, 68, 76, 171, 194, 57, 48, 247, 233, 51, 105, 5, 23,
	42, 50, 216, 45, 239, 148, 249, 84, 70, 125, 108, 241, 62, 66, 64, 240, 173, 185, 250, 49, 6, 37, 26, 21, 244, 60,
	223, 255, 16, 145, 27, 109, 58, 102, 142, 253, 120, 149, 160, 124, 156, 79, 186, 135, 127, 14, 121, 22, 65, 54,
	153, 91, 213, 174, 24, 252, 131, 192, 190, 202, 208, 35, 94, 231, 56, 95, 183, 163, 111, 147, 25, 67, 36, 92, 236,
	71, 166, 1, 187, 100, 130, 143, 237, 178, 158, 104, 184, 159, 177, 52, 214, 230, 119, 87, 114, 201, 179, 198, 3,
	248, 182, 39, 11, 152, 196, 113, 20, 232, 69, 141, 207, 234, 53, 86, 180, 226, 74, 150, 218, 29, 133, 8, 44, 123,
	28, 146, 89, 101, 154, 220, 126, 155, 122, 210, 168, 254, 162, 129, 33, 18, 209, 61, 191, 199, 157, 245, 55, 164,
	167, 215, 246, 144, 107, 235, 63,
}
local GRADIENTS = {
	-1, -0.875, -0.75, -0.625, -0.5, -0.375, -0.25, -0.125, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1,
}

function CameraShake.perlin1D(x: number): number
	local floor = math.floor(x)
	local t = x - floor
	local index = 0
	if ScalarMath.isFinite(floor) and floor >= -2147483648 and floor < 2147483648 then
		index = floor % 256
	end
	local t2 = t * t
	local a = t * GRADIENTS[bit32.band(PERMUTATION[index + 1], 15) + 1]
	local fade = ((t * 6 - 15) * t + 10) * t * t2
	fade += fade
	local b = (t - 1) * GRADIENTS[bit32.band(PERMUTATION[index + 2], 15) + 1]
	return ((b - a) * fade + a) + a
end
local perlin1D = CameraShake.perlin1D

-- Row-major rotation matrix of the source (pitch, yaw, roll in degrees), 16 entries, 1-based.
local function rotationMatrix(pitch: number, yaw: number, roll: number)
	local sp, cp = math.sin(pitch * RAD_F), math.cos(pitch * RAD_F)
	local sy, cy = math.sin(yaw * RAD_F), math.cos(yaw * RAD_F)
	local sr, cr = math.sin(roll * RAD_F), math.cos(roll * RAD_F)
	return {
		cy * cp, cp * sy, sp, 0,
		((sr * sp) * cy) - (cr * sy), ((sr * sp) * sy) + (cr * cy), -(cp * sr), 0,
		-(((cr * sp) * cy) + (sr * sy)), (cy * sr) - ((cr * sp) * sy), cr * cp, 0,
		0, 0, 0, 1,
	}
end

local function matrixRotator(m)
	local yaw = atan2(m[2], m[1]) * DEG
	local pitch = atan2(m[3], math.sqrt((m[2] * m[2]) + (m[1] * m[1]))) * DEG
	local s = rotationMatrix(pitch, yaw, 0)
	local numerator = ((s[5] * m[9]) + (s[6] * m[10])) + (s[7] * m[11])
	local denominator = ((s[6] * m[6]) + (s[5] * m[5])) + (s[7] * m[7])
	return { pitch = pitch, yaw = yaw, roll = atan2(numerator, denominator) * DEG }
end

local function rowMultiply(a, b)
	local out = table.create(16, 0)
	for r = 0, 3 do
		for c = 0, 3 do
			out[r * 4 + c + 1] = (((a[r * 4 + 1] * b[c + 1]) + (a[r * 4 + 2] * b[4 + c + 1])) + (a[r * 4 + 3] * b[8 + c + 1]))
				+ (a[r * 4 + 4] * b[12 + c + 1])
		end
	end
	return out
end

local function normalFloat(a: number): number
	a = math.fmod(a, 360)
	if a < 0 then
		a += 360
	end
	if a > 180 then
		a -= 360
	end
	return a
end

local function scaledPitch(base: number, offset: number): number
	local b = wrapDegrees(base)
	local v = b + wrapDegrees(offset)
	local n = normalFloat(v)
	return normalFloat(if n > 89.9 then 89.9 elseif n < -89.9 then -89.9 else v) - b
end

-- Composes a shake rotation (degrees) onto a base rotation.
local function composeRotation(base, pitch: number, yaw: number, roll: number)
	local matrix = rotationMatrix(base.pitch, base.yaw, base.roll)
	return matrixRotator(rowMultiply(rotationMatrix(pitch, yaw, roll), matrix))
end

local function applyCameraShake(base, raw, shakeScale: number)
	local scale = math.max(0, shakeScale)
	local b = wrapDegrees(base.pitch)
	local v = b + wrapDegrees(raw.pitch)
	local n = wrapDegrees(v)
	local limit = 89.9
	local pitch = wrapDegrees(if n > limit then limit elseif n < -limit then -limit else v) - b
	return composeRotation(base, scaledPitch(base.pitch, pitch * scale), raw.yaw * scale, raw.roll * scale)
end

---------------------------------------------------------------------------
-- Movement vibration.
---------------------------------------------------------------------------

function CameraShake.movementVibrationTarget(input, intensity: number): number
	local motion = if input.speed < 150
		then mapRangeClamped(input.speed, 0, 150, 0, 1)
		else mapRangeClamped(input.speed, 20, 300, 0, 2) + mapRangeClamped(math.abs(input.yawRate + input.pitchRate), 1, 2, 0, 1)
	return (motion + mapRangeClamped(input.tremor or 0, 0.5, 1, 0, 2)) * intensity
end

local MovementVibration = {}
MovementVibration.__index = MovementVibration
CameraShake.MovementVibration = MovementVibration

-- draw15: function returning a 15-bit random integer.
function MovementVibration.new(draw15: () -> number)
	local phase = {}
	-- z, y, x, roll, yaw, pitch, fov: RNG order. Every axis draws, used or not.
	for i = 1, 7 do
		phase[i] = math.min(math.floor(f32(draw15() * VIBRATION_PHASE_SCALE)), 254)
	end
	return setmetatable({
		phase = phase,
		scale = 0.5,
		elapsed = 0,
		inTime = 0,
		blendingIn = true,
		intensity = 1,
		target = 0,
		raw = nil,
	}, MovementVibration)
end

function MovementVibration:componentTick(dt: number, input)
	self.target = CameraShake.movementVibrationTarget(input, self.intensity)
	self.scale = fInterpTo(self.scale, self.target, dt, 5)
end

function MovementVibration:publishIntensity(hit: boolean, distanceCm: number)
	self.intensity = if hit then mapRangeClamped(distanceCm, 350, 1500, 1, 0.3) else 0.3
end

local VIBRATION_INDICES = { 6, 5, 4 } -- pitch, yaw, roll phases
local VIBRATION_AMPLITUDES = { 0.3, 0.25, 0.2 }
local VIBRATION_FREQUENCIES = { 11, 9, 13 }

function MovementVibration:apply(base, dt: number)
	if dt > 0 then
		local values = {}
		for i = 1, 3 do
			local index = VIBRATION_INDICES[i]
			-- float phase = float(dt) * frequency + phase, rounded as the source does.
			self.phase[index] = f32(f32(f32(dt) * VIBRATION_FREQUENCIES[i]) + self.phase[index])
			values[i] = perlin1D(self.phase[index]) * VIBRATION_AMPLITUDES[i]
		end
		self.elapsed += dt
		local envelope = 1
		if self.blendingIn then
			self.inTime += dt
			if self.inTime < 0.2 then
				envelope = self.inTime / 0.2
			else
				self.inTime = 0.2
				self.blendingIn = false
			end
		end
		self.raw = { pitch = values[1] * envelope, yaw = values[2] * envelope, roll = values[3] * envelope }
	end
	if not self.raw then
		return base
	end
	local scale = math.max(0, self.scale)
	local raw = self.raw
	return composeRotation(base, scaledPitch(base.pitch, raw.pitch * scale), raw.yaw * scale, raw.roll * scale)
end

---------------------------------------------------------------------------
-- Mouse shake: one camera shake started by mouse movement, and the layer that stacks them.
---------------------------------------------------------------------------

local BLEND_SECONDS = 0.1
-- New shakes start at most this often, so the stacked shake does not grow with frame rate.
local EMIT_SECONDS = 1 / 30

local MouseShake = {}
MouseShake.__index = MouseShake
CameraShake.MouseShake = MouseShake

function MouseShake.new(draw15: () -> number)
	-- pitch, yaw, roll, x, y, z, fov: random draw order. The z oscillator starts at phase zero.
	local phase = {}
	for i = 1, 7 do
		phase[i] = if i ~= 6 then f32(draw15() * RANDOM_PHASE_SCALE) else 0
	end
	return setmetatable({
		phase = phase,
		remaining = 0.2,
		envelope = 0,
		finished = false,
		rotation = { pitch = 0, yaw = 0, roll = 0 },
		inTime = 0,
		outTime = 0,
		inFlag = true,
		outFlag = false,
	}, MouseShake)
end

function MouseShake:update(dt: number)
	if self.remaining > 0 then
		self.remaining = math.max(self.remaining - dt, 0)
	end
	if self.inFlag then
		self.inTime += dt
	end
	if self.outFlag then
		self.outTime += dt
	end
	if self.remaining > 0 and self.remaining < BLEND_SECONDS then
		self.outFlag = true
		self.outTime = BLEND_SECONDS - self.remaining
	end
	if self.inFlag and self.inTime > BLEND_SECONDS then
		self.inFlag = false
	end
	local exhausted = self.remaining <= 0
	if self.outFlag and self.outTime > BLEND_SECONDS then
		self.outTime = BLEND_SECONDS
		exhausted = true
	end
	self.envelope = if exhausted
		then 0
		else math.min(
			if self.outFlag then 1 - self.outTime / BLEND_SECONDS else 1,
			if self.inFlag then self.inTime / BLEND_SECONDS else 1
		)
	local r = self.rotation
	r.pitch, r.yaw, r.roll = 0, 0, 0
	self.finished = self.remaining <= 0
	if self.envelope > 0 then
		local phase = self.phase
		local function oscillate(i: number, amplitude: number, frequency: number, perlin: boolean): number
			phase[i] += f32(dt * frequency)
			local wave = if perlin then perlin1D(f32(phase[i])) else math.sin(phase[i])
			return wave * amplitude
		end
		-- The location oscillators (indices 5 and 6) still advance, as in the source.
		oscillate(5, 0.2, 0, false)
		oscillate(6, 5, 0.3, false)
		r.pitch = oscillate(1, 0.3, 0.1, false) * self.envelope
		r.yaw = oscillate(2, 0.2, 2, true) * self.envelope
		r.roll = oscillate(3, 0.5, 2, true) * self.envelope
	end
	return self
end

local MouseShakeLayer = {}
MouseShakeLayer.__index = MouseShakeLayer
CameraShake.MouseShakeLayer = MouseShakeLayer

function MouseShakeLayer.new(draw15: () -> number)
	return setmetatable({ draw15 = draw15, instances = {}, previousNonzero = false, events = 0, emitWait = 0 }, MouseShakeLayer)
end

function MouseShakeLayer:inputFrame(x: number, y: number, dt: number, speed: number)
	local nonzero = (x * x) + (y * y) >= LOOK_ACTUATION_SQUARED
	local event = nonzero or self.previousNonzero
	self.previousNonzero = nonzero
	if not event then
		self.emitWait = 0 -- The first motion frame starts a shake at once.
		return
	end
	if self.emitWait > 0 then
		self.emitWait -= dt
		if self.emitWait > 0 then
			return
		end
	end
	self.emitWait = math.max(self.emitWait + EMIT_SECONDS, 0)
	local inverse = if dt == 0 then 0 else 1 / dt
	local yaw, pitch = (x * inverse) / 50, (y * inverse) / 50
	local eventScale = math.clamp(((math.abs(pitch) + math.abs(yaw)) - 1) / 3, 0, 1) * 3
		+ math.clamp(speed / 600, 0, 1) * 1.5
	self.events += 1
	table.insert(self.instances, { id = self.events, scale = eventScale * 0.5, shake = MouseShake.new(self.draw15) })
end

function MouseShakeLayer:update(base, dt: number)
	local result = base
	local kept = {}
	for _, entry in self.instances do
		local raw = entry.shake:update(dt)
		if not raw.finished then
			result = applyCameraShake(result, raw.rotation, entry.scale)
			table.insert(kept, entry)
		end
	end
	self.instances = kept
	return result
end

---------------------------------------------------------------------------
-- Camera effects: one pending delta per frame; the first present applies it.
---------------------------------------------------------------------------

local CameraEffects = {}
CameraEffects.__index = CameraEffects
CameraShake.CameraEffects = CameraEffects

function CameraEffects.new(vibration, mouse)
	return setmetatable({ vibration = vibration, mouse = mouse, enabled = true, pendingDelta = 0, cached = nil }, CameraEffects)
end

function CameraEffects:tick(dt: number, movement, lookX: number, lookY: number)
	self.pendingDelta = dt
	self.vibration:componentTick(dt, movement)
	self.mouse:inputFrame(lookX, lookY, dt, movement.speed)
end

function CameraEffects:publishIntensity(hit: boolean, distanceCm: number)
	self.vibration:publishIntensity(hit, distanceCm)
end

-- base: { pitch, yaw, roll } in degrees. Returns the shaken rotation.
function CameraEffects:present(base)
	if self.pendingDelta > 0 then
		self.cached = self.mouse:update(self.vibration:apply(base, self.pendingDelta), self.pendingDelta)
		self.pendingDelta = 0
	end
	return if self.enabled and self.cached then self.cached else base
end

return CameraShake
