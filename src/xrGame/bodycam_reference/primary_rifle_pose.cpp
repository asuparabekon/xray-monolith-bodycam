#include "primary_rifle_pose.h"
#include "constants.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
std::vector<int> Parents(const std::vector<RifleTrack>& tracks)
{
	std::vector<int> out;
	for (const auto& t : tracks)
		out.push_back(t.parent);
	return out;
}
// Index of each of the 89 bones in the 179-bone source skeleton, whose last 21 bones are the virtual ones.
std::vector<unsigned> SourceBoneIndices()
{
	std::vector<unsigned> out;
	for (unsigned i = 0; i < kSkeletonBoneCount; ++i)
		out.push_back(i < kPhysicalBoneCount ? i : i + (kSourceBoneCount - kSkeletonBoneCount));
	return out;
}
}
NamedCurves ToNamedCurves(const AnimationCurves& curves)
{
	NamedCurves out;
	out.reserve(curves.size());
	for (const auto& e : curves)
		out.push_back({ e.first, e.second, 0 });
	return out;
}
PrimaryRiflePose::PrimaryRiflePose(std::vector<RifleTrack> tracks, unsigned spine, std::vector<LocalPose> cclamp,
	std::array<RiflePoseSample, 4> samples, RegionalManifest manifest, PackedPose base)
	: PrimaryRiflePose(
		  tracks, RifleLocalPose(Parents(tracks), spine, cclamp, std::move(samples)), cclamp, std::move(manifest), std::move(base))
{
}
PrimaryRiflePose::PrimaryRiflePose(
	std::vector<RifleTrack> tracks, RifleLocalPose local, std::vector<LocalPose> cclamp, RegionalManifest manifest, PackedPose base)
	: tracks_(std::move(tracks)), local_(std::move(local)), gun_(tracks_, cclamp),
	  regional_(std::move(manifest), Parents(tracks_), SourceBoneIndices()), base_(std::move(base))
{
	if (tracks_.size() != kSkeletonBoneCount || base_.size() != kSkeletonBoneCount)
		throw std::invalid_argument("Primary rifle pose needs 89 tracks and an 89-bone base pose");
	rest_ = local_.Evaluate(0, 0, 0).curves;
	published_ = layerInputs_ = rest_;
}
void PrimaryRiflePose::BeginUpdate()
{
	layerInputs_ = published_;
}
void PrimaryRiflePose::PublishCompletedCurves(AnimationCurves curves)
{
	published_ = std::move(curves);
}
PrimaryRifleOutput PrimaryRiflePose::Apply(const PrimaryRifleInput& in, const std::vector<LocalPose>& physical,
	const OverlayMontages& montages, const ActionCurvePublication& actionCurves, const RegionalStage& stage)
{
	if (physical.size() != kPhysicalBoneCount)
		throw std::invalid_argument("Physical pose has " + std::to_string(physical.size()) + " bones, expected 68");
	PrimaryRifleOutput out;
	out.physical = physical;
	if (!in.hasUpdate || (!in.coherent && in.amount == 0))
	{
		published_ = rest_;
		if (in.recordDiagnostics)
			out.publishedCurves = published_;
		return out;
	}
	const auto local = local_.Evaluate(in.weightGait, in.basePoseClf, in.sprintWeight);
	if (in.recordDiagnostics)
		out.overlayCurves = local.curves;
	auto target = local.pose;
	if (in.locomotion)
	{
		auto overlay = in.coherent ? gun_.AttachGunToHand(local.pose) : local.pose;
		if (in.coherent)
		{
			const auto promoted = overlay;
			if (montages)
				overlay = montages(overlay);
			gun_.CaptureHelperOffsets(overlay);
			overlay = gun_.ApplyMontageMask(promoted, overlay);
		}
		const auto composed =
			regional_.Evaluate(in.locomotion->pose, base_, GltfToPackedRiflePose(overlay), ToNamedCurves(layerInputs_), stage);
		target = PackedToGltfRiflePose(composed.pose);
		out.regional = true;
		if (in.recordDiagnostics)
			out.properties = composed.properties;
		published_ = actionCurves ? actionCurves(local.curves) : local.curves;
		for (const auto& e : in.locomotion->curves)
		{
			float& prior = published_[e.name];
			prior = e.value + prior;
		}
		if (in.coherent)
			out.primaryPublication = ToNamedCurves(published_);
	}
	carried_ = target;
	const double alpha = in.coherent ? 1 : std::clamp(double(in.amount), 0., 1.);
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		out.physical[i] = alpha == 1 ? target[i] : BlendAnimationLocal(physical[i], target[i], alpha);
	out.applied = true;
	if (in.recordDiagnostics)
		out.publishedCurves = published_;
	return out;
}
std::vector<Matrix> PrimaryRiflePose::IncomingVirtualComponents(const std::vector<LocalPose>& physical) const
{
	if (!carried_ || physical.size() != kPhysicalBoneCount)
		throw std::logic_error("No rifle pose has been applied yet, or the physical pose does not have 68 bones");
	auto pose = *carried_;
	std::copy(physical.begin(), physical.end(), pose.begin());
	return RiflePoseComponents(pose, tracks_);
}
}
