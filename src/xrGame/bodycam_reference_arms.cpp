#include "bodycam_reference_arms.h"
#include "bodycam_reference/ads_pose.h"
#include "bodycam_reference/animation_blend.h"
#include "bodycam_reference/grip_contact.h"
#include "bodycam_reference/weapon_composition.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
// Moves the root bones of the pose into the space of root.
void MoveRoots(SkeletonPose& pose, const Matrix& root)
{
	for (unsigned i = 0; i < pose.Size(); ++i)
		if (pose.Parent(i) < 0)
			pose.SetLocal(i, Decompose(Multiply(root, Compose(pose.Local(i)))));
}
// One frame of a motion of the rig, with its roots in Reference model space.
SkeletonPose SampleInModel(const EngineSkeletonAdapter& rig, const char* motion, float seconds, const Matrix& model)
{
	auto pose = rig.SampleAuthoredPose(motion, seconds);
	MoveRoots(pose, model);
	return pose;
}
// Sets pose to the hip calibration blended toward the aimed one by alpha.
void BlendBaseline(SkeletonPose& pose, const SkeletonPose& hip, const std::optional<SkeletonPose>& aim, double alpha)
{
	for (unsigned i = 0; i < pose.Size(); ++i)
		pose.SetLocal(i, aim ? BlendAnimationLocal(hip.Local(i), aim->Local(i), alpha) : hip.Local(i));
}
std::vector<BoneCorrespondence> ResolveBones(const EngineSkeletonAdapter& rig, const ArmCalibration& calibration)
{
	if (calibration.bones.empty())
		throw std::invalid_argument("Empty authored arm correspondence");
	std::vector<BoneCorrespondence> bones;
	bones.reserve(calibration.bones.size());
	for (const auto& entry : calibration.bones)
	{
		const auto target = rig.FindBone(entry.engine.c_str());
		if (!target)
			throw std::invalid_argument("Missing arm bone: " + entry.engine);
		bones.push_back({ entry.reference, *target });
	}
	return bones;
}
// side is "r" or "l". The three arm bones must be mapped and directly parented.
ArmChain ResolveChain(
	const EngineSkeletonAdapter& rig, const ArmCalibration& calibration, const SkeletonPose& pose, const std::string& side)
{
	const auto bone = [&](const std::string& name)
	{
		if (std::none_of(calibration.bones.begin(), calibration.bones.end(), [&](const auto& b) { return b.engine == name; }))
			throw std::invalid_argument("Arm calibration must map the upper arm, forearm and hand");
		return *rig.FindBone(name.c_str());
	};
	const ArmChain chain{ bone(side + "_upperarm"), bone(side + "_forearm"), bone(side + "_hand") };
	ValidateArmChain(pose, chain);
	if (pose.Parent(chain.lower) != int(chain.upper) || pose.Parent(chain.hand) != int(chain.lower))
		throw std::invalid_argument("Arm bones must be directly parented");
	return chain;
}
}

EngineArmsAdapter::EngineArmsAdapter(IKinematics& right, IKinematics& left, const EngineBasis& basis, const SkeletonPose& reference,
	const ArmCalibration& rightCalibration, const ArmCalibration& leftCalibration)
	: right_(right, basis), left_(left, basis), rightModel_(rightCalibration.referenceFromModel),
	  leftModel_(leftCalibration.referenceFromModel),
	  rightCalibration_(SampleInModel(right_, rightCalibration.motion.c_str(), rightCalibration.seconds, rightModel_)),
	  leftCalibration_(SampleInModel(left_, leftCalibration.motion.c_str(), leftCalibration.seconds, leftModel_)),
	  rightBridge_(reference, rightCalibration_, ResolveBones(right_, rightCalibration)),
	  leftBridge_(reference, leftCalibration_, ResolveBones(left_, leftCalibration)),
	  rightChain_(ResolveChain(right_, rightCalibration, rightCalibration_, "r")),
	  leftChain_(ResolveChain(left_, leftCalibration, leftCalibration_, "l")), capturedRight_(rightCalibration_),
	  capturedLeft_(leftCalibration_), capturedReference_(reference), importedRight_(reference), importedLeft_(reference),
	  baselineRight_(rightCalibration_), baselineLeft_(leftCalibration_), contactRight_(rightCalibration_), contactLeft_(leftCalibration_)
{
	referenceOwner_.resize(reference.Size());
	for (const auto& bone : rightCalibration.bones)
		referenceOwner_[bone.reference] = 1;
	for (const auto& bone : leftCalibration.bones)
	{
		if (referenceOwner_[bone.reference])
			throw std::invalid_argument("Arm rigs cannot own the same reference bone");
		referenceOwner_[bone.reference] = 2;
	}
	std::vector<unsigned> depth(reference.Size());
	for (unsigned i = 0; i < reference.Size(); ++i)
	{
		referenceParents_.push_back(reference.Parent(i));
		referenceOrder_.push_back(i);
		for (int parent = reference.Parent(i); parent >= 0; parent = reference.Parent(parent))
			++depth[i];
	}
	std::stable_sort(referenceOrder_.begin(), referenceOrder_.end(), [&](unsigned a, unsigned b) { return depth[a] < depth[b]; });
}
void EngineArmsAdapter::CalibrateWeapon(const char* aimMotion, const char* anchor)
{
	const auto bone = right_.FindBone(anchor);
	if (!bone)
		throw std::invalid_argument("Weapon anchor bone hidden or missing");
	anchor_ = *bone;
	if (aimMotion && *aimMotion)
	{
		rightAimCalibration_ = SampleInModel(right_, aimMotion, 0, rightModel_);
		leftAimCalibration_ = SampleInModel(left_, aimMotion, 0, leftModel_);
	}
	else
	{
		rightAimCalibration_.reset();
		leftAimCalibration_.reset();
	}
}
const std::vector<LocalPose>& EngineArmsAdapter::Capture(const SkeletonPose& referenceBase)
{
	capturedRight_.ResetLocals(right_.Capture());
	MoveRoots(capturedRight_, rightModel_);
	capturedLeft_.ResetLocals(left_.Capture());
	MoveRoots(capturedLeft_, leftModel_);
	MergePose(referenceBase, capturedRight_, capturedLeft_, capturedLocals_);
	capturedReference_.ResetLocals(capturedLocals_);
	return capturedLocals_;
}
void EngineArmsAdapter::MergePose(
	const SkeletonPose& referenceBase, const SkeletonPose& right, const SkeletonPose& left, std::vector<LocalPose>& locals)
{
	importedRight_.ResetLocals(rightBridge_.Import(right, referenceBase));
	importedLeft_.ResetLocals(leftBridge_.Import(left, referenceBase));
	// Take every owned bone's model-space matrix from its own import: applying
	// the two imports as local poses in turn moves a bone whose ancestor the
	// other model owns.
	locals.resize(referenceParents_.size());
	auto& components = mergeComponents_;
	components.resize(referenceParents_.size());
	for (unsigned i : referenceOrder_)
	{
		const int parent = referenceParents_[i];
		locals[i] = referenceBase.Local(i);
		if (referenceOwner_[i])
		{
			const auto desired = (referenceOwner_[i] == 1 ? importedRight_ : importedLeft_).Component(i);
			locals[i] = Decompose(parent < 0 ? desired : Multiply(InverseAffine(components[parent]), desired));
		}
		const auto local = Compose(locals[i]);
		components[i] = parent < 0 ? local : Multiply(components[parent], local);
	}
}
std::vector<LocalPose> EngineArmsAdapter::CalibrationPose(const SkeletonPose& base, double aimBlend)
{
	BlendBaseline(baselineRight_, rightCalibration_, rightAimCalibration_, aimBlend);
	BlendBaseline(baselineLeft_, leftCalibration_, leftAimCalibration_, aimBlend);
	std::vector<LocalPose> locals;
	MergePose(base, baselineRight_, baselineLeft_, locals);
	return locals;
}
// How the solve moved the shoulder of one arm, in model space.
Matrix EngineArmsAdapter::BodyMotion(const SkeletonPose& before, const SkeletonPose& after, bool right) const
{
	const auto& bridge = right ? rightBridge_ : leftBridge_;
	const auto& calibration = right ? rightCalibration_ : leftCalibration_;
	const auto& chain = right ? rightChain_ : leftChain_;
	const auto prior = bridge.ExportComponents(before, calibration);
	const auto solved = bridge.ExportComponents(after, calibration);
	const int parent = calibration.Parent(chain.upper);
	const unsigned shoulder = parent >= 0 ? unsigned(parent) : chain.upper;
	return Multiply(solved[shoulder], InverseAffine(prior[shoulder]));
}
void EngineArmsAdapter::SetNeutralFit(unsigned endpoint, const SkeletonPose& before, const SkeletonPose& after, const Matrix& neutralGun,
	const Matrix& itemOffset, double aimBlend)
{
	BlendBaseline(baselineRight_, rightCalibration_, rightAimCalibration_, aimBlend);
	const auto baselineGun = Multiply(baselineRight_.Component(anchor_), itemOffset);
	const auto staticMotion = Multiply(neutralGun, InverseAffine(baselineGun));
	auto& fit = neutralFit_.at(endpoint);
	fit.right = Multiply(InverseAffine(BodyMotion(before, after, true)), staticMotion);
	fit.left = Multiply(InverseAffine(BodyMotion(before, after, false)), staticMotion);
}
void EngineArmsAdapter::Publish(const SkeletonPose& solved, double aimBlend, const Matrix& proceduralGun, const Matrix& itemOffset,
	const Matrix* leftHandFromRight, double motionInfluence, const Vector& elbowCm, double magnifiedWeight)
{
	if (motionInfluence == 0)
	{
		// The models keep the engine's matrices, exactly.
		reachAccommodation_ = {};
		return;
	}
	const auto& rightHand = capturedRight_.Component(rightChain_.hand);
	const auto& leftHand = capturedLeft_.Component(leftChain_.hand);
	BlendBaseline(baselineRight_, rightCalibration_, rightAimCalibration_, aimBlend);
	const auto baselineWeapon = Multiply(baselineRight_.Component(anchor_), itemOffset);
	const auto relation = leftHandFromRight ? *leftHandFromRight : Multiply(InverseAffine(rightHand), leftHand);
	const auto targets = ComposeWeaponTargets({ baselineWeapon, proceduralGun, rightHand, relation });
	const auto leftRoot = Multiply(Multiply(rightHand, relation), InverseAffine(leftHand));
	const auto& before = capturedReference_;
	// The neutral fit carries the whole engine pose at rest, so only the
	// procedural motion beyond it bends the arms and a short rig is not driven
	// to its reach limit by the weapon position alone.
	const auto fittedBody = [&](bool right)
	{
		const auto side = right ? &NeutralFit::right : &NeutralFit::left;
		const auto ads = BlendTransform(neutralFit_[2].*side, neutralFit_[1].*side, magnifiedWeight);
		const auto fit = BlendTransform(neutralFit_[0].*side, ads, aimBlend);
		return Multiply(BodyMotion(before, solved, right), fit);
	};
	reachAccommodation_[0] = SolveReferenceContact(publishedRight_, contactRight_, capturedRight_, rightChain_, kIdentityMatrix,
		fittedBody(true), targets.modelMotion, motionInfluence);
	reachAccommodation_[1] = SolveReferenceContact(publishedLeft_, contactLeft_, capturedLeft_, leftChain_, leftRoot, fittedBody(false),
		targets.modelMotion, motionInfluence, elbowCm);
	const auto inverseRightModel = InverseAffine(rightModel_), inverseLeftModel = InverseAffine(leftModel_);
	for (auto& component : publishedRight_)
		component = Multiply(inverseRightModel, component);
	for (auto& component : publishedLeft_)
		component = Multiply(inverseLeftModel, component);
	// Convert both before writing either: a pose that cannot be published leaves both models untouched.
	right_.EngineTransforms(publishedRight_, rightTransforms_);
	left_.EngineTransforms(publishedLeft_, leftTransforms_);
	right_.Publish(rightTransforms_);
	left_.Publish(leftTransforms_);
}
}
