#include "marker_clock.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
MarkerState RangeTime(float length, const float* times, int previous, int next, float fraction)
{
	float p = previous == -1 ? 0 : times[previous];
	const float n = next == -1 ? length : times[next];
	if (p >= n)
		p -= length;
	float time = (n - p) * fraction + p;
	const float po = p - time, no = n - time;
	if (time < 0)
		time += length;
	time = time <= length ? time : length;
	time = time > 0 ? time : 0;
	return { time, { previous, po }, { next, no } };
}
}
PositiveMarkerClock::PositiveMarkerClock(float length, std::array<std::string, 2> names, std::array<float, 2> times)
	: length_(length), names_(std::move(names)), times_(times)
{
	if (!(length > 0) || !std::isfinite(length) || names_[0] == names_[1] || !std::isfinite(times[0]) || !std::isfinite(times[1]) ||
		times[0] < 0 || !(times[0] < times[1]) || !(times[1] < length))
		throw std::invalid_argument("Marker sequence needs a positive length and two distinct markers in ascending time within it");
}
unsigned PositiveMarkerClock::Find(const std::string& name, unsigned start) const
{
	for (unsigned i = 0; i < 2; ++i)
	{
		const auto index = (start + i) % 2;
		if (names_[index] == name)
			return index;
	}
	throw std::invalid_argument("Marker name is not in the sequence");
}
MarkerState PositiveMarkerClock::Lookup(float t) const
{
	MarkerState out{ t, { -1, -t }, { -1, length_ - t } };
	for (int cycle = -1; cycle <= 1; ++cycle)
		for (int i = 0; i < 2; ++i)
		{
			const float candidate = length_ * float(cycle) + times_[i];
			const MarkerPair pair{ i, candidate - t };
			if (candidate < t)
				out.previous = pair;
			else
			{
				out.next = pair;
				return out;
			}
		}
	throw std::logic_error("Marker lookup found no next marker");
}
MarkerPosition PositiveMarkerClock::Position(MarkerState s) const
{
	float p = times_[s.previous.index], n = times_[s.next.index], t = s.time;
	if (p > n)
	{
		if (p > t)
			p -= length_;
		if (n < t)
			n += length_;
	}
	else if (p > t)
		t += length_;
	if (p == n)
		p -= length_;
	return { names_[s.previous.index], names_[s.next.index], (t - p) / (n - p) };
}
MarkerState PositiveMarkerClock::Map(const MarkerPosition& position) const
{
	const unsigned previous = Find(position.previousName), next = Find(position.nextName);
	if (previous == next)
		throw std::invalid_argument("Marker position names the same marker twice");
	return RangeTime(length_, times_.data(), int(previous), int(next), position.fraction);
}
MarkerAdvance PositiveMarkerClock::Leader(MarkerState s, float movement) const
{
	MarkerAdvance out{ s, {} };
	auto& state = out.state;
	state.time = std::clamp(state.time, 0.f, length_);
	float remaining = movement;
	while (remaining > state.next.offset)
	{
		const int crossed = state.next.index;
		state.time = times_[crossed];
		remaining -= state.next.offset;
		state.previous = { crossed, 0 };
		out.passed.push_back({ names_[crossed], remaining });
		const int next = (crossed + 1) % 2;
		state.next = { next, ((next == 0 ? length_ : 0.f) + times_[next]) - state.time };
	}
	state.time = std::fmod(remaining + state.time, length_);
	if (state.time < 0)
		state.time += length_;
	state.next.offset -= remaining;
	state.previous.offset -= remaining;
	return out;
}
MarkerState PositiveMarkerClock::Follower(MarkerState s, const MarkerContext& context) const
{
	s = Map(context.start);
	for (const auto& e : context.passed)
	{
		s.previous.index = s.next.index;
		s.next.index = int(Find(e.name, unsigned(s.next.index)));
	}
	if (!context.passed.empty())
	{
		s.previous.index = s.next.index;
		s.next.index = int(Find(context.end.nextName, unsigned(s.next.index)));
	}
	return RangeTime(length_, times_.data(), s.previous.index, s.next.index, context.end.fraction);
}
}
