#if !defined(BODYCAM_STANDALONE)
#include "stdafx.h"
#endif
#include "bodycam_simulation.h"
#include "bodycam_smoothing.h"
#include <algorithm>
#include <cmath>

// Settings arrive range-checked from the registry in bodycam_settings.cpp, so
// nothing here clamps them again.

namespace Bodycam
{
namespace
{
// SmoothTime reaches about 95 % of the way in the given time: three time constants.
constexpr float kTimeConstantsToSettle = 3.f;

float SmoothTime(float current, float target, float seconds, float dt)
{
	return SmoothFloat(current, target, kTimeConstantsToSettle / seconds, dt);
}

// Critically damped spring, integrated in steps of at most 1/120 s.
void UpdateArmPoseSpring(SVec3& current, SVec3& velocity, const SVec3& target, float response, float dt)
{
	const float clamped_dt = std::min(dt, kMaxSmoothingStep);
	const float frequency = std::clamp(response * kArmSpringHzPerResponse, 1.0f, 6.0f);
	const float omega = 2.f * kPi * frequency;
	const float spring = omega * omega;
	const float damper = 2.f * omega;
	const int steps = std::max(1, static_cast<int>(std::ceil(clamped_dt * 120.f)));
	const float step_dt = clamped_dt / static_cast<float>(steps);

	for (int step = 0; step < steps; ++step)
	{
		velocity.x += ((target.x - current.x) * spring - velocity.x * damper) * step_dt;
		velocity.y += ((target.y - current.y) * spring - velocity.y * damper) * step_dt;
		velocity.z += ((target.z - current.z) * spring - velocity.z * damper) * step_dt;
		current.x += velocity.x * step_dt;
		current.y += velocity.y * step_dt;
		current.z += velocity.z * step_dt;
	}
}

// Shared signals: smoothed mouse speed and movement intent.
constexpr float kCrouchIntentScale = 0.45f;
constexpr float kSprintIntentScale = 1.35f;

// Half a turn per 30 fps frame: a snap of the aim cannot read as a faster throw.
constexpr float kMaxMouseSpeed = 30.f * kPi;

void UpdateInputSignals(const InputSignalSettings& settings, InputSignals& signals, const SimulationInput& input)
{
	const float safe_dt = std::max(input.dt, 1.f / 240.f);
	if (!signals.mouse_aim_initialized)
	{
		signals.mouse_aim_yaw = input.mouse_aim_yaw;
		signals.mouse_aim_pitch = input.mouse_aim_pitch;
		signals.mouse_speed.Set(0.f, 0.f, 0.f);
		signals.mouse_aim_initialized = true;
	}
	else
	{
		const float previous_yaw = signals.mouse_aim_yaw;
		const float previous_pitch = signals.mouse_aim_pitch;
		if (input.gun_first_follow)
		{
			signals.mouse_aim_yaw = input.mouse_aim_yaw;
			signals.mouse_aim_pitch = input.mouse_aim_pitch;
		}
		else
		{
			signals.mouse_aim_yaw = SmoothAngle(previous_yaw, input.mouse_aim_yaw, settings.mouse_filter, input.dt);
			signals.mouse_aim_pitch = SmoothAngle(previous_pitch, input.mouse_aim_pitch, settings.mouse_filter, input.dt);
		}
		signals.mouse_speed.Set(std::clamp(AngleDifference(signals.mouse_aim_yaw, previous_yaw) / safe_dt, -kMaxMouseSpeed, kMaxMouseSpeed),
			std::clamp(AngleDifference(signals.mouse_aim_pitch, previous_pitch) / safe_dt, -kMaxMouseSpeed, kMaxMouseSpeed), 0.f);
	}

	SVec3 move_target;
	if (input.move_flags & smfRight)
		move_target.x += 1.f;
	if (input.move_flags & smfLeft)
		move_target.x -= 1.f;
	if (input.move_flags & smfForward)
		move_target.z += 1.f;
	if (input.move_flags & smfBack)
		move_target.z -= 1.f;
	if (move_target.Magnitude() > 1.f)
		move_target.NormalizeSafe();
	if (input.move_flags & smfCrouch)
		move_target.Mul(kCrouchIntentScale);
	if (input.move_flags & smfSprint)
		move_target.Mul(kSprintIntentScale);
	SmoothVector(signals.move_intent, move_target, settings.move_filter, input.dt);
}

// Sprint camera sway.
// Smoothing 0..1 stretches the enter, exit and settle times from 0.55x to 1.8x.
constexpr float kSwayTimeScaleBase = 0.55f;
constexpr float kSwayTimeScaleRange = 1.25f;
constexpr float kSwaySettleTime = 0.35f; // Seconds, before the smoothing scale.
// Leaving a sprint above this amount starts a settle of this share of it.
constexpr float kSwaySettleThreshold = 0.15f;
constexpr float kSwaySettleShare = 0.45f;
// Stride phase rate in rad/s: base plus a share of the sway amount.
constexpr float kSwayPhaseRate = 6.2f;
constexpr float kSwayPhaseRateGain = 2.1f;
constexpr float kSwayIdle = 0.001f;

SprintSway UpdateSprintSway(const SprintSwaySettings& settings, SprintSwayState& state, const SimulationInput& input)
{
	SprintSway sway;
	if (!settings.enable || !input.firearm_equipped)
	{
		state = SprintSwayState();
		return sway;
	}
	sway.reach = settings.camera_pos * std::max(1.f, settings.strength);

	const bool moving = !!(input.move_flags & smfAnyMove);
	const bool blocked = input.ads || !!(input.move_flags & (smfCrouch | smfFall | smfJump));
	const bool sprinting = moving && !blocked && !!(input.move_flags & smfSprint);
	const float speed_fraction = std::clamp(input.actor_speed_fraction, 0.f, 1.f);
	const float target = sprinting ? speed_fraction : 0.f;
	const float time_scale = kSwayTimeScaleBase + settings.smoothness * kSwayTimeScaleRange;
	const float enter_time = settings.enter_time * time_scale;
	const float exit_time = settings.exit_time * time_scale;

	const float previous_amount = state.amount;
	state.amount = SmoothTime(state.amount, target, target > state.amount ? enter_time : exit_time, input.dt);
	if (target <= kEpsilon && previous_amount > kSwaySettleThreshold)
		state.settle = std::max(state.settle, previous_amount * kSwaySettleShare);
	state.settle = SmoothTime(state.settle, 0.f, kSwaySettleTime * time_scale, input.dt);

	if (state.amount > kSwayIdle || state.settle > kSwayIdle)
	{
		state.phase += input.dt * (kSwayPhaseRate + kSwayPhaseRateGain * state.amount);
		while (state.phase > 2.f * kPi)
			state.phase -= 2.f * kPi;
	}
	else
	{
		state.phase = 0.f;
	}

	// No sway while aiming or while another system plays the sprint motion.
	const bool silent = input.ads || input.sprint_camera_owned_elsewhere;
	const float amount = silent ? 0.f : state.amount * settings.strength;
	const float settle = silent ? 0.f : state.settle * settings.strength;
	const float wave = std::sin(state.phase);
	const float step = std::sin(state.phase * 2.f);
	// The camera pitches down, sways sideways with the stride, dips and bobs
	// at twice the stride, and pulls back; the settle eases it up and forward.
	sway.pitch = DegToRad(settings.camera_pitch) * (amount + 0.35f * settle);
	sway.offset.Set(settings.camera_pos * 0.25f * wave * amount,
		settings.camera_pos * (-0.30f * amount + 0.18f * step * amount + 0.20f * settle),
		settings.camera_pos * (-0.85f * amount + 0.35f * settle));
	return sway;
}

// Viewmodel lowering.
// The enter and return speed settings are 0..1; squared, they scale this rate (1/s).
constexpr float kLoweringMaxRate = 16.f;
constexpr float kLoweringMinRate = 0.01f;

// How far the weapon lowers for a movement direction: most when walking
// forward, not at all when backing up.
float LoweringDirectionShare(std::uint32_t move_flags)
{
	bool fwd = !!(move_flags & smfForward);
	bool back = !!(move_flags & smfBack);
	bool left = !!(move_flags & smfLeft);
	bool right = !!(move_flags & smfRight);
	if (fwd && back)
		fwd = back = false;
	if (left && right)
		left = right = false;

	const bool strafe = left || right;
	if (fwd && strafe)
		return 0.75f;
	if (fwd)
		return 1.f;
	if (back && strafe)
		return 0.25f;
	if (back)
		return 0.f;
	if (strafe)
		return 0.5f;
	return 0.f;
}

// A speed of 1 jumps to the target and a speed of 0 holds still.
float ApproachLowering(float current, float target, float speed, float dt)
{
	if (speed >= 1.f - kEpsilon)
		return target;
	if (speed <= kEpsilon)
		return current;
	return SmoothFloat(current, target, std::max(speed * speed * kLoweringMaxRate, kLoweringMinRate), dt);
}

float LoweringGaitShare(const LoweringSettings& settings, const SimulationInput& input)
{
	const bool walking = !!(input.move_flags & smfAnyMove) && !(input.move_flags & (smfSprint | smfCrouch));
	if (!walking)
		return 0.f;
	return settings.move * (input.accelerated ? settings.walk : settings.slow_walk);
}

bool LoweringActive(const LoweringSettings& settings)
{
	return settings.enable && settings.weight > kEpsilon;
}

void UpdateLowering(const LoweringSettings& settings, LoweringState& state, const SimulationInput& input)
{
	const bool left_ads = !input.ads && state.prev_ads;
	if (!LoweringActive(settings))
	{
		state = LoweringState();
		state.prev_ads = input.ads;
		return;
	}
	state.prev_ads = input.ads;

	if (left_ads)
		state.ads_recovery = std::max(state.ads_recovery, settings.aim_timeout);

	state.fire_recovery = std::max(0.f, state.fire_recovery - input.dt);
	state.ads_recovery = std::max(0.f, state.ads_recovery - input.dt);
	state.combat_timer =
		input.combat ? std::max(state.combat_timer, settings.combat_timeout) : std::max(0.f, state.combat_timer - input.dt);

	const float gait_share = LoweringGaitShare(settings, input);
	float target = LoweringDirectionShare(input.move_flags) * gait_share;

	if (gait_share <= kEpsilon || input.ads || state.fire_recovery > 0.f || state.ads_recovery > 0.f)
		target = 0.f;
	if (settings.disable_in_combat && state.combat_timer > 0.f)
		target = 0.f;

	state.target = target;
	state.amount = ApproachLowering(state.amount, target, target < state.amount ? settings.return_speed : settings.speed, input.dt);

	const float holster_target = input.weapon_lowered ? 1.f : 0.f;
	state.holster =
		ApproachLowering(state.holster, holster_target, holster_target < state.holster ? settings.return_speed : settings.speed, input.dt);

	state.rot.Set(settings.yaw * state.amount, settings.pitch * state.amount, settings.roll * state.amount);
	state.pos.Set(
		settings.x * state.amount, settings.y * state.amount - settings.holster_offset * state.holster, settings.z * state.amount);
}

void NotifyLoweringOfShot(const LoweringSettings& settings, LoweringState& state)
{
	if (!settings.enable)
		return;
	state.fire_recovery = std::max(state.fire_recovery, settings.fire_timeout);
	state.combat_timer = std::max(state.combat_timer, settings.combat_timeout);
}

// Fire impulse.
// A shot adds a kick target that decays; the viewmodel offset follows it.
// Returns whether the kick is live this frame.
bool UpdateFireImpulse(const FireImpulseSettings& settings, FireImpulseState& state, const SimulationInput& input)
{
	if (!settings.enable)
	{
		state = FireImpulseState();
		return false;
	}
	const bool active = state.impulse_pos.Magnitude() > kEpsilon || state.impulse_rot.Magnitude() > kEpsilon ||
		state.pos.Magnitude() > kEpsilon || state.rot.Magnitude() > kEpsilon;

	// The caps apply when a shot lands; this catches a cap lowered mid-kick.
	ClampVector(state.impulse_pos, settings.pos_cap);
	ClampVector(state.impulse_rot, settings.rot_cap);
	const float decay = BlendFactor(settings.decay, input.dt);
	state.impulse_pos.Mul(1.f - decay);
	state.impulse_rot.Mul(1.f - decay);

	const float ads_scale = Lerp(1.f, settings.ads_scale, input.ads_blend);
	SVec3 target_pos = state.impulse_pos;
	SVec3 target_rot = state.impulse_rot;
	target_pos.Mul(ads_scale);
	target_rot.Mul(ads_scale);
	const float rate = FollowRate(settings.follow_speed, settings.damping);
	SmoothVector(state.pos, target_pos, rate, input.dt);
	SmoothVector(state.rot, target_rot, rate, input.dt);
	return active;
}

void NotifyFireImpulseOfShot(
	const FireImpulseSettings& settings, FireImpulseState& state, const InputSignals& signals, float power, bool ads)
{
	if (!settings.enable)
		return;

	const float p = power * (ads ? settings.ads : settings.hip);
	if (p <= kEpsilon)
		return;

	const float side = signals.mouse_speed.x >= 0.f ? -1.f : 1.f;
	state.impulse_pos.Add(0.f, -settings.kick_drop * p, -settings.kick_back * p);
	state.impulse_rot.Add(0.f, settings.kick_pitch * p, settings.kick_roll * side * p);
	ClampVector(state.impulse_pos, settings.pos_cap);
	ClampVector(state.impulse_rot, settings.rot_cap);
}

// STALKER 2 style arm IK.
// Mouse speeds (deg/s) that count as a full throw before the soft limit.
constexpr float kYawThrowSpeed = 420.f;
constexpr float kPitchThrowSpeed = 320.f;
constexpr float kMouseYawShare = 0.35f; // Yaw the controller takes relative to roll.
// Fade times in seconds for the controller and for its walk motion.
constexpr float kArmFadeIn = 0.16f;
constexpr float kArmFadeOut = 0.30f;
constexpr float kArmWalkFadeIn = 0.12f;
constexpr float kArmWalkFadeOut = 0.24f;
constexpr float kArmIdle = 0.001f;

// Passes small values through and approaches +-1 smoothly for large ones.
float SoftLimit(float value)
{
	return value / std::pow(1.f + std::pow(std::fabs(value), 8.f), 1.f / 8.f);
}

float MouseThrow(float angular_speed, float full_scale_speed, float sensitivity)
{
	return SoftLimit(angular_speed / full_scale_speed * sensitivity);
}

SVec3 LimitArmRotation(const SVec3& rotation, const Stalker2ArmSettings& settings)
{
	SVec3 result;
	result.Set(std::clamp(rotation.x, -settings.mouse_max_yaw, settings.mouse_max_yaw),
		std::clamp(rotation.y, -settings.mouse_max_pitch, settings.mouse_max_pitch),
		std::clamp(rotation.z, -settings.mouse_max_roll, settings.mouse_max_roll));
	return result;
}

// The weapon and both hands turn as one controller: mouse movement rolls and
// yaws it, vertical movement lets the arms follow, and walking adds authored
// motion where the weapon's own animation has little.
void UpdateStalker2Arm(const Stalker2ArmSettings& settings, Stalker2ArmState& state, const SimulationInput& input,
	const InputSignals& signals, SimulationOutput& output)
{
	const float strength = settings.strength * settings.weight;
	const bool arm_allowed = settings.enable && input.firearm_equipped && strength > kEpsilon;

	const float target_weight = arm_allowed ? 1.f : 0.f;
	state.weight = SmoothTime(state.weight, target_weight, target_weight > state.weight ? kArmFadeIn : kArmFadeOut, input.dt);

	const float ads_scale = Lerp(1.f, settings.ads_scale, input.ads_blend);
	const bool movement_allowed =
		arm_allowed && !!(input.move_flags & smfAnyMove) && !(input.move_flags & (smfSprint | smfCrouch | smfFall | smfJump));
	// The smoothed intent can still carry sprint scale just after a sprint.
	const float move_amount = movement_allowed ? std::min(signals.move_intent.Magnitude(), 1.f) : 0.f;
	const float gait_scale = input.accelerated ? 1.f : settings.slow_walk_scale;
	const float movement_target = move_amount * std::clamp(input.actor_speed_fraction, 0.f, 1.f) * gait_scale;
	state.movement_weight = SmoothTime(
		state.movement_weight, movement_target, movement_target > state.movement_weight ? kArmWalkFadeIn : kArmWalkFadeOut, input.dt);
	const float yaw_throw = arm_allowed ? MouseThrow(signals.mouse_speed.x, DegToRad(kYawThrowSpeed), settings.mouse_sensitivity) : 0.f;
	const float pitch_throw = arm_allowed ? MouseThrow(signals.mouse_speed.y, DegToRad(kPitchThrowSpeed), settings.mouse_sensitivity) : 0.f;

	const float final_weight = state.weight * strength * ads_scale * settings.wrist_scale;
	const float mouse_weight = settings.mouse_strength * final_weight;
	SVec3 wrist_rot_target;
	wrist_rot_target.Set(
		yaw_throw * settings.mouse_yaw * kMouseYawShare * mouse_weight, 0.f, -yaw_throw * settings.mouse_roll * mouse_weight);
	wrist_rot_target = LimitArmRotation(wrist_rot_target, settings);
	UpdateArmPoseSpring(state.wrist_rot, state.wrist_rot_vel, wrist_rot_target, settings.response, input.dt);

	SVec3 arm_follow_target = wrist_rot_target;
	arm_follow_target.Mul(settings.arm_follow_scale);
	arm_follow_target.Add(0.f, pitch_throw * settings.mouse_pitch * mouse_weight, 0.f);
	arm_follow_target = LimitArmRotation(arm_follow_target, settings);
	UpdateArmPoseSpring(state.arm_follow_rot, state.arm_follow_rot_vel, arm_follow_target, settings.arm_follow_response, input.dt);

	output.stalker2_arm_active =
		state.weight > kArmIdle || state.wrist_rot.Magnitude() > kArmIdle || state.arm_follow_rot.Magnitude() > kArmIdle;
	output.stalker2_wrist_rot = state.wrist_rot;
	output.stalker2_wrist_rot.Mul(DegToRad(1.f));
	output.stalker2_arm_follow_rot = state.arm_follow_rot;
	output.stalker2_arm_follow_rot.Mul(DegToRad(1.f));
	output.stalker2_movement_weight = state.movement_weight * state.weight * settings.movement_strength * ads_scale;
	output.stalker2_movement_response = settings.movement_response;
}
} // namespace

AdsState ResolveAdsState(bool weapon_zoomed, float weapon_blend)
{
	return { weapon_zoomed, std::clamp(weapon_blend, 0.f, 1.f) };
}

void SVec3::Set(float nx, float ny, float nz)
{
	x = nx;
	y = ny;
	z = nz;
}

void SVec3::Add(const SVec3& value)
{
	x += value.x;
	y += value.y;
	z += value.z;
}

void SVec3::Add(float nx, float ny, float nz)
{
	x += nx;
	y += ny;
	z += nz;
}

void SVec3::Mul(float value)
{
	x *= value;
	y *= value;
	z *= value;
}

float SVec3::Magnitude() const
{
	return std::sqrt(x * x + y * y + z * z);
}

void SVec3::NormalizeSafe()
{
	const float magnitude = Magnitude();
	if (magnitude > kEpsilon)
		Mul(1.f / magnitude);
}

float NormalizeAngle(float value)
{
	while (value > kPi)
		value -= 2.f * kPi;
	while (value < -kPi)
		value += 2.f * kPi;
	return value;
}

float AngleDifference(float target, float current)
{
	return NormalizeAngle(target - current);
}

float DegToRad(float value)
{
	return value * kPi / 180.f;
}

float RadToDeg(float value)
{
	return value * 180.f / kPi;
}

void ResetSimulation(SimulationState& state, float yaw, float pitch, float ads_blend)
{
	state = SimulationState();
	state.signals.mouse_aim_yaw = yaw;
	state.signals.mouse_aim_pitch = pitch;
	state.signals.mouse_aim_initialized = true;
	state.lowering.prev_ads = ads_blend > 0.f;
}

void RebaseSimulationLook(SimulationState& state, float yaw_delta, float pitch_delta)
{
	if (state.signals.mouse_aim_initialized)
	{
		state.signals.mouse_aim_yaw = NormalizeAngle(state.signals.mouse_aim_yaw + yaw_delta);
		state.signals.mouse_aim_pitch = NormalizeAngle(state.signals.mouse_aim_pitch + pitch_delta);
	}
}

void UpdateSimulation(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output)
{
	output = SimulationOutput();

	UpdateInputSignals(settings.input, state.signals, input);
	output.sprint_sway = UpdateSprintSway(settings.sprint, state.sprint, input);
	const bool kick_active = UpdateFireImpulse(settings.fire, state.fire, input);
	UpdateLowering(settings.lowering, state.lowering, input);
	UpdateStalker2Arm(settings.stalker2_arm, state.stalker2_arm, input, state.signals, output);

	// The viewmodel offset is the sum of the two features that move it.
	output.lower_amount = state.lowering.amount;
	output.viewmodel_active = LoweringActive(settings.lowering) || kick_active;
	if (output.viewmodel_active)
	{
		output.lowering_pos = state.lowering.pos;
		output.lowering_rot = state.lowering.rot;
		output.lowering_pos.Mul(settings.lowering.weight);
		output.lowering_rot.Mul(settings.lowering.weight);
		output.viewmodel_pos.Add(state.fire.pos);
		output.viewmodel_pos.Add(output.lowering_pos);
		output.viewmodel_rot.Add(state.fire.rot);
		output.viewmodel_rot.Add(output.lowering_rot);
	}
}

void NotifyShot(const SimulationSettings& settings, SimulationState& state, float power, bool ads)
{
	NotifyLoweringOfShot(settings.lowering, state.lowering);
	NotifyFireImpulseOfShot(settings.fire, state.fire, state.signals, power, ads);
}

AuthoredMotionGains CalculateAuthoredMotionGains(const AuthoredMotionMetrics& metrics)
{
	// How far a weapon's own walk animation falls short of the motion the
	// controller would otherwise add: 0 when it already has that much.
	auto deficit = [](float authored, float wanted) { return std::clamp(1.f - authored / wanted, 0.f, 1.f); };

	const float lead_rotation_deficit = deficit(metrics.lead_rotation, DegToRad(2.f));
	const float lead_translation_deficit = deficit(metrics.lead_translation, 0.012f);
	AuthoredMotionGains gains;
	gains.wrist = deficit(metrics.wrist_rotation, DegToRad(4.f));
	gains.controller = std::max(std::min(lead_rotation_deficit, lead_translation_deficit), gains.wrist * 0.75f);
	gains.arm = 0.5f * (deficit(metrics.forearm_rotation, DegToRad(3.f)) + deficit(metrics.upperarm_rotation, DegToRad(2.f)));
	return gains;
}

AuthoredWalkMotion CalculateAuthoredWalkMotion(float phase, float weight, const AuthoredMotionGains& gains)
{
	// One stride per unit of phase: a sideways sway with a third harmonic and
	// a step bounce at twice the stride.
	const float cycle = (phase - std::floor(phase)) * 2.f * kPi;
	const float lateral = std::sin(cycle) + 0.16f * std::sin(3.f * cycle + 0.35f);
	const float step = std::sin(2.f * cycle - 0.35f) + 0.12f * std::sin(4.f * cycle + 0.20f);
	AuthoredWalkMotion motion;
	// Degrees, then metres, at full gain and weight.
	motion.rotation.Set(DegToRad(1.25f * step * gains.controller * weight), DegToRad(0.55f * lateral * gains.controller * weight),
		DegToRad(-2.40f * lateral * gains.controller * weight));
	motion.translation.Set(0.0040f * lateral * gains.controller * weight, 0.0025f * step * gains.controller * weight,
		-0.0012f * step * gains.controller * weight);
	return motion;
}

float CalculateAuthoredArmFollow(float arm_gain)
{
	return 0.30f - 0.18f * arm_gain;
}
} // namespace Bodycam
