#pragma once
#include "marker_clock.h"
#include <optional>
#include <vector>

namespace BodycamReference
{
struct SequenceDeltaHistory
{
	float previousTime = 0, delta = 0;
	bool valid = false;
};
struct RelevantSequenceSnapshot
{
	bool ignoreForRelevancy = false, looping = false;
	float blendWeight = 0, accumulator = 0, effectiveSignedRate = 1;
	std::optional<float> assetLength;
	std::optional<SequenceDeltaHistory> history;
};
struct AutomaticSequenceResult
{
	bool canTake = false, wrapped = false;
	std::optional<float> timeOffset, remaining;
	std::optional<unsigned> playerIndex;
};
AutomaticSequenceResult AutomaticSequenceTransition(
	const std::vector<std::optional<RelevantSequenceSnapshot>>& ordered, double triggerTime, double crossfadeDuration, bool nextIsConduit);
float RelevantSequenceRemaining(const std::vector<std::optional<RelevantSequenceSnapshot>>& ordered);
// Rotation in place starts beyond this aim angle, in degrees, at a rate set by the aim yaw rate.
constexpr double kRotateAngle = 50;
struct RotateNodeInput
{
	float playRate;
	bool looping;
};
class RotateInPlaceLatch
{
public:
	void ClearFlags() { left_ = right_ = false; }
	void UpdateStationary(unsigned viewMode, unsigned rotationMode, bool tablet, double aimingAngle, double aimYawRate);
	RotateNodeInput NodeInputs(bool left) const;
	bool Left() const { return left_; }
	bool Right() const { return right_; }
	double Rate() const { return rate_; }

private:
	bool left_ = false, right_ = false;
	double rate_ = 1;
};
struct RotateSequenceState
{
	float accumulator = 0, blendWeight = 0, playRate = 0;
	int previousIndex = kNoMarker, nextIndex = kNoMarker;
	SequenceDeltaHistory history;
	bool relevanceFlag = false, rateClampInitialized = false, looping = false;
};
struct RotateSequenceRecord
{
	unsigned nodeId;
	float rate, weight, rootMotionWeight;
	bool loop;
};
class RotateSequencePlayer
{
public:
	explicit RotateSequencePlayer(unsigned nodeId) : nodeId_(nodeId) {}
	void Initialize(RotateNodeInput input);
	RotateSequenceRecord Update(double weight, double rootMotionWeight, RotateNodeInput input);
	void Tick(const RotateSequenceRecord&, double dt);
	RelevantSequenceSnapshot RelevantSnapshot() const;
	const RotateSequenceState& State() const { return state_; }
	void ResetBlendWeight() { state_.blendWeight = 0; }

private:
	void CopyInputs(RotateNodeInput input);
	unsigned nodeId_;
	RotateSequenceState state_{};
};
}
