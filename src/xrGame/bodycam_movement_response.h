#pragma once
// Movement inertia: the actor's control acceleration eases toward what the
// movement keys ask for, which gives starts, stops and turns a duration.

namespace Bodycam
{
struct MovementInertiaSettings;

struct MovementInertiaState
{
	bool initialized = false;
	Fvector accel = { 0.f, 0.f, 0.f };
};

struct MovementInertiaInput
{
	float dt = 0.f;
	u32 move_flags = 0;
	bool ads = false;
	Fvector target_accel = { 0.f, 0.f, 0.f };
};

struct MovementInertiaOutput
{
	Fvector accel = { 0.f, 0.f, 0.f };
	float target_speed = 0.f; // m/s
	float filtered_speed = 0.f; // m/s, the commanded speed after inertia
	float speed_fraction = 0.f; // Filtered over target, 0..1.
};

MovementInertiaOutput UpdateMovementInertia(
	const MovementInertiaSettings& settings, MovementInertiaState& state, const MovementInertiaInput& input);
} // namespace Bodycam
