-- Movement inertia: eases the commanded planar velocity. Pure Luau.
--
-- Speeding up reaches about 95 % in accelTime, slowing down in decelTime and a full
-- stop in stopTime. Reversing direction lengthens the time by up to turnPenalty. The
-- eased speed never exceeds the target speed while speeding up, and never rises
-- while slowing down (no overshoot). Bypassed while crouched, climbing, in the air,
-- or aiming when bypassAiming is set.

local BMath = require(script.Parent.Math)

local MovementInertia = {}
MovementInertia.__index = MovementInertia

-- config: the Settings `inertia` group.
function MovementInertia.new(config)
	local self = setmetatable({}, MovementInertia)
	self.config = config
	self:reset()
	return self
end

function MovementInertia:reset(x: number?, z: number?)
	self.x, self.z = x or 0, z or 0
	self.speedFraction = 1
end

-- Whether the inertia is skipped this frame.
function MovementInertia:bypassed(state): boolean
	if not self.config.enabled then
		return true
	end
	if state.crouching or state.climbing or state.grounded == false then
		return true
	end
	return self.config.bypassAiming and state.aiming == true
end

-- One frame. targetX, targetZ: commanded planar velocity (studs/s).
-- state: { crouching, climbing, grounded, aiming }. Returns eased x, z, speedFraction.
function MovementInertia:update(dt: number, targetX: number, targetZ: number, state): (number, number, number)
	local cfg = self.config
	local targetSpeed = math.sqrt(targetX * targetX + targetZ * targetZ)
	if self:bypassed(state or {}) then
		self.x, self.z = targetX, targetZ
		self.speedFraction = 1
		return self.x, self.z, 1
	end
	local speed = math.sqrt(self.x * self.x + self.z * self.z)
	local time
	if targetSpeed < 1e-3 then
		time = cfg.stopTime
	elseif targetSpeed >= speed then
		time = cfg.accelTime
	else
		time = cfg.decelTime
	end
	if speed > 1e-3 and targetSpeed > 1e-3 then
		local cos = (self.x * targetX + self.z * targetZ) / (speed * targetSpeed)
		time *= 1 + cfg.turnPenalty * 0.5 * (1 - BMath.clamp(cos, -1, 1))
	end
	local a = BMath.alpha(BMath.rateForTime(time), dt)
	local nx = self.x + (targetX - self.x) * a
	local nz = self.z + (targetZ - self.z) * a
	-- No overshoot: at most the target speed when speeding up, never faster than
	-- before when slowing down.
	local limit = if targetSpeed >= speed then targetSpeed else speed
	local n = math.sqrt(nx * nx + nz * nz)
	if n > limit and n > 0 then
		nx, nz = nx * limit / n, nz * limit / n
		n = limit
	end
	if targetSpeed < 1e-3 and n < 1e-3 then
		nx, nz, n = 0, 0, 0
	end
	self.x, self.z = nx, nz
	self.speedFraction = if targetSpeed > 1e-3 then BMath.clamp(n / targetSpeed, 0, 1) else 1
	return nx, nz, self.speedFraction
end

return MovementInertia
