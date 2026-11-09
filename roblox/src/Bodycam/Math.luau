-- Shared numeric helpers for the Bodycam package. Pure Luau: no Roblox API.
--
-- Springs are advanced with their exact solution for a target held fixed over the
-- step, so they stay stable at any frame time.

local BodycamMath = {}

local TAU = 2 * math.pi

function BodycamMath.clamp(x: number, lo: number, hi: number): number
	if x < lo then
		return lo
	elseif x > hi then
		return hi
	end
	return x
end

function BodycamMath.lerp(a: number, b: number, t: number): number
	return a + (b - a) * t
end

-- An angle in (-pi, pi].
function BodycamMath.wrapAngle(a: number): number
	local w = (a + math.pi) % TAU - math.pi
	if w <= -math.pi then
		w += TAU
	end
	return w
end

-- Natural frequency in Hz to angular frequency in rad/s.
function BodycamMath.omega(hz: number): number
	return TAU * hz
end

-- Exponential smoothing factor for a rate (1/s) over dt: the share of the remaining
-- distance covered this step.
function BodycamMath.alpha(rate: number, dt: number): number
	if rate <= 0 or dt <= 0 then
		return 0
	end
	return 1 - math.exp(-rate * dt)
end

-- Moves current toward target by exponential smoothing at rate (1/s).
function BodycamMath.smooth(current: number, target: number, rate: number, dt: number): number
	return current + (target - current) * BodycamMath.alpha(rate, dt)
end

-- The smoothing rate that covers about 95 % of a step in `seconds`.
function BodycamMath.rateForTime(seconds: number): number
	if seconds <= 1e-6 then
		return math.huge
	end
	return 3 / seconds
end

-- Smoothly saturating map of a normalised input: linear up to 0.5, then a cubic ease
-- that reaches exactly +-1 (with zero slope) at +-1 and stays there.
function BodycamMath.softSaturate(t: number): number
	local s = if t < 0 then -1 else 1
	local a = math.abs(t)
	if a <= 0.5 then
		return t
	elseif a >= 1 then
		return s
	end
	local u = (a - 0.5) * 2
	return s * (0.5 + 0.5 * u + 0.5 * u * u - 0.5 * u * u * u)
end

-- Soft limit: x passes almost unchanged while small and reaches `limit` (with zero
-- slope) when |x| reaches `limit`.
function BodycamMath.softLimit(x: number, limit: number): number
	if limit <= 0 then
		return 0
	end
	return limit * BodycamMath.softSaturate(x / limit)
end

-- Damped spring step toward a fixed target. omega rad/s, zeta damping ratio.
-- Returns the new position and velocity.
function BodycamMath.spring(x: number, v: number, target: number, omega: number, zeta: number, dt: number): (number, number)
	if dt <= 0 or omega <= 0 then
		return x, v
	end
	local y0 = x - target
	if zeta < 0.9999 then
		local wd = omega * math.sqrt(1 - zeta * zeta)
		local e = math.exp(-zeta * omega * dt)
		local c, s = math.cos(wd * dt), math.sin(wd * dt)
		local y = e * (y0 * c + (v + zeta * omega * y0) / wd * s)
		local vy = e * (v * c - (zeta * omega * v + omega * omega * y0) / wd * s)
		return target + y, vy
	elseif zeta <= 1.0001 then
		local e = math.exp(-omega * dt)
		local b = v + omega * y0
		return target + (y0 + b * dt) * e, (v - omega * b * dt) * e
	end
	local root = math.sqrt(zeta * zeta - 1)
	local r1 = -omega * (zeta - root)
	local r2 = -omega * (zeta + root)
	local c1 = (v - r2 * y0) / (r1 - r2)
	local c2 = y0 - c1
	local e1, e2 = math.exp(r1 * dt), math.exp(r2 * dt)
	return target + c1 * e1 + c2 * e2, c1 * r1 * e1 + c2 * r2 * e2
end

-- Critically damped spring step (zeta = 1).
function BodycamMath.criticalSpring(x: number, v: number, target: number, omega: number, dt: number): (number, number)
	return BodycamMath.spring(x, v, target, omega, 1, dt)
end

-- The largest displacement a spring at rest reaches after a unit velocity impulse.
-- Dividing a wanted peak by this gives the impulse velocity.
function BodycamMath.impulsePeak(omega: number, zeta: number): number
	if omega <= 0 then
		return 0
	end
	if zeta < 0.9999 then
		local q = math.sqrt(1 - zeta * zeta)
		local wd = omega * q
		local tp = math.atan2(q, zeta) / wd
		return math.exp(-zeta * omega * tp) * math.sin(wd * tp) / wd
	elseif zeta <= 1.0001 then
		return math.exp(-1) / omega
	end
	local root = math.sqrt(zeta * zeta - 1)
	local r1 = -omega * (zeta - root)
	local r2 = -omega * (zeta + root)
	local tp = math.log(r2 / r1) / (r1 - r2)
	return (math.exp(r1 * tp) - math.exp(r2 * tp)) / (r1 - r2)
end

-- Impulse velocity that makes a resting spring peak at `peak`.
function BodycamMath.impulseFor(peak: number, omega: number, zeta: number): number
	local p = BodycamMath.impulsePeak(omega, zeta)
	return if p > 0 then peak / p else 0
end

---------------------------------------------------------------------------
-- 3-vector spring: { x = {a, b, c}, v = {a, b, c} } in plain arrays.
---------------------------------------------------------------------------

export type Spring3 = { x: { number }, v: { number } }

function BodycamMath.spring3(): Spring3
	return { x = { 0, 0, 0 }, v = { 0, 0, 0 } }
end

function BodycamMath.resetSpring3(s: Spring3)
	for i = 1, 3 do
		s.x[i] = 0
		s.v[i] = 0
	end
end

-- Advances every axis toward target (array of 3, default zero).
function BodycamMath.stepSpring3(s: Spring3, target: { number }?, omega: number, zeta: number, dt: number)
	for i = 1, 3 do
		local t = if target then target[i] else 0
		s.x[i], s.v[i] = BodycamMath.spring(s.x[i], s.v[i], t, omega, zeta, dt)
	end
end

-- Critically damped 3-vector step.
function BodycamMath.criticalSpring3(s: Spring3, target: { number }?, omega: number, dt: number)
	BodycamMath.stepSpring3(s, target, omega, 1, dt)
end

-- Clamps one axis to [lo, hi]; velocity pushing further out is removed.
function BodycamMath.capAxis(s: Spring3, i: number, lo: number, hi: number)
	if s.x[i] > hi then
		s.x[i] = hi
		if s.v[i] > 0 then
			s.v[i] = 0
		end
	elseif s.x[i] < lo then
		s.x[i] = lo
		if s.v[i] < 0 then
			s.v[i] = 0
		end
	end
end

return BodycamMath
