#pragma once
#include <array>
#include <string>
#include <vector>
namespace BodycamReference
{
// Marker pair index before a marker is resolved; -1 stands for the sequence start or end.
constexpr int kNoMarker = -2;
struct MarkerPair
{
	int index;
	float offset;
};
struct MarkerState
{
	float time;
	MarkerPair previous, next;
};
struct MarkerPosition
{
	std::string previousName, nextName;
	float fraction;
};
struct PassedMarker
{
	std::string name;
	float remainingMovement;
};
struct MarkerAdvance
{
	MarkerState state;
	std::vector<PassedMarker> passed;
};
struct MarkerContext
{
	MarkerPosition start, end;
	std::vector<PassedMarker> passed;
};
// Sync-marker clock of a sequence with two markers, played forwards and not mirrored.
class PositiveMarkerClock
{
public:
	PositiveMarkerClock(float length, std::array<std::string, 2> names, std::array<float, 2> times);
	MarkerState Lookup(float time) const;
	MarkerPosition Position(MarkerState) const;
	MarkerState Map(const MarkerPosition&) const;
	MarkerAdvance Leader(MarkerState, float movement) const;
	MarkerState Follower(MarkerState, const MarkerContext&) const;

private:
	unsigned Find(const std::string&, unsigned start = 0) const;
	float length_;
	std::array<std::string, 2> names_;
	std::array<float, 2> times_;
};
}
