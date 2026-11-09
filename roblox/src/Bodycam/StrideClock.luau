-- Stride clock: a foot phase from planar speed, for games without footstep markers.
-- Pure Luau.
--
-- One full stride (phase 0 -> 1: left contact at 0, right at 0.5) every strideLength
-- studs at walk speed, lengthening linearly to sprintStrideLength at sprint speed.
-- Returns nil when not grounded or slower than minSpeed; the phase is kept so the
-- next step continues from it.

local BMath = require(script.Parent.Math)

local StrideClock = {}
StrideClock.__index = StrideClock

-- config: the Settings `stride` group.
function StrideClock.new(config)
	local self = setmetatable({}, StrideClock)
	self.config = config
	self.phase = 0
	return self
end

function StrideClock:reset(phase: number?)
	self.phase = phase or 0
end

-- Stride length (studs) at a speed.
function StrideClock.lengthAt(config, speed: number, walkSpeed: number, sprintSpeed: number): number
	local span = sprintSpeed - walkSpeed
	local t = if span > 1e-3 then BMath.clamp((speed - walkSpeed) / span, 0, 1) else 0
	return BMath.lerp(config.strideLength, config.sprintStrideLength, t)
end

-- One frame. Returns the foot phase in [0, 1), or nil.
function StrideClock:update(dt: number, speed: number, grounded: boolean, walkSpeed: number, sprintSpeed: number): number?
	local cfg = self.config
	if not cfg.enabled or not grounded or speed < cfg.minSpeed then
		return nil
	end
	local length = math.max(StrideClock.lengthAt(cfg, speed, walkSpeed, sprintSpeed), 1e-3)
	self.phase = (self.phase + speed * dt / length) % 1
	return self.phase
end

return StrideClock
