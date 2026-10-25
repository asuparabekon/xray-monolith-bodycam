#include "rotate_player.h"
#include "constants.h"
#include "scalar_math.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr double kMinYawRate = 90, kMaxYawRate = 270, kMinRotateRate = float(1.15), kMaxRotateRate = 3;
std::optional<unsigned> Select(const std::vector<std::optional<RelevantSequenceSnapshot>>& players)
{
	std::optional<unsigned> selected;
	float best = 0;
	for (unsigned i = 0; i < players.size(); ++i)
	{
		if (!players[i])
			continue;
		const auto& p = *players[i];
		if (!p.ignoreForRelevancy && p.blendWeight > best)
		{
			selected = i;
			best = p.blendWeight;
		}
	}
	return selected;
}
}
AutomaticSequenceResult AutomaticSequenceTransition(
	const std::vector<std::optional<RelevantSequenceSnapshot>>& players, double triggerTime, double duration, bool conduit)
{
	const float trigger = float(triggerTime), crossfade = float(duration);
	AutomaticSequenceResult result;
	result.playerIndex = Select(players);
	if (!result.playerIndex)
		return result;
	const auto& p = *players[*result.playerIndex];
	if (!p.assetLength)
		return result;
	float remaining = *p.assetLength - p.accumulator;
	if (p.looping && remaining > 0 && p.history && p.history->valid)
	{
		const auto& h = *p.history;
		if ((p.accumulator - h.previousTime) * h.delta < 0)
		{
			remaining = 0;
			result.wrapped = true;
		}
	}
	const float threshold = trigger < 0 ? (conduit ? 0 : crossfade) : trigger;
	result.timeOffset = threshold - remaining;
	result.remaining = remaining;
	result.canTake = *result.timeOffset >= 0;
	return result;
}
float RelevantSequenceRemaining(const std::vector<std::optional<RelevantSequenceSnapshot>>& players)
{
	const auto index = Select(players);
	if (!index || !players[*index]->assetLength)
		return std::numeric_limits<float>::max();
	const auto& p = *players[*index];
	const float length = *p.assetLength;
	const float adjusted = p.effectiveSignedRate < 0 ? length - p.accumulator : p.accumulator;
	return length - adjusted;
}
void RotateInPlaceLatch::UpdateStationary(unsigned view, unsigned rotation, bool tablet, double angle, double yawRate)
{
	if (!((view == 1 && !tablet) || rotation == 2))
	{
		ClearFlags();
		return;
	}
	if (!std::isfinite(angle) || !std::isfinite(yawRate))
		throw std::invalid_argument("Aiming angle and yaw rate must be finite");
	left_ = angle < -kRotateAngle;
	right_ = angle > kRotateAngle;
	if (left_ || right_)
		rate_ = MapRangeClamped(yawRate, kMinYawRate, kMaxYawRate, kMinRotateRate, kMaxRotateRate);
}
RotateNodeInput RotateInPlaceLatch::NodeInputs(bool left) const
{
	return { float(rate_), left ? left_ : right_ };
}
void RotateSequencePlayer::CopyInputs(RotateNodeInput input)
{
	state_.playRate = input.playRate;
	state_.looping = input.looping;
}
void RotateSequencePlayer::Initialize(RotateNodeInput input)
{
	state_.previousIndex = state_.nextIndex = kNoMarker;
	state_.relevanceFlag = false;
	CopyInputs(input);
	state_.accumulator = 0;
	state_.rateClampInitialized = true;
}
RotateSequenceRecord RotateSequencePlayer::Update(double weight, double rootWeight, RotateNodeInput input)
{
	const float w = float(weight), root = float(rootWeight);
	state_.blendWeight = w;
	state_.relevanceFlag = state_.relevanceFlag || w >= kBlendFull;
	CopyInputs(input);
	state_.rateClampInitialized = true;
	return { nodeId_, state_.playRate, w, root, state_.looping };
}
void RotateSequencePlayer::Tick(const RotateSequenceRecord& record, double seconds)
{
	const float dt = float(seconds);
	const float previous = state_.accumulator, delta = record.rate * dt;
	if (delta != 0)
	{
		float time = previous + delta;
		if (record.loop)
		{
			if (time < 0 || time > 1)
			{
				time = std::fmod(time, 1.f);
				if (time < 0)
					time += 1;
			}
		}
		else
			time = std::clamp(time, 0.f, 1.f);
		state_.accumulator = time;
	}
	state_.history = { previous, delta, true };
}
RelevantSequenceSnapshot RotateSequencePlayer::RelevantSnapshot() const
{
	RelevantSequenceSnapshot snapshot;
	snapshot.looping = state_.looping;
	snapshot.blendWeight = state_.blendWeight;
	snapshot.accumulator = state_.accumulator;
	// The rotate clock runs over a normalised length of one.
	snapshot.assetLength = 1.f;
	snapshot.history = state_.history;
	return snapshot;
}
}
