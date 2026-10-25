#include "locomotion_marker_tick.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
template <class T>
bool Valid(const T& s)
{
	return s.previous.index >= 0 && s.next.index >= 0;
}
MarkerState State(const BlendMarkerSample& s)
{
	return { s.time, { s.previous.index, s.previous.offset.value() }, { s.next.index, s.next.offset.value() } };
}
void Assign(BlendMarkerSample& sample, MarkerState state)
{
	sample.time = state.time;
	sample.previous = { state.previous.index, state.previous.offset };
	sample.next = { state.next.index, state.next.offset };
}
}
BlendMarkerResult TickLocomotionMarkers(std::vector<BlendMarkerSample> samples, const std::map<std::string, MarkerSequence>& sequences,
	BlendMarkerNode node, CandidateContext context, double historyDelta)
{
	const float history = float(historyDelta);
	std::map<std::string, PositiveMarkerClock> clocks;
	for (const auto& s : samples)
	{
		const auto& q = sequences.at(s.sequenceIdentity);
		for (const auto& name : q.names)
			if (std::find(context.validNames.begin(), context.validNames.end(), name) == context.validNames.end())
				throw std::invalid_argument("Sequence marker names are not the sync group's marker names");
		clocks.emplace(s.sequenceIdentity, PositiveMarkerClock(q.length, q.names, q.times));
	}
	if (context.resync)
		node.normalizedTime = context.currentRatio;
	unsigned chosen = 0;
	if (!context.follower)
	{
		// A weight at or below the smallest normal float never leads.
		float best = std::numeric_limits<float>::min();
		bool found = false;
		for (unsigned i = 0; i < samples.size(); ++i)
		{
			const float w = std::clamp(float(samples[i].weight), 0.f, 1.f);
			if (w > best)
			{
				best = w;
				chosen = i;
				found = true;
			}
		}
		if (!found)
			throw std::invalid_argument("Unsupported: no marker sample has a positive weight");
	}
	else if (samples.empty())
		throw std::invalid_argument("Unsupported: a follower has no marker sample");
	else
		for (unsigned i = 1; i < samples.size(); ++i)
			if (std::clamp(samples[i].weight, 0.0, 1.0) > std::clamp(samples[chosen].weight, 0.0, 1.0))
				chosen = i;
	auto& selected = samples[chosen];
	const auto& sequence = sequences.at(selected.sequenceIdentity);
	const auto& clock = clocks.at(selected.sequenceIdentity);
	std::vector<BlendMarkerEvent> events;
	const auto mapStart = [&](BlendMarkerSample& s) { Assign(s, clocks.at(s.sequenceIdentity).Map(context.markers.start.value())); };
	const auto follow = [&](unsigned index)
	{
		auto& s = samples[index];
		if (!Valid(s))
			mapStart(s);
		s.previousTime = s.time;
		Assign(s,
			clocks.at(s.sequenceIdentity)
				.Follower(State(s), { context.markers.start.value(), context.markers.end.value(), context.markers.passed }));
		events.push_back({ "follower", index });
	};
	if (!context.follower)
	{
		context.previousRatio = node.normalizedTime;
		const bool wasValid = Valid(node);
		bool reinitialized = false;
		if (!wasValid)
		{
			selected.previous = {};
			selected.next = {};
			selected.time = sequence.length * node.normalizedTime;
			reinitialized = true;
		}
		else if (!Valid(selected) && context.markers.start)
			mapStart(selected);
		context.movement = (float(selected.rateScale) * history) * sequence.rateScale;
		if (context.movement > 1e-8f)
		{
			if (!Valid(selected))
			{
				if (context.markers.start)
					mapStart(selected);
				else
					Assign(selected, clock.Lookup(selected.time));
			}
			context.markers.start = clock.Position(State(selected));
			selected.previousTime = selected.time;
			const auto advanced = clock.Leader(State(selected), context.movement);
			Assign(selected, advanced.state);
			context.markers.passed.insert(context.markers.passed.end(), advanced.passed.begin(), advanced.passed.end());
			context.markers.end = clock.Position(State(selected));
			events.push_back({ "leader", chosen });
			for (unsigned i = 0; i < samples.size(); ++i)
				if (i != chosen)
				{
					if (reinitialized)
					{
						samples[i].previous = {};
						samples[i].next = {};
					}
					follow(i);
				}
		}
		else if (!wasValid)
		{
			Assign(selected, clock.Lookup(selected.time));
			context.markers.start = clock.Position(State(selected));
			context.markers.end = context.markers.start;
			events.push_back({ "tiny-initialize", chosen });
			for (unsigned i = 0; i < samples.size(); ++i)
				if (i != chosen)
				{
					samples[i].previous = {};
					samples[i].next = {};
					follow(i);
				}
		}
		node.normalizedTime = selected.time / sequence.length;
		node.previous = selected.previous;
		node.next = selected.next;
		context.currentRatio = node.normalizedTime;
	}
	else
	{
		if (context.deltaTime != 0)
		{
			if (!Valid(node))
				selected.time = sequence.length * node.normalizedTime;
			for (unsigned i = 0; i < samples.size(); ++i)
				follow(i);
		}
		node.normalizedTime = selected.time / sequence.length;
		node.previous = selected.previous;
		node.next = selected.next;
	}
	return { std::move(samples), node, std::move(context), chosen, std::move(events) };
}
}
