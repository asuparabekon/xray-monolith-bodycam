#pragma once
#include "ads_pose.h"
#include "recoil_curve.h"
#include "recoil_pose.h"

namespace BodycamReference
{
Matrix CopySwayPole(const Matrix& current, const Matrix& source, const Matrix& handParent);
Matrix HandLocalAfterGunCopy(const Matrix& copiedGun, const Matrix& incomingHand, const Matrix& sourceHand);
Matrix ApplyRecoilPose(const Matrix& gun, const RecoilPose& snapshot);
Matrix ApplyLowerSway(Matrix gun, double centimeters);
enum class GripPoseKind
{
	None,
	Authored,
	Other
};
class LeftElbowOffset
{
public:
	const Vector& Update(bool aiming, double deltaSeconds, bool weaponValid, GripPoseKind grip, const Vector& hip = Vector{},
		const Vector& aim = Vector{ 0, 0, 7.5 });

private:
	Vector value_{};
};
class LowerSway
{
public:
	LowerSway();
	double Update(double lookDeltaX, bool aiming, double deltaSeconds, bool curveValid);

private:
	RecoilCurve curve_;
	double value_ = 0;
};
}
