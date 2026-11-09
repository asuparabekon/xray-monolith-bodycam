-- Port of src/xrGame/bodycam_movement_response.h/.cpp.
-- Movement inertia: the commanded planar velocity eases toward what the
-- movement keys ask for, which gives starts, stops and turns a duration.
--
-- The engine filters the control acceleration (top speed = acceleration / 10).
-- Here the input is the target planar velocity in metres per second; it is
-- scaled by the same factor internally so every threshold matches the source.

local Simulation = require(script.Parent.Simulation)

local MoveFlags = Simulation.MoveFlags
local band = bit32.band

-- The character controller's top speed is the control acceleration over this.
local ACCEL_PER_SPEED = 10
local EPSILON = 1.0e-5
-- Seconds. The shortest time to target, and the longest frame that is integrated.
local MIN_RESPONSE_TIME = 0.01
local MAX_RESPONSE_STEP = 0.05
local BYPASS_FLAGS = MoveFlags.Crouch + MoveFlags.Climb + MoveFlags.Jump + MoveFlags.Fall + MoveFlags.Landing

local MovementInertia = {}

function MovementInertia.defaultSettings()
	return {
		enable = true,
		disable_ads = true,
		accel_time = 0.42, -- Seconds to reach the target speed.
		decel_time = 0.38,
		turn_response = 0.55, -- Extra time for a reversal, as a share of the time.
		stop_response = 0.45, -- Scale on the time of a full stop.
		sprint_speed_scale = 1, -- Divides the acceleration time while sprinting.
	}
end

function MovementInertia.newState()
	return { initialized = false, x = 0, y = 0, z = 0 }
end

-- Share of the remaining distance covered this frame.
local function response(seconds: number, dt: number): number
	return 1 - math.exp(-math.min(dt, MAX_RESPONSE_STEP) / math.max(seconds, MIN_RESPONSE_TIME))
end

local function xzMagnitude(x: number, z: number): number
	return math.sqrt(x * x + z * z)
end

-- 0 for the same planar direction, 1 for a reversal.
local function directionChange(fx: number, fz: number, tx: number, tz: number): number
	local fromMag = xzMagnitude(fx, fz)
	local toMag = xzMagnitude(tx, tz)
	if fromMag <= EPSILON or toMag <= EPSILON then
		return 0
	end
	local dot = math.clamp((fx * tx + fz * tz) / (fromMag * toMag), -1, 1)
	return (1 - dot) * 0.5
end

local function shouldBypass(settings, input): boolean
	if not settings.enable then
		return true
	end
	if settings.disable_ads and input.ads then
		return true
	end
	return band(input.move_flags or 0, BYPASS_FLAGS) ~= 0
end

-- input = { dt, move_flags, ads, target_velocity = {x, y, z} (m/s) }
-- Returns { velocity = {x, y, z}, target_speed, filtered_speed, speed_fraction } (m/s).
function MovementInertia.update(settings, state, input)
	local target = input.target_velocity
	local tx, ty, tz = target.x * ACCEL_PER_SPEED, target.y * ACCEL_PER_SPEED, target.z * ACCEL_PER_SPEED
	local targetSpeed = xzMagnitude(tx, tz)
	local output = { target_speed = targetSpeed / ACCEL_PER_SPEED }

	if shouldBypass(settings, input) then
		state.initialized = false
		state.x, state.y, state.z = 0, 0, 0
		output.velocity = { x = target.x, y = target.y, z = target.z }
		output.filtered_speed = output.target_speed
		output.speed_fraction = if output.target_speed > EPSILON then 1 else 0
		return output
	end

	if not state.initialized then
		state.initialized = true
		state.x, state.y, state.z = tx, ty, tz
	end

	local currentSpeed = xzMagnitude(state.x, state.z)
	local stopping = targetSpeed <= EPSILON
	local slowingDown = targetSpeed + EPSILON < currentSpeed
	local sprinting = band(input.move_flags or 0, MoveFlags.Sprint) ~= 0
	local turn = directionChange(state.x, state.z, tx, tz)

	local responseTime = if stopping or slowingDown
		then settings.decel_time
		else settings.accel_time / (if sprinting then settings.sprint_speed_scale else 1)
	responseTime *= 1 + settings.turn_response * turn
	if stopping then
		responseTime *= settings.stop_response
	end

	local blend = response(responseTime, input.dt)
	state.x += (tx - state.x) * blend
	state.y = ty
	state.z += (tz - state.z) * blend

	-- The speed never exceeds what the keys ask for: slowing to a lower speed
	-- is immediate, and only a full stop eases out.
	local newSpeed = xzMagnitude(state.x, state.z)
	if not stopping and newSpeed > targetSpeed then
		local scale = targetSpeed / newSpeed
		state.x *= scale
		state.z *= scale
	elseif stopping and newSpeed < EPSILON then
		state.x = 0
		state.z = 0
	end

	output.velocity = { x = state.x / ACCEL_PER_SPEED, y = state.y / ACCEL_PER_SPEED, z = state.z / ACCEL_PER_SPEED }
	output.filtered_speed = xzMagnitude(state.x, state.z) / ACCEL_PER_SPEED
	output.speed_fraction = if output.target_speed > EPSILON then output.filtered_speed / output.target_speed else 0
	return output
end

return MovementInertia
