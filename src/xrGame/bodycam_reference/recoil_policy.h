#pragma once
#include "constants.h"
#include "recoil_pose.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace BodycamReference
{
// Recoil family from the weapon's `kind` config value.
enum class RecoilFamily
{
	Unknown,
	Pistol,
	Smg,
	Rifle,
	Shotgun,
	Sniper
};

inline RecoilFamily RecoilFamilyFromKind(std::string_view kind)
{
	if (kind == "w_pistol")
		return RecoilFamily::Pistol;
	if (kind == "w_smg")
		return RecoilFamily::Smg;
	if (kind == "w_rifle" || kind == "w_lmg")
		return RecoilFamily::Rifle;
	if (kind == "w_shotgun")
		return RecoilFamily::Shotgun;
	if (kind == "w_sniper")
		return RecoilFamily::Sniper;
	return RecoilFamily::Unknown;
}

struct RecoilAngles
{
	// CWeapon::CameraRecoil stores these in radians after config conversion.
	double dispersionRadians = 0;
	double maxVerticalRadians = 0;
};
// Typical dispersion / maximum vertical recoil ratio of each family's weapons in the host game.
// A weapon at or above its family's ratio keeps the full gun recoil; lower ratios scale it down to .8.
// Zero keeps the full gun recoil.
struct RecoilBudgets
{
	double pistol = 0, smg = 0, rifle = 0, shotgun = 0, sniper = 0;
};
struct RecoilMetadata
{
	RecoilFamily family = RecoilFamily::Unknown;
	RecoilAngles hip, aim;
	RecoilBudgets budgets;
	// Full-auto or burst fire: each shot sets the automatic pushback.
	bool automatic = false;
};
struct RecoilPolicy
{
	double hipScale = 1, aimScale = 1;

	double GunPoseScale(bool aiming) const { return aiming ? aimScale : hipScale; }
};

inline double RecoilBudgetFraction(RecoilFamily family, const RecoilBudgets& budgets)
{
	switch (family)
	{
	case RecoilFamily::Pistol: return budgets.pistol;
	case RecoilFamily::Smg: return budgets.smg;
	case RecoilFamily::Rifle: return budgets.rifle;
	case RecoilFamily::Shotgun: return budgets.shotgun;
	case RecoilFamily::Sniper: return budgets.sniper;
	default: return 0;
	}
}

inline double RecoilGunPoseScale(const RecoilAngles& angles, double budget)
{
	if (!std::isfinite(budget) || budget <= 0 || !std::isfinite(angles.dispersionRadians) || angles.dispersionRadians < 0 ||
		angles.dispersionRadians > kPi || !std::isfinite(angles.maxVerticalRadians) || angles.maxVerticalRadians <= 0 ||
		angles.maxVerticalRadians > kPi)
		return 1; // Unavailable or malformed metadata keeps the unscaled gun pose.
	const double fraction = angles.dispersionRadians / angles.maxVerticalRadians;
	return .8 + .2 * std::clamp(fraction / budget, 0., 1.);
}

inline RecoilPolicy BuildRecoilPolicy(const RecoilMetadata& metadata)
{
	const double budget = RecoilBudgetFraction(metadata.family, metadata.budgets);
	return { RecoilGunPoseScale(metadata.hip, budget), RecoilGunPoseScale(metadata.aim, budget) };
}

inline RecoilPose ScaleRecoilGunPose(RecoilPose pose, double scale)
{
	// Scales the procedural gun recoil only.
	if (scale == 1)
		return pose;
	pose.rotation.pitch *= scale;
	pose.rotation.yaw *= scale;
	pose.rotation.roll *= scale;
	for (auto& value : pose.location)
		value *= scale;
	return pose;
}
}
