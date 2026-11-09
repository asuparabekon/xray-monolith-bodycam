-- Port of src/xrGame/bodycam_reference/scalar_math.h and constants.h.
-- The C++ source rounds some intermediates to single precision; Luau numbers are
-- doubles, so results agree to about 1e-6 rather than bit for bit.

local ScalarMath = {}

ScalarMath.PI = math.pi
ScalarMath.DEGREES_PER_RADIAN = 57.29577951308232
ScalarMath.RADIANS_PER_DEGREE = 0.017453292519943295
ScalarMath.CENTIMETRES_PER_METRE = 100
-- Control degrees per unit of look input (control.h).
ScalarMath.CONTROL_DEGREES_PER_LOOK = 2.5

local function trunc(x: number): number
	return if x >= 0 then math.floor(x) else math.ceil(x)
end
ScalarMath.trunc = trunc


-- Wraps an angle in degrees to (-180, 180].
function ScalarMath.wrapDegrees(value: number): number
	local angle = value - trunc(value / 360) * 360
	if not (angle >= 0) then
		angle += 360
	end
	if angle > 180 then
		angle -= 360
	end
	return angle
end

local function clamp01(x: number): number
	if x < 0 then
		return 0
	end
	if x > 1 then
		return 1
	end
	return x
end
ScalarMath.clamp01 = clamp01

-- Maps [inLow, inHigh] linearly onto [outLow, outHigh] and clamps outside the input range.
function ScalarMath.mapRangeClamped(value: number, inLow: number, inHigh: number, outLow: number, outHigh: number): number
	return outLow + (outHigh - outLow) * clamp01((value - inLow) / (inHigh - inLow))
end

-- Moves current towards target by the fraction speed * dt and snaps once they are within 1e-4.
function ScalarMath.fInterpTo(current: number, target: number, dt: number, speed: number): number
	if speed <= 0 then
		return target
	end
	local delta = target - current
	if delta * delta < 1e-8 then
		return target
	end
	return current + delta * clamp01(speed * dt)
end

-- Rounds to single precision. Used only where the source accumulates in float and
-- the rounding visibly changes the result over time (the shake oscillator phases).
local pack, unpack = string.pack, string.unpack
function ScalarMath.f32(value: number): number
	return (unpack("<f", pack("<f", value)))
end

-- Clamps a world frame delta in seconds to [.0005, .4] in single precision, as the source
-- does; the head and follow see this float-rounded delta everywhere.
function ScalarMath.clampFrameDelta(seconds: number): number
	return ScalarMath.f32(math.max(0.0005, math.min(0.4, seconds)))
end

function ScalarMath.isFinite(value: number): boolean
	return value == value and value ~= math.huge and value ~= -math.huge
end

-- atan2 that returns 0 for (0, 0), as the source does.
function ScalarMath.atan2(y: number, x: number): number
	if y == 0 and x == 0 then
		return 0
	end
	return math.atan2(y, x)
end

return ScalarMath
