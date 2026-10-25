#include "bodycam_reference_host.h"
#include "bodycam_reference/constants.h"
#include "bodycam_reference/regrip_presentation.h"
#include "bodycam_reference/camera_anchor.h"
#include <stdexcept>
#include <utility>
#include <cmath>
#include <algorithm>

namespace BodycamReference
{
namespace
{
std::vector<int> PhysicalParents(const RifleAssetBank& assets)
{
	std::vector<int> parents;
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		parents.push_back(assets.Tracks()[i].parent);
	return parents;
}
}

EngineCharacterHost::EngineCharacterHost(std::unique_ptr<CharacterSession> session, const EngineBasis& basis)
	: session_(std::move(session)), basis_(basis), parents_(PhysicalParents(session_->RifleAssets())),
	  idlePose_(parents_, std::vector<LocalPose>(kPhysicalBoneCount)), solvedPose_(idlePose_), cameraEquipment_(session_->Equipment()),
	  neutralHipWeapon_(session_->NeutralHipWeaponCamera())
{
	session_->Graph().primary.EnableEngineMotion();
	session_->Graph().primary.RecordPoseDiagnostics(false);
}

void EngineCharacterHost::BindArms(
	IKinematics& right, IKinematics& left, const ArmCalibration& rightCalibration, const ArmCalibration& leftCalibration)
{
	auto calibration = session_->RifleAssets().RetargetedCclamp();
	calibration.resize(kPhysicalBoneCount);
	auto arms = std::make_unique<EngineArmsAdapter>(
		right, left, basis_, SkeletonPose(parents_, std::move(calibration)), rightCalibration, leftCalibration);
	UnbindWeapon();
	arms_ = std::move(arms);
}
void EngineCharacterHost::UnbindWeapon()
{
	StopFire(true);
	session_->Graph().recoil.SetRecoilPolicy({});
	pose_.reset();
	arms_.reset();
	semantics_.reset();
	actions_ = GenericActions{};
	itemOffset_ = kIdentityMatrix;
	session_->SetEquipment(cameraEquipment_, true);
}
void EngineCharacterHost::CalibrateWeapon(const char* aimMotion, const char* anchor, const EngineRowMatrix& itemOffset)
{
	arms_->CalibrateWeapon(aimMotion, anchor);
	itemOffset_ = basis_.ToReference(itemOffset);
	neutralFitDirty_ = true;
}
void EngineCharacterHost::SetEquipment(const EquipmentTransforms& equipment)
{
	session_->SetEquipment(equipment);
	neutralAdsWeapon_ = session_->NeutralAdsWeaponCamera();
	neutralFitDirty_ = true;
}

void EngineCharacterHost::ConfigureActions(const std::vector<ActionAlias>& aliases, const std::vector<std::uint8_t>& semanticsPacket,
	const std::vector<std::string>& extraReloadAliases)
{
	actions_ = GenericActions(aliases, extraReloadAliases);
	auto clips = actions_.SemanticsClips();
	if (!clips.empty())
		semantics_ = std::make_unique<ActionSemantics>(semanticsPacket, std::move(clips), session_->RandomUnit());
}
bool EngineCharacterHost::AdmitAction(std::uint64_t motion, std::uint64_t generation, bool crouched, bool empty, bool aimed)
{
	const bool suppressLookDown = aimed && actions_.PreservesAim(motion);
	if (!actions_.Admit(motion, generation))
		return false;
	if (semantics_)
		semantics_->Admit(motion, generation, crouched, empty, aimed, suppressLookDown, actions_.SemanticsCurve(motion));
	return true;
}
void EngineCharacterHost::SetActionSamples(std::vector<ActionSample> samples)
{
	if (semantics_)
		semantics_->Samples(std::move(samples));
}
void EngineCharacterHost::SetReloadPhase(ReloadPhase phase)
{
	if (semantics_)
		semantics_->SetReloadPhase(phase);
}
AnimationCurves EngineCharacterHost::ActionCurves(const AnimationCurves& base) const
{
	return semantics_ ? semantics_->Apply(base) : base;
}

void EngineCharacterHost::ConfigureRecoil(const RecoilMetadata& metadata)
{
	session_->Graph().recoil.SetRecoilPolicy(BuildRecoilPolicy(metadata));
}
void EngineCharacterHost::BeginShot(bool aiming, bool automatic)
{
	if (!firing_)
	{
		session_->Graph().regrip.Semi().Begin(Motion().worldTime);
		firing_ = true;
	}
	session_->Graph().recoil.BeginShot(aiming, automatic);
}
void EngineCharacterHost::StopFire(bool cancelled)
{
	// The engine reports the end of one burst from fire end, hide and detach alike.
	if (!firing_)
		return;
	session_->Graph().recoil.Stop();
	firing_ = false;
	session_->Graph().regrip.Semi().End(cancelled);
}
void EngineCharacterHost::AddMouse(double x, double y)
{
	session_->Graph().motion.AddMouse(x, y);
}
void EngineCharacterHost::SetAimControl(const Rotation& control)
{
	session_->Graph().motion.SetExternalControl(control);
}
void EngineCharacterHost::ObserveVelocity(std::uint64_t physicsStep, double x, double z)
{
	if (observedVelocityStep_ && physicsStep <= *observedVelocityStep_)
		return;
	observedVelocity_ = Velocity{ x, z };
	observedVelocityStep_ = physicsStep;
}

void EngineCharacterHost::BeginFrame(
	std::uint32_t frame, double applicationSeconds, bool aiming, bool magnified, const CharacterMovementInput& input)
{
	auto& graph = session_->Graph();
	if (observedVelocity_)
	{
		graph.movement.SyncWorldVelocity(observedVelocity_->x, observedVelocity_->z);
		observedVelocity_.reset();
	}
	graph.regrip.Events().AimChanged(aiming);
	if (input.reloading && firing_)
		StopFire(true);
	if (!session_->BeginFrame(frame, false, applicationSeconds, aiming, magnified, input))
		throw std::logic_error("Reference frame admission failed");
	aiming_ = aiming;
	magnified_ = magnified;
	frame_ = frame;
	frameSeconds_ = applicationSeconds;
	pose_.reset();
}
void EngineCharacterHost::Solve(const EngineCharacterView& view, const ActionCurvePublication& actions)
{
	auto& graph = session_->Graph();
	if (arms_ && neutralFitDirty_)
		RefreshNeutralFit();
	const auto& authored = CapturePose();
	ViewmodelPoseInput poseInput;
	// The regrip samples are written into storage kept from the previous frame.
	poseInput.regripSamples.swap(regripSamples_);
	const auto cameraFire = PrepareRegrip(poseInput, view);
	const auto elbowCm = Motion().elbowCm;
	const auto solved = graph.character.EvaluateInPlace(authored, poseInput, {}, actions);
	regripSamples_.swap(poseInput.regripSamples);
	graph.character.Complete(view.scopeCapture, view.cameraHit, view.cameraHitDistanceCm);
	const auto cameraActor = CameraActorAnchor(view.actor, graph.movement.State().actorYaw, graph.primary.ReferenceCameraPoint());
	const auto presentation = graph.character.Present(cameraActor, cameraFire, view.horizontalFov);
	const auto pose = PreparePresentation(presentation, view);
	if (arms_)
	{
		const auto proceduralGun = Multiply(InverseAffine(presentation.armsCamera), presentation.weaponCamera);
		const auto leftHandFromRight =
			view.leftHandFromRight ? std::optional<Matrix>(basis_.ToReference(*view.leftHandFromRight)) : std::nullopt;
		solvedPose_.ResetLocals(solved.physical);
		arms_->Publish(solvedPose_, view.aimBlend, proceduralGun, itemOffset_, leftHandFromRight ? &*leftHandFromRight : nullptr,
			view.motionInfluence, elbowCm, graph.aim.MagnifiedWeight());
	}
	completedFrame_ = frame_;
	pose_ = pose;
}
// The Reference skeleton this frame starts from: the stance, with the engine's arms when a weapon is bound.
const std::vector<LocalPose>& EngineCharacterHost::CapturePose()
{
	const auto& idle = session_->Idle().Evaluate(Motion().worldSeconds);
	if (!arms_)
		return idle;
	idlePose_.ResetLocals(idle);
	return arms_->Capture(idlePose_);
}
// Fills the action and regrip inputs of the pose and returns the camera's fire rotation.
Rotation EngineCharacterHost::PrepareRegrip(ViewmodelPoseInput& poseInput, const EngineCharacterView& view)
{
	auto& graph = session_->Graph();
	if (semantics_)
		poseInput.actionPositionCm = semantics_->Offset();
	// With a bound weapon the engine's action clips place the weapon.
	poseInput.engineActionPlacementWeight = arms_ ? 1. : 0.;
	const auto& curves = graph.primary.LayerInputs();
	const auto curve = [&](const char* name)
	{
		const auto i = curves.find(name);
		return i == curves.end() ? 0. : double(i->second);
	};
	const double actionWeight = curve("FP_Action_Weight");
	poseInput.attachWeight = std::clamp((1. - actionWeight) * (1. - curve("Mask_AttachLeftHand")), 0., 1.);
	const auto& samples = graph.regrip.Update(Motion().worldSeconds, false);
	CameraIntensityInput light;
	light.scopeActive = aiming_ && magnified_;
	light.aimpointSource = light.scopeActive ? 2 : 1; // The source project's ids: 1 iron sight, 2 scope.
	light.actionWeight = actionWeight;
	light.followRotation = curve("FollowCameraRotation");
	light.blockingHit = view.cameraHit;
	light.distanceCm = view.cameraHitDistanceCm;
	intensity_.Update(light, Motion().worldSeconds);
	poseInput.regripAlpha = aiming_ ? intensity_.Regrip() : 1;
	session_->RegripAssets().SamplePose(samples, poseInput.regripSamples);
	return EvaluateRegripCamera(session_->RegripAssets(), samples, poseInput.regripAlpha, intensity_.AnimationScale());
}

EngineCharacterPose EngineCharacterHost::PreparePresentation(const CharacterPresentation& presentation, const EngineCharacterView& view)
{
	EngineCharacterPose pose{ basis_.CameraWorldToEngine(Compose(presentation.camera)),
		basis_.CameraRelativeToEngine(presentation.weaponCamera),
		basis_.CameraRelativeToEngine(Multiply(presentation.armsCamera, arms_ ? arms_->ReferenceFromRightModel() : kIdentityMatrix)),
		basis_.CameraRelativeToEngine(Multiply(presentation.armsCamera, arms_ ? arms_->ReferenceFromLeftModel() : kIdentityMatrix)),
		basis_.CameraRelativeToEngine(presentation.weaponCamera), basis_.CameraRelativeToEngine(neutralHipWeapon_) };
	const auto clearance = stanceClearance_.Update(view.crouched, view.lowCrouched, view.lookDownRadians, view.aimBlend, frameSeconds_);
	ApplyWeaponPlacement(pose, view, clearance);
	for (const auto* matrix : { &pose.cameraWorld, &pose.weaponCamera, &pose.rightModelCamera, &pose.leftModelCamera })
		for (float value : *matrix)
			if (!std::isfinite(value))
				throw std::runtime_error("Nonfinite camera or HUD pose");
	return pose;
}

// Moves the weapon and both arm models together: the neutral weapon goes to the
// configured position and the animation keeps its displacement from neutral.
// The stance clearance is added on top and is not part of the configured position.
void EngineCharacterHost::ApplyWeaponPlacement(EngineCharacterPose& pose, const EngineCharacterView& view, const Vector& clearance) const
{
	const Vector animatedGun{ pose.weaponCamera[12], pose.weaponCamera[13], pose.weaponCamera[14] };
	Vector positionedGun = animatedGun;
	if (view.weaponPosition)
	{
		const auto hip = NeutralHipWeaponCamera(), ads = NeutralAdsWeaponCamera();
		for (unsigned axis = 0; axis < 3; ++axis)
		{
			const double sourceNeutral = hip[12 + axis] + (ads[12 + axis] - hip[12 + axis]) * view.aimBlend;
			const double configuredNeutral =
				view.weaponPosition->hip[axis] + (view.weaponPosition->ads[axis] - view.weaponPosition->hip[axis]) * view.aimBlend;
			positionedGun[axis] = configuredNeutral + (animatedGun[axis] - sourceNeutral);
		}
	}
	positionedGun[1] += clearance[1];
	positionedGun[2] += clearance[2];
	for (auto* matrix : { &pose.weaponCamera, &pose.rightModelCamera, &pose.leftModelCamera })
		for (unsigned axis = 0; axis < 3; ++axis)
			(*matrix)[12 + axis] = float(positionedGun[axis] + ((*matrix)[12 + axis] - animatedGun[axis]));
}

// Fits the arms to the neutral weapon at the hip and at both aimed sights.
void EngineCharacterHost::RefreshNeutralFit()
{
	auto idle = session_->Idle();
	idle.ResetTime();
	const SkeletonPose base(parents_, idle.Evaluate(0));
	for (unsigned endpoint = 0; endpoint < 3; ++endpoint)
	{
		const bool aiming = endpoint != 0;
		const auto authored = arms_->CalibrationPose(base, aiming ? 1. : 0.);
		const auto neutral = session_->EvaluateNeutralPose(authored, aiming, endpoint != 2);
		const auto gun = Multiply(InverseAffine(neutral.armsCamera), neutral.weaponCamera);
		arms_->SetNeutralFit(endpoint, SkeletonPose(parents_, neutral.beforePhysical), SkeletonPose(parents_, neutral.afterPhysical), gun,
			itemOffset_, aiming ? 1. : 0.);
	}
	neutralFitDirty_ = false;
}
}
