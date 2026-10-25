#pragma once
#include "character_graph.h"
#include "equipment_asset.h"
#include "movement_asset.h"
#include "recoil_asset.h"
#include "regrip_asset_bank.h"
#include <optional>
#include <memory>

namespace BodycamReference
{
struct CharacterAssetPackets
{
	std::vector<std::uint8_t> upper, locomotion, rifle, recoil, movement, equipment, idle, regrip;
};
// A pose evaluated on a private neutral graph. The pose snapshots are model-local;
// the two presentation matrices are camera-relative.
struct NeutralPose
{
	Matrix armsCamera, weaponCamera;
	std::vector<LocalPose> beforePhysical, afterPhysical;
};
// The decoded assets of one equipped profile and the character graph built on them.
class CharacterSession
{
public:
	CharacterSession(const CharacterAssetPackets&, CharacterRandomSource, Rotation initialControl, double horizontalFov,
		std::optional<EquipmentTransforms> equipped = {});
	CharacterSession(const CharacterSession&) = delete;
	CharacterSession& operator=(const CharacterSession&) = delete;
	CharacterSession(CharacterSession&&) = delete;
	CharacterSession& operator=(CharacterSession&&) = delete;
	// Begins a simulation frame at most once per frame number; false when paused or already begun.
	bool BeginFrame(std::uint32_t frame, bool paused, double applicationDelta, bool aiming, bool magnified, const CharacterMovementInput&,
		const std::function<void()>& afterGrounded = {});
	bool AcceptsFrame(std::uint32_t frame, bool paused) const { return !paused && simulationFrame_ != frame; }
	const std::function<double()>& RandomUnit() const { return randomSource_.unit; }
	CharacterGraph& Graph() { return *graph_; }
	IdlePose& Idle() { return idle_; }
	const RegripAssetBank& RegripAssets() const { return regrip_; }
	const RifleAssetBank& RifleAssets() const { return rifle_; }
	void SetEquipment(const EquipmentTransforms& value, bool newlyEquipped = false)
	{
		ValidateEquipmentTransforms(value);
		equipment_ = value;
		graph_->aim.SetEquipment(value.magnifiedAim, value.cantedAim);
		if (newlyEquipped)
			graph_->motion.ResetSightModeObservation();
	}
	const EquipmentTransforms& Equipment() const { return equipment_; }
	Matrix NeutralAdsWeaponCamera() const;
	Matrix NeutralHipWeaponCamera() const;
	Matrix NeutralWeaponCamera(bool aiming) const;
	NeutralPose EvaluateNeutralPose(const std::vector<LocalPose>& authored, bool aiming, bool magnified) const;

private:
	std::unique_ptr<CharacterGraph> MakeGraph(CharacterRandomSource, Rotation control, double horizontalFov) const;
	std::unique_ptr<CharacterGraph> MakeNeutralGraph(bool aiming) const;
	PoseAssetBank poses_;
	LocomotionAssetBank locomotion_;
	RifleAssetBank rifle_;
	RecoilSetup recoil_;
	MovementCurveSampler movement_;
	EquipmentTransforms equipment_;
	IdlePose idle_;
	RegripAssetBank regrip_;
	CharacterRandomSource randomSource_;
	std::unique_ptr<CharacterGraph> graph_;
	std::optional<std::uint32_t> simulationFrame_;
};
}
