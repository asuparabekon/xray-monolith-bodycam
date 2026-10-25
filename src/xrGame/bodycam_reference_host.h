#pragma once
#include "bodycam_reference/action_semantics.h"
#include "bodycam_reference/action_aliases.h"
#include "bodycam_reference/recoil_policy.h"
#include "bodycam_reference/script_layer_policy.h"
#include "bodycam_reference_assets.h"
#include "bodycam_reference_arms.h"
#include "bodycam_reference/camera_intensity.h"
#include "bodycam_reference/stance_clearance.h"

class CWeapon;

namespace BodycamReference
{
// Host game tuning from the [bodycam_reference_tuning] configuration section, read once
// (bodycam_reference_host_bind.cpp). A missing or malformed section is reported once and gives the
// library's neutral values.
struct EngineReferenceTuning
{
	std::vector<std::string> extraReloadAliases;
	Vector pistolHipOffset{};
	RecoilBudgets recoilBudgets;
	ScriptLayerPolicy scriptLayers;
};
const EngineReferenceTuning& ReferenceTuning();
// The recoil metadata of the equipped weapon: its upgrades, family budget and fire mode.
RecoilMetadata EngineRecoilMetadata(const CWeapon& weapon);
// Whether player_hud applies a script layer this frame.
bool ApplyScriptLayer(bool reference, const char* name);

// Identifies a hand motion to the host. The two arm models number their
// motions separately: model 0 is the right arm, 1 the left.
inline std::uint64_t HandMotionKey(unsigned model, unsigned motion)
{
	return (std::uint64_t(model) << 32) | motion;
}

// One solved frame in engine coordinates. All but cameraWorld are relative to the camera.
struct EngineCharacterPose
{
	EngineRowMatrix cameraWorld, weaponCamera;
	EngineRowMatrix rightModelCamera, leftModelCamera;
	// The weapon's motion and its neutral hip pose; their difference carries
	// the HUD of an item that has no weapon binding.
	EngineRowMatrix hudMotion, neutralHudCamera;
};
// What the engine observed this frame.
struct EngineCharacterView
{
	CameraActorPosition actor;
	double horizontalFov = 90;
	bool scopeCapture = false, cameraHit = false;
	double cameraHitDistanceCm = 0;
	// The engine's left hand relative to its right hand.
	std::optional<EngineRowMatrix> leftHandFromRight;
	double aimBlend = 0; // The engine's own ADS progress, 0..1.
	double motionInfluence = 1; // 0 keeps the engine's HUD animation exactly; 1 is full Reference.
	// Where the neutral weapon sits, in camera-local engine metres.
	struct WeaponPosition
	{
		Vector hip, ads;
	};
	std::optional<WeaponPosition> weaponPosition;
	bool crouched = false, lowCrouched = false;
	double lookDownRadians = 0;
};

// The Reference character of one actor: always its camera, and the arms and
// weapon while a weapon is bound. The engine plays its own animations and
// answers the collision queries.
class EngineCharacterHost
{
public:
	EngineCharacterHost(std::unique_ptr<CharacterSession>, const EngineBasis&);
	EngineCharacterHost(const EngineCharacterHost&) = delete;
	EngineCharacterHost& operator=(const EngineCharacterHost&) = delete;

	// Replaces the weapon binding. Throws, keeping the old one, when a calibration does not fit its model.
	void BindArms(IKinematics& right, IKinematics& left, const ArmCalibration& rightCalibration, const ArmCalibration& leftCalibration);
	void UnbindWeapon();
	bool HasWeaponBinding() const { return bool(arms_); }
	// See EngineArmsAdapter::CalibrateWeapon. itemOffset places the weapon on its anchor bone.
	void CalibrateWeapon(const char* aimMotion, const char* anchor, const EngineRowMatrix& itemOffset);
	void SetEquipment(const EquipmentTransforms&);
	const RifleAssetBank& RifleAssets() const { return session_->RifleAssets(); }
	const EngineBasis& Basis() const { return basis_; }

	// The action clips among the bound weapon's hand motions.
	void ConfigureActions(const std::vector<ActionAlias>& aliases, const std::vector<std::uint8_t>& semanticsPacket,
		const std::vector<std::string>& extraReloadAliases = {});
	void SelectAction(std::uint64_t motion, std::uint64_t generation, const std::string& alias)
	{
		actions_.Select(motion, generation, alias);
	}
	std::optional<std::string> AimedAlias(const std::string& alias) const { return actions_.AimedAliasFor(alias); }
	bool IsAction(std::uint64_t motion) const { return actions_.HasMotion(motion); }
	bool SameActionClip(std::uint64_t a, std::uint64_t b) const { return actions_.SameClip(a, b); }
	bool SameActionKind(std::uint64_t a, std::uint64_t b) const { return actions_.SameKind(a, b); }
	bool ActionIsReload(std::uint64_t motion) const { return actions_.IsReload(motion); }
	bool ActionPreservesAim(std::uint64_t motion) const { return actions_.PreservesAim(motion); }
	// False when this playback of the motion has been admitted already.
	bool AdmitAction(std::uint64_t motion, std::uint64_t generation, bool crouched, bool empty, bool aimed);
	void SetActionSamples(std::vector<ActionSample> samples);
	void SetReloadPhase(ReloadPhase phase);
	AnimationCurves ActionCurves(const AnimationCurves& base) const;

	void ConfigureRecoil(const RecoilMetadata&);
	// An automatic shot (full-auto or burst) also sets the automatic pushback.
	void BeginShot(bool aiming, bool automatic = false);
	void StopFire(bool cancelled);
	void AddMouse(double x, double y);
	// Replaces the aim direction. Call before BeginFrame.
	void SetAimControl(const Rotation&);
	// The actor's velocity after a physics step, in Reference X/Z metres per
	// second. The next BeginFrame takes it, once per step.
	void ObserveVelocity(std::uint64_t physicsStep, double x, double z);

	bool CompletedFrame(std::uint32_t frame) const { return completedFrame_ && *completedFrame_ == frame; }
	void BeginFrame(std::uint32_t frame, double applicationSeconds, bool aiming, bool magnified, const CharacterMovementInput&);
	// Completes the frame BeginFrame started; writes the arm models when a weapon is bound.
	void Solve(const EngineCharacterView&, const ActionCurvePublication&);
	// Null between BeginFrame and Solve.
	const EngineCharacterPose* Pose() const { return pose_ ? &*pose_ : nullptr; }
	const MotionFrameOutput& Motion() const { return session_->Graph().motion.Output(); }
	std::optional<double> LocomotionPhase() const
	{
		const auto* frame = session_->Graph().primary.LastLocomotion();
		return frame ? frame->upper.footPhase : std::optional<double>{};
	}

	// The neutral weapon in camera space, and the neutral aimed eye in weapon space.
	EngineRowMatrix NeutralAdsWeaponCamera() const { return basis_.CameraRelativeToEngine(neutralAdsWeapon_); }
	EngineRowMatrix NeutralHipWeaponCamera() const { return basis_.CameraRelativeToEngine(neutralHipWeapon_); }
	EngineRowMatrix NeutralEyeInWeapon() const { return basis_.ToEngine(InverseAffine(neutralAdsWeapon_)); }

private:
	const std::vector<LocalPose>& CapturePose();
	Rotation PrepareRegrip(ViewmodelPoseInput&, const EngineCharacterView&);
	EngineCharacterPose PreparePresentation(const CharacterPresentation&, const EngineCharacterView&);
	void ApplyWeaponPlacement(EngineCharacterPose&, const EngineCharacterView&, const Vector& clearance) const;
	void RefreshNeutralFit();

	std::unique_ptr<CharacterSession> session_;
	EngineBasis basis_;
	std::vector<int> parents_;
	// The stance and the solved skeleton of every frame, built once with the physical topology.
	SkeletonPose idlePose_, solvedPose_;
	std::vector<RegripSample> regripSamples_;
	EquipmentTransforms cameraEquipment_;
	Matrix neutralHipWeapon_;
	Matrix neutralAdsWeapon_ = kIdentityMatrix;

	std::unique_ptr<EngineArmsAdapter> arms_;
	Matrix itemOffset_ = kIdentityMatrix;
	bool neutralFitDirty_ = true;
	GenericActions actions_;
	std::unique_ptr<ActionSemantics> semantics_;

	std::uint32_t frame_ = 0;
	std::optional<std::uint32_t> completedFrame_;
	std::optional<EngineCharacterPose> pose_;
	double frameSeconds_ = 0;
	bool aiming_ = false, magnified_ = false, firing_ = false;
	struct Velocity
	{
		double x, z;
	};
	std::optional<Velocity> observedVelocity_;
	std::optional<std::uint64_t> observedVelocityStep_;
	CameraIntensity intensity_{ 2, 0 }; // Animation scale 2 and no regrip before the first frame.
	StanceClearance stanceClearance_;
};
}
