#if !defined(BODYCAM_STANDALONE)
#	include "stdafx.h"
#endif
#include "bodycam_simulation.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Bodycam
{
namespace
{
constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinSprintBridgeHandoffSpeed = 0.35f;
constexpr float kMaxSprintBridgeHandoffSpeed = 1.f;

struct SprintImpulseMotion
{
	SVec3 camera_pos;
	float camera_roll = 0.f;
	float fov = 0.f;
};

template <typename T>
T Clamp(T value, T min_value, T max_value)
{
	return value < min_value ? min_value : (value > max_value ? max_value : value);
}

float Abs(float value)
{
	return std::fabs(value);
}

float AngleNormalizeSigned(float value)
{
	while (value > kPi)
		value -= 2.f * kPi;
	while (value < -kPi)
		value += 2.f * kPi;
	return value;
}

float AngleDifferenceSigned(float target, float current)
{
	return AngleNormalizeSigned(target - current);
}

float SoftRamp(float value, float width)
{
	if (width <= kEpsilon)
		return 1.f;

	const float t = Clamp(value / width, 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

float CalcDesiredAngle(float current, float target, float deadzone, float softzone, float inner_gain)
{
	const float error = AngleDifferenceSigned(target, current);
	const float abs_error = Abs(error);
	const float inside = AngleNormalizeSigned(current + error * inner_gain);
	if (abs_error <= deadzone)
		return inside;

	const float ramp = SoftRamp(abs_error - deadzone, softzone);
	const float full = AngleNormalizeSigned(target - Clamp(error, -deadzone, deadzone));
	return AngleNormalizeSigned(inside + AngleDifferenceSigned(full, inside) * ramp);
}

float SpringAngle(float current, float target, float freq, float damping, float dt)
{
	dt = Clamp(dt, 0.f, 0.033f);
	const float response = std::max(freq, 0.01f) * std::max(damping, 0.01f);
	const float factor = Clamp(1.f - std::exp(-response * dt), 0.f, 1.f);
	return AngleNormalizeSigned(current + AngleDifferenceSigned(target, current) * factor);
}

void SpringVector(SVec3& current, const SVec3& target, float freq, float damping, float dt)
{
	dt = Clamp(dt, 0.f, 0.033f);
	const float response = std::max(freq, 0.01f) * std::max(damping, 0.01f);
	const float factor = Clamp(1.f - std::exp(-response * dt), 0.f, 1.f);
	current.x += (target.x - current.x) * factor;
	current.y += (target.y - current.y) * factor;
	current.z += (target.z - current.z) * factor;
}

float SmoothFloat(float current, float target, float response, float dt)
{
	dt = Clamp(dt, 0.f, 0.033f);
	const float factor = Clamp(1.f - std::exp(-std::max(response, 0.01f) * dt), 0.f, 1.f);
	return current + (target - current) * factor;
}

float Lerp(float a, float b, float t)
{
	return a + (b - a) * Clamp(t, 0.f, 1.f);
}

float SmoothStep(float value)
{
	value = Clamp(value, 0.f, 1.f);
	return value * value * (3.f - 2.f * value);
}

void SmoothVector(SVec3& current, const SVec3& target, float response, float dt)
{
	current.x = SmoothFloat(current.x, target.x, response, dt);
	current.y = SmoothFloat(current.y, target.y, response, dt);
	current.z = SmoothFloat(current.z, target.z, response, dt);
}

float FollowAimAxis(float& current, float target, float response, float dt)
{
	const float previous = current;
	const float factor = Clamp(1.f - std::exp(-std::max(response, 0.01f) * Clamp(dt, 0.f, 0.033f)), 0.f, 1.f);
	current = AngleNormalizeSigned(current + AngleDifferenceSigned(target, current) * factor);
	return AngleDifferenceSigned(current, previous);
}

void UpdateViewmodelMouseResponse(const SimulationSettings& settings, SimulationState& state,
	const SimulationInput& input, bool enabled, float safe_dt)
{
	if (!enabled)
	{
		state.viewmodel.mouse_speed.Set(0.f, 0.f, 0.f);
		state.viewmodel.prev_mouse_speed.Set(0.f, 0.f, 0.f);
		state.viewmodel.mouse_accel.Set(0.f, 0.f, 0.f);
		state.viewmodel.mouse_aim_initialized = false;
		return;
	}

	const float aim_yaw = input.visual_aim_available ? input.visual_aim_yaw : input.target_yaw;
	const float aim_pitch = input.visual_aim_available ? input.visual_aim_pitch : input.target_pitch;
	if (!state.viewmodel.mouse_aim_initialized)
	{
		state.viewmodel.mouse_aim_yaw = aim_yaw;
		state.viewmodel.mouse_aim_pitch = aim_pitch;
		state.viewmodel.mouse_speed.Set(0.f, 0.f, 0.f);
		state.viewmodel.prev_mouse_speed.Set(0.f, 0.f, 0.f);
		state.viewmodel.mouse_accel.Set(0.f, 0.f, 0.f);
		state.viewmodel.mouse_aim_initialized = true;
		return;
	}

	const float yaw_step = FollowAimAxis(state.viewmodel.mouse_aim_yaw, aim_yaw,
		settings.viewmodel.mouse_filter, input.dt);
	const float pitch_step = FollowAimAxis(state.viewmodel.mouse_aim_pitch, aim_pitch,
		settings.viewmodel.mouse_filter, input.dt);
	state.viewmodel.mouse_speed.Set(yaw_step / safe_dt, pitch_step / safe_dt, 0.f);

	state.viewmodel.mouse_accel = state.viewmodel.mouse_speed;
	state.viewmodel.mouse_accel.Sub(state.viewmodel.prev_mouse_speed);
	state.viewmodel.mouse_accel.Mul(1.f / safe_dt);
	if (Abs(state.viewmodel.mouse_accel.x) < DegToRad(80.f))
		state.viewmodel.mouse_accel.x = 0.f;
	if (Abs(state.viewmodel.mouse_accel.y) < DegToRad(80.f))
		state.viewmodel.mouse_accel.y = 0.f;
	state.viewmodel.prev_mouse_speed = state.viewmodel.mouse_speed;
}

void UpdateArmPoseSpring(SVec3& current, SVec3& velocity, const SVec3& target, float response, float dt)
{
	const float clamped_dt = Clamp(dt, 0.f, 0.033f);
	const float frequency = Clamp(response * 0.12f, 1.0f, 6.0f);
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

bool ClampVector(SVec3& value, float limit)
{
	const SVec3 before = value;
	value.x = Clamp(value.x, -limit, limit);
	value.y = Clamp(value.y, -limit, limit);
	value.z = Clamp(value.z, -limit, limit);
	return std::fabs(before.x - value.x) > kEpsilon || std::fabs(before.y - value.y) > kEpsilon || std::fabs(before.z - value.z) > kEpsilon;
}

void ApplySprintCameraImpulse(SimulationState& state, const SVec3& camera_pos, float camera_roll)
{
	state.camera.impulse_pos.Add(camera_pos);
	state.camera.impulse_roll += camera_roll;
}

void ClearSprintImpulseQueue(SimulationState& state)
{
	state.sprint_impulse.camera_pos.Set(0.f, 0.f, 0.f);
	state.sprint_impulse.camera_roll = 0.f;
}

float SprintTransitionSide(const SimulationState& state)
{
	float side = state.viewmodel.move_intent.x;
	if (Abs(side) <= 0.05f)
		side = state.viewmodel.mouse_speed.x;
	return side >= 0.f ? 1.f : -1.f;
}

float SprintTransitionPower(const SimulationSettings& settings, const SimulationInput& input, bool starting)
{
	const float event_scale = starting ? settings.impulse.sprint_start_impulse : settings.impulse.sprint_stop_impulse * 1.75f;
	const float ads_scale = input.ads ? settings.impulse.sprint_ads_mult : 1.f;
	const float holster_scale = input.weapon_lowered ? 1.15f : 1.f;
	return Clamp(settings.impulse.sprint_impulse * settings.sprint.accent * Clamp(settings.sprint.strength, 0.f, 2.f) *
		event_scale * ads_scale * holster_scale, 0.f, 6.f);
}

float SprintCameraImpulsePower(const SimulationSettings& settings, float transition_power)
{
	const float camera_amount = Clamp(settings.impulse.sprint_camera_impulse, 0.f, 5.f) / 5.f;
	return transition_power * camera_amount;
}

SprintImpulseMotion BuildSprintTransitionMotion(const SimulationSettings& settings, float power, float side, bool starting)
{
	SprintImpulseMotion motion;
	const float camera_power = SprintCameraImpulsePower(settings, power);

	if (starting)
	{
		motion.camera_pos.Set(-0.0024f * camera_power * side, -0.0050f * camera_power, -0.0100f * camera_power);
		motion.camera_roll = DegToRad(-0.35f * camera_power * side);
		motion.fov = 0.40f * Clamp(settings.impulse.sprint_fov_impulse, 0.f, 5.f) * power;
		return motion;
	}

	motion.camera_pos.Set(0.0030f * camera_power * side, 0.0090f * camera_power, 0.0170f * camera_power);
	motion.camera_roll = DegToRad(0.45f * camera_power * side);
	motion.fov = -0.18f * Clamp(settings.impulse.sprint_fov_impulse, 0.f, 5.f) * power;
	return motion;
}

void QueueOrApplySprintImpulse(const SimulationSettings& settings, SimulationState& state, const SprintImpulseMotion& motion)
{
	const float speed = Clamp(settings.impulse.sprint_impulse_speed, 0.05f, 1.f);
	state.camera.impulse_fov_target += motion.fov;

	if (speed >= 1.f - kEpsilon)
	{
		ApplySprintCameraImpulse(state, motion.camera_pos, motion.camera_roll);
		return;
	}

	state.sprint_impulse.camera_pos.Add(motion.camera_pos);
	state.sprint_impulse.camera_roll += motion.camera_roll;
}

float CalcLoweringBaseAmount(std::uint32_t move_flags)
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

float SpringLoweringScalar(float current, float target, float normalized_speed, float dt)
{
	normalized_speed = Clamp(normalized_speed, 0.f, 1.f);
	if (normalized_speed >= 1.f - kEpsilon)
		return target;
	if (normalized_speed <= kEpsilon)
		return current;

	dt = Clamp(dt, 0.f, 0.033f);
	const float response = std::max(normalized_speed * normalized_speed * 16.f, 0.01f);
	const float factor = Clamp(1.f - std::exp(-response * dt), 0.f, 1.f);
	return current + (target - current) * factor;
}

float CalcLoweringStateMultiplier(const SimulationSettings& settings, const SimulationInput& input)
{
	const bool moving = !!(input.move_flags & smfAnyMove);
	if (!moving)
		return 0.f;
	if (input.move_flags & smfSprint)
		return 0.f;
	if (input.move_flags & smfCrouch)
		return 0.f;

	const float state_multiplier = input.accelerated ? Clamp(settings.lowering.walk, 0.f, 1.f) : Clamp(settings.lowering.slow_walk, 0.f, 1.f);
	return Clamp(settings.lowering.move, 0.f, 1.f) * state_multiplier;
}

void ResetLowering(SimulationState& state)
{
	state.lowering.amount = 0.f;
	state.lowering.target = 0.f;
	state.lowering.holster = 0.f;
	state.lowering.fire_recovery = 0.f;
	state.lowering.ads_recovery = 0.f;
	state.lowering.combat_timer = 0.f;
	state.viewmodel.airborne_time = 0.f;
	state.lowering.pos.Set(0.f, 0.f, 0.f);
	state.lowering.rot.Set(0.f, 0.f, 0.f);
}

void UpdateLowering(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input)
{
	if (!settings.features.lower_enable || settings.features.layer_lower_weight <= kEpsilon)
	{
		ResetLowering(state);
		return;
	}

	if (!input.ads && state.prev_ads)
		state.lowering.ads_recovery = std::max(state.lowering.ads_recovery, std::max(settings.lowering.aim_timeout, 0.f));

	state.lowering.fire_recovery = std::max(0.f, state.lowering.fire_recovery - input.dt);
	state.lowering.ads_recovery = std::max(0.f, state.lowering.ads_recovery - input.dt);
	state.lowering.combat_timer = input.combat ? std::max(state.lowering.combat_timer, std::max(settings.lowering.combat_timeout, 0.f)) : std::max(0.f, state.lowering.combat_timer - input.dt);

	const float base_amount = CalcLoweringBaseAmount(input.move_flags);
	const float movement_multiplier = CalcLoweringStateMultiplier(settings, input);
	float target = base_amount * movement_multiplier;

	if (movement_multiplier <= kEpsilon || input.ads || state.lowering.fire_recovery > 0.f || state.lowering.ads_recovery > 0.f)
		target = 0.f;
	if (settings.features.lower_disable_in_combat && state.lowering.combat_timer > 0.f)
		target = 0.f;

	state.lowering.target = target;
	const float amount_speed = target < state.lowering.amount ? settings.lowering.return_speed : settings.lowering.speed;
	state.lowering.amount = Clamp(SpringLoweringScalar(state.lowering.amount, target, amount_speed, input.dt), 0.f, 2.f);

	const float holster_target = input.weapon_lowered ? 1.f : 0.f;
	const float holster_speed = holster_target < state.lowering.holster ? settings.lowering.return_speed : settings.lowering.speed;
	state.lowering.holster = Clamp(SpringLoweringScalar(state.lowering.holster, holster_target, holster_speed, input.dt), 0.f, 1.f);

	state.lowering.rot.Set(settings.lowering.yaw * state.lowering.amount, settings.lowering.pitch * state.lowering.amount, settings.lowering.roll * state.lowering.amount);
	state.lowering.pos.Set(settings.lowering.x * state.lowering.amount,
		settings.lowering.y * state.lowering.amount - settings.lowering.holster_offset * state.lowering.holster,
		settings.lowering.z * state.lowering.amount);
}

void AddSprintTransitionImpulse(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, bool starting)
{
	const float power = SprintTransitionPower(settings, input, starting);
	if (power <= kEpsilon)
		return;

	QueueOrApplySprintImpulse(settings, state, BuildSprintTransitionMotion(settings, power, SprintTransitionSide(state), starting));
}

void UpdateSprintFovPulse(const SimulationSettings& settings, SimulationState& state, float dt)
{
	const float speed = Clamp(settings.impulse.sprint_fov_speed, 0.05f, 1.f);
	const float clamped_dt = Clamp(dt, 0.f, 0.033f);
	const float target_decay = Clamp(1.f - std::exp(-(0.9f + speed * 1.6f) * clamped_dt), 0.f, 1.f);
	const float response = 4.f + speed * speed * 18.f;

	state.camera.impulse_fov_target *= 1.f - target_decay;
	state.camera.impulse_fov = SmoothFloat(state.camera.impulse_fov, state.camera.impulse_fov_target, response, dt);
	if (Abs(state.camera.impulse_fov_target) < 0.001f && Abs(state.camera.impulse_fov) < 0.001f)
	{
		state.camera.impulse_fov_target = 0.f;
		state.camera.impulse_fov = 0.f;
	}
}

void UpdateSprintImpulseQueue(const SimulationSettings& settings, SimulationState& state, float dt)
{
	const float speed = Clamp(settings.impulse.sprint_impulse_speed, 0.05f, 1.f);
	if (speed >= 1.f - kEpsilon)
	{
		ApplySprintCameraImpulse(state, state.sprint_impulse.camera_pos, state.sprint_impulse.camera_roll);
		ClearSprintImpulseQueue(state);
		return;
	}

	const float amount = Clamp(1.f - std::exp(-(8.f + speed * speed * 42.f) * Clamp(dt, 0.f, 0.033f)), 0.f, 1.f);
	SVec3 camera_pos = state.sprint_impulse.camera_pos;
	const float camera_roll = state.sprint_impulse.camera_roll * amount;
	camera_pos.Mul(amount);
	ApplySprintCameraImpulse(state, camera_pos, camera_roll);

	state.sprint_impulse.camera_pos.Sub(camera_pos);
	state.sprint_impulse.camera_roll -= camera_roll;
	if (state.sprint_impulse.camera_pos.Magnitude() < 0.00001f && Abs(state.sprint_impulse.camera_roll) < DegToRad(0.001f))
	{
		ClearSprintImpulseQueue(state);
	}
}

void ResetSprint(SimulationState& state)
{
	state.sprint.amount = 0.f;
	state.sprint.target = 0.f;
	state.sprint.viewmodel_amount = 0.f;
	state.sprint.settle = 0.f;
	state.sprint.phase = 0.f;
	state.sprint.prev_amount = 0.f;
}

void ResetArm(SimulationArmState& arm)
{
	arm = SimulationArmState();
}

float SmoothTime(float current, float target, float seconds, float dt)
{
	const float response = 3.f / std::max(seconds, 0.01f);
	return SmoothFloat(current, target, response, dt);
}

void UpdateSprintLayer(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input)
{
	if (!settings.features.sprint_transition_enable || !input.firearm_equipped)
	{
		ResetSprint(state);
		ClearSprintImpulseQueue(state);
		return;
	}

	const bool moving = !!(input.move_flags & smfAnyMove);
	const bool blocked = input.ads || !!(input.move_flags & (smfCrouch | smfFall | smfJump));
	const bool sprinting = moving && !blocked && !!(input.move_flags & smfSprint);
	const float speed_fraction = input.actor_speed_fraction > 0.f ? Clamp(input.actor_speed_fraction, 0.f, 1.15f) : 1.f;
	const float target = sprinting ? Clamp(speed_fraction, 0.f, 1.f) : 0.f;
	const float handoff_speed = ClampSprintBridgeHandoffSpeed(settings.sprint.bridge_handoff_speed);
	const float viewmodel_target = sprinting && target < handoff_speed ? SmoothStep(target / handoff_speed) : 0.f;
	const float smoothness = Clamp(settings.sprint.smoothness, 0.f, 1.f);
	const float time_scale = 0.55f + smoothness * 1.25f;
	const float enter_time = std::max(settings.sprint.enter_time * time_scale, 0.01f);
	const float exit_time = std::max(settings.sprint.exit_time * time_scale, 0.01f);
	const float viewmodel_exit_time = sprinting && viewmodel_target <= kEpsilon ? std::min(exit_time, 0.18f) : exit_time;
	const float camera_settle_time = std::max(0.35f * time_scale, 0.01f);

	state.sprint.prev_amount = state.sprint.amount;
	state.sprint.target = target;
	state.sprint.amount = Clamp(SmoothTime(state.sprint.amount, target, target > state.sprint.amount ? enter_time : exit_time, input.dt), 0.f, 1.25f);
	if (!sprinting)
		state.sprint.viewmodel_amount = 0.f;
	else
		state.sprint.viewmodel_amount = Clamp(SmoothTime(state.sprint.viewmodel_amount, viewmodel_target, viewmodel_target > state.sprint.viewmodel_amount ? enter_time : viewmodel_exit_time, input.dt), 0.f, 1.25f);
	if (target <= kEpsilon && state.sprint.prev_amount > 0.15f)
		state.sprint.settle = std::max(state.sprint.settle, state.sprint.prev_amount * 0.45f);
	state.sprint.settle = SmoothTime(state.sprint.settle, 0.f, camera_settle_time, input.dt);

	if (state.sprint.amount > 0.001f || state.sprint.settle > 0.001f)
	{
		state.sprint.phase += input.dt * (6.2f + 2.1f * state.sprint.amount);
		while (state.sprint.phase > 2.f * kPi)
			state.sprint.phase -= 2.f * kPi;
	}
	else
	{
		state.sprint.phase = 0.f;
	}
}

float SprintStrength(const SimulationSettings& settings)
{
	return Clamp(settings.sprint.strength, 0.f, 2.f);
}

bool ArmStateActive(const SimulationArmState& arm)
{
	return arm.weight > 0.001f || arm.motion_weight > 0.001f ||
		arm.clavicle.Magnitude() + arm.upperarm.Magnitude() + arm.forearm.Magnitude() + arm.twist.Magnitude() + arm.hand.Magnitude() +
		arm.left_clavicle.Magnitude() + arm.left_upperarm.Magnitude() + arm.left_forearm.Magnitude() +
		arm.left_twist.Magnitude() + arm.left_hand.Magnitude() > DegToRad(0.001f);
}

void AddArmStateToOutput(const SimulationArmState& arm, SimulationOutput& output)
{
	output.arm_active = output.arm_active || ArmStateActive(arm);
	output.arm_clavicle.Add(arm.clavicle);
	output.arm_upperarm.Add(arm.upperarm);
	output.arm_forearm.Add(arm.forearm);
	output.arm_twist.Add(arm.twist);
	output.arm_hand.Add(arm.hand);
	output.arm_left_clavicle.Add(arm.left_clavicle);
	output.arm_left_upperarm.Add(arm.left_upperarm);
	output.arm_left_forearm.Add(arm.left_forearm);
	output.arm_left_twist.Add(arm.left_twist);
	output.arm_left_hand.Add(arm.left_hand);
}

void UpdateBodycamArmLayer(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output)
{
	const float layer_weight = Clamp(settings.features.layer_arm_weight, 0.f, 1.f);
	const SimulationArmSettings& style = settings.bodycam_arm;
	const float raw_strength = Clamp(style.strength, 0.f, 8.f);
	const bool arm_allowed = settings.features.bodycam_arm_enable && input.firearm_equipped && layer_weight > kEpsilon && raw_strength > kEpsilon;
	const float target_weight = arm_allowed ? 1.f : 0.f;
	state.arm.weight = SmoothTime(state.arm.weight, target_weight, target_weight > state.arm.weight ? 0.18f : 0.28f, input.dt);

	const float strength = raw_strength * layer_weight;
	if (!arm_allowed && state.arm.weight <= 0.001f)
		state.arm.controller.Set(0.f, 0.f, 0.f);

	const float ads_mult = Lerp(1.f, Clamp(style.ads_scale, 0.f, 1.f), Clamp(input.ads_blend, 0.f, 1.f));
	const float yaw_throw = CalculateMouseThrow(state.viewmodel.mouse_speed.x, DegToRad(420.f), 1.f);
	const float pitch_throw = CalculateMouseThrow(state.viewmodel.mouse_speed.y, DegToRad(320.f), 1.f);
	SVec3 raw_controller;
	raw_controller.Set(arm_allowed ? pitch_throw : 0.f, arm_allowed ? yaw_throw : 0.f, arm_allowed ? -yaw_throw : 0.f);
	state.arm.prev_controller = state.arm.controller;
	SmoothVector(state.arm.controller, raw_controller, 8.f, input.dt);

	const float lag_lift = state.arm.controller.x;
	const float lag_side = state.arm.controller.y;
	const float lag_roll = state.arm.controller.z;
	SVec3 controller_delta = state.arm.controller;
	controller_delta.Sub(state.arm.prev_controller);
	SVec3 settle_target;
	settle_target.Set(-controller_delta.x * 1.6f, -controller_delta.y * 1.8f, -controller_delta.z * 1.6f);
	settle_target.Mul(arm_allowed ? 1.f : 0.f);
	SmoothVector(state.arm.settle, settle_target, arm_allowed ? 10.f : 5.f, input.dt);
	const float separation = Clamp(Abs(yaw_throw - lag_side) + Abs(-yaw_throw - lag_roll), 0.f, 1.f);
	const float brace_amount = Clamp(std::max(Abs(lag_side), Abs(lag_roll)) + separation * 0.35f, 0.f, 1.f);
	const float target_motion_weight = arm_allowed ? Clamp(std::max(Abs(yaw_throw), Abs(pitch_throw)), 0.f, 1.f) : 0.f;
	state.arm.motion_weight = SmoothTime(state.arm.motion_weight, target_motion_weight, target_motion_weight > state.arm.motion_weight ? 0.20f : 0.62f, input.dt);
	const float final_weight = state.arm.weight * SmoothStep(state.arm.motion_weight);
	SVec3 base;
	base.Set(DegToRad((lag_lift + state.arm.settle.x * 0.45f) * style.mouse_pitch),
		0.f,
		DegToRad((lag_roll + state.arm.settle.z * 0.55f) * style.mouse_roll));
	base.Mul(strength * ads_mult);

	SVec3 right_base = base;
	SVec3 left_base = base;
	const float organic_side = lag_side + state.arm.settle.y * 0.55f;
	const float organic_roll = lag_roll + state.arm.settle.z * 0.45f;
	const float tuck = organic_side * brace_amount * strength * ads_mult;
	const float brace = (brace_amount + Abs(state.arm.settle.y) * 0.18f + Abs(state.arm.settle.z) * 0.12f) * strength * ads_mult;
	const float secondary_roll = style.secondary_roll;
	right_base.Add(DegToRad(0.65f * brace), DegToRad(-style.mouse_yaw * tuck), DegToRad(0.35f * secondary_roll * organic_roll));
	left_base.Add(DegToRad(0.78f * brace), DegToRad(style.mouse_yaw * tuck), DegToRad(0.35f * secondary_roll * organic_roll));

	SVec3 clavicle = right_base;
	SVec3 upperarm = right_base;
	SVec3 forearm = right_base;
	SVec3 twist = right_base;
	SVec3 hand = right_base;
	SVec3 left_clavicle = left_base;
	SVec3 left_upperarm = left_base;
	SVec3 left_forearm = left_base;
	SVec3 left_twist = left_base;
	SVec3 left_hand = left_base;
	clavicle.Mul(0.10f);
	upperarm.Mul(Clamp(style.upperarm_scale, 0.f, 1.f) * 0.88f);
	forearm.Mul(Clamp(style.forearm_scale, 0.f, 1.5f) * 0.62f);
	twist.Mul(Clamp(style.twist_scale, 0.f, 2.f) * 0.20f);
	hand.Mul(Clamp(style.hand_scale, 0.f, 1.f));
	left_clavicle.Mul(0.10f);
	left_upperarm.Mul(Clamp(style.upperarm_scale, 0.f, 1.f) * 0.88f);
	left_forearm.Mul(Clamp(style.forearm_scale, 0.f, 1.5f) * 0.62f);
	left_twist.Mul(Clamp(style.twist_scale, 0.f, 2.f) * 0.20f);
	left_hand.Mul(Clamp(style.hand_scale, 0.f, 1.f));

	clavicle.Mul(final_weight);
	upperarm.Mul(final_weight);
	forearm.Mul(final_weight);
	twist.Mul(final_weight);
	hand.Mul(final_weight);
	left_clavicle.Mul(final_weight);
	left_upperarm.Mul(final_weight);
	left_forearm.Mul(final_weight);
	left_twist.Mul(final_weight);
	left_hand.Mul(final_weight);

	const float response = std::max(style.response, 0.1f);
	UpdateArmPoseSpring(state.arm.clavicle, state.arm.clavicle_vel, clavicle, response * 0.70f, input.dt);
	UpdateArmPoseSpring(state.arm.upperarm, state.arm.upperarm_vel, upperarm, response * 0.90f, input.dt);
	UpdateArmPoseSpring(state.arm.forearm, state.arm.forearm_vel, forearm, response * 1.15f, input.dt);
	UpdateArmPoseSpring(state.arm.twist, state.arm.twist_vel, twist, response * 1.25f, input.dt);
	UpdateArmPoseSpring(state.arm.hand, state.arm.hand_vel, hand, response * 1.35f, input.dt);
	UpdateArmPoseSpring(state.arm.left_clavicle, state.arm.left_clavicle_vel, left_clavicle, response * 0.70f, input.dt);
	UpdateArmPoseSpring(state.arm.left_upperarm, state.arm.left_upperarm_vel, left_upperarm, response * 0.90f, input.dt);
	UpdateArmPoseSpring(state.arm.left_forearm, state.arm.left_forearm_vel, left_forearm, response * 1.15f, input.dt);
	UpdateArmPoseSpring(state.arm.left_twist, state.arm.left_twist_vel, left_twist, response * 1.25f, input.dt);
	UpdateArmPoseSpring(state.arm.left_hand, state.arm.left_hand_vel, left_hand, response * 1.35f, input.dt);

	AddArmStateToOutput(state.arm, output);
}

void UpdateStalker2ArmLayer(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output)
{
	SimulationStalker2ArmState& style_state = state.stalker2_arm;
	const SimulationStalker2ArmSettings& style = settings.stalker2_arm;
	const float layer_weight = Clamp(settings.features.layer_arm_weight, 0.f, 1.f);
	const float strength = Clamp(style.strength, 0.f, 4.f) * layer_weight;
	const bool arm_allowed = settings.features.stalker2_arm_enable && input.firearm_equipped &&
		layer_weight > kEpsilon && strength > kEpsilon;

	const float target_weight = arm_allowed ? 1.f : 0.f;
	style_state.weight = SmoothTime(style_state.weight, target_weight,
		target_weight > style_state.weight ? 0.16f : 0.30f, input.dt);

	const float ads_scale = Lerp(1.f, Clamp(style.ads_scale, 0.f, 1.f), Clamp(input.ads_blend, 0.f, 1.f));
	const bool movement_allowed = arm_allowed && !!(input.move_flags & smfAnyMove) &&
		!(input.move_flags & (smfSprint | smfCrouch | smfFall | smfJump));
	const float move_amount = movement_allowed ? Clamp(state.viewmodel.move_intent.Magnitude(), 0.f, 1.f) : 0.f;
	const float movement_target = CalculateStalker2MovementAmount(move_amount,
		input.actor_speed_fraction, input.accelerated, style.slow_walk_scale);
	style_state.movement_weight = SmoothTime(style_state.movement_weight, movement_target,
		movement_target > style_state.movement_weight ? 0.12f : 0.24f, input.dt);
	const float yaw_throw = arm_allowed ? CalculateMouseThrow(state.viewmodel.mouse_speed.x,
		DegToRad(420.f), style.mouse_sensitivity) : 0.f;
	const float pitch_throw = arm_allowed ? CalculateMouseThrow(state.viewmodel.mouse_speed.y,
		DegToRad(320.f), style.mouse_sensitivity) : 0.f;

	const float final_weight = style_state.weight * strength * ads_scale * Clamp(style.wrist_scale, 0.f, 1.f);
	const float mouse_strength = Clamp(style.mouse_strength, 0.f, 4.f);
	const float mouse_weight = mouse_strength * final_weight;
	const SVec3 wrist_rot_target = ClampStalker2MouseRotation(
		CalculateStalker2MouseControllerRotation(yaw_throw, style.mouse_yaw, style.mouse_roll, mouse_weight),
		style.mouse_max_yaw, style.mouse_max_pitch, style.mouse_max_roll);

	UpdateArmPoseSpring(style_state.wrist_rot, style_state.wrist_rot_vel,
		wrist_rot_target, std::max(style.response, 0.1f), input.dt);
	SVec3 arm_follow_target = wrist_rot_target;
	arm_follow_target.Mul(Clamp(style.arm_follow_scale, 0.f, 1.f));
	arm_follow_target.Add(CalculateStalker2VerticalArmFollow(pitch_throw,
		style.mouse_pitch, mouse_weight));
	arm_follow_target = ClampStalker2MouseRotation(arm_follow_target,
		style.mouse_max_yaw, style.mouse_max_pitch, style.mouse_max_roll);
	UpdateArmPoseSpring(style_state.arm_follow_rot, style_state.arm_follow_rot_vel,
		arm_follow_target, std::max(style.arm_follow_response, 0.1f), input.dt);

	output.stalker2_arm_active = style_state.weight > 0.001f ||
		style_state.wrist_rot.Magnitude() > 0.001f || style_state.arm_follow_rot.Magnitude() > 0.001f;
	output.stalker2_wrist_rot = style_state.wrist_rot;
	output.stalker2_wrist_rot.Mul(DegToRad(1.f));
	output.stalker2_arm_follow_rot = style_state.arm_follow_rot;
	output.stalker2_arm_follow_rot.Mul(DegToRad(1.f));
	output.stalker2_movement_weight = style_state.movement_weight * style_state.weight *
		Clamp(style.movement_strength, 0.f, 4.f) * ads_scale;
	output.stalker2_movement_response = std::max(style.movement_response, 0.1f);
}
} // namespace

AdsState ResolveAdsState(bool weapon_zoomed, float weapon_blend)
{
	const float blend = Clamp(weapon_blend, 0.f, 1.f);
	return { weapon_zoomed, blend };
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

void SVec3::Sub(const SVec3& value)
{
	x -= value.x;
	y -= value.y;
	z -= value.z;
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

float ArmCorrectionAngle(float dot, float cross_magnitude)
{
	dot = Clamp(dot, -1.f, 1.f);
	cross_magnitude = std::max(cross_magnitude, 0.f);
	return std::atan2(cross_magnitude, dot);
}

SVec3 SolveArmMidpoint(const SVec3& start, const SVec3& end, const SVec3& current_mid, const SVec3& desired_mid)
{
	SVec3 line = end;
	line.Sub(start);
	const float length = line.Magnitude();
	if (length < kEpsilon)
		return current_mid;
	line.Mul(1.f / length);

	SVec3 first_segment = current_mid;
	first_segment.Sub(start);
	SVec3 second_segment = end;
	second_segment.Sub(current_mid);
	const float first_length = first_segment.Magnitude();
	const float second_length = second_segment.Magnitude();
	if (first_length < kEpsilon || second_length < kEpsilon)
		return current_mid;

	const float along = Clamp((first_length * first_length - second_length * second_length + length * length) / (2.f * length), 0.f, length);
	const float radius_sq = std::max(first_length * first_length - along * along, 0.f);
	const float radius = std::sqrt(radius_sq);
	if (radius < kEpsilon)
		return current_mid;

	SVec3 center = line;
	center.Mul(along);
	center.Add(start);
	SVec3 radial = current_mid;
	radial.Sub(center);
	const float radial_length = radial.Magnitude();
	if (radial_length < kEpsilon)
		return current_mid;
	radial.Mul(1.f / radial_length);

	SVec3 tangent;
	tangent.Set(line.y * radial.z - line.z * radial.y,
		line.z * radial.x - line.x * radial.z,
		line.x * radial.y - line.y * radial.x);
	const float tangent_length = tangent.Magnitude();
	if (tangent_length < kEpsilon)
		return current_mid;
	tangent.Mul(1.f / tangent_length);

	SVec3 desired_offset = desired_mid;
	desired_offset.Sub(current_mid);
	const float tangent_distance = desired_offset.x * tangent.x + desired_offset.y * tangent.y + desired_offset.z * tangent.z;
	const float angle = std::atan2(tangent_distance, radius);

	SVec3 result = radial;
	result.Mul(radius * std::cos(angle));
	SVec3 tangent_part = tangent;
	tangent_part.Mul(radius * std::sin(angle));
	result.Add(tangent_part);
	result.Add(center);
	return result;
}

float DegToRad(float value)
{
	return value * kPi / 180.f;
}

float RadToDeg(float value)
{
	return value * 180.f / kPi;
}

float ClampSprintBridgeHandoffSpeed(float value)
{
	return Clamp(value, kMinSprintBridgeHandoffSpeed, kMaxSprintBridgeHandoffSpeed);
}

void ResetSimulation(SimulationState& state, float yaw, float pitch, std::uint32_t move_flags, float ads_blend)
{
	ads_blend = Clamp(ads_blend, 0.f, 1.f);
	state.camera.yaw = yaw;
	state.camera.pitch = pitch;
	state.camera.roll = 0.f;
	state.camera.pos.Set(0.f, 0.f, 0.f);
	state.camera.impulse_pos.Set(0.f, 0.f, 0.f);
	state.camera.impulse_roll = 0.f;
	state.camera.impulse_fov = 0.f;
	state.camera.impulse_fov_target = 0.f;
	state.viewmodel.pos.Set(0.f, 0.f, 0.f);
	state.viewmodel.rot.Set(0.f, 0.f, 0.f);
	state.viewmodel.mouse_speed.Set(0.f, 0.f, 0.f);
	state.viewmodel.prev_mouse_speed.Set(0.f, 0.f, 0.f);
	state.viewmodel.mouse_accel.Set(0.f, 0.f, 0.f);
	state.viewmodel.mouse_aim_yaw = yaw;
	state.viewmodel.mouse_aim_pitch = pitch;
	state.viewmodel.mouse_aim_initialized = true;
	state.viewmodel.move_intent.Set(0.f, 0.f, 0.f);
	state.viewmodel.impulse_pos.Set(0.f, 0.f, 0.f);
	state.viewmodel.impulse_rot.Set(0.f, 0.f, 0.f);
	state.viewmodel.fire_impulse_pos.Set(0.f, 0.f, 0.f);
	state.viewmodel.fire_impulse_rot.Set(0.f, 0.f, 0.f);
	state.viewmodel.fire_pos.Set(0.f, 0.f, 0.f);
	state.viewmodel.fire_rot.Set(0.f, 0.f, 0.f);
	ClearSprintImpulseQueue(state);
	ResetSprint(state);
	ResetLowering(state);
	ResetArm(state.arm);
	state.stalker2_arm = SimulationStalker2ArmState();
	state.ads_blend = ads_blend;
	state.prev_move_flags = move_flags;
	state.prev_ads = ads_blend > 0.f;
	state.camera.initialized = true;
}

void RebaseSimulationLook(SimulationState& state, float yaw_delta, float pitch_delta)
{
	if (state.camera.initialized)
	{
		state.camera.yaw = AngleNormalizeSigned(state.camera.yaw + yaw_delta);
		state.camera.pitch = AngleNormalizeSigned(state.camera.pitch + pitch_delta);
	}

	if (state.viewmodel.mouse_aim_initialized)
	{
		state.viewmodel.mouse_aim_yaw = AngleNormalizeSigned(state.viewmodel.mouse_aim_yaw + yaw_delta);
		state.viewmodel.mouse_aim_pitch = AngleNormalizeSigned(state.viewmodel.mouse_aim_pitch + pitch_delta);
	}
}

void UpdateSimulation(const SimulationSettings& settings, SimulationState& state, const SimulationInput& input, SimulationOutput& output)
{
	output = SimulationOutput();
	const float safe_dt = Clamp(input.dt, 1.f / 240.f, 1.f / 30.f);

	if (!state.camera.initialized)
	{
		state.camera.yaw = input.target_yaw;
		state.camera.pitch = input.target_pitch;
		state.camera.initialized = true;
	}

	const bool vm_spring_enabled = settings.features.vm_enable && settings.features.layer_vm_weight > kEpsilon;
	const bool lower_enabled = settings.features.lower_enable && settings.features.layer_lower_weight > kEpsilon;
	const bool fire_impulse_enabled = settings.features.fire_impulse_enable;
	if (!fire_impulse_enabled)
	{
		state.viewmodel.fire_impulse_pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_impulse_rot.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_rot.Set(0.f, 0.f, 0.f);
	}
	const bool fire_impulse_active = fire_impulse_enabled &&
		(state.viewmodel.fire_impulse_pos.Magnitude() > kEpsilon ||
			state.viewmodel.fire_impulse_rot.Magnitude() > kEpsilon ||
			state.viewmodel.fire_pos.Magnitude() > kEpsilon ||
			state.viewmodel.fire_rot.Magnitude() > kEpsilon);
	const bool vm_effects_enabled = vm_spring_enabled || lower_enabled;
	const bool mouse_input_enabled = vm_effects_enabled ||
		((settings.features.bodycam_arm_enable || settings.features.stalker2_arm_enable) && settings.features.layer_arm_weight > kEpsilon);
	UpdateViewmodelMouseResponse(settings, state, input, mouse_input_enabled, safe_dt);

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
		move_target.Mul(0.45f);
	if (input.move_flags & smfSprint)
		move_target.Mul(1.35f);
	SmoothVector(state.viewmodel.move_intent, move_target, settings.viewmodel.move_filter, input.dt);
	UpdateSprintLayer(settings, state, input);
	const bool sprint_bridge_active = settings.features.sprint_transition_enable && input.firearm_equipped && !input.ads &&
		settings.features.layer_vm_weight > kEpsilon && SprintStrength(settings) > kEpsilon &&
		(state.sprint.viewmodel_amount > kEpsilon || !!(input.move_flags & smfSprint));
	const bool hud_transform_enabled = vm_effects_enabled || sprint_bridge_active || fire_impulse_active;

	if (settings.features.sprint_transition_enable)
	{
		if (input.firearm_equipped && (input.move_flags & smfSprint) && !(state.prev_move_flags & smfSprint))
			AddSprintTransitionImpulse(settings, state, input, true);
		else if (input.firearm_equipped && !(input.move_flags & smfSprint) && (state.prev_move_flags & smfSprint))
			AddSprintTransitionImpulse(settings, state, input, false);
		UpdateSprintImpulseQueue(settings, state, input.dt);
	}

	if (vm_effects_enabled)
	{
		const bool airborne = !!(input.move_flags & (smfFall | smfJump));
		const bool landing = !!(input.move_flags & smfLanding);
		const bool real_landing = landing && state.viewmodel.airborne_time > 0.12f;
		if (input.ads && !state.prev_ads)
		{
			state.viewmodel.impulse_pos.Add(0.f, 0.002f * settings.impulse.ads_impulse, -0.006f * settings.impulse.ads_impulse);
			state.viewmodel.impulse_rot.Add(-0.45f * settings.impulse.ads_impulse, 0.f, 0.f);
		}
		else if (!input.ads && state.prev_ads)
		{
			state.viewmodel.impulse_pos.Add(0.f, -0.002f * settings.impulse.ads_impulse, 0.006f * settings.impulse.ads_impulse);
			state.viewmodel.impulse_rot.Add(0.35f * settings.impulse.ads_impulse, 0.f, 0.f);
		}
		if (real_landing && !(state.prev_move_flags & smfLanding))
		{
			state.viewmodel.impulse_pos.Add(0.f, -0.008f * settings.impulse.land_impulse, 0.f);
			state.viewmodel.impulse_rot.Add(1.1f * settings.impulse.land_impulse, 0.f, 0.f);
		}
		if (airborne)
			state.viewmodel.airborne_time += input.dt;
		else if (!landing)
			state.viewmodel.airborne_time = 0.f;

		const float flick_yaw = Clamp(state.viewmodel.mouse_accel.x / DegToRad(9000.f), -1.f, 1.f);
		const float flick_pitch = Clamp(state.viewmodel.mouse_accel.y / DegToRad(7000.f), -1.f, 1.f);
		state.viewmodel.impulse_pos.Add(-flick_yaw * 0.004f * settings.impulse.flick_impulse, flick_pitch * 0.002f * settings.impulse.flick_impulse, 0.f);
		state.viewmodel.impulse_rot.Add(flick_pitch * 0.20f * settings.impulse.flick_impulse, -flick_yaw * 0.10f * settings.impulse.flick_impulse, -flick_yaw * 0.28f * settings.impulse.flick_impulse);
		output.impulse_pos_clamped = ClampVector(state.viewmodel.impulse_pos, std::max(settings.impulse.impulse_pos_cap, 0.f));
		output.impulse_rot_clamped = ClampVector(state.viewmodel.impulse_rot, std::max(settings.impulse.impulse_rot_cap, 0.f));

		const float impulse_decay = Clamp(1.f - std::exp(-std::max(settings.impulse.decay, 0.01f) * Clamp(input.dt, 0.f, 0.033f)), 0.f, 1.f);
		state.viewmodel.impulse_pos.Mul(1.f - impulse_decay);
		state.viewmodel.impulse_rot.Mul(1.f - impulse_decay);
		state.camera.impulse_pos.Mul(1.f - impulse_decay);
		state.camera.impulse_roll *= 1.f - impulse_decay;
	}
	if (fire_impulse_enabled)
	{
		output.impulse_pos_clamped |= ClampVector(
			state.viewmodel.fire_impulse_pos, std::max(settings.impulse.impulse_pos_cap, 0.f));
		output.impulse_rot_clamped |= ClampVector(
			state.viewmodel.fire_impulse_rot, std::max(settings.impulse.impulse_rot_cap, 0.f));
		const float impulse_decay = Clamp(
			1.f - std::exp(-std::max(settings.impulse.decay, 0.01f) * Clamp(input.dt, 0.f, 0.033f)), 0.f, 1.f);
		state.viewmodel.fire_impulse_pos.Mul(1.f - impulse_decay);
		state.viewmodel.fire_impulse_rot.Mul(1.f - impulse_decay);
	}

	UpdateSprintFovPulse(settings, state, input.dt);

	UpdateLowering(settings, state, input);
	state.ads_blend = Clamp(input.ads_blend, 0.f, 1.f);
	state.prev_move_flags = input.move_flags;
	state.prev_ads = input.ads;

	float deadzone_yaw = DegToRad(settings.camera.hip.deadzone_yaw);
	float deadzone_pitch = DegToRad(settings.camera.hip.deadzone_pitch);
	float softzone_yaw = DegToRad(settings.camera.hip.softzone_yaw);
	float softzone_pitch = DegToRad(settings.camera.hip.softzone_pitch);
	float max_yaw = DegToRad(settings.camera.hip.max_yaw);
	float max_pitch = DegToRad(settings.camera.hip.max_pitch);
	float spring_freq = std::max(settings.camera.hip.spring_freq, 0.01f);
	float spring_damping = std::max(settings.camera.hip.spring_damping, 0.f);
	float camera_roll = DegToRad(settings.camera.hip.roll);
	float camera_pos = settings.camera.hip.pos;
	float inner_gain = settings.camera.hip.inner_gain;
	float ads_mouse_mult = 1.f;
	float ads_impulse_mult = 1.f;
	if (state.ads_blend > kEpsilon)
	{
		const float ads_blend = Clamp(state.ads_blend, 0.f, 1.f);
		inner_gain = Lerp(inner_gain, settings.camera.ads.inner_gain, ads_blend);
		spring_freq = std::max(Lerp(spring_freq, settings.camera.ads.spring_freq, ads_blend), 0.01f);
		spring_damping = std::max(Lerp(spring_damping, settings.camera.ads.spring_damping, ads_blend), 0.f);
		deadzone_yaw = DegToRad(Lerp(settings.camera.hip.deadzone_yaw, settings.camera.ads.deadzone_yaw, ads_blend));
		deadzone_pitch = DegToRad(Lerp(settings.camera.hip.deadzone_pitch, settings.camera.ads.deadzone_pitch, ads_blend));
		softzone_yaw = DegToRad(Lerp(settings.camera.hip.softzone_yaw, settings.camera.ads.softzone_yaw, ads_blend));
		softzone_pitch = DegToRad(Lerp(settings.camera.hip.softzone_pitch, settings.camera.ads.softzone_pitch, ads_blend));
		max_yaw = DegToRad(Lerp(settings.camera.hip.max_yaw, settings.camera.ads.max_yaw, ads_blend));
		max_pitch = DegToRad(Lerp(settings.camera.hip.max_pitch, settings.camera.ads.max_pitch, ads_blend));
		camera_roll = DegToRad(Lerp(settings.camera.hip.roll, settings.camera.ads.roll, ads_blend));
		camera_pos = Lerp(settings.camera.hip.pos, settings.camera.ads.pos, ads_blend);
		ads_mouse_mult = Lerp(1.f, settings.viewmodel.ads_mouse_mult, ads_blend);
		ads_impulse_mult = Lerp(1.f, settings.viewmodel.ads_impulse_mult, ads_blend);
	}

	state.camera.yaw = SpringAngle(state.camera.yaw, CalcDesiredAngle(state.camera.yaw, input.target_yaw, deadzone_yaw, softzone_yaw, Clamp(inner_gain, 0.f, 1.f)), spring_freq, spring_damping, input.dt);
	state.camera.pitch = SpringAngle(state.camera.pitch, CalcDesiredAngle(state.camera.pitch, input.target_pitch, deadzone_pitch, softzone_pitch, Clamp(inner_gain, 0.f, 1.f)), spring_freq, spring_damping, input.dt);

	const float yaw_error = AngleDifferenceSigned(input.target_yaw, state.camera.yaw);
	const float pitch_error = AngleDifferenceSigned(input.target_pitch, state.camera.pitch);
	if (Abs(yaw_error) > max_yaw)
	{
		state.camera.yaw = AngleNormalizeSigned(input.target_yaw - Clamp(yaw_error, -max_yaw, max_yaw));
	}
	if (Abs(pitch_error) > max_pitch)
	{
		state.camera.pitch = AngleNormalizeSigned(input.target_pitch - Clamp(pitch_error, -max_pitch, max_pitch));
	}

	const float clamped_yaw_error = AngleDifferenceSigned(input.target_yaw, state.camera.yaw);
	const float clamped_pitch_error = AngleDifferenceSigned(input.target_pitch, state.camera.pitch);
	const float yaw_norm = max_yaw > kEpsilon ? Clamp(clamped_yaw_error / max_yaw, -1.f, 1.f) : 0.f;
	const float pitch_norm = max_pitch > kEpsilon ? Clamp(clamped_pitch_error / max_pitch, -1.f, 1.f) : 0.f;

	const float move_x = state.viewmodel.move_intent.x;
	const float move_z = state.viewmodel.move_intent.z;
	const float move_scale = input.ads ? 0.35f : 1.f;
	const float sprint_visual_amount = state.sprint.amount * SprintStrength(settings);
	const float sprint_amount = input.ads ? 0.f : sprint_visual_amount;
	const float sprint_viewmodel_amount = input.ads ? 0.f : state.sprint.viewmodel_amount * SprintStrength(settings);
	const float sprint_settle = input.ads ? 0.f : state.sprint.settle * SprintStrength(settings);
	const float sprint_pitch = DegToRad(settings.sprint.camera_pitch) * (sprint_amount + 0.35f * sprint_settle);
	const float sprint_wave = std::sin(state.sprint.phase);
	const float sprint_step = std::sin(state.sprint.phase * 2.f);
	const float sprint_side = Abs(move_x) > 0.05f ? move_x : (state.viewmodel.mouse_speed.x >= 0.f ? 1.f : -1.f);
	const float target_roll = -yaw_norm * camera_roll +
		DegToRad(settings.sprint.camera_roll) * sprint_amount * (0.35f * sprint_wave - 0.55f * move_x) -
		DegToRad(settings.sprint.camera_roll) * 0.25f * sprint_settle * sprint_side;
	state.camera.roll = SpringAngle(state.camera.roll, target_roll, spring_freq * 0.75f, spring_damping, input.dt);
	state.camera.roll = Clamp(state.camera.roll, -(Abs(camera_roll) + DegToRad(Abs(settings.camera.move_roll) + Abs(settings.sprint.camera_roll))),
		Abs(camera_roll) + DegToRad(Abs(settings.camera.move_roll) + Abs(settings.sprint.camera_roll)));

	SVec3 camera_pos_target;
	camera_pos_target.Set(-yaw_norm * camera_pos - move_x * settings.camera.move_pos * move_scale,
		pitch_norm * camera_pos * 0.55f,
		-move_z * settings.camera.move_pos * 0.65f * move_scale);
	camera_pos_target.Add(settings.sprint.camera_pos * 0.25f * sprint_wave * sprint_amount,
		settings.sprint.camera_pos * (-0.30f * sprint_amount + 0.18f * sprint_step * sprint_amount + 0.20f * sprint_settle),
		settings.sprint.camera_pos * (-0.85f * sprint_amount + 0.35f * sprint_settle));
	SpringVector(state.camera.pos, camera_pos_target, spring_freq * 0.8f, spring_damping, input.dt);
	ClampVector(state.camera.pos, Abs(camera_pos) + Abs(settings.camera.move_pos) + Abs(settings.sprint.camera_pos) * std::max(1.f, SprintStrength(settings)));

	if (hud_transform_enabled)
	{
		const float yaw_throw = CalculateMouseThrow(state.viewmodel.mouse_speed.x, DegToRad(420.f), 1.f);
		const float pitch_throw = CalculateMouseThrow(state.viewmodel.mouse_speed.y, DegToRad(320.f), 1.f);
		SVec3 vm_mouse_pos, vm_mouse_rot, vm_impulse_pos, vm_impulse_rot;
		if (vm_spring_enabled)
		{
			vm_mouse_pos.Set(-yaw_throw * settings.viewmodel.mouse_pos,
				pitch_throw * settings.viewmodel.mouse_pos * 0.45f,
				0.f);
			vm_mouse_rot.Set(pitch_throw * settings.viewmodel.mouse_rot,
				-yaw_throw * settings.viewmodel.mouse_rot * 0.62f,
				-yaw_throw * settings.viewmodel.mouse_rot * 2.70f);
		}
		if (input.ads && vm_spring_enabled)
		{
			const float anchor = Clamp(settings.viewmodel.ads_anchor, 0.f, 1.f);
			vm_mouse_pos.x += yaw_norm * settings.viewmodel.ads_anchor_pos * anchor;
			vm_mouse_pos.y -= pitch_norm * settings.viewmodel.ads_anchor_pos * 0.45f * anchor;
			vm_mouse_rot.x -= pitch_norm * settings.viewmodel.ads_anchor_rot * anchor;
			vm_mouse_rot.z += yaw_norm * settings.viewmodel.ads_anchor_rot * anchor;
		}
		vm_impulse_pos = state.viewmodel.impulse_pos;
		vm_impulse_rot = state.viewmodel.impulse_rot;
		vm_mouse_pos.Mul(ads_mouse_mult);
		vm_mouse_rot.Mul(ads_mouse_mult);
		vm_impulse_pos.Mul(ads_impulse_mult);
		vm_impulse_rot.Mul(ads_impulse_mult);
		SVec3 fire_target_pos = state.viewmodel.fire_impulse_pos;
		SVec3 fire_target_rot = state.viewmodel.fire_impulse_rot;
		fire_target_pos.Mul(ads_impulse_mult);
		fire_target_rot.Mul(ads_impulse_mult);
		SpringVector(state.viewmodel.fire_pos, fire_target_pos,
			settings.viewmodel.follow_speed, settings.viewmodel.damping, input.dt);
		SpringVector(state.viewmodel.fire_rot, fire_target_rot,
			settings.viewmodel.follow_speed, settings.viewmodel.damping, input.dt);
		ClampVector(state.viewmodel.fire_pos, std::max(settings.impulse.impulse_pos_cap, 0.f));
		ClampVector(state.viewmodel.fire_rot, std::max(settings.impulse.impulse_rot_cap, 0.f));

		SVec3 vm_sprint_pos;
		SVec3 vm_sprint_rot;
		if (sprint_bridge_active)
		{
			const float run_wave = sprint_wave * sprint_viewmodel_amount;
			const float run_step = sprint_step * sprint_viewmodel_amount;
			vm_sprint_pos.Set(-0.28f * settings.sprint.bridge_pos * run_wave,
				0.20f * settings.sprint.bridge_pos * run_step,
				-0.20f * settings.sprint.bridge_pos * sprint_viewmodel_amount);
			vm_sprint_rot.Set(settings.sprint.bridge_pitch * (-sprint_viewmodel_amount + 0.22f * run_step),
				settings.sprint.bridge_yaw * (0.45f * move_x * sprint_viewmodel_amount + 0.35f * run_wave),
				settings.sprint.bridge_roll * (-0.55f * move_x * sprint_viewmodel_amount - 0.45f * run_wave));
		}

		SVec3 vm_pos_target = vm_mouse_pos;
		vm_pos_target.Add(vm_impulse_pos);
		SVec3 vm_rot_target = vm_mouse_rot;
		vm_rot_target.Add(vm_impulse_rot);
		const float vm_limit_mult = input.ads ? std::max(0.20f, std::max(ads_mouse_mult, ads_impulse_mult)) : 1.f;
		const float impulse_pos_allowance = Clamp(vm_impulse_pos.Magnitude() * 0.75f, 0.f, std::max(settings.impulse.impulse_pos_cap, 0.f) * 0.45f);
		const float impulse_rot_allowance = Clamp(vm_impulse_rot.Magnitude() * 0.65f, 0.f, std::max(settings.impulse.impulse_rot_cap, 0.f) * 0.50f);
		const float vm_pos_limit = Abs(settings.viewmodel.max_pos) * vm_limit_mult + impulse_pos_allowance;
		const float vm_rot_limit = Abs(settings.viewmodel.max_rot) * vm_limit_mult + impulse_rot_allowance;
		ClampVector(vm_pos_target, vm_pos_limit);
		ClampVector(vm_rot_target, vm_rot_limit);
		SpringVector(state.viewmodel.pos, vm_pos_target, settings.viewmodel.follow_speed, settings.viewmodel.damping, input.dt);
		SpringVector(state.viewmodel.rot, vm_rot_target, settings.viewmodel.follow_speed, settings.viewmodel.damping, input.dt);
		ClampVector(state.viewmodel.pos, vm_pos_limit);
		ClampVector(state.viewmodel.rot, vm_rot_limit);

		SVec3 weighted_vm_pos = state.viewmodel.pos;
		SVec3 weighted_vm_rot = state.viewmodel.rot;
		SVec3 weighted_lower_pos = state.lowering.pos;
		SVec3 weighted_lower_rot = state.lowering.rot;
		weighted_vm_pos.Mul(settings.features.layer_vm_weight);
		weighted_vm_rot.Mul(settings.features.layer_vm_weight);
		vm_sprint_pos.Mul(settings.features.layer_vm_weight);
		vm_sprint_rot.Mul(settings.features.layer_vm_weight);
		weighted_vm_pos.Add(vm_sprint_pos);
		weighted_vm_rot.Add(vm_sprint_rot);
		weighted_lower_pos.Mul(settings.features.layer_lower_weight);
		weighted_lower_rot.Mul(settings.features.layer_lower_weight);
		output.viewmodel_pos = weighted_vm_pos;
		output.viewmodel_pos.Add(state.viewmodel.fire_pos);
		output.viewmodel_pos.Add(weighted_lower_pos);
		output.viewmodel_rot = weighted_vm_rot;
		output.viewmodel_rot.Add(state.viewmodel.fire_rot);
		output.viewmodel_rot.Add(weighted_lower_rot);
		output.viewmodel_active = true;
	}
	else
	{
		state.viewmodel.pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.rot.Set(0.f, 0.f, 0.f);
		state.viewmodel.impulse_pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.impulse_rot.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_impulse_pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_impulse_rot.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_pos.Set(0.f, 0.f, 0.f);
		state.viewmodel.fire_rot.Set(0.f, 0.f, 0.f);
	}
	UpdateBodycamArmLayer(settings, state, input, output);
	UpdateStalker2ArmLayer(settings, state, input, output);

	output.yaw = state.camera.yaw;
	output.pitch = state.camera.pitch + sprint_pitch;
	output.roll = state.camera.roll + state.camera.impulse_roll;
	output.camera_pos = state.camera.pos;
	output.camera_pos.Add(state.camera.impulse_pos);
	output.fov_offset = state.camera.impulse_fov;
	output.lower_amount = state.lowering.amount;
}

AuthoredMotionGains CalculateAuthoredMotionGains(const AuthoredMotionMetrics& metrics)
{
	auto deficit = [](float authored, float target)
	{
		return target > kEpsilon ? Clamp(1.f - authored / target, 0.f, 1.f) : 0.f;
	};

	const float lead_rotation_deficit = deficit(metrics.lead_rotation, DegToRad(2.f));
	const float lead_translation_deficit = deficit(metrics.lead_translation, 0.012f);
	AuthoredMotionGains gains;
	gains.wrist = deficit(metrics.wrist_rotation, DegToRad(4.f));
	gains.controller = std::max(std::min(lead_rotation_deficit, lead_translation_deficit), gains.wrist * 0.75f);
	gains.arm = 0.5f * (deficit(metrics.forearm_rotation, DegToRad(3.f)) +
		deficit(metrics.upperarm_rotation, DegToRad(2.f)));
	return gains;
}

SVec3 CalculateAuthoredWalkRotation(float phase, float weight, const AuthoredMotionGains& gains)
{
	phase -= std::floor(phase);
	weight = Clamp(weight, 0.f, 4.f);
	const float cycle = phase * 2.f * kPi;
	const float lateral = std::sin(cycle) + 0.16f * std::sin(3.f * cycle + 0.35f);
	const float step = std::sin(2.f * cycle - 0.35f) + 0.12f * std::sin(4.f * cycle + 0.20f);
	const float controller_gain = std::max(gains.controller, gains.wrist * 0.75f);
	SVec3 rotation;
	rotation.Set(
		DegToRad(1.25f * step * controller_gain * weight),
		DegToRad(0.55f * lateral * controller_gain * weight),
		DegToRad(-2.40f * lateral * controller_gain * weight));
	return rotation;
}

SVec3 CalculateAuthoredWalkTranslation(float phase, float weight, const AuthoredMotionGains& gains)
{
	phase -= std::floor(phase);
	weight = Clamp(weight, 0.f, 4.f);
	const float cycle = phase * 2.f * kPi;
	const float lateral = std::sin(cycle) + 0.16f * std::sin(3.f * cycle + 0.35f);
	const float step = std::sin(2.f * cycle - 0.35f) + 0.12f * std::sin(4.f * cycle + 0.20f);
	const float controller_gain = std::max(gains.controller, gains.wrist * 0.75f);
	SVec3 translation;
	translation.Set(
		0.0040f * lateral * controller_gain * weight,
		0.0025f * step * controller_gain * weight,
		-0.0012f * step * controller_gain * weight);
	return translation;
}

float CalculateAuthoredArmFollow(float arm_gain)
{
	return Clamp(0.30f - 0.18f * Clamp(arm_gain, 0.f, 1.f), 0.12f, 0.30f);
}

SVec3 CalculateStalker2MouseControllerRotation(float yaw_throw, float yaw_scale, float roll_scale, float weight)
{
	SVec3 rotation;
	rotation.Set(
		yaw_throw * yaw_scale * 0.35f * weight,
		0.f,
		-yaw_throw * roll_scale * weight);
	return rotation;
}

SVec3 CalculateStalker2VerticalArmFollow(float pitch_throw, float pitch_scale, float weight)
{
	SVec3 rotation;
	rotation.Set(0.f, pitch_throw * pitch_scale * weight, 0.f);
	return rotation;
}

float CalculateMouseThrow(float angular_speed, float full_scale_speed, float sensitivity)
{
	if (full_scale_speed <= kEpsilon)
		return 0.f;
	const float normalized = angular_speed / full_scale_speed * Clamp(sensitivity, 0.1f, 4.f);
	return SoftLimitMouseResponse(normalized, 1.f);
}

float SoftLimitMouseResponse(float value, float limit)
{
	limit = std::max(limit, 0.f);
	if (limit <= 0.f)
		return 0.f;

	const float normalized = value / limit;
	const float magnitude = std::fabs(normalized);
	const float denominator = std::pow(1.f + std::pow(magnitude, 8.f), 1.f / 8.f);
	return value / denominator;
}

SVec3 ClampStalker2MouseRotation(const SVec3& rotation, float max_yaw, float max_pitch, float max_roll)
{
	SVec3 result;
	result.Set(
		Clamp(rotation.x, -std::max(max_yaw, 0.f), std::max(max_yaw, 0.f)),
		Clamp(rotation.y, -std::max(max_pitch, 0.f), std::max(max_pitch, 0.f)),
		Clamp(rotation.z, -std::max(max_roll, 0.f), std::max(max_roll, 0.f)));
	return result;
}

float CalculateStalker2MovementAmount(float move_intent, float speed_fraction, bool accelerated, float slow_walk_scale)
{
	const float gait_scale = accelerated ? 1.f : Clamp(slow_walk_scale, 0.f, 1.f);
	return Clamp(move_intent, 0.f, 1.f) * Clamp(speed_fraction, 0.f, 1.f) * gait_scale;
}

void AddFireImpulse(const SimulationSettings& settings, SimulationState& state, float power, bool ads)
{
	if (settings.features.lower_enable)
	{
		state.lowering.fire_recovery = std::max(state.lowering.fire_recovery, std::max(settings.lowering.fire_timeout, 0.f));
		state.lowering.combat_timer = std::max(state.lowering.combat_timer, std::max(settings.lowering.combat_timeout, 0.f));
	}
	if (!settings.features.fire_impulse_enable)
		return;

	const float fire_impulse = ads ? settings.impulse.ads_fire_impulse : settings.impulse.fire_impulse;
	const float p = Clamp(power, 0.f, 3.f) * fire_impulse;
	if (p <= kEpsilon)
		return;

	const float side = state.viewmodel.mouse_speed.x >= 0.f ? -1.f : 1.f;
	state.viewmodel.fire_impulse_pos.Add(0.f, -0.0015f * p, -0.0040f * p);
	state.viewmodel.fire_impulse_rot.Add(-0.32f * p, 0.f, 0.10f * side * p);
	ClampVector(state.viewmodel.fire_impulse_pos, std::max(settings.impulse.impulse_pos_cap, 0.f));
	ClampVector(state.viewmodel.fire_impulse_rot, std::max(settings.impulse.impulse_rot_cap, 0.f));
}

bool AddNamedImpulse(const SimulationSettings& settings, SimulationState& state, const char* kind, float power, bool ads)
{
	if (!kind)
		return false;

	if (std::strcmp(kind, "fire") == 0)
	{
		AddFireImpulse(settings, state, power, ads);
		return true;
	}

	enum class ImpulseKind
	{
		Land,
		SprintIn,
		SprintOut,
		AdsIn,
		AdsOut,
	};

	ImpulseKind impulse;
	if (std::strcmp(kind, "land") == 0)
		impulse = ImpulseKind::Land;
	else if (std::strcmp(kind, "sprint_in") == 0)
		impulse = ImpulseKind::SprintIn;
	else if (std::strcmp(kind, "sprint_out") == 0)
		impulse = ImpulseKind::SprintOut;
	else if (std::strcmp(kind, "ads_in") == 0)
		impulse = ImpulseKind::AdsIn;
	else if (std::strcmp(kind, "ads_out") == 0)
		impulse = ImpulseKind::AdsOut;
	else
		return false;

	if (!settings.features.vm_enable)
		return true;

	const float p = Clamp(power, 0.f, 3.f);
	if (p <= kEpsilon)
		return true;

	switch (impulse)
	{
	case ImpulseKind::Land:
		state.viewmodel.impulse_pos.Add(0.f, -0.008f * p, 0.f);
		state.viewmodel.impulse_rot.Add(1.1f * p, 0.f, 0.f);
		break;
	case ImpulseKind::SprintIn:
		state.viewmodel.impulse_pos.Add(0.f, -0.004f * p, -0.010f * p);
		state.viewmodel.impulse_rot.Add(-0.8f * p, 0.f, -0.8f * p);
		break;
	case ImpulseKind::SprintOut:
		state.viewmodel.impulse_pos.Add(0.f, 0.004f * p, 0.007f * p);
		state.viewmodel.impulse_rot.Add(0.5f * p, 0.f, 0.6f * p);
		break;
	case ImpulseKind::AdsIn:
		state.viewmodel.impulse_pos.Add(0.f, 0.002f * p, -0.006f * p);
		state.viewmodel.impulse_rot.Add(-0.45f * p, 0.f, 0.f);
		break;
	case ImpulseKind::AdsOut:
		state.viewmodel.impulse_pos.Add(0.f, -0.002f * p, 0.006f * p);
		state.viewmodel.impulse_rot.Add(0.35f * p, 0.f, 0.f);
		break;
	}

	ClampVector(state.viewmodel.impulse_pos, std::max(settings.impulse.impulse_pos_cap, 0.f));
	ClampVector(state.viewmodel.impulse_rot, std::max(settings.impulse.impulse_rot_cap, 0.f));
	return true;
}
} // namespace Bodycam
