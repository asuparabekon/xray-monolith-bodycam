#include "rifle_local_pose.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
AnimationCurves SampleRifleFoldedCurves(const std::vector<RifleFoldedCurve>& curves, float time)
{
	AnimationCurves output;
	for (const auto& curve : curves)
	{
		if (curve.keys.empty())
			throw std::invalid_argument("Rifle curve has no keys");
		unsigned key = 0;
		while (key + 1 < curve.keys.size() && curve.keys[key + 1].time <= time)
			++key;
		const auto& a = curve.keys[key];
		const auto* b = key + 1 < curve.keys.size() ? &curve.keys[key + 1] : nullptr;
		if ((time < curve.keys.front().time && !curve.constantBefore) || (!b && time > a.time && !curve.constantAfter))
			throw std::invalid_argument("Rifle curve is sampled outside its keys without constant extrapolation");
		if (!b || time == a.time || a.mode == 0 || (a.mode == 1 && a.value == b->value))
			output[curve.name] = a.value;
		else
			throw std::invalid_argument("Rifle curve is sampled between keys that do not hold a constant value");
	}
	return output;
}
std::array<RiflePoseSample, 4> SampleRifle(
	const MovingRifleStepPose& pose, const RifleTranslationRetarget& retarget, const std::vector<RifleFoldedCurve>& curves)
{
	std::array<RiflePoseSample, 4> out;
	for (unsigned i = 0; i < 4; ++i)
		out[i] = { retarget.Apply(pose.SampleTime(kRifleSampleTimes[i])), SampleRifleFoldedCurves(curves, kRifleSampleTimes[i]) };
	return out;
}
std::vector<LocalPose> Mix(const std::vector<LocalPose>& a, const std::vector<LocalPose>& b, float alpha)
{
	if (alpha == 0)
		return a;
	if (alpha == 1)
		return b;
	std::vector<LocalPose> result;
	result.reserve(a.size());
	for (unsigned i = 0; i < a.size(); ++i)
		result.push_back(BlendAnimationLocal(a[i], b[i], alpha));
	return result;
}
}
RifleLocalPose::RifleLocalPose(std::vector<int> parents, unsigned spine, std::vector<LocalPose> cclamp, const MovingRifleStepPose& pose,
	const RifleTranslationRetarget& retarget, const std::vector<RifleFoldedCurve>& curves)
	: RifleLocalPose(std::move(parents), spine, retarget.Apply(std::move(cclamp)), SampleRifle(pose, retarget, curves))
{
}
RifleLocalPose::RifleLocalPose(
	std::vector<int> parents, unsigned spine, std::vector<LocalPose> cclamp, std::array<RiflePoseSample, 4> samples)
	: parents_(std::move(parents)), cclamp_(std::move(cclamp)), samples_(std::move(samples))
{
	if (spine >= parents_.size() || cclamp_.size() != parents_.size())
		throw std::invalid_argument("Rifle C-clamp pose or spine index does not match the skeleton");
	for (const auto& sample : samples_)
		if (sample.pose.size() != parents_.size())
			throw std::invalid_argument("Sampled rifle pose does not match the skeleton");
	mask_.resize(parents_.size());
	for (unsigned i = 0; i < parents_.size(); ++i)
		mask_[i] = i == spine || (parents_[i] >= 0 && mask_[parents_[i]]);
	rotations_ = Rotations(cclamp_);
}
std::vector<Quaternion> RifleLocalPose::Rotations(const std::vector<LocalPose>& pose) const
{
	std::vector<Quaternion> result;
	result.reserve(pose.size());
	for (unsigned i = 0; i < pose.size(); ++i)
		result.push_back(
			NormalizeQuaternion(parents_[i] < 0 ? pose[i].rotation : QuaternionProduct(result[parents_[i]], pose[i].rotation)));
	return result;
}
RifleLocalResult RifleLocalPose::Evaluate(double weightGait, double basePoseClf, double sprintWeight) const
{
	const float clf = float(basePoseClf), gait = std::clamp(float(weightGait), 0.f, 1.f),
				sprint = std::clamp(float(sprintWeight), 0.f, 1.f);
	auto a = Mix(samples_[0].pose, samples_[1].pose, gait);
	auto curves = BlendAnimationCurves(samples_[0].curves, samples_[1].curves, gait);
	a = Mix(a, samples_[2].pose, clf);
	curves = BlendAnimationCurves(curves, samples_[2].curves, clf);
	curves["Layering_Head_Add"] = 0;
	curves["Layering_Spine_Add"] = .3f * sprint;
	curves["Enable_SpineRotation"] = 1.f + (.4f - 1.f) * sprint;
	const auto base = Rotations(a);
	for (unsigned i = 0; i < a.size(); ++i)
		if (mask_[i])
		{
			const int parent = parents_[i];
			const auto p = parent < 0 ? Quaternion{ 0, 0, 0, 1 } : mask_[parent] ? rotations_[parent] : base[parent];
			a[i] = cclamp_[i];
			a[i].rotation = NormalizeQuaternion(QuaternionProduct(Conjugate(p), rotations_[i]));
		}
	auto fastCurves = samples_[3].curves;
	fastCurves["Disable_IK_Offset"] = 1;
	fastCurves["Enable_SpineRotation"] = .2f;
	RifleLocalResult result;
	result.pose = Mix(a, samples_[3].pose, sprint);
	result.curves = BlendAnimationCurves(curves, fastCurves, sprint);
	result.branchA = std::move(a);
	result.curvesA = std::move(curves);
	result.curvesB = std::move(fastCurves);
	result.gait = gait;
	result.clf = clf;
	result.sprint = sprint;
	return result;
}
}
