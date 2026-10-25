#include "blendspace_fir.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// Shorter steps leave the output unchanged.
constexpr float kMinStepSeconds = 1e-4f;
constexpr unsigned kInitialSlots = 10, kSlotGrowth = 5;
// Past kRebaseAfter the clock and the sample times shift back by kRebaseOffset so float ages keep their precision.
// The shift is exact for times in [kRebaseOffset / 2, 2 * kRebaseOffset], so ages below the limit are unchanged.
constexpr float kRebaseAfter = 4096.f, kRebaseOffset = 2048.f;
}
BlendSpaceFir::BlendSpaceFir(float windowDuration, FirType type) : window_(windowDuration), type_(type)
{
	if (!std::isfinite(window_) || window_ < 0)
		throw std::invalid_argument("FIR window must be finite and not negative");
}
void BlendSpaceFir::Reset(float lastOutput)
{
	lastOutput_ = lastOutput;
	currentTime_ = 0;
	stackIndex_ = 0;
	data_.clear();
}
float BlendSpaceFir::Weight(float age) const
{
	const float x = age / window_;
	switch (type_)
	{
	case FirType::Average: return 1;
	case FirType::Linear: return 1.f - x;
	case FirType::Cubic: return 1.f - x * x * x;
	case FirType::EaseInOut:
	{
		const float centered = x - .5f;
		return 1.f - (centered * centered) * 4.f;
	}
	}
	throw std::logic_error("Invalid FIR configuration");
}
float BlendSpaceFir::Update(float input, float deltaTime)
{
	if (!(window_ > 0))
	{
		lastOutput_ = input;
		return input;
	}
	if (deltaTime <= kMinStepSeconds)
		return lastOutput_;
	currentTime_ += deltaTime;
	if (currentTime_ > kRebaseAfter)
	{
		currentTime_ -= kRebaseOffset;
		for (auto& sample : data_)
			if (sample.time > 0)
				sample.time = std::max(sample.time - kRebaseOffset, std::numeric_limits<float>::min());
	}
	if (data_.empty())
	{
		data_.resize(kInitialSlots);
		stackIndex_ = 0;
	}
	else
		for (auto& sample : data_)
			if (currentTime_ - sample.time > window_)
				sample.time = 0;
	unsigned slot = stackIndex_;
	if (data_[slot].time > 0)
	{
		unsigned found = unsigned(data_.size());
		for (unsigned count = 0; count < data_.size(); ++count)
		{
			const unsigned candidate = (slot + count) % unsigned(data_.size());
			if (!(data_[candidate].time > 0))
			{
				found = candidate;
				break;
			}
		}
		slot = found;
		if (found == data_.size())
			data_.resize(data_.size() + kSlotGrowth);
	}
	stackIndex_ = slot;
	data_[slot] = { input, currentTime_ };
	float sumWeight = 0, sumInput = 0;
	for (const auto& sample : data_)
	{
		if (!(sample.time > 0))
			continue;
		const float age = currentTime_ - sample.time;
		if (age > window_)
			continue;
		const float weight = Weight(age);
		if (!(weight > 0))
			continue;
		sumWeight += weight;
		sumInput += weight * sample.input;
	}
	lastOutput_ = sumWeight > 0 ? sumInput / sumWeight : 0;
	stackIndex_ = (slot + 1) % unsigned(data_.size());
	return lastOutput_;
}
}
