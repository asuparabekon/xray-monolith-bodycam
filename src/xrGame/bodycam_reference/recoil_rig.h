#pragma once
#include "recoil_controller.h"
#include "recoil_secondary.h"
#include "recoil_fire_sway.h"
#include "recoil_profile.h"
#include "recoil_pose.h"
#include "recoil_timeline.h"
#include "recoil_policy.h"

namespace BodycamReference
{
struct RecoilSetup
{
	RecoilCurves hipCurves, aimCurves;
	RecoilCurve fireSwayCurve;
	RecoilSourceProfile profile;
	RecoilAttributes attributes;
	ControllerRecoilConfig controller;
	TranslationRecoilConfig translation;
	SwayRecoilConfig sway;
	FireSwayConfig fireSway;
	Vector swayPivot{};
};
class RecoilRig
{
public:
	RecoilRig(RecoilSetup setup, std::function<unsigned()> draw15, std::function<double()> unit);
	// An automatic shot also sets the automatic pushback.
	void BeginShot(bool aiming, bool automatic = false);
	// Takes effect on the next shot, so a running recoil keeps its scale.
	void SetRecoilPolicy(const RecoilPolicy& policy) { policy_ = policy; }
	void Stop();
	RecoilPoseSnapshot PreUpdate(bool autoUpdate, bool componentValid);
	// Advances the controller recoil after adding the mouse input; the returned deltas apply to the next
	// frame's control rotation.
	std::array<double, 2> UpdateController(double dt, double processedX, double processedY);
	void Publish(double dt, bool aiming, bool scopeCaptureActive, double primaryEnableAds);
	const RecoilPose& PublishedPose() const { return published_; }
	std::uint64_t Publication() const { return publication_; }
	std::uint64_t RandomDraws() const { return random_.Draws(); }
	const RecoilTimelineState& Timeline() const { return timeline_.State(); }

private:
	void GenerateTargets(bool aiming, bool capture);
	RecoilSetup setup_;
	std::function<double()> unit_;
	RecoilRandom random_;
	RecoilTimeline timeline_;
	ControllerRecoil controller_;
	TranslationRecoil translation_;
	SwayRecoil sway_;
	FireSway fireSway_;
	RecoilPoseCache cache_;
	RecoilPose published_;
	std::uint64_t publication_ = 0;
	bool started_ = false;
	RecoilPolicy policy_;
	double gunPoseScale_ = 1;
};
}
