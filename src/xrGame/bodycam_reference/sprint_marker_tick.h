#pragma once
#include "group_candidates.h"
namespace BodycamReference
{
// Length of the sprint cycle; its marker clock wraps here.
constexpr float kSprintCycleSeconds = .6f;
struct SequenceMarkerHistory
{
	float previousTime = 0, delta = 0;
	bool valid = false;
};
struct SprintMarkerResult
{
	MarkerState state;
	SequenceMarkerHistory history;
	CandidateContext context;
	bool notifyRootMotionPending = true;
};
SprintMarkerResult TickSprintMarkers(
	const std::array<std::string, 2>& names, std::array<float, 2> times, MarkerState state, double recordRate, CandidateContext context);
}
