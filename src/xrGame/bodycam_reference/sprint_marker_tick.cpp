#include "sprint_marker_tick.h"
namespace BodycamReference
{
SprintMarkerResult TickSprintMarkers(
	const std::array<std::string, 2>& names, std::array<float, 2> times, MarkerState state, double recordRate, CandidateContext context)
{
	constexpr float length = kSprintCycleSeconds;
	const float rate = float(recordRate);
	PositiveMarkerClock clock(length, names, times);
	const auto valid = [](MarkerState s) { return s.previous.index >= 0 && s.next.index >= 0; };
	float previousTime = state.time, movement = 0;
	if (context.resync)
	{
		state.time = length * context.currentRatio;
		previousTime = state.time;
	}
	if (!context.follower)
	{
		movement = rate * context.deltaTime;
		context.movement = movement;
		context.previousRatio = previousTime / length;
		if (movement != 0)
		{
			if (!valid(state))
				state = context.markers.start ? clock.Map(*context.markers.start) : clock.Lookup(state.time);
			context.markers.start = clock.Position(state);
			previousTime = state.time;
			const auto advanced = clock.Leader(state, movement);
			state = advanced.state;
			context.markers.passed.insert(context.markers.passed.end(), advanced.passed.begin(), advanced.passed.end());
			context.markers.end = clock.Position(state);
		}
		else if (!valid(state))
			state = clock.Lookup(state.time);
		context.currentRatio = state.time / length;
	}
	else
	{
		if (!valid(state))
		{
			state = clock.Map(*context.markers.start);
			previousTime = state.time;
		}
		state = clock.Follower(state, { *context.markers.start, *context.markers.end, context.markers.passed });
		if (state.time != previousTime)
		{
			movement = state.time - previousTime;
			if (rate * movement < 0)
				movement += length;
		}
	}
	return { state, { previousTime, movement, true }, std::move(context), true };
}
}
