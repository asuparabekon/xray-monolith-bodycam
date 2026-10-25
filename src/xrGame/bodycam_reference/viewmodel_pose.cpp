#include "viewmodel_pose.h"
#include "viewmodel_helpers.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace BodycamReference
{
namespace
{
// A reference transform from a source position in centimetres and a source rotation.
Matrix SourceTransform(const Vector& position, const Quaternion& q)
{
	LocalPose pose;
	pose.position = SourceToReference(position);
	pose.rotation = NormalizeQuaternion(SourceToReference(q));
	return Compose(pose);
}
Matrix M4ActionOffset(const Vector& action)
{
	return SourceTransform({ 4 + action[0], 3.3179722 + action[1], -8.230048 + action[2] }, { .13052619, 0, 0, .9914449 });
}
}
Matrix M4PoseOffset()
{
	return SourceTransform({ 8, 0, -5 }, { -.00057096017, .04361565, .013077137, .99896264 });
}
Matrix ApplyMovementAdditives(const Matrix& current, const AnimationCurves& curves)
{
	const auto value = [&](const char* name) { return CurveValue(curves, name); };
	const auto q = RecoilQuaternion({ value("IK_Pitch"), value("IK_Yaw"), value("IK_Roll") });
	auto pose = Decompose(current);
	pose.position[0] += value("IK_X") * kMetresPerCentimetre;
	pose.position[1] += value("IK_Z") * kMetresPerCentimetre;
	pose.position[2] += value("IK_Y") * kMetresPerCentimetre;
	pose.rotation = NormalizeQuaternion(QuaternionProduct(SourceToReference(q), pose.rotation));
	return Compose(pose);
}
ViewmodelPoseGraph::ViewmodelPoseGraph(std::vector<int> parents, std::vector<LocalPose> initial, ViewmodelBinding binding)
	: skeleton_(parents, std::move(initial)), regrip_(std::move(parents), binding.spine.head), binding_(binding)
{
	ValidateArmChain(skeleton_, binding_.right);
	ValidateArmChain(skeleton_, binding_.left);
	for (unsigned bone : binding_.spine.spine)
		if (bone >= skeleton_.Size())
			throw std::invalid_argument("Spine binding outside skeleton");
	if (binding_.spine.neck >= skeleton_.Size() || binding_.spine.head >= skeleton_.Size() || binding_.pelvis >= skeleton_.Size())
		throw std::invalid_argument("Viewmodel binding outside skeleton");
}
SwayIkResult ViewmodelPoseGraph::Evaluate(
	const std::vector<LocalPose>& authored, const ViewmodelPoseInput& input, const std::function<void()>& publishMovement)
{
	skeleton_.ResetLocals(authored);
	const auto& right = binding_.right;
	const auto& left = binding_.left;
	const Matrix inverseGun = InverseAffine(input.gun);
	RegripTargets targets;
	targets.gun = input.gun;
	targets.poleL = Multiply(InverseAffine(input.evaluatorHandL), input.evaluatorPoleL);
	// The right hand is expressed relative to the gun after the gun is replaced.
	targets.handR = input.coherent && input.fullVirtual
		? HandLocalAfterGunCopy(input.gun, input.incomingVirtualHandR, skeleton_.Component(right.hand))
		: Multiply(inverseGun, skeleton_.Component(right.hand));
	targets.poleR = Multiply(inverseGun, skeleton_.Component(right.lower));
	const auto gates = MovementProceduralGates(input.disableIkOffset, input.enableSpineRotation);
	const Matrix offset = BlendTransform(M4PoseOffset(), M4ActionOffset(input.actionPositionCm),
		std::clamp(input.actionWeight, 0., 1.) * (1. - std::clamp(input.engineActionPlacementWeight, 0., 1.)));
	const Matrix offsetTarget = ApplyComponentAdditive(targets.gun, offset);
	const Matrix head = skeleton_.Component(binding_.spine.head);
	targets.gun = input.coherent ? BlendAdsTarget(targets.gun, offsetTarget, head, gates.ikOffsetAlpha) : offsetTarget;
	targets.gun = SolveAdsPose({ targets.gun, input.referenceAppliedGun, input.referenceCameraPoint, input.currentCameraPoint,
		input.aimPointLocal, head, input.adsAlpha, input.magnified });
	targets.handL = Multiply(InverseAffine(input.evaluatorGunL), input.evaluatorHandL);
	if (input.attachWeight < 1)
		targets.handL = BlendTransform(Multiply(inverseGun, skeleton_.Component(left.hand)), targets.handL, input.attachWeight);

	// Without a regrip the skeleton already holds the authored locals.
	if (!(input.regripSamples.empty() || input.regripAlpha <= 0))
	{
		auto locals = authored;
		regrip_.Apply(locals, targets, input.regripSamples, input.regripAlpha);
		skeleton_.ResetLocals(locals);
	}
	targets.gun = ApplyRecoilPose(targets.gun, input.recoil);
	if (input.coherent && input.applyMovementAdditives)
	{
		targets.gun = ApplyMovementAdditives(targets.gun, input.consumedMovementAdditives);
		if (publishMovement)
			publishMovement();
	}
	const auto sway =
		ApplySwayAndCopyPoles(skeleton_, binding_.spine.head, right, left, targets, input.springs, input.pivotCm, input.lowerSwayCm);
	ApplySpineAimOffset(skeleton_, binding_.spine, input.cachedAimPitch, input.cachedAimYaw, input.leanAlpha,
		input.coherent ? gates.yawAlpha : 1, input.coherent ? gates.pitchAlpha : 1);
	auto result = TransportHeadAndSolveArms(skeleton_, binding_.spine.head, right, left, sway, input.elbowCm);
	if (input.coherent && input.applyFlatGround)
		ApplyFlatPelvisControl(skeleton_, binding_.pelvis, binding_.spine.head, input.pelvisOffsetCm, input.pelvisAlpha,
			{ &result.gun, &result.handL, &result.handR, &result.poleL, &result.poleR });
	return result;
}
}
