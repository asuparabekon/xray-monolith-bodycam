-- Port of SPT-SWAY src/Util/SwayNoise.cs and src/Util/OnePole.cs.
-- SPT-SWAY: Copyright (c) 2026 Savant, MIT License (see roblox/THIRD-PARTY-NOTICES.md).
--
-- Band-limited 1-D value noise: a hash-based value noise with a smootherstep
-- interpolant, continuous in the first and second derivative, so a stack of these
-- layers looks like flesh rather than like a sawtooth. The hash reproduces the C#
-- unsigned 32-bit arithmetic exactly; the final division matches the C# float.

local SwayNoise = {}

local TWO_32 = 4294967296
local bxor, rshift = bit32.bxor, bit32.rshift
local pack, unpack = string.pack, string.unpack

local function f32(x: number): number
	return (unpack("<f", pack("<f", x)))
end

-- (a * b) mod 2^32 for unsigned 32-bit a and b, exact in doubles.
local function mul32(a: number, b: number): number
	local lo = a % 65536
	local hi = (a - lo) / 65536
	return ((hi * b) % TWO_32 * 65536 + lo * b) % TWO_32
end
SwayNoise.mul32 = mul32

-- Hash(int i, int seed) in [-1, 1].
function SwayNoise.hash(i: number, seed: number): number
	local h = (mul32(i % TWO_32, 374761393) + mul32(seed % TWO_32, 668265263)) % TWO_32
	h = mul32(bxor(h, rshift(h, 13)), 1274126177)
	h = bxor(h, rshift(h, 16))
	return (f32(h) / TWO_32) * 2 - 1
end
local hash = SwayNoise.hash

local function smootherstep(t: number): number
	return t * t * t * (t * (t * 6 - 15) + 10)
end

-- Signed noise in roughly [-1, 1] at position x on stream seed.
function SwayNoise.signed(x: number, seed: number): number
	local i = math.floor(x)
	local f = x - i
	local t = smootherstep(f)
	local a = hash(i, seed)
	local b = hash(i + 1, seed)
	return a + (b - a) * math.clamp(t, 0, 1)
end
local signed = SwayNoise.signed

-- Several detuned octaves summed and normalised. spread pushes each octave off an
-- exact frequency multiple so the layers never realign into a beat.
function SwayNoise.fractal(x: number, seed: number, octaves: number, spread: number): number
	local sum, norm, amp, freq = 0, 0, 1, 1
	for o = 0, octaves - 1 do
		-- 1.87 rather than 2.0: an irrational-ish ratio keeps the octaves
		-- from ever sharing a period.
		local detune = 1 + spread * (hash(o * 71 + 13, seed) * 0.5)
		sum += signed(x * freq * detune, seed + o * 977) * amp
		norm += amp
		amp *= 0.55
		freq *= 1.87
	end
	return if norm > 0 then sum / norm else 0
end

---------------------------------------------------------------------------
-- OnePole: one-pole low-pass, frame-rate independent. The coefficient is derived
-- from the real elapsed time, so the same cutoff behaves identically at 60 and at
-- 240 fps. Works on numbers or on {x, y, z} tables (filterVector).
---------------------------------------------------------------------------

local OnePole = {}
OnePole.__index = OnePole
SwayNoise.OnePole = OnePole

function OnePole.new()
	return setmetatable({ primed = false, state = 0, x = 0, y = 0, z = 0 }, OnePole)
end

local function coefficient(dt: number, cutoffHz: number): number
	return math.clamp(1 - math.exp(-2 * math.pi * cutoffHz * dt), 0, 1)
end

-- A cutoff of zero or less passes the signal through untouched.
function OnePole:filter(value: number, dt: number, cutoffHz: number): number
	if cutoffHz <= 0 or not self.primed then
		self.state, self.primed = value, true
		return value
	end
	self.state += (value - self.state) * coefficient(dt, cutoffHz)
	return self.state
end

function OnePole:filterVector(x: number, y: number, z: number, dt: number, cutoffHz: number): (number, number, number)
	if cutoffHz <= 0 or not self.primed then
		self.x, self.y, self.z, self.primed = x, y, z, true
		return x, y, z
	end
	local a = coefficient(dt, cutoffHz)
	self.x += (x - self.x) * a
	self.y += (y - self.y) * a
	self.z += (z - self.z) * a
	return self.x, self.y, self.z
end

function OnePole:reset()
	self.primed, self.state, self.x, self.y, self.z = false, 0, 0, 0, 0
end

return SwayNoise
