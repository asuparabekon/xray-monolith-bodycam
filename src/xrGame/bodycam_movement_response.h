#pragma once

namespace Bodycam
{
struct MovementResponseSettings;

struct MovementResponseState
{
	bool initialized = false;
	Fvector accel = { 0.f, 0.f, 0.f };
	float target_speed = 0.f;
	float actual_speed = 0.f;
	float speed_fraction = 0.f;
};

struct MovementResponseInput
{
	float dt = 0.f;
	u32 move_flags = 0;
	bool ads = false;
	Fvector target_accel = { 0.f, 0.f, 0.f };
};

struct MovementResponseOutput
{
	Fvector accel = { 0.f, 0.f, 0.f };
	float target_speed = 0.f;
	float actual_speed = 0.f;
	float speed_fraction = 0.f;
	bool active = false;
};

void ResetMovementResponse(MovementResponseState& state);
MovementResponseOutput UpdateMovementResponse(const MovementResponseSettings& settings, MovementResponseState& state, const MovementResponseInput& input);
} // namespace Bodycam
