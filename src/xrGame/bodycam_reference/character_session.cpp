#include "character_session.h"
#include "constants.h"
#include <utility>

namespace BodycamReference
{
namespace
{
EquipmentTransforms ResolveEquipment(const std::vector<std::uint8_t>& packet, const std::optional<EquipmentTransforms>& equipped)
{
	// The packet is decoded even when equipped replaces it, so a damaged packet is still rejected.
	const auto reference = LoadEquipmentTransforms(packet);
	if (!equipped)
		return reference;
	ValidateEquipmentTransforms(*equipped);
	return *equipped;
}
std::vector<std::string> PhysicalNames(const RifleAssetBank& rifle)
{
	std::vector<std::string> names;
	names.reserve(kPhysicalBoneCount);
	for (unsigned i = 0; i < kPhysicalBoneCount; ++i)
		names.push_back(rifle.Tracks()[i].name);
	return names;
}
// The neutral evaluations run on a private graph with a constant random source, so they neither
// read nor advance the live graph. The draws are the midpoints of their ranges.
constexpr unsigned kNeutralDraw15 = 16384;
constexpr double kNeutralUnitDraw = .5;
constexpr double kNeutralFrameSeconds = 1. / 60;
constexpr double kNeutralFov = 90;
CharacterMovementInput NeutralMovementInput()
{
	CharacterMovementInput movement;
	movement.cameraShake = movement.weaponSway = movement.stressMotion = movement.sightRegrip = false;
	return movement;
}
}
std::unique_ptr<CharacterGraph> CharacterSession::MakeGraph(CharacterRandomSource random, Rotation control, double fov) const
{
	return std::make_unique<CharacterGraph>(recoil_, regrip_.Durations(), movement_, poses_, locomotion_, rifle_, equipment_.magnifiedAim,
		equipment_.cantedAim, std::move(random), control, fov);
}
std::unique_ptr<CharacterGraph> CharacterSession::MakeNeutralGraph(bool aiming) const
{
	auto neutral = MakeGraph({ [] { return kNeutralDraw15; }, [] { return kNeutralUnitDraw; } }, {}, kNeutralFov);
	neutral->motion.SeedControl({}, aiming);
	neutral->movement.SeedControl({}, 0, 0);
	return neutral;
}
CharacterSession::CharacterSession(const CharacterAssetPackets& data, CharacterRandomSource random, Rotation control, double fov,
	std::optional<EquipmentTransforms> equipped)
	: poses_(PoseAssetBank::Load(data.upper)), locomotion_(LocomotionAssetBank::Load(data.locomotion)),
	  rifle_(RifleAssetBank::Load(data.rifle)), recoil_(LoadRecoilSetup(data.recoil)), movement_(LoadMovementSampler(data.movement)),
	  equipment_(ResolveEquipment(data.equipment, equipped)), idle_(LoadIdlePose(data.idle)),
	  regrip_(RegripAssetBank::Load(data.regrip, PhysicalNames(rifle_))), randomSource_(std::move(random)),
	  graph_(MakeGraph(randomSource_, control, fov))
{
}
Matrix CharacterSession::NeutralAdsWeaponCamera() const
{
	return NeutralWeaponCamera(true);
}
Matrix CharacterSession::NeutralHipWeaponCamera() const
{
	return NeutralWeaponCamera(false);
}
Matrix CharacterSession::NeutralWeaponCamera(bool aiming) const
{
	const auto neutral = MakeNeutralGraph(aiming);
	auto idle = idle_;
	idle.ResetTime();
	// The seeded graph starts at full ADS, so one frame reaches the neutral endpoint.
	neutral->character.Begin(kNeutralFrameSeconds, aiming, true, NeutralMovementInput());
	neutral->character.Evaluate(idle, ViewmodelPoseInput{});
	neutral->character.Complete(false, false, 0);
	return neutral->character.Present({}, {}, kNeutralFov).weaponCamera;
}
NeutralPose CharacterSession::EvaluateNeutralPose(const std::vector<LocalPose>& authored, bool aiming, bool magnified) const
{
	const auto neutral = MakeNeutralGraph(aiming);
	neutral->primary.EnableEngineMotion();
	// A graph starts in magnified mode; start at the requested endpoint so a canted frame does not
	// sample the transition between the two.
	neutral->aim = AimPointBlend(equipment_.magnifiedAim, equipment_.cantedAim, magnified);
	ViewmodelPoseInput input;
	input.engineActionPlacementWeight = 1;
	neutral->character.Begin(kNeutralFrameSeconds, aiming, magnified, NeutralMovementInput());
	const auto solved = neutral->character.Evaluate(authored, input);
	neutral->character.Complete(false, false, 0);
	const auto presentation = neutral->character.Present({}, {}, kNeutralFov);
	return { presentation.armsCamera, presentation.weaponCamera, authored, solved.physical };
}
bool CharacterSession::BeginFrame(std::uint32_t frame, bool paused, double delta, bool aiming, bool magnified,
	const CharacterMovementInput& input, const std::function<void()>& afterGrounded)
{
	if (!AcceptsFrame(frame, paused))
		return false;
	graph_->character.Begin(delta, aiming, magnified, input, afterGrounded);
	simulationFrame_ = frame;
	return true;
}
}
