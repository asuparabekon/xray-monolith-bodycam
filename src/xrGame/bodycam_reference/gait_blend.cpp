#include "gait_blend.h"
#include "constants.h"
#include <algorithm>
#include <cmath>

namespace BodycamReference
{
float CubicRangeAlpha(float x)
{
	const float square = x * x, twice = x + x;
	return std::clamp(square * 3.f - twice * square, 0.f, 1.f);
}
void CubicRangeBlend::SetRange(float first, float last)
{
	begin = first;
	end = last;
	rangeDirty_ = true;
	endpointDirty_ = true;
}
void CubicRangeBlend::SetDuration(double seconds)
{
	duration = float(std::max(seconds, 0.0));
	durationDirty_ = true;
}
float CubicRangeBlend::Mapped(float input) const
{
	return (end - begin) * MapAlpha(input) + begin;
}
float CubicRangeBlend::Update(double seconds)
{
	const float dt = float(seconds);
	if (rangeDirty_)
	{
		alpha = begin == end ? 1.f : std::clamp((value - begin) / (end - begin), 0.f, 1.f);
		value = Mapped(alpha);
		rangeDirty_ = false;
	}
	if (durationDirty_)
	{
		if (duration <= 0)
		{
			remaining = 0;
			alpha = 1;
			value = Mapped(alpha);
		}
		else
			remaining = std::abs(1.f - alpha) * duration;
		durationDirty_ = false;
	}
	if (endpointDirty_)
	{
		target = Mapped(1);
		endpointDirty_ = false;
	}
	if (value == target)
		return value;
	const float oldRemaining = remaining;
	if (oldRemaining > dt)
	{
		remaining = oldRemaining - dt;
		alpha = std::clamp(((1.f - alpha) / oldRemaining) * dt + alpha, 0.f, 1.f);
	}
	else
	{
		remaining = 0;
		alpha = 1;
	}
	value = Mapped(alpha);
	return value;
}
void GaitBlendList::Initialize()
{
	lastIndex_ = -1;
	duration_ = 0;
	weights_ = { 1, 0 };
	blends_.fill(CubicRangeBlend{});
	if (policy_ != Policy::Gait)
		for (auto& blend : blends_)
			blend = CubicRangeBlend([](float alpha) { return alpha; });
	blends_[0].alpha = 1;
}
GaitBlendTrace GaitBlendList::Update(unsigned gait, double dt, double parentWeight, bool activeBranch)
{
	const unsigned selected = policy_ == Policy::Gait ? 2 : policy_ == Policy::LeftHip ? 5 : 3;
	const unsigned index = gait == selected ? 1 : 0;
	const int previous = lastIndex_;
	GaitBlendTrace trace{};
	trace.index = index;
	trace.changed = int(index) != previous;
	if (trace.changed)
	{
		const float distance = std::abs(1.f - weights_[index]);
		const float transitionTime =
			policy_ == Policy::Gait ? (index == 0 ? .3f : .2f) : (policy_ == Policy::RightHip && index == 1 ? .1f : 0.f);
		duration_ = previous == -1 ? 0 : distance * transitionTime;
		if (duration_ == 0 && previous != -1)
			trace.updates.push_back({ unsigned(previous), 0, activeBranch, true });
		for (unsigned i = 0; i < 2; ++i)
		{
			blends_[i].SetDuration(duration_);
			blends_[i].SetRange(weights_[i], i == index ? 1.f : 0.f);
		}
		lastIndex_ = int(index);
	}
	float sum = 0;
	for (unsigned i = 0; i < 2; ++i)
	{
		weights_[i] = blends_[i].Update(dt);
		sum += weights_[i];
	}
	trace.normalized = sum > kBlendEpsilon && std::abs(sum - 1.f) > kBlendEpsilon;
	if (trace.normalized)
	{
		const float reciprocal = 1.f / sum;
		for (auto& weight : weights_)
			weight *= reciprocal;
	}
	for (unsigned i = 0; i < 2; ++i)
		if (weights_[i] > kBlendEpsilon)
			trace.updates.push_back({ i, float(parentWeight) * weights_[i], i == index ? activeBranch : false, false });
	trace.duration = duration_;
	trace.sum = sum;
	trace.weights = weights_;
	trace.internalValues = { blends_[0].value, blends_[1].value };
	return trace;
}
std::vector<SprintMaskContext> DispatchSprintMask(double mask, double parentWeight)
{
	const float alpha = std::clamp(float(mask), 0.f, 1.f), parent = float(parentWeight);
	const bool belowFull = alpha < kBlendFull;
	const bool aboveZero = alpha > kBlendEpsilon;
	if (belowFull && aboveZero)
		return { { false, parent * (1.f - alpha) }, { true, parent * alpha } };
	return { { !belowFull, parent } };
}
void ForwardGaitDispatch::InitializeMask(const std::function<void()>& initializeChildren)
{
	initializeChildren();
	maskRelevance_ = { false, false };
}
ForwardGaitTrace ForwardGaitDispatch::Update(unsigned gait, double dt, double parentWeight, double mask, bool activeBranch)
{
	const auto contexts = DispatchSprintMask(mask, parentWeight);
	maskAlpha_ = std::clamp(float(mask), 0.f, 1.f);
	maskRelevance_ = { false, false };
	ForwardGaitTrace trace;
	for (const auto& context : contexts)
		maskRelevance_[context.branchB ? 1 : 0] = true;
	for (const auto& context : contexts)
	{
		if (!context.branchB)
		{
			trace.gait = gait_.Update(gait, dt, context.weight, activeBranch);
			for (const auto& u : trace.gait->updates)
				trace.queues[u.child].push_back({ u.weight, u.activeBranch, false, u.child, u.outgoingZero });
		}
		else
			trace.queues[0].push_back({ context.weight, activeBranch, true, std::nullopt, false });
	}
	for (unsigned i = 0; i < 2; ++i)
		for (const auto& context : trace.queues[i])
			if (!trace.winners[i] || context.weight > trace.winners[i]->weight)
				trace.winners[i] = context;
	return trace;
}
}
