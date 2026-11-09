-- Port of src/xrGame/bodycam_smoothing.h and the angle helpers of bodycam_simulation.cpp.
-- Plain numbers only; no Roblox types, so the module also runs outside Roblox.

local MathUtil = {}

MathUtil.PI = math.pi
MathUtil.EPSILON = 1.0e-6
-- Smoothing integrates at most this much time per frame, so a long frame eases instead of snapping.
MathUtil.MAX_SMOOTHING_STEP = 0.033
-- A follow with zero damping would never move; this is the slowest it gets.
MathUtil.MIN_FOLLOW_DAMPING = 0.01

local PI = math.pi
local TWO_PI = 2 * math.pi
local exp = math.exp
local min = math.min
local max = math.max

-- std::clamp. Unlike math.clamp it does not raise when low > high.
function MathUtil.clamp(value: number, low: number, high: number): number
	if value < low then
		return low
	end
	if value > high then
		return high
	end
	return value
end
local clamp = MathUtil.clamp

-- Wraps to -pi..pi. Non-finite input returns 0 (the C++ loop never ends on it).
function MathUtil.normalizeAngle(value: number): number
	if value ~= value or value == math.huge or value == -math.huge then
		return 0
	end
	if value > PI or value < -PI then
		-- Same result as the source's repeated subtraction, without the loop on large values.
		while value > PI do
			value -= TWO_PI
		end
		while value < -PI do
			value += TWO_PI
		end
	end
	return value
end
local normalizeAngle = MathUtil.normalizeAngle

-- Shortest signed turn from current to target.
function MathUtil.angleDifference(target: number, current: number): number
	return normalizeAngle(target - current)
end

function MathUtil.degToRad(value: number): number
	return value * PI / 180
end

function MathUtil.radToDeg(value: number): number
	return value * 180 / PI
end

-- Share of the remaining distance covered this frame at the given rate (1/s).
function MathUtil.blendFactor(rate: number, dt: number): number
	return 1 - exp(-rate * min(dt, MathUtil.MAX_SMOOTHING_STEP))
end
local blendFactor = MathUtil.blendFactor

function MathUtil.smoothFloat(current: number, target: number, rate: number, dt: number): number
	return current + (target - current) * blendFactor(rate, dt)
end
local smoothFloat = MathUtil.smoothFloat

function MathUtil.smoothAngle(current: number, target: number, rate: number, dt: number): number
	return normalizeAngle(current + normalizeAngle(target - current) * blendFactor(rate, dt))
end

-- Smooths a Vec3 table in place.
function MathUtil.smoothVector(current, target, rate: number, dt: number)
	current.x = smoothFloat(current.x, target.x, rate, dt)
	current.y = smoothFloat(current.y, target.y, rate, dt)
	current.z = smoothFloat(current.z, target.z, rate, dt)
end

-- The follow settings are a speed and a damping; their product is the rate.
function MathUtil.followRate(speed: number, damping: number): number
	return speed * max(damping, MathUtil.MIN_FOLLOW_DAMPING)
end

function MathUtil.lerp(a: number, b: number, t: number): number
	return a + (b - a) * t
end

-- Clamps each component of a Vec3 table to -limit..limit in place.
function MathUtil.clampVector(value, limit: number)
	value.x = clamp(value.x, -limit, limit)
	value.y = clamp(value.y, -limit, limit)
	value.z = clamp(value.z, -limit, limit)
end

function MathUtil.isFinite(value: number): boolean
	return value == value and value ~= math.huge and value ~= -math.huge
end

return MathUtil
