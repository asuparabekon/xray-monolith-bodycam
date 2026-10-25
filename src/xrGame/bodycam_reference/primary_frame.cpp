#include "primary_frame.h"
#include <stdexcept>
#include "asset_reader.h"
#include "scalar_math.h"
namespace BodycamReference
{
namespace
{
float Curve(const NamedCurves& curves, const char* name)
{
	for (const auto& c : curves)
		if (c.name == name)
			return c.value;
	return 0;
}
std::array<unsigned, 6> LateBones(const std::vector<RifleTrack>& tracks)
{
	const std::array<const char*, 6> names = { "hand_r", "vb ik_hand_gun", "vb hand_r", "vb rhs_ik_hand_gun", "hand_l", "ik_hand_gun" };
	std::array<unsigned, 6> indices{};
	for (unsigned i = 0; i < names.size(); ++i)
	{
		bool found = false;
		for (unsigned j = 0; j < tracks.size(); ++j)
		{
			if (LowerAscii(tracks[j].name) != names[i])
				continue;
			if (found)
				throw std::invalid_argument(std::string("Rifle skeleton has two tracks named ") + names[i]);
			indices[i] = j;
			found = true;
		}
		if (!found)
			throw std::invalid_argument(std::string("Rifle skeleton has no track named ") + names[i]);
	}
	if (tracks[indices[kLateVirtualHandR]].parent != int(indices[kLateVirtualGun]) ||
		tracks[indices[kLateVirtualRhsGun]].parent != int(indices[kLateHandR]))
		throw std::invalid_argument("Rifle skeleton: vb hand_r is not under vb ik_hand_gun, or vb rhs_ik_hand_gun not under hand_r");
	return indices;
}
}
PrimaryFrame::PrimaryFrame(
	const LocomotionAssetBank& assets, const PoseAssetBank& poses, const RifleAssetBank& rifle, WalkingStressAdapter stress)
	: locomotion_(assets.Cycle(), assets.Upper(), poses, assets.Properties(), assets.Rotations()),
	  rifle_(rifle.CreatePrimary(assets.Regional(), PackPose(poses.Sample("idle:base", 0)))), reference_(rifle.CreateReference()),
	  viewmodel_(rifle.CreateViewmodel()), walking_(assets.Walking(), std::move(stress)), lateBones_(LateBones(rifle.Tracks()))
{
}
const LocomotionFrameResult& PrimaryFrame::Update(
	double dt, const LocomotionEssential& essential, double aimingProgress, bool aiming, std::optional<double> aimYawRate)
{
	rifle_.BeginUpdate();
	delta_ = dt;
	auto presentation = essential;
	// Engine walking and jogging use the walk family and only engine sprint the run family; speed,
	// acceleration and movement settings stay physical for stride and camera.
	if (idleMotion_ && essential.engineRifleGait)
		presentation.gait = *essential.engineRifleGait;
	last_ = locomotion_.Update(dt, presentation, aimingProgress, aimYawRate, recordPoseDiagnostics_);
	if (idleMotion_)
	{
		idleMotion_->Update(dt, essential.speed, aiming);
		walking_.SetLocomotionSpeed(essential.speed);
	}
	walking_.Update(dt, aiming, rifle_.LayerInputs());
	return *last_;
}
PrimaryRifleOutput PrimaryFrame::Apply(const std::vector<LocalPose>& physical, const OverlayMontages& montages,
	const ActionCurvePublication& actions, const RegionalStage& stage)
{
	if (!last_)
		throw std::logic_error("Primary frame applied before its first update");
	actionWeight_ = FInterpTo(actionWeight_, CurveValue(rifle_.LayerInputs(), "FP_Action_Weight"), delta_, 8);
	PrimaryRifleInput in;
	in.recordDiagnostics = recordPoseDiagnostics_;
	in.hasUpdate = true;
	in.coherent = true;
	in.amount = last_->upper.amount;
	in.weightGait = Curve(last_->upper.pose.curves, "Weight_Gait");
	in.basePoseClf = Curve(last_->upper.pose.curves, "BasePose_CLF");
	in.sprintWeight = last_->sprintWeight;
	in.locomotion = &last_->upper.pose;
	auto out = rifle_.Apply(in, physical, montages, actions, stage);
	// The gun before the procedural layers, for the frozen-character trace.
	if (recordPoseDiagnostics_)
		postOverlayGun_ = rifle_.IncomingVirtualComponents(out.physical)[lateBones_[kLateGun]];
	if (out.primaryPublication)
		locomotion_.PublishPrimaryCurves(*out.primaryPublication);
	return out;
}
void PrimaryFrame::PublishCompletedCurves(AnimationCurves curves)
{
	auto named = ToNamedCurves(curves);
	rifle_.PublishCompletedCurves(std::move(curves));
	locomotion_.PublishPrimaryCurves(std::move(named));
}
void PrimaryFrame::CompleteWalking()
{
	const auto output = walking_.Evaluate(ToNamedCurves(rifle_.PublishedCurves()));
	AnimationCurves curves;
	for (const auto& c : output)
		curves[c.name] = c.value;
	PublishCompletedCurves(std::move(curves));
}
PrimaryVirtualPose PrimaryFrame::LateVirtualPose(const std::vector<LocalPose>& physical) const
{
	const auto incoming = rifle_.IncomingVirtualComponents(physical);
	const auto& target = incoming[lateBones_[kLateVirtualGun]];
	return { rifle_.GunFromHand(incoming[lateBones_[kLateHandR]], target, incoming[lateBones_[kLateVirtualRhsGun]]), target,
		incoming[lateBones_[kLateVirtualHandR]] };
}
void PrimaryFrame::PrepareViewmodel(std::vector<LocalPose>& physical, ViewmodelPoseInput& input) const
{
	input.currentCameraPoint = reference_.CurrentCameraPoint(physical);
	input.actionWeight = actionWeight_;
	reference_.ApplyPhysicalLeft(physical, input.adsAlpha, input.attachWeight);
	const auto late = LateVirtualPose(physical);
	input.gun = late.gun;
	input.incomingVirtualHandR = late.incomingHandR;
	const auto left = reference_.EvaluateLeft(input.adsAlpha);
	input.evaluatorGunL = left.gun;
	input.evaluatorHandL = left.hand;
	input.evaluatorPoleL = left.pole;
	input.referenceAppliedGun = reference_.ReferenceAppliedGun();
	input.referenceCameraPoint = reference_.ReferenceCameraPoint();
	input.consumedMovementAdditives.clear();
	for (const auto& c : walking_.State().consumed)
		input.consumedMovementAdditives[c.name] = c.value;
	input.applyMovementAdditives = true;
	input.coherent = true;
	input.fullVirtual = true;
	input.disableIkOffset = CurveValue(rifle_.LayerInputs(), "Disable_IK_Offset");
	input.enableSpineRotation = CurveValue(rifle_.LayerInputs(), "Enable_SpineRotation");
}
SwayIkResult PrimaryFrame::EvaluateViewmodel(std::vector<LocalPose>& physical, ViewmodelPoseInput input)
{
	return EvaluateViewmodelInPlace(physical, input);
}
SwayIkResult PrimaryFrame::EvaluateViewmodelInPlace(std::vector<LocalPose>& physical, ViewmodelPoseInput& input)
{
	PrepareViewmodel(physical, input);
	const auto result = viewmodel_.Evaluate(physical, input, [this] { CompleteWalking(); });
	for (unsigned i = 0; i < physical.size(); ++i)
		physical[i] = viewmodel_.Skeleton().Local(i);
	return result;
}
PhysicalHandPoints PrimaryFrame::FinalHandPoints() const
{
	const auto& pose = viewmodel_.Skeleton();
	return { pose.Component(lateBones_[kLateHandL]), pose.Component(lateBones_[kLateHandR]) };
}
}
