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

struct SprintHudState
{
	bool active = false;
	bool ready = true;
	bool changed = false;
	bool notify = false;
};

inline SprintHudState ResolveSprintHudState(bool current_active, bool raw_sprint,
	bool transition_owned, bool ads, bool moving, float speed_fraction, float handoff_speed)
{
	SprintHudState state;
	state.ready = !transition_owned || !raw_sprint ||
		(!ads && speed_fraction >= handoff_speed);
	state.active = raw_sprint && state.ready;
	state.changed = state.active != current_active;

	// A full stop already sends the generic movement callback. Let that callback
	// restart the HUD mixer instead of replaying the same animation twice.
	state.notify = state.changed && (state.active || moving);
	return state;
}

void ResetMovementResponse(MovementResponseState& state);
MovementResponseOutput UpdateMovementResponse(const MovementResponseSettings& settings, MovementResponseState& state, const MovementResponseInput& input);
} // namespace Bodycam
