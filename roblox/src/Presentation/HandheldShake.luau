-- Handheld camera shake: port of pmndrs/drei src/core/CameraShake.tsx (MIT, Copyright
-- (c) 2020 react-spring) on a port of the 2-D simplex noise of pmndrs/three-stdlib
-- src/math/SimplexNoise.ts (MIT, Copyright (c) 2021-2023 Poimandres; itself from
-- Stefan Gustavson's public-domain simplex noise). See roblox/THIRD-PARTY-NOTICES.md.
--
-- Each axis reads its own simplex stream at time * frequency; the amplitude is
-- intensity squared times the axis maximum (radians). With decay on, intensity drains
-- at decayRate per second.

local HandheldShake = {}
HandheldShake.__index = HandheldShake

---------------------------------------------------------------------------
-- SimplexNoise (2-D only).
---------------------------------------------------------------------------

local GRAD3 = {
	{ 1, 1, 0 },
	{ -1, 1, 0 },
	{ 1, -1, 0 },
	{ -1, -1, 0 },
	{ 1, 0, 1 },
	{ -1, 0, 1 },
	{ 1, 0, -1 },
	{ -1, 0, -1 },
	{ 0, 1, 1 },
	{ 0, -1, 1 },
	{ 0, 1, -1 },
	{ 0, -1, -1 },
}
local F2 = 0.5 * (math.sqrt(3) - 1)
local G2 = (3 - math.sqrt(3)) / 6

local SimplexNoise = {}
SimplexNoise.__index = SimplexNoise
HandheldShake.SimplexNoise = SimplexNoise

-- random: function() -> [0, 1) (default math.random).
function SimplexNoise.new(random: (() -> number)?)
	local r = random or math.random
	local p, perm = {}, {}
	for i = 0, 255 do
		p[i] = math.floor(r() * 256)
	end
	for i = 0, 511 do
		perm[i] = p[bit32.band(i, 255)]
	end
	return setmetatable({ perm = perm }, SimplexNoise)
end

function SimplexNoise:noise(xin: number, yin: number): number
	local perm = self.perm
	local s = (xin + yin) * F2
	local i = math.floor(xin + s)
	local j = math.floor(yin + s)
	local t = (i + j) * G2
	local x0 = xin - (i - t)
	local y0 = yin - (j - t)
	local i1, j1 = 0, 1
	if x0 > y0 then
		i1, j1 = 1, 0
	end
	local x1, y1 = x0 - i1 + G2, y0 - j1 + G2
	local x2, y2 = x0 - 1 + 2 * G2, y0 - 1 + 2 * G2
	local ii, jj = bit32.band(i, 255), bit32.band(j, 255)
	local gi0 = perm[ii + perm[jj]] % 12
	local gi1 = perm[ii + i1 + perm[jj + j1]] % 12
	local gi2 = perm[ii + 1 + perm[jj + 1]] % 12
	local n0, n1, n2 = 0, 0, 0
	local t0 = 0.5 - x0 * x0 - y0 * y0
	if t0 >= 0 then
		t0 *= t0
		local g = GRAD3[gi0 + 1]
		n0 = t0 * t0 * (g[1] * x0 + g[2] * y0)
	end
	local t1 = 0.5 - x1 * x1 - y1 * y1
	if t1 >= 0 then
		t1 *= t1
		local g = GRAD3[gi1 + 1]
		n1 = t1 * t1 * (g[1] * x1 + g[2] * y1)
	end
	local t2 = 0.5 - x2 * x2 - y2 * y2
	if t2 >= 0 then
		t2 *= t2
		local g = GRAD3[gi2 + 1]
		n2 = t2 * t2 * (g[1] * x2 + g[2] * y2)
	end
	-- Scaled to return values in the interval [-1, 1].
	return 70 * (n0 + n1 + n2)
end

---------------------------------------------------------------------------
-- CameraShake.
---------------------------------------------------------------------------

-- drei defaults: intensity 1, decay off, decayRate 0.65, max 0.1 rad and 0.1 Hz per axis.
function HandheldShake.defaultSettings()
	return {
		intensity = 1,
		decay = false,
		decayRate = 0.65,
		maxYaw = 0.1,
		maxPitch = 0.1,
		maxRoll = 0.1,
		yawFrequency = 0.1,
		pitchFrequency = 0.1,
		rollFrequency = 0.1,
	}
end

-- A gentle bodycam handheld preset: small angles, faster than drei's default drift.
function HandheldShake.bodycamSettings()
	local s = HandheldShake.defaultSettings()
	s.intensity = 0.55
	s.maxYaw, s.maxPitch, s.maxRoll = 0.012, 0.010, 0.016
	s.yawFrequency, s.pitchFrequency, s.rollFrequency = 0.45, 0.55, 0.35
	return s
end

function HandheldShake.new(settings, random: (() -> number)?)
	local self = setmetatable({}, HandheldShake)
	self.settings = settings or HandheldShake.defaultSettings()
	self.intensity = self.settings.intensity
	self.elapsed = 0
	self.yawNoise = SimplexNoise.new(random)
	self.pitchNoise = SimplexNoise.new(random)
	self.rollNoise = SimplexNoise.new(random)
	return self
end

-- 0..1; like the source, values outside are clamped.
function HandheldShake:setIntensity(value: number)
	self.intensity = math.clamp(value, 0, 1)
end

function HandheldShake:getIntensity(): number
	return self.intensity
end

-- Returns yaw, pitch, roll offsets in radians for this frame.
function HandheldShake:update(dt: number): (number, number, number)
	local s = self.settings
	self.elapsed += dt
	local shake = self.intensity ^ 2
	local t = self.elapsed
	local yaw = s.maxYaw * shake * self.yawNoise:noise(t * s.yawFrequency, 1)
	local pitch = s.maxPitch * shake * self.pitchNoise:noise(t * s.pitchFrequency, 1)
	local roll = s.maxRoll * shake * self.rollNoise:noise(t * s.rollFrequency, 1)
	if s.decay and self.intensity > 0 then
		self:setIntensity(self.intensity - s.decayRate * dt)
	end
	return yaw, pitch, roll
end

return HandheldShake
