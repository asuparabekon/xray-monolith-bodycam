// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "recoil_rig.h"
#include <utility>

namespace BodycamReference
{
RecoilRig::RecoilRig(RecoilSetup setup, std::function<unsigned()> draw15, std::function<double()> unit)
	: setup_(std::move(setup)), unit_(std::move(unit)), random_(std::move(draw15)), timeline_(setup_.hipCurves)
{
	controller_.Configure(setup_.controller);
	translation_.Configure(setup_.translation);
	sway_.Configure(setup_.sway);
	fireSway_.Configure(setup_.fireSway);
}
void RecoilRig::BeginShot(bool aiming, bool automatic)
{
	gunPoseScale_ = policy_.GunPoseScale(aiming);
	if (automatic)
		translation_.SetAutoPushback();
	const double horizontal = unit_(), vertical = unit_();
	controller_.Fire({ horizontal, vertical });
	timeline_.SetCurves(aiming ? setup_.aimCurves : setup_.hipCurves);
	fireSway_.HoldTargets(true);
	timeline_.Play();
	started_ = true;
}
void RecoilRig::Stop()
{
	controller_.Stop();
	fireSway_.Stop();
}
RecoilPoseSnapshot RecoilRig::PreUpdate(bool autoUpdate, bool valid)
{
	cache_.PreUpdate(published_, autoUpdate, valid, publication_);
	return cache_.Evaluate();
}
std::array<double, 2> RecoilRig::UpdateController(double dt, double x, double y)
{
	controller_.AccumulateMouse(x, y);
	controller_.Update(static_cast<float>(dt));
	return controller_.State().delta;
}
void RecoilRig::GenerateTargets(bool aiming, bool capture)
{
	const auto profile = BuildRecoilProfile(setup_.profile, aiming, capture, setup_.attributes);
	const auto targets = random_.Targets(profile, timeline_.State().targets[0]);
	timeline_.SetTargets(
		{ targets.rotation[0], targets.rotation[1], targets.rotation[2], targets.location[0], targets.location[1], targets.location[2] });
	const double pitch = random_.Unit(), yaw = random_.Unit();
	sway_.Fire({ pitch, yaw }, aiming);
	const double x = random_.Unit(), y = random_.Unit();
	translation_.Fire({ x, y }, aiming);
	fireSway_.Fire([this](float index) { return setup_.fireSwayCurve.Sample(index); });
}
void RecoilRig::Publish(double delta, bool aiming, bool capture, double enableAds)
{
	if (!started_)
		return;
	const double dt = static_cast<float>(delta);
	if (timeline_.Tick(dt, dt))
		GenerateTargets(aiming, capture);
	translation_.Update(dt, aiming);
	sway_.Update(dt);
	const auto& main = timeline_.State().value;
	const auto& sway = sway_.State().value;
	const auto composed = ComposeRecoilSway({ main[1], main[2], main[0] }, { sway[0], sway[1], sway[2] }, setup_.swayPivot);
	fireSway_.Update(dt, enableAds);
	const auto& secondary = translation_.State().value;
	const auto& fire = fireSway_.State().value;
	published_.rotation = composed.rotation;
	published_.location = { main[3] + secondary[2] + composed.location[0] + fire[0], main[4] + secondary[0] + composed.location[1],
		main[5] + secondary[1] + secondary[3] + composed.location[2] + fire[1] };
	published_ = ScaleRecoilGunPose(published_, gunPoseScale_);
	++publication_;
}
}
