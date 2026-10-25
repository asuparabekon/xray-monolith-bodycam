#pragma once
#include "classic_regrip.h"
#include "recoil_rig.h"
#include "viewmodel_helpers.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace BodycamReference
{
// The Reference procedural gun recoil for Classic. Only the gun pose is used: the engine's aim recoil
// moves the camera, so the recoil controller's aim deltas are computed and dropped.
struct ClassicRecoilInput
{
	double seconds = 0;
	bool paused = false;
	bool aiming = false;
	bool magnified = false;
	// The engine's ADS blend. It gates the hip-only fire sway and blends the two intensities.
	double adsWeight = 0;
	double hipIntensity = 1;
	double adsIntensity = 1;
};

inline RecoilPose ScaleClassicRecoilPose(RecoilPose pose, double scale)
{
	scale = std::isfinite(scale) ? std::max(scale, 0.) : 0.;
	pose.rotation.pitch *= scale;
	pose.rotation.yaw *= scale;
	pose.rotation.roll *= scale;
	for (auto& value : pose.location)
		value *= scale;
	return pose;
}

inline bool ClassicRecoilPoseActive(const RecoilPose& pose)
{
	// The pose is in degrees and centimetres. Below 1e-4 of either, the recoil tail is over.
	constexpr double epsilon = 1e-4;
	return std::abs(pose.rotation.pitch) > epsilon || std::abs(pose.rotation.yaw) > epsilon || std::abs(pose.rotation.roll) > epsilon ||
		std::abs(pose.location[0]) > epsilon || std::abs(pose.location[1]) > epsilon || std::abs(pose.location[2]) > epsilon;
}

class ClassicRecoil
{
public:
	ClassicRecoil(RecoilSetup setup, std::function<unsigned()> draw15, std::function<double()> unit)
		: rig_(std::move(setup), std::move(draw15), std::move(unit))
	{
	}
	// Policy is latched per shot, as in Reference; a running tail keeps its size.
	void BeginShot(bool aiming, const RecoilMetadata& metadata)
	{
		rig_.SetRecoilPolicy(BuildRecoilPolicy(metadata));
		rig_.BeginShot(aiming, metadata.automatic);
		firing_ = true;
	}
	// Trigger release lets the authored tail finish.
	void Stop()
	{
		if (!firing_)
			return;
		rig_.Stop();
		firing_ = false;
	}
	bool Firing() const { return firing_; }
	const RecoilPose& Update(const ClassicRecoilInput& input)
	{
		if (input.paused || !(input.seconds > 0))
			return pose_;
		const double ads = std::clamp(std::isfinite(input.adsWeight) ? input.adsWeight : 0., 0., 1.);
		rig_.UpdateController(input.seconds, 0, 0);
		rig_.Publish(input.seconds, input.aiming, input.aiming && input.magnified, ads);
		pose_ = ScaleClassicRecoilPose(rig_.PublishedPose(), input.hipIntensity + (input.adsIntensity - input.hipIntensity) * ads);
		return pose_;
	}
	const RecoilPose& Pose() const { return pose_; }
	std::uint64_t RandomDraws() const { return rig_.RandomDraws(); }

private:
	RecoilRig rig_;
	RecoilPose pose_{};
	bool firing_ = false;
};

// Reference applies recoil to its component-space gun about the gun origin.
// Evaluate it in the same calibrated source frame as Classic regrip, then
// carry the gun and both relative grips rigidly.
inline RegripTargets ApplyClassicRecoil(
	const ClassicReferenceRegripPose& frame, const RegripTargets& engineTargets, const RecoilPose& pose, const Matrix& engineCamera)
{
	if (!ClassicRecoilPoseActive(pose))
		return engineTargets;
	const auto engineFromSource = frame.EngineFromSource(engineCamera);
	const auto virtualGun = frame.VirtualGun(engineTargets, engineFromSource);
	const auto source = Multiply(InverseAffine(engineFromSource), virtualGun);
	const auto recoiled = Multiply(engineFromSource, ApplyRecoilPose(source, pose));
	auto result = engineTargets;
	result.gun = Multiply(Multiply(recoiled, InverseAffine(virtualGun)), engineTargets.gun);
	return result;
}
}
