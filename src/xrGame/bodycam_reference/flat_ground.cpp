// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "flat_ground.h"
#include "constants.h"

namespace BodycamReference
{
namespace
{
constexpr float kMinFloorDistance = 1.9f, kMaxFloorDistance = 2.4f, kFloorDistance = 2.15f;
// The floor counts as hit from 50 cm above to 45 cm below the root.
constexpr double kFloorSearchAbove = 50, kFloorSearchBelow = 45;
constexpr float kPelvisRaiseSpeed = 10, kPelvisLowerSpeed = 15;
}
FlatFloorAdjustment AdjustFlatFloorDistance(double distance)
{
	const float d = float(distance), move = d < kMinFloorDistance || d > kMaxFloorDistance ? kFloorDistance - d : 0;
	return { d + move, move };
}
const FlatGroundState& FlatGroundPelvis::Update(double dt, double left, double right, double rootZ, double floorZ, bool walkable)
{
	auto& s = state_;
	s.enables = { float(left), float(right) };
	s.hit = walkable && floorZ <= rootZ + kFloorSearchAbove && floorZ >= rootZ - kFloorSearchBelow;
	for (unsigned i = 0; i < 2; ++i)
		if (s.enables[i] > 0 && s.hit)
			s.targets[i] = { 0, 0, (floorZ + 13.5) - (rootZ + 13.5) };
	s.alpha = (double(s.enables[0]) + s.enables[1]) / 2;
	s.target = s.targets[s.targets[0][2] < s.targets[1][2] ? 0 : 1];
	s.offset = s.alpha > 0
		? VectorInterpTo(s.offset, s.target, float(dt), s.target[2] > s.offset[2] ? kPelvisRaiseSpeed : kPelvisLowerSpeed)
		: Vector{};
	return s;
}
Matrix ApplyFlatPelvisControl(
	SkeletonPose& skeleton, unsigned pelvis, unsigned head, double offset, double alpha, const std::vector<Matrix*>& flattened)
{
	if (float(alpha) <= kBlendEpsilon)
		return Compose({});
	const Matrix before = InverseAffine(skeleton.Component(head));
	const Matrix basis = InverseAffine(skeleton.ParentComponent(pelvis));
	const auto original = skeleton.Local(pelvis);
	auto target = original;
	const double dy = offset * kMetresPerCentimetre;
	for (unsigned i = 0; i < 3; ++i)
		target.position[i] += basis[4 + i] * dy;
	skeleton.SetLocal(pelvis, BlendSkeletalControl(original, target, alpha));
	const Matrix delta = Multiply(skeleton.Component(head), before);
	for (auto* matrix : flattened)
		*matrix = Multiply(delta, *matrix);
	return delta;
}
}
