// The Reference host as player_hud owns it: binding the weapon in the hands,
// and the input and fire events the engine forwards.
#include "../xrEngine/stdafx.h"
#include "player_hud.h"
#include "bodycam_reference_host.h"
#include "bodycam_reference/automatic_binding.h"
#include "pch_script.h"
#include "Weapon.h"
#include "WeaponMagazinedWGrenade.h"
#include "WeaponKnife.h"
#include "WeaponBinoculars.h"
#include "bodycam_reference/constants.h"
#include "bodycam_reference/shot_ownership.h"
#include "bodycam_reference/ads_calibration.h"
#include "bodycam_reference/pistol_placement.h"
#include "bodycam_settings.h"
#include "bodycam_math.h"
#include "../xrEngine/svp_reference_optics.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <sstream>

namespace
{
// The motion player_hud_motion_container::load substitutes for a hand motion
// the model lacks. The alias keeps the name it asked for.
constexpr char kIdleFallbackMotion[] = "hand_idle_doun";
// Mouse counts to the degrees the library takes, before its own sensitivity.
constexpr double kMouseScale = .05;
constexpr char kTuningSection[] = "bodycam_reference_tuning";

// A comma-separated list, each item trimmed; empty items are dropped.
std::vector<std::string> TuningList(const char* key)
{
	std::vector<std::string> result;
	std::istringstream stream(BodycamReference::ReadEngineReferenceSetting(kTuningSection, key));
	for (std::string item; std::getline(stream, item, ',');)
	{
		const auto begin = item.find_first_not_of(" \t"), end = item.find_last_not_of(" \t");
		if (begin != std::string::npos)
			result.push_back(item.substr(begin, end - begin + 1));
	}
	return result;
}
template <std::size_t N>
std::array<double, N> TuningNumbers(const char* key)
{
	auto text = BodycamReference::ReadEngineReferenceSetting(kTuningSection, key);
	std::replace(text.begin(), text.end(), ',', ' ');
	std::istringstream stream(text);
	stream.imbue(std::locale::classic());
	std::array<double, N> result;
	for (auto& value : result)
		if (!(stream >> value) || !std::isfinite(value))
			throw std::invalid_argument(std::string("Invalid tuning value: ") + kTuningSection + "." + key);
	if (!(stream >> std::ws).eof())
		throw std::invalid_argument(std::string("Trailing tuning values: ") + kTuningSection + "." + key);
	return result;
}
BodycamReference::EngineReferenceTuning ReadTuning()
{
	BodycamReference::EngineReferenceTuning tuning;
	try
	{
		tuning.extraReloadAliases = TuningList("extra_reload_aliases");
		const auto pistol = TuningNumbers<3>("pistol_hip_offset_m");
		tuning.pistolHipOffset = { pistol[0], pistol[1], pistol[2] };
		auto& budgets = tuning.recoilBudgets;
		budgets.pistol = TuningNumbers<1>("recoil_budget_pistol")[0];
		budgets.smg = TuningNumbers<1>("recoil_budget_smg")[0];
		budgets.rifle = TuningNumbers<1>("recoil_budget_rifle")[0];
		budgets.shotgun = TuningNumbers<1>("recoil_budget_shotgun")[0];
		budgets.sniper = TuningNumbers<1>("recoil_budget_sniper")[0];
		tuning.scriptLayers.referenceSkipped = TuningList("reference_skipped_script_layers");
	}
	catch (const std::exception& e)
	{
		Msg("! Reference tuning unavailable, using neutral values: %s", e.what());
		tuning = {};
	}
	return tuning;
}

// The motion the model plays for an alias: its first variant.
std::string CalibrationMotion(IKinematicsAnimated& model, const player_hud_motion* alias)
{
	if (!alias || alias->m_animations.empty())
		throw std::invalid_argument("Missing idle calibration alias");
	const auto& name = alias->m_animations.front().name;
	if (model.ID_Cycle_Safe(name.c_str()).valid())
		return name.c_str();
	if (model.ID_Cycle_Safe(kIdleFallbackMotion).valid())
		return kIdleFallbackMotion;
	throw std::invalid_argument("Calibration motion cannot be resolved");
}
// The aimed idle of the weapon's mode, else the plain aimed idle, else the idle.
player_hud_motion* AimedIdleAlias(attachable_hud_item& item, CWeapon& weapon)
{
	auto* launcher = smart_cast<CWeaponMagazinedWGrenade*>(&weapon);
	auto* alias = item.m_hand_motions->find_motion(
		BodycamReference::AdsCalibrationAlias(weapon.IsGrenadeLauncherAttached(), launcher && launcher->GetGrenadeLauncherMode()));
	if (!alias || alias->m_animations.empty())
		alias = item.m_hand_motions->find_motion("anm_idle_aim");
	if (!alias || alias->m_animations.empty())
		alias = item.m_hand_motions->find_motion("anm_idle");
	return alias;
}
Fmatrix ItemOffset(const attachable_hud_item& item)
{
	return Bodycam::AttachTransform(item.m_measures.m_item_attach[0], item.m_measures.m_item_attach[1]);
}
// Maps the four arm bones of one side ("r" or "l") by the names the engine's hand models use.
void MapArm(BodycamReference::ArmCalibration& calibration, const BodycamReference::EngineSkeletonAdapter& rig,
	const BodycamReference::RifleAssetBank& rifle, const char* side)
{
	static const std::pair<const char*, const char*> bones[] = { { "clavicle", "clavicle" }, { "upperarm", "upperarm" },
		{ "lowerarm", "forearm" }, { "hand", "hand" } };
	const auto& tracks = rifle.Tracks();
	const auto end = tracks.begin() + BodycamReference::kPhysicalBoneCount;
	for (const auto& bone : bones)
	{
		const auto referenceName = std::string(bone.first) + "_" + side, engineName = std::string(side) + "_" + bone.second;
		const auto found = std::find_if(tracks.begin(), end, [&](const auto& track) { return track.name == referenceName; });
		if (found == end || !rig.FindBone(engineName.c_str()))
			throw std::invalid_argument("Unsupported arm topology: " + engineName);
		calibration.bones.push_back({ unsigned(found - tracks.begin()), engineName });
	}
}
// Offers the neutral weapon positions of the binding to the position editor
// as its defaults. A pistol's default hip position is the Reference pistol rest.
void SeedWeaponPosition(const BodycamReference::EngineCharacterHost& host, const CWeapon& weapon)
{
	Bodycam::ReferenceWeaponPosition position;
	position.hip = Bodycam::FromRows(host.NeutralHipWeaponCamera()).c;
	position.ads = Bodycam::FromRows(host.NeutralAdsWeaponCamera()).c;
	const auto pistol =
		BodycamReference::PistolHipTranslation(weapon.m_kind == "w_pistol", BodycamReference::ReferenceTuning().pistolHipOffset);
	position.hip.x += float(pistol[0]);
	position.hip.y += float(pistol[1]);
	position.hip.z -= float(pistol[2]);
	Bodycam::SeedReferenceWeaponPosition(weapon.cNameSect().c_str(), position);
}
}

const BodycamReference::EngineReferenceTuning& BodycamReference::ReferenceTuning()
{
	static const auto tuning = ReadTuning();
	return tuning;
}

BodycamReference::RecoilMetadata BodycamReference::EngineRecoilMetadata(const CWeapon& weapon)
{
	// The equipped weapon carries its upgrades; its section does not.
	RecoilMetadata metadata;
	metadata.family = RecoilFamilyFromKind(weapon.m_kind.c_str() ? weapon.m_kind.c_str() : "");
	metadata.hip = { weapon.cam_recoil.Dispersion, weapon.cam_recoil.MaxAngleVert };
	metadata.aim = { weapon.zoom_cam_recoil.Dispersion, weapon.zoom_cam_recoil.MaxAngleVert };
	metadata.budgets = ReferenceTuning().recoilBudgets;
	// Fire mode 1 is single shot; -1 is full auto and larger values are bursts. The getter is not const.
	metadata.automatic = const_cast<CWeapon&>(weapon).GetCurrentFireMode() != 1;
	return metadata;
}

bool BodycamReference::ApplyScriptLayer(bool reference, const char* name)
{
	return ReferenceTuning().scriptLayers.Apply(reference, name ? name : "");
}

// The engine's own aimed camera relative to the weapon, for the sight of zoomType.
bool player_hud::EngineAdsCameraFromWeapon(unsigned zoomType, Fmatrix& result)
{
	using namespace BodycamReference;
	if (!m_model || !m_attached_items[0] || m_ancors.empty())
		return false;
	auto* item = m_attached_items[0];
	auto* weapon = smart_cast<CWeapon*>(item->m_parent_hud_item);
	if (!weapon)
		return false;
	try
	{
		const auto name = CalibrationMotion(*m_model, AimedIdleAlias(*item, *weapon));
		if (m_engine_ads_sample_motion != name)
		{
			const EngineBasis engineBasis(kIdentityMatrix);
			EngineSkeletonAdapter rig(*m_model->dcast_PKinematics(), engineBasis);
			const auto anchor = rig.FindBone(m_model->dcast_PKinematics()->LL_GetData(m_ancors[0]).name.c_str());
			if (!anchor)
				return false;
			const auto sample = rig.SampleAuthoredPose(name.c_str());
			m_engine_ads_sample_gun = Bodycam::FromRows(engineBasis.ToEngine(sample.Component(*anchor)));
			m_engine_ads_sample_motion = name;
		}
		Fvector position, rotation, aimRotation;
		auto* launcher = smart_cast<CWeaponMagazinedWGrenade*>(weapon);
		weapon->ResolveHudOffsetTarget(
			AdsCalibrationOffset(launcher && launcher->GetGrenadeLauncherMode(), zoomType), position, rotation, aimRotation, false);
		Fmatrix aim, offset, part, gun;
		aim.setHPB(aimRotation);
		offset.rotateX(rotation.x);
		part.rotateY(rotation.y);
		offset.mulA_43(part);
		part.rotateZ(rotation.z);
		offset.mulA_43(part);
		offset.translate_over(position);
		const auto hands = Bodycam::AttachTransform(item->m_measures.m_hands_attach[0], item->m_measures.m_hands_attach[1]);
		gun.mul_43(m_engine_ads_sample_gun, ItemOffset(*item));
		result.mul_43(aim, offset);
		result.mulB_43(hands);
		result.mulB_43(gun);
		return true;
	}
	catch (const std::exception& e)
	{
		Msg("! Reference aimed camera unavailable: %s", e.what());
		return false;
	}
}

// Follows the bound weapon's sight and mode: both change the engine's aimed camera.
void player_hud::RefreshReferenceEquipment()
{
	using namespace BodycamReference;
	if (!m_reference_host || !m_reference_host->HasWeaponBinding())
		return;
	Fmatrix aim, canted;
	if (!EngineAdsCameraFromWeapon(0, aim) || !EngineAdsCameraFromWeapon(1, canted))
		return;
	if (m_reference_pose_aim_motion != m_engine_ads_sample_motion)
	{
		const auto anchor = m_model->dcast_PKinematics()->LL_GetData(m_ancors[0]).name;
		m_reference_host->CalibrateWeapon(
			m_engine_ads_sample_motion.c_str(), anchor.c_str(), Bodycam::ToRows(ItemOffset(*m_attached_items[0])));
		m_reference_pose_aim_motion = m_engine_ads_sample_motion;
	}
	if (Bodycam::ToRows(aim) == Bodycam::ToRows(m_reference_last_aim) &&
		Bodycam::ToRows(canted) == Bodycam::ToRows(m_reference_last_canted))
		return;
	const auto& basis = m_reference_host->Basis();
	m_reference_host->SetEquipment(AutomaticEquipment(basis.ToReference(Bodycam::ToRows(aim)), basis.ToReference(Bodycam::ToRows(canted))));
	if (auto* weapon = smart_cast<CWeapon*>(m_attached_items[0]->m_parent_hud_item))
		SeedWeaponPosition(*m_reference_host, *weapon);
	m_reference_last_aim = aim;
	m_reference_last_canted = canted;
}

// Binds the primary item, which the caller has checked is this weapon on both
// arm models. A weapon that cannot be bound is reported once and stays on the
// engine's HUD until it is equipped again.
void player_hud::BindReferenceWeapon(CWeapon& weapon)
{
	using namespace BodycamReference;
	try
	{
		if (m_ancors.empty())
			throw std::invalid_argument("Hand models are not ready");
		auto* item = m_attached_items[0];
		auto& rightModel = *m_model->dcast_PKinematics();
		auto& leftModel = *m_model_2->dcast_PKinematics();
		const auto& basis = m_reference_host->Basis();
		const auto& rifle = m_reference_host->RifleAssets();
		EngineSkeletonAdapter rightRig(rightModel, basis), leftRig(leftModel, basis);
		const auto* idle = item->m_hand_motions->find_motion("anm_idle");
		ArmCalibration right, left;
		right.motion = CalibrationMotion(*m_model, idle);
		left.motion = CalibrationMotion(*m_model_2, idle);
		const auto rightPose = rightRig.SampleAuthoredPose(right.motion.c_str());
		const auto anchorName = rightModel.LL_GetData(m_ancors[0]).name;
		const auto anchor = rightRig.FindBone(anchorName.c_str());
		if (!anchor)
			throw std::invalid_argument("Weapon anchor bone hidden or missing");
		// The model root that puts the engine's idle weapon on the Reference one.
		const auto itemOffset = ItemOffset(*item);
		const auto engineGun = Multiply(rightPose.Component(*anchor), basis.ToReference(Bodycam::ToRows(itemOffset)));
		const auto referenceGun = rifle.CreateReference().EvaluateLeft(0).gun;
		right.referenceFromModel = left.referenceFromModel = AutomaticModelRoot(referenceGun, engineGun);
		MapArm(right, rightRig, rifle, "r");
		MapArm(left, leftRig, rifle, "l");
		Fmatrix aim, canted;
		if (!EngineAdsCameraFromWeapon(0, aim) || !EngineAdsCameraFromWeapon(1, canted))
			throw std::invalid_argument("Aimed camera unavailable");
		const auto equipment = AutomaticEquipment(basis.ToReference(Bodycam::ToRows(aim)), basis.ToReference(Bodycam::ToRows(canted)));
		m_reference_host->BindArms(rightModel, leftModel, right, left);
		ConfigureReferenceActions();
		const auto aimMotion = CalibrationMotion(*m_model, AimedIdleAlias(*item, weapon));
		m_reference_host->CalibrateWeapon(aimMotion.c_str(), anchorName.c_str(), Bodycam::ToRows(itemOffset));
		m_reference_pose_aim_motion = aimMotion;
		m_reference_host->SetEquipment(equipment);
		SeedWeaponPosition(*m_reference_host, weapon);
		m_reference_last_aim = aim;
		m_reference_last_canted = canted;
	}
	catch (const std::exception& e)
	{
		UnbindReferenceWeapon();
		Msg("! Reference weapon binding unavailable: section=%s reason=%s", weapon.cNameSect().c_str(), e.what());
	}
	m_reference_profile_attempted = true;
}

// Tells the host which motion of each arm model plays for which alias of the bound weapon.
void player_hud::ConfigureReferenceActions()
{
	using namespace BodycamReference;
	auto* item = m_attached_items[0];
	IKinematicsAnimated* models[] = { m_model, m_model_2 };
	std::vector<ActionAlias> aliases;
	for (const auto& alias : item->m_hand_motions->m_anims)
		for (const auto& variant : alias.m_animations)
		{
			auto* itemModel = item->m_model->dcast_PKinematicsAnimated();
			const auto itemMotion = alias.m_base_name != alias.m_additional_name ? alias.m_additional_name : variant.name;
			const bool itemResolved = !itemModel || itemModel->ID_Cycle_Safe(itemMotion.c_str()).valid();
			for (unsigned side = 0; side < 2; ++side)
			{
				auto id = models[side]->ID_Cycle_Safe(variant.name.c_str());
				const bool fallback = !id.valid();
				if (fallback)
					id = models[side]->ID_Cycle_Safe(kIdleFallbackMotion);
				if (id.valid())
					aliases.push_back(
						{ HandMotionKey(side, id.val), alias.m_alias_name.c_str(), !fallback && itemResolved, variant.name.c_str() });
			}
		}
	// The fallback motion is an idle whichever aliases resolve to it, so it is never an action.
	for (unsigned side = 0; side < 2; ++side)
	{
		const auto id = models[side]->ID_Cycle_Safe(kIdleFallbackMotion);
		if (id.valid())
			aliases.push_back({ HandMotionKey(side, id.val), "anm_idle" });
	}
	m_reference_host->ConfigureActions(aliases,
		ReadEngineReferencePacket(ReadEngineReferenceSetting(kCharacterSection, "native_action_semantics")),
		ReferenceTuning().extraReloadAliases);
}

// Also runs while the HUD loads, when there is no host yet.
void player_hud::UnbindReferenceWeapon()
{
	svp_reference_optics_clear();
	m_reference_profile_attempted = false;
	m_engine_ads_sample_motion.clear();
	m_reference_pose_aim_motion.clear();
	Device.reference_hud_projection_frame = u32(-1);
	if (m_reference_host)
		m_reference_host->UnbindWeapon();
}

void player_hud::ResetReferenceHost()
{
	svp_reference_optics_clear();
	m_reference_profile_attempted = false;
	if (!m_reference_host)
		return;
	Device.reference_hud_projection_frame = u32(-1);
	Bodycam::GetMotionSelection().DetachActor();
	delete m_reference_host;
	m_reference_host = nullptr;
	m_reference_actor = nullptr;
}

bool player_hud::ReferenceOwnsCamera() const
{
	return m_reference_host && Bodycam::ReferenceActive();
}

// True when the Reference host poses the primary item; with an item, when it is that item.
bool player_hud::ReferenceOwnsMotion(const CHudItem* item) const
{
	return ReferenceOwnsCamera() && m_reference_host->HasWeaponBinding() && m_attached_items[0] &&
		(!item || m_attached_items[0]->m_parent_hud_item == item);
}

void player_hud::ReferenceSelectPlayedAction(std::uint64_t motion, const char* alias)
{
	if (!m_reference_host || !m_reference_host->HasWeaponBinding() || !alias)
		return;
	HudHandPlayback playback;
	if (HandPlaybackSnapshot(playback) && playback.motion.val == motion)
		m_reference_host->SelectAction(BodycamReference::HandMotionKey(playback.model, playback.motion.val), playback.generation, alias);
}

bool player_hud::ReferenceMouse(int dx, int dy)
{
	if (!ReferenceOwnsCamera())
		return false;
	if (!Device.Paused())
		m_reference_host->AddMouse(double(dx) * kMouseScale, -double(dy) * kMouseScale);
	return true;
}

bool player_hud::ReferenceFire(bool firing, bool aiming, const CHudItem* item, bool cancelled)
{
	if (!item)
		return false;
	const auto* firearm = smart_cast<const CWeapon*>(item);
	const bool tool = smart_cast<const CWeaponKnife*>(item) || smart_cast<const CWeaponBinoculars*>(item);
	using namespace BodycamReference;
	if (!OwnsEngineShot(
			{ ReferenceOwnsCamera(), m_attached_items[0] && m_attached_items[0]->m_parent_hud_item == item, firearm != nullptr, tool }))
		return false;
	try
	{
		if (firing)
		{
			const auto metadata = EngineRecoilMetadata(*firearm);
			m_reference_host->ConfigureRecoil(metadata);
			m_reference_host->BeginShot(aiming, metadata.automatic);
		}
		else
			m_reference_host->StopFire(cancelled);
		return true;
	}
	catch (const std::exception& error)
	{
		Msg("! Reference fire event failed: %s", error.what());
	}
	UnbindReferenceWeapon();
	return false;
}
