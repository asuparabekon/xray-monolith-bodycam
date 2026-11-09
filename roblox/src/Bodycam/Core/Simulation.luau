-- Port of src/xrGame/bodycam_simulation.h/.cpp: the Classic bodycam simulation.
-- Plain data in, plain data out. Field names keep the C++ names so the settings
-- registry (Settings.luau) and the original docs (docs/CLASSIC.md) still apply.
--
-- Every feature is one unit: a settings table with its own enable flag, a state
-- table and an update that reads only its own settings, its own state and the
-- shared frame signals. A feature therefore works with every other feature off.

local MathUtil = require(script.Parent.MathUtil)
local Vec3 = require(script.Parent.Vec3)

local clamp = MathUtil.clamp
local smoothFloat = MathUtil.smoothFloat
local smoothAngle = MathUtil.smoothAngle
local smoothVector = MathUtil.smoothVector
local angleDifference = MathUtil.angleDifference
local degToRad = MathUtil.degToRad
local blendFactor = MathUtil.blendFactor
local followRate = MathUtil.followRate
local lerp = MathUtil.lerp
local clampVector = MathUtil.clampVector
local EPSILON = MathUtil.EPSILON
local MAX_SMOOTHING_STEP = MathUtil.MAX_SMOOTHING_STEP
local PI = math.pi
local band = bit32.band

local Simulation = {}

Simulation.DEFAULT_STALKER2_MOVEMENT_RESPONSE = 5
-- The arm springs run at response * ARM_SPRING_HZ_PER_RESPONSE Hz, limited to 1..6 Hz.
Simulation.ARM_SPRING_HZ_PER_RESPONSE = 0.12
-- The arm follow response of the slowest spring, 1 Hz.
Simulation.ARM_FOLLOW_RESPONSE_MIN = 1 / 0.12

-- ESimMoveFlags
local MoveFlags = {
	Forward = 1,
	Back = 2,
	Left = 4,
	Right = 8,
	Crouch = 16,
	Sprint = 32,
	Fall = 64,
	Jump = 128,
	-- Not read by the simulation; Movement Inertia bypasses on them (mcClimb, mcLanding).
	Climb = 256,
	Landing = 512,
}
MoveFlags.AnyMove = MoveFlags.Forward + MoveFlags.Back + MoveFlags.Left + MoveFlags.Right
Simulation.MoveFlags = MoveFlags

local function has(flags: number, mask: number): boolean
	return band(flags, mask) ~= 0
end

---------------------------------------------------------------------------
-- Settings, state and output constructors (C++ struct defaults).
---------------------------------------------------------------------------

function Simulation.defaultSettings()
	return {
		input = { mouse_filter = 18, move_filter = 8 },
		sprint = {
			enable = true,
			strength = 1.5,
			smoothness = 1,
			enter_time = 0.32,
			exit_time = 0.48,
			camera_pitch = -0.25,
			camera_pos = 0.004,
		},
		lowering = {
			enable = true,
			disable_in_combat = true,
			weight = 1,
			pitch = 0,
			yaw = 0,
			roll = 0,
			x = 0,
			y = -0.08,
			z = 0.1,
			holster_offset = 0.015,
			slow_walk = 0.5,
			walk = 1,
			move = 1,
			fire_timeout = 0.03,
			aim_timeout = 0.005,
			speed = 0.05,
			return_speed = 0.4,
			combat_timeout = 2,
		},
		fire = {
			enable = true,
			hip = 5,
			ads = 1.35,
			ads_scale = 0.18,
			follow_speed = 9,
			damping = 0.78,
			decay = 8,
			pos_cap = 0.08,
			rot_cap = 8,
			kick_drop = 0.0015,
			kick_back = 0.0040,
			kick_pitch = 0.32,
			kick_roll = 0.10,
		},
		stalker2_arm = {
			enable = true,
			weight = 1,
			strength = 1.5,
			response = 20,
			arm_follow_response = Simulation.ARM_FOLLOW_RESPONSE_MIN,
			arm_follow_scale = 0,
			ads_scale = 0.2,
			mouse_strength = 1,
			mouse_sensitivity = 1.5,
			mouse_max_yaw = 0,
			mouse_max_pitch = 18,
			mouse_max_roll = 15.5,
			movement_strength = 1,
			movement_response = Simulation.DEFAULT_STALKER2_MOVEMENT_RESPONSE,
			slow_walk_scale = 0.5,
			mouse_pitch = 18,
			mouse_yaw = 0,
			mouse_roll = 28,
			wrist_scale = 1,
		},
	}
end

local function newSprintSwayState()
	return { amount = 0, settle = 0, phase = 0 }
end

local function newLoweringState()
	return {
		amount = 0,
		target = 0,
		holster = 0,
		fire_recovery = 0,
		ads_recovery = 0,
		combat_timer = 0,
		prev_ads = false,
		pos = Vec3.new(),
		rot = Vec3.new(),
	}
end

local function newFireImpulseState()
	return { impulse_pos = Vec3.new(), impulse_rot = Vec3.new(), pos = Vec3.new(), rot = Vec3.new() }
end

local function newStalker2ArmState()
	return {
		wrist_rot = Vec3.new(),
		wrist_rot_vel = Vec3.new(),
		arm_follow_rot = Vec3.new(),
		arm_follow_rot_vel = Vec3.new(),
		weight = 0,
		movement_weight = 0,
	}
end

function Simulation.newState()
	return {
		signals = {
			mouse_speed = Vec3.new(), -- rad/s; x yaw, y pitch.
			mouse_aim_yaw = 0,
			mouse_aim_pitch = 0,
			mouse_aim_initialized = false,
			move_intent = Vec3.new(), -- x right, z forward; scaled by gait.
		},
		sprint = newSprintSwayState(),
		lowering = newLoweringState(),
		fire = newFireImpulseState(),
		stalker2_arm = newStalker2ArmState(),
	}
end

-- Frame input (SimulationInput).
function Simulation.newInput()
	return {
		dt = 0,
		move_flags = 0,
		ads = false,
		ads_blend = 0, -- 0..1
		weapon_lowered = false,
		combat = false,
		accelerated = false,
		firearm_equipped = true,
		-- Speed over its target, 0..1. 1 while moving without movement inertia.
		actor_speed_fraction = 0,
		-- Another system (the footstep camera) plays the sprint camera motion.
		sprint_camera_owned_elsewhere = false,
		-- The camera rig's gun-first follow owns the camera lag: the mouse is read unfiltered.
		gun_first_follow = false,
		-- The aim as accumulated from mouse deltas; equals the camera's aim within rounding.
		mouse_aim_yaw = 0,
		mouse_aim_pitch = 0,
	}
end

function Simulation.newOutput()
	return {
		sprint_sway = { offset = Vec3.new(), pitch = 0, reach = 0 },
		viewmodel_active = false,
		viewmodel_pos = Vec3.new(),
		viewmodel_rot = Vec3.new(), -- degrees: yaw, pitch, roll
		lowering_pos = Vec3.new(),
		lowering_rot = Vec3.new(),
		lower_amount = 0,
		stalker2_arm_active = false,
		stalker2_wrist_rot = Vec3.new(), -- radians
		stalker2_arm_follow_rot = Vec3.new(), -- radians
		stalker2_movement_weight = 0,
		stalker2_movement_response = Simulation.DEFAULT_STALKER2_MOVEMENT_RESPONSE,
	}
end

local function resetOutput(output)
	local sway = output.sprint_sway
	Vec3.set(sway.offset, 0, 0, 0)
	sway.pitch = 0
	sway.reach = 0
	output.viewmodel_active = false
	Vec3.set(output.viewmodel_pos, 0, 0, 0)
	Vec3.set(output.viewmodel_rot, 0, 0, 0)
	Vec3.set(output.lowering_pos, 0, 0, 0)
	Vec3.set(output.lowering_rot, 0, 0, 0)
	output.lower_amount = 0
	output.stalker2_arm_active = false
	Vec3.set(output.stalker2_wrist_rot, 0, 0, 0)
	Vec3.set(output.stalker2_arm_follow_rot, 0, 0, 0)
	output.stalker2_movement_weight = 0
	output.stalker2_movement_response = Simulation.DEFAULT_STALKER2_MOVEMENT_RESPONSE
end

---------------------------------------------------------------------------
-- Shared helpers.
---------------------------------------------------------------------------

-- SmoothTime reaches about 95 % of the way in the given time: three time constants.
local TIME_CONSTANTS_TO_SETTLE = 3

local function smoothTime(current: number, target: number, seconds: number, dt: number): number
	return smoothFloat(current, target, TIME_CONSTANTS_TO_SETTLE / seconds, dt)
end

-- Critically damped spring, integrated in steps of at most 1/120 s.
local function updateArmPoseSpring(current, velocity, target, response: number, dt: number)
	local clampedDt = math.min(dt, MAX_SMOOTHING_STEP)
	local frequency = clamp(response * Simulation.ARM_SPRING_HZ_PER_RESPONSE, 1, 6)
	local omega = 2 * PI * frequency
	local spring = omega * omega
	local damper = 2 * omega
	-- The source computes clampedDt * 120 in single precision: at exactly 60 fps that is
	-- 2.0000001 in double but 2.0 in float, so the epsilon keeps the source's step count.
	local steps = math.max(1, math.ceil(clampedDt * 120 - 1e-6))
	local stepDt = clampedDt / steps

	for _ = 1, steps do
		velocity.x += ((target.x - current.x) * spring - velocity.x * damper) * stepDt
		velocity.y += ((target.y - current.y) * spring - velocity.y * damper) * stepDt
		velocity.z += ((target.z - current.z) * spring - velocity.z * damper) * stepDt
		current.x += velocity.x * stepDt
		current.y += velocity.y * stepDt
		current.z += velocity.z * stepDt
	end
end

---------------------------------------------------------------------------
-- Shared signals: smoothed mouse speed and movement intent.
---------------------------------------------------------------------------

local CROUCH_INTENT_SCALE = 0.45
local SPRINT_INTENT_SCALE = 1.35
-- Half a turn per 30 fps frame: a snap of the aim cannot read as a faster throw.
local MAX_MOUSE_SPEED = 30 * PI

local moveTarget = Vec3.new()

local function updateInputSignals(settings, signals, input)
	local safeDt = math.max(input.dt, 1 / 240)
	if not signals.mouse_aim_initialized then
		signals.mouse_aim_yaw = input.mouse_aim_yaw
		signals.mouse_aim_pitch = input.mouse_aim_pitch
		Vec3.set(signals.mouse_speed, 0, 0, 0)
		signals.mouse_aim_initialized = true
	else
		local previousYaw = signals.mouse_aim_yaw
		local previousPitch = signals.mouse_aim_pitch
		if input.gun_first_follow then
			signals.mouse_aim_yaw = input.mouse_aim_yaw
			signals.mouse_aim_pitch = input.mouse_aim_pitch
		else
			signals.mouse_aim_yaw = smoothAngle(previousYaw, input.mouse_aim_yaw, settings.mouse_filter, input.dt)
			signals.mouse_aim_pitch = smoothAngle(previousPitch, input.mouse_aim_pitch, settings.mouse_filter, input.dt)
		end
		Vec3.set(
			signals.mouse_speed,
			clamp(angleDifference(signals.mouse_aim_yaw, previousYaw) / safeDt, -MAX_MOUSE_SPEED, MAX_MOUSE_SPEED),
			clamp(angleDifference(signals.mouse_aim_pitch, previousPitch) / safeDt, -MAX_MOUSE_SPEED, MAX_MOUSE_SPEED),
			0
		)
	end

	local flags = input.move_flags
	Vec3.set(moveTarget, 0, 0, 0)
	if has(flags, MoveFlags.Right) then
		moveTarget.x += 1
	end
	if has(flags, MoveFlags.Left) then
		moveTarget.x -= 1
	end
	if has(flags, MoveFlags.Forward) then
		moveTarget.z += 1
	end
	if has(flags, MoveFlags.Back) then
		moveTarget.z -= 1
	end
	if Vec3.magnitude(moveTarget) > 1 then
		Vec3.normalizeSafe(moveTarget)
	end
	if has(flags, MoveFlags.Crouch) then
		Vec3.mul(moveTarget, CROUCH_INTENT_SCALE)
	end
	if has(flags, MoveFlags.Sprint) then
		Vec3.mul(moveTarget, SPRINT_INTENT_SCALE)
	end
	smoothVector(signals.move_intent, moveTarget, settings.move_filter, input.dt)
end

---------------------------------------------------------------------------
-- Sprint camera sway.
---------------------------------------------------------------------------

-- Smoothing 0..1 stretches the enter, exit and settle times from 0.55x to 1.8x.
local SWAY_TIME_SCALE_BASE = 0.55
local SWAY_TIME_SCALE_RANGE = 1.25
local SWAY_SETTLE_TIME = 0.35 -- Seconds, before the smoothing scale.
-- Leaving a sprint above this amount starts a settle of this share of it.
local SWAY_SETTLE_THRESHOLD = 0.15
local SWAY_SETTLE_SHARE = 0.45
-- Stride phase rate in rad/s: base plus a share of the sway amount.
local SWAY_PHASE_RATE = 6.2
local SWAY_PHASE_RATE_GAIN = 2.1
local SWAY_IDLE = 0.001

local function updateSprintSway(settings, state, input, sway)
	if not settings.enable or not input.firearm_equipped then
		state.amount, state.settle, state.phase = 0, 0, 0
		return
	end
	sway.reach = settings.camera_pos * math.max(1, settings.strength)

	local flags = input.move_flags
	local moving = has(flags, MoveFlags.AnyMove)
	local blocked = input.ads or has(flags, MoveFlags.Crouch + MoveFlags.Fall + MoveFlags.Jump)
	local sprinting = moving and not blocked and has(flags, MoveFlags.Sprint)
	local speedFraction = clamp(input.actor_speed_fraction, 0, 1)
	local target = if sprinting then speedFraction else 0
	local timeScale = SWAY_TIME_SCALE_BASE + settings.smoothness * SWAY_TIME_SCALE_RANGE
	local enterTime = settings.enter_time * timeScale
	local exitTime = settings.exit_time * timeScale

	local previousAmount = state.amount
	state.amount = smoothTime(state.amount, target, if target > state.amount then enterTime else exitTime, input.dt)
	if target <= EPSILON and previousAmount > SWAY_SETTLE_THRESHOLD then
		state.settle = math.max(state.settle, previousAmount * SWAY_SETTLE_SHARE)
	end
	state.settle = smoothTime(state.settle, 0, SWAY_SETTLE_TIME * timeScale, input.dt)

	if state.amount > SWAY_IDLE or state.settle > SWAY_IDLE then
		state.phase += input.dt * (SWAY_PHASE_RATE + SWAY_PHASE_RATE_GAIN * state.amount)
		while state.phase > 2 * PI do
			state.phase -= 2 * PI
		end
	else
		state.phase = 0
	end

	-- No sway while aiming or while another system plays the sprint motion.
	local silent = input.ads or input.sprint_camera_owned_elsewhere
	local amount = if silent then 0 else state.amount * settings.strength
	local settle = if silent then 0 else state.settle * settings.strength
	local wave = math.sin(state.phase)
	local step = math.sin(state.phase * 2)
	-- The camera pitches down, sways sideways with the stride, dips and bobs
	-- at twice the stride, and pulls back; the settle eases it up and forward.
	sway.pitch = degToRad(settings.camera_pitch) * (amount + 0.35 * settle)
	Vec3.set(
		sway.offset,
		settings.camera_pos * 0.25 * wave * amount,
		settings.camera_pos * (-0.30 * amount + 0.18 * step * amount + 0.20 * settle),
		settings.camera_pos * (-0.85 * amount + 0.35 * settle)
	)
end

---------------------------------------------------------------------------
-- Viewmodel lowering.
---------------------------------------------------------------------------

-- The enter and return speed settings are 0..1; squared, they scale this rate (1/s).
local LOWERING_MAX_RATE = 16
local LOWERING_MIN_RATE = 0.01

-- How far the weapon lowers for a movement direction: most when walking
-- forward, not at all when backing up.
local function loweringDirectionShare(flags: number): number
	local fwd = has(flags, MoveFlags.Forward)
	local back = has(flags, MoveFlags.Back)
	local left = has(flags, MoveFlags.Left)
	local right = has(flags, MoveFlags.Right)
	if fwd and back then
		fwd, back = false, false
	end
	if left and right then
		left, right = false, false
	end
	local strafe = left or right
	if fwd and strafe then
		return 0.75
	end
	if fwd then
		return 1
	end
	if back and strafe then
		return 0.25
	end
	if back then
		return 0
	end
	if strafe then
		return 0.5
	end
	return 0
end

-- A speed of 1 jumps to the target and a speed of 0 holds still.
local function approachLowering(current: number, target: number, speed: number, dt: number): number
	if speed >= 1 - EPSILON then
		return target
	end
	if speed <= EPSILON then
		return current
	end
	return smoothFloat(current, target, math.max(speed * speed * LOWERING_MAX_RATE, LOWERING_MIN_RATE), dt)
end

local function loweringGaitShare(settings, input): number
	local flags = input.move_flags
	local walking = has(flags, MoveFlags.AnyMove) and not has(flags, MoveFlags.Sprint + MoveFlags.Crouch)
	if not walking then
		return 0
	end
	return settings.move * (if input.accelerated then settings.walk else settings.slow_walk)
end

local function loweringActive(settings): boolean
	return settings.enable and settings.weight > EPSILON
end

local function resetLowering(state)
	state.amount, state.target, state.holster = 0, 0, 0
	state.fire_recovery, state.ads_recovery, state.combat_timer = 0, 0, 0
	state.prev_ads = false
	Vec3.set(state.pos, 0, 0, 0)
	Vec3.set(state.rot, 0, 0, 0)
end

local function updateLowering(settings, state, input)
	local leftAds = not input.ads and state.prev_ads
	if not loweringActive(settings) then
		resetLowering(state)
		state.prev_ads = input.ads
		return
	end
	state.prev_ads = input.ads

	if leftAds then
		state.ads_recovery = math.max(state.ads_recovery, settings.aim_timeout)
	end

	state.fire_recovery = math.max(0, state.fire_recovery - input.dt)
	state.ads_recovery = math.max(0, state.ads_recovery - input.dt)
	state.combat_timer = if input.combat
		then math.max(state.combat_timer, settings.combat_timeout)
		else math.max(0, state.combat_timer - input.dt)

	local gaitShare = loweringGaitShare(settings, input)
	local target = loweringDirectionShare(input.move_flags) * gaitShare

	if gaitShare <= EPSILON or input.ads or state.fire_recovery > 0 or state.ads_recovery > 0 then
		target = 0
	end
	if settings.disable_in_combat and state.combat_timer > 0 then
		target = 0
	end

	state.target = target
	state.amount = approachLowering(
		state.amount,
		target,
		if target < state.amount then settings.return_speed else settings.speed,
		input.dt
	)

	local holsterTarget = if input.weapon_lowered then 1 else 0
	state.holster = approachLowering(
		state.holster,
		holsterTarget,
		if holsterTarget < state.holster then settings.return_speed else settings.speed,
		input.dt
	)

	Vec3.set(state.rot, settings.yaw * state.amount, settings.pitch * state.amount, settings.roll * state.amount)
	Vec3.set(
		state.pos,
		settings.x * state.amount,
		settings.y * state.amount - settings.holster_offset * state.holster,
		settings.z * state.amount
	)
end

local function notifyLoweringOfShot(settings, state)
	if not settings.enable then
		return
	end
	state.fire_recovery = math.max(state.fire_recovery, settings.fire_timeout)
	state.combat_timer = math.max(state.combat_timer, settings.combat_timeout)
end

---------------------------------------------------------------------------
-- Fire impulse. A shot adds a kick target that decays; the viewmodel offset follows it.
---------------------------------------------------------------------------

local targetPos = Vec3.new()
local targetRot = Vec3.new()

-- Returns whether the kick is live this frame.
local function updateFireImpulse(settings, state, input): boolean
	if not settings.enable then
		Vec3.set(state.impulse_pos, 0, 0, 0)
		Vec3.set(state.impulse_rot, 0, 0, 0)
		Vec3.set(state.pos, 0, 0, 0)
		Vec3.set(state.rot, 0, 0, 0)
		return false
	end
	local active = Vec3.magnitude(state.impulse_pos) > EPSILON
		or Vec3.magnitude(state.impulse_rot) > EPSILON
		or Vec3.magnitude(state.pos) > EPSILON
		or Vec3.magnitude(state.rot) > EPSILON

	-- The caps apply when a shot lands; this catches a cap lowered mid-kick.
	clampVector(state.impulse_pos, settings.pos_cap)
	clampVector(state.impulse_rot, settings.rot_cap)
	local decay = blendFactor(settings.decay, input.dt)
	Vec3.mul(state.impulse_pos, 1 - decay)
	Vec3.mul(state.impulse_rot, 1 - decay)

	local adsScale = lerp(1, settings.ads_scale, input.ads_blend)
	Vec3.assign(targetPos, state.impulse_pos)
	Vec3.assign(targetRot, state.impulse_rot)
	Vec3.mul(targetPos, adsScale)
	Vec3.mul(targetRot, adsScale)
	local rate = followRate(settings.follow_speed, settings.damping)
	smoothVector(state.pos, targetPos, rate, input.dt)
	smoothVector(state.rot, targetRot, rate, input.dt)
	return active
end

local function notifyFireImpulseOfShot(settings, state, signals, power: number, ads: boolean)
	if not settings.enable then
		return
	end
	local p = power * (if ads then settings.ads else settings.hip)
	if p <= EPSILON then
		return
	end
	local side = if signals.mouse_speed.x >= 0 then -1 else 1
	Vec3.addXYZ(state.impulse_pos, 0, -settings.kick_drop * p, -settings.kick_back * p)
	Vec3.addXYZ(state.impulse_rot, 0, settings.kick_pitch * p, settings.kick_roll * side * p)
	clampVector(state.impulse_pos, settings.pos_cap)
	clampVector(state.impulse_rot, settings.rot_cap)
end

---------------------------------------------------------------------------
-- STALKER 2 style arm IK.
---------------------------------------------------------------------------

-- Mouse speeds (deg/s) that count as a full throw before the soft limit.
local YAW_THROW_SPEED = 420
local PITCH_THROW_SPEED = 320
local MOUSE_YAW_SHARE = 0.35 -- Yaw the controller takes relative to roll.
-- Fade times in seconds for the controller and for its walk motion.
local ARM_FADE_IN = 0.16
local ARM_FADE_OUT = 0.30
local ARM_WALK_FADE_IN = 0.12
local ARM_WALK_FADE_OUT = 0.24
local ARM_IDLE = 0.001

-- Passes small values through and approaches +-1 smoothly for large ones.
local function softLimit(value: number): number
	return value / (1 + math.abs(value) ^ 8) ^ (1 / 8)
end

local function mouseThrow(angularSpeed: number, fullScaleSpeed: number, sensitivity: number): number
	return softLimit(angularSpeed / fullScaleSpeed * sensitivity)
end

local function limitArmRotation(rotation, settings)
	Vec3.set(
		rotation,
		clamp(rotation.x, -settings.mouse_max_yaw, settings.mouse_max_yaw),
		clamp(rotation.y, -settings.mouse_max_pitch, settings.mouse_max_pitch),
		clamp(rotation.z, -settings.mouse_max_roll, settings.mouse_max_roll)
	)
end

local wristTarget = Vec3.new()
local armFollowTarget = Vec3.new()
local DEG = degToRad(1)

-- The weapon and both hands turn as one controller: mouse movement rolls and
-- yaws it, vertical movement lets the arms follow, and walking adds authored
-- motion where the weapon's own animation has little.
local function updateStalker2Arm(settings, state, input, signals, output)
	local strength = settings.strength * settings.weight
	local armAllowed = settings.enable and input.firearm_equipped and strength > EPSILON

	local targetWeight = if armAllowed then 1 else 0
	state.weight = smoothTime(
		state.weight,
		targetWeight,
		if targetWeight > state.weight then ARM_FADE_IN else ARM_FADE_OUT,
		input.dt
	)

	local adsScale = lerp(1, settings.ads_scale, input.ads_blend)
	local flags = input.move_flags
	local movementAllowed = armAllowed
		and has(flags, MoveFlags.AnyMove)
		and not has(flags, MoveFlags.Sprint + MoveFlags.Crouch + MoveFlags.Fall + MoveFlags.Jump)
	-- The smoothed intent can still carry sprint scale just after a sprint.
	local moveAmount = if movementAllowed then math.min(Vec3.magnitude(signals.move_intent), 1) else 0
	local gaitScale = if input.accelerated then 1 else settings.slow_walk_scale
	local movementTarget = moveAmount * clamp(input.actor_speed_fraction, 0, 1) * gaitScale
	state.movement_weight = smoothTime(
		state.movement_weight,
		movementTarget,
		if movementTarget > state.movement_weight then ARM_WALK_FADE_IN else ARM_WALK_FADE_OUT,
		input.dt
	)
	local yawThrow = if armAllowed
		then mouseThrow(signals.mouse_speed.x, degToRad(YAW_THROW_SPEED), settings.mouse_sensitivity)
		else 0
	local pitchThrow = if armAllowed
		then mouseThrow(signals.mouse_speed.y, degToRad(PITCH_THROW_SPEED), settings.mouse_sensitivity)
		else 0

	local finalWeight = state.weight * strength * adsScale * settings.wrist_scale
	local mouseWeight = settings.mouse_strength * finalWeight
	Vec3.set(
		wristTarget,
		yawThrow * settings.mouse_yaw * MOUSE_YAW_SHARE * mouseWeight,
		0,
		-yawThrow * settings.mouse_roll * mouseWeight
	)
	limitArmRotation(wristTarget, settings)
	updateArmPoseSpring(state.wrist_rot, state.wrist_rot_vel, wristTarget, settings.response, input.dt)

	Vec3.assign(armFollowTarget, wristTarget)
	Vec3.mul(armFollowTarget, settings.arm_follow_scale)
	Vec3.addXYZ(armFollowTarget, 0, pitchThrow * settings.mouse_pitch * mouseWeight, 0)
	limitArmRotation(armFollowTarget, settings)
	updateArmPoseSpring(
		state.arm_follow_rot,
		state.arm_follow_rot_vel,
		armFollowTarget,
		settings.arm_follow_response,
		input.dt
	)

	output.stalker2_arm_active = state.weight > ARM_IDLE
		or Vec3.magnitude(state.wrist_rot) > ARM_IDLE
		or Vec3.magnitude(state.arm_follow_rot) > ARM_IDLE
	Vec3.assign(output.stalker2_wrist_rot, state.wrist_rot)
	Vec3.mul(output.stalker2_wrist_rot, DEG)
	Vec3.assign(output.stalker2_arm_follow_rot, state.arm_follow_rot)
	Vec3.mul(output.stalker2_arm_follow_rot, DEG)
	output.stalker2_movement_weight = state.movement_weight * state.weight * settings.movement_strength * adsScale
	output.stalker2_movement_response = settings.movement_response
end

---------------------------------------------------------------------------
-- Public API.
---------------------------------------------------------------------------

function Simulation.resolveAdsState(weaponZoomed: boolean, weaponBlend: number)
	return { active = weaponZoomed, blend = clamp(weaponBlend, 0, 1) }
end

function Simulation.resetState(state, yaw: number, pitch: number, adsBlend: number)
	local fresh = Simulation.newState()
	for key, value in fresh do
		state[key] = value
	end
	state.signals.mouse_aim_yaw = yaw
	state.signals.mouse_aim_pitch = pitch
	state.signals.mouse_aim_initialized = true
	state.lowering.prev_ads = adsBlend > 0
end

function Simulation.rebaseLook(state, yawDelta: number, pitchDelta: number)
	local signals = state.signals
	if signals.mouse_aim_initialized then
		signals.mouse_aim_yaw = MathUtil.normalizeAngle(signals.mouse_aim_yaw + yawDelta)
		signals.mouse_aim_pitch = MathUtil.normalizeAngle(signals.mouse_aim_pitch + pitchDelta)
	end
end

-- Runs one frame. `output` is reused between frames (Simulation.newOutput()).
function Simulation.update(settings, state, input, output)
	resetOutput(output)

	updateInputSignals(settings.input, state.signals, input)
	updateSprintSway(settings.sprint, state.sprint, input, output.sprint_sway)
	local kickActive = updateFireImpulse(settings.fire, state.fire, input)
	updateLowering(settings.lowering, state.lowering, input)
	updateStalker2Arm(settings.stalker2_arm, state.stalker2_arm, input, state.signals, output)

	-- The viewmodel offset is the sum of the two features that move it.
	output.lower_amount = state.lowering.amount
	output.viewmodel_active = loweringActive(settings.lowering) or kickActive
	if output.viewmodel_active then
		Vec3.assign(output.lowering_pos, state.lowering.pos)
		Vec3.assign(output.lowering_rot, state.lowering.rot)
		Vec3.mul(output.lowering_pos, settings.lowering.weight)
		Vec3.mul(output.lowering_rot, settings.lowering.weight)
		Vec3.add(output.viewmodel_pos, state.fire.pos)
		Vec3.add(output.viewmodel_pos, output.lowering_pos)
		Vec3.add(output.viewmodel_rot, state.fire.rot)
		Vec3.add(output.viewmodel_rot, output.lowering_rot)
	end
	return output
end

-- A shot: lowering starts its recovery timers and the fire impulse kicks.
function Simulation.notifyShot(settings, state, power: number, ads: boolean)
	notifyLoweringOfShot(settings.lowering, state.lowering)
	notifyFireImpulseOfShot(settings.fire, state.fire, state.signals, power, ads)
end

---------------------------------------------------------------------------
-- Authored walk motion the STALKER 2 controller adds where a weapon's own
-- animation has little.
---------------------------------------------------------------------------

function Simulation.calculateAuthoredMotionGains(metrics)
	-- How far a weapon's own walk animation falls short of the motion the
	-- controller would otherwise add: 0 when it already has that much.
	local function deficit(authored: number, wanted: number): number
		return clamp(1 - authored / wanted, 0, 1)
	end
	local leadRotationDeficit = deficit(metrics.lead_rotation or 0, degToRad(2))
	local leadTranslationDeficit = deficit(metrics.lead_translation or 0, 0.012)
	local gains = {}
	gains.wrist = deficit(metrics.wrist_rotation or 0, degToRad(4))
	gains.controller = math.max(math.min(leadRotationDeficit, leadTranslationDeficit), gains.wrist * 0.75)
	gains.arm = 0.5 * (deficit(metrics.forearm_rotation or 0, degToRad(3)) + deficit(metrics.upperarm_rotation or 0, degToRad(2)))
	return gains
end

function Simulation.calculateAuthoredWalkMotion(phase: number, weight: number, gains)
	-- One stride per unit of phase: a sideways sway with a third harmonic and
	-- a step bounce at twice the stride.
	local cycle = (phase - math.floor(phase)) * 2 * PI
	local lateral = math.sin(cycle) + 0.16 * math.sin(3 * cycle + 0.35)
	local step = math.sin(2 * cycle - 0.35) + 0.12 * math.sin(4 * cycle + 0.20)
	local g = gains.controller * weight
	return {
		-- radians
		rotation = Vec3.new(degToRad(1.25 * step * g), degToRad(0.55 * lateral * g), degToRad(-2.40 * lateral * g)),
		-- metres
		translation = Vec3.new(0.0040 * lateral * g, 0.0025 * step * g, -0.0012 * step * g),
	}
end

-- Share of the authored walk rotation that the arms follow.
function Simulation.calculateAuthoredArmFollow(armGain: number): number
	return 0.30 - 0.18 * armGain
end

return Simulation
