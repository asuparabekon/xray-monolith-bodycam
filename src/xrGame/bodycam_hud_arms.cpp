#include "stdafx.h"
#include "bodycam_hud_arms.h"
#include "bodycam_camera.h"
#include "bodycam_simulation.h"
#include "player_hud.h"
#include "Weapon.h"
#include "Actor.h"
#include "WeaponKnife.h"
#include "WeaponBinoculars.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "bodycam_settings.h"
#include "bodycam_math.h"
#include "Level.h"
#include "bodycam_reference/constants.h"
#include "bodycam_reference/regrip_presentation.h"
#include "bodycam_reference_assets.h"
#include "bodycam_reference_host.h"
#include "bodycam_reference/classic_regrip.h"
#include "bodycam_reference/classic_recoil.h"
#include "bodycam_reference/recoil_asset.h"
#include "bodycam_reference/classic_reach.h"
#include "bodycam_reference/classic_sway.h"
#include "bodycam_reference/classic_arm_settle.h"
#include "classic_handling.h"
#include "../xrEngine/CameraBase.h"
#include "bodycam_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace Bodycam
{
namespace
{
enum ArmSide
{
	kRightArm,
	kLeftArm,
	kArmCount
};

struct HudArmChain
{
	u16 clavicle = BI_NONE;
	u16 upperarm = BI_NONE;
	u16 forearm = BI_NONE;
	u16 twist = BI_NONE;
	u16 hand = BI_NONE;
	Fvector authored_pole = { 0.f, 0.f, 0.f };
	bool authored_pole_valid = false;

	// The bones the solver needs; the clavicle and the twist bone are optional.
	bool Solvable() const { return upperarm != BI_NONE && forearm != BI_NONE && hand != BI_NONE; }
};

// The two samples of a transform that are furthest apart so far.
struct MotionRange
{
	Fmatrix a, b;
	float extent = 0.f; // Radians between rotations or metres between positions.

	void Seed(const Fmatrix& sample) { a = b = sample; }
};

// How far a weapon's own walk animation moves the gun and the arm joints,
// measured over one cycle of the animation.
struct AuthoredMotionProfile
{
	bool initialized = false;
	bool complete = false;
	float previous_phase = 0.f;
	float phase_travel = 0.f;
	u32 sample_count = 0;
	MotionRange lead_rotation;
	MotionRange lead_translation;
	MotionRange wrist[kArmCount];
	MotionRange forearm[kArmCount];
	MotionRange upperarm[kArmCount];
	AuthoredMotionGains gains;

	AuthoredMotionMetrics Metrics() const
	{
		AuthoredMotionMetrics metrics;
		metrics.lead_rotation = lead_rotation.extent;
		metrics.lead_translation = lead_translation.extent;
		metrics.wrist_rotation = std::max(wrist[kRightArm].extent, wrist[kLeftArm].extent);
		metrics.forearm_rotation = std::max(forearm[kRightArm].extent, forearm[kLeftArm].extent);
		metrics.upperarm_rotation = std::max(upperarm[kRightArm].extent, upperarm[kLeftArm].extent);
		return metrics;
	}
};

// A profile is complete after this share of a cycle and this many samples.
constexpr float kProfilePhaseTravel = 0.95f;
constexpr u32 kProfileSampleCount = 20;
// A larger phase step between two samples is a restart, not travel.
constexpr float kProfileMaximumPhaseStep = 0.25f;
// Blend amount below which the hand motion is still blending in and is not sampled.
constexpr float kProfileMinimumBlend = 0.95f;
// Walking response below which the profile is not sampled.
constexpr float kProfileMinimumMovement = 0.001f;

// Fixed seeds: regrip and recoil variation is repeatable per session.
constexpr u32 kRegripSeed = 0x162bca;
constexpr u32 kRecoilSeed = 0x173ec1;
u32 NextRandom(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return state;
}

// Seconds; a longer frame is treated as one of this length.
constexpr float kMaximumStepTime = 0.05f;
// The phase follower. Phases are cycles (0..1), rates are cycles per second.
// An observed rate above the larger of these is an animation restart and is ignored.
constexpr float kRestartRateFloor = 2.5f;
constexpr float kPhaseRateOutlierScale = 2.f; // Times the followed rate.
constexpr float kPhaseRateResponse = 8.f; // 1/s
// The followed phase moves toward the observed one by at most the larger of these per frame.
constexpr float kMinimumPhaseCorrection = 0.002f;
constexpr float kPhaseCorrectionShare = 0.25f; // Share of the frame's own phase step.

float WrapCycle(float value)
{
	return value - std::floor(value);
}

float SignedCycleDelta(float value)
{
	if (value > 0.5f)
		return value - 1.f;
	if (value < -0.5f)
		return value + 1.f;
	return value;
}

// Follows the phase of the playing hand motion at a smoothed rate, so the
// motion added on top of it does not jump when the animation does.
struct AuthoredPhaseFollower
{
	float phase = 0.f;
	float observed_phase = 0.f;
	float rate = 0.f;
	bool valid = false;

	// step_time is at most kMaximumStepTime.
	void Update(float observed, float step_time)
	{
		observed = WrapCycle(observed);
		if (!valid)
		{
			phase = observed;
			observed_phase = observed;
			rate = 0.f;
			valid = true;
			return;
		}

		const float observed_delta = SignedCycleDelta(observed - observed_phase);
		observed_phase = observed;
		if (step_time > EPS_S && observed_delta >= 0.f)
		{
			const float observed_rate = observed_delta / step_time;
			const float restart_rate = std::max(kRestartRateFloor, rate * kPhaseRateOutlierScale);
			if (rate < EPS_S || observed_rate <= restart_rate)
			{
				const float rate_alpha = 1.f - std::exp(-kPhaseRateResponse * step_time);
				rate += (observed_rate - rate) * rate_alpha;
			}
		}

		float predicted = phase + rate * step_time;
		predicted = WrapCycle(predicted);
		const float phase_error = SignedCycleDelta(observed - predicted);
		const float correction_limit = std::max(kMinimumPhaseCorrection, rate * step_time * kPhaseCorrectionShare);
		phase = predicted + std::clamp(phase_error, -correction_limit, correction_limit);
		phase = WrapCycle(phase);
	}
};

u16 ResolveArmBone(IKinematics* model, LPCSTR primary, LPCSTR fallback)
{
	const u16 primary_id = model->LL_BoneID(primary);
	return primary_id != BI_NONE ? primary_id : model->LL_BoneID(fallback);
}

// Weight change per second.
constexpr float kProfileFadeOutSpeed = 6.f;
constexpr float kProfileReadyFadeInSpeed = 2.5f;
constexpr float kControllerEnterSpeed = 10.f;
constexpr float kControllerExitSpeed = 14.f;
// A weight below this is treated as zero.
constexpr float kIdleWeight = 0.0001f;
// Metres kept between the wrist and the fully stretched or fully folded arm.
constexpr float kReachMargin = 0.0001f;
// Squared metres; a smaller handling offset is not applied.
constexpr float kMinimumHandlingOffsetSq = 1e-12f;
// A script animation that takes over the lowering (influence below this) keeps
// the Classic layers off the hands.
constexpr float kFullLoweringInfluence = .999f;

// The Reference camera trace that scales regrip by the distance to what the
// player looks at: 15 m along the view, raised 0.5 m at its far end.
constexpr float kIntensityTraceLength = 15.f;
constexpr float kIntensityTraceRise = .5f;
// Reference aimpoint sources that take regrip.
constexpr double kAimpointSights = 1.;
constexpr double kAimpointScope = 2.;
// Reference's intensity before the first trace: animation scale 2, regrip 0.
constexpr double kInitialAnimationScale = 2.;
constexpr double kInitialRegrip = 0.;
// CameraPovFromWorld wants a field of view; the rotation it returns does not depend on it.
constexpr double kControlFov = 90.;

Fvector BonePosition(IKinematics* K, u16 bone_id)
{
	return K->LL_GetBoneInstance(bone_id).mTransform.c;
}

void SetBoneTransform(IKinematics* K, u16 bone_id, const Fmatrix& transform)
{
	CBoneInstance& bone = K->LL_GetBoneInstance(bone_id);
	bone.mTransform.set(transform);
	bone.mRenderTransform.mul_43(bone.mTransform, K->LL_GetData(bone_id).m2b_transform);
}

void RotateBoneTowardChild(IKinematics* K, u16 bone_id, const Fvector& old_child, const Fvector& new_origin, const Fvector& new_child)
{
	Fmatrix transform = K->LL_GetBoneInstance(bone_id).mTransform;
	Fvector old_dir;
	old_dir.sub(old_child, transform.c);
	Fvector new_dir;
	new_dir.sub(new_child, new_origin);
	if (old_dir.magnitude() < EPS_S || new_dir.magnitude() < EPS_S)
		return;
	old_dir.normalize();
	new_dir.normalize();

	const float dot = old_dir.dotproduct(new_dir);
	Fvector axis;
	axis.crossproduct(old_dir, new_dir);
	const float cross_magnitude = axis.magnitude();
	const float angle = std::atan2(cross_magnitude, dot);

	Fmatrix rotation;
	rotation.identity();
	if (cross_magnitude > EPS_S)
	{
		axis.mul(1.f / cross_magnitude);
		rotation.rotation(axis, angle);
	}
	else if (dot < 0.f)
	{
		axis.crossproduct(old_dir, Fvector().set(0.f, 1.f, 0.f));
		if (axis.magnitude() < EPS_S)
			axis.crossproduct(old_dir, Fvector().set(1.f, 0.f, 0.f));
		axis.normalize_safe();
		rotation.rotation(axis, angle);
	}

	transform.mulA_43(rotation);
	transform.c.set(new_origin);
	SetBoneTransform(K, bone_id, transform);
}

// The joint of a two-bone chain, bent toward current_joint.
Fvector SolveArmJoint(const Fvector& start, const Fvector& end, const Fvector& current_joint, float first_length, float second_length)
{
	Fvector line;
	line.sub(end, start);
	const float distance = line.magnitude();
	if (distance < EPS_S)
		return current_joint;
	line.mul(1.f / distance);

	const float along = (first_length * first_length - second_length * second_length + distance * distance) / (2.f * distance);
	const float height = _sqrt(std::max(first_length * first_length - along * along, 0.f));
	Fvector center;
	center.mad(start, line, along);
	Fvector radial;
	radial.sub(current_joint, center);
	radial.mad(line, -radial.dotproduct(line));
	if (radial.magnitude() < EPS_S)
	{
		radial.crossproduct(line, Fvector().set(0.f, 1.f, 0.f));
		if (radial.magnitude() < EPS_S)
			radial.crossproduct(line, Fvector().set(1.f, 0.f, 0.f));
	}
	radial.normalize_safe();
	center.mad(radial, height);
	return center;
}

Fmatrix RotationAboutPivot(const Fvector& pivot, const Fmatrix& rotation)
{
	Fvector rotated_pivot = pivot;
	rotation.transform_dir(rotated_pivot);
	Fmatrix delta = rotation;
	delta.c.sub(pivot, rotated_pivot);
	return delta;
}

Fvector CalculateFireAxis(
	IKinematics* hands, u16 anchor_id, const Fvector& item_position, const Fvector& item_rotation_degrees, const Fvector& fire_direction)
{
	const Fmatrix& anchor = hands->LL_GetBoneInstance(anchor_id).mTransform;
	Fvector item_rotation = item_rotation_degrees;
	item_rotation.mul(PI / 180.f);
	Fmatrix item_offset;
	item_offset.setHPB(item_rotation.x, item_rotation.y, item_rotation.z);
	item_offset.translate_over(item_position);

	Fmatrix item_transform;
	item_transform.mul_43(anchor, item_offset);
	Fvector axis = fire_direction;
	item_transform.transform_dir(axis);
	if (axis.magnitude() < EPS_S)
		axis.set(anchor.k);
	axis.normalize_safe();
	return axis;
}

Fmatrix ExtractTwistRotation(const Fmatrix& rotation, const Fvector& axis)
{
	Fquaternion source;
	source.set(rotation);
	source.normalize();
	const float projection = source.x * axis.x + source.y * axis.y + source.z * axis.z;
	Fquaternion twist;
	twist.set(source.w, axis.x * projection, axis.y * projection, axis.z * projection);
	if (twist.magnitude() < EPS_S)
		twist.identity();
	else
		twist.normalize();

	Fmatrix result;
	result.rotation(twist);
	return result;
}

Fmatrix ConvertModelDelta(const Fmatrix& source_to_world, const Fmatrix& target_to_world, const Fmatrix& source_delta)
{
	Fmatrix world_to_source;
	world_to_source.invert(source_to_world);
	Fmatrix world_delta;
	world_delta.mul_43(source_to_world, source_delta);
	world_delta.mulB_43(world_to_source);

	Fmatrix world_to_target;
	world_to_target.invert(target_to_world);
	Fmatrix target_delta;
	target_delta.mul_43(world_to_target, world_delta);
	target_delta.mulB_43(target_to_world);
	return target_delta;
}

void TransformBone(IKinematics* K, u16 bone_id, const Fmatrix& delta)
{
	Fmatrix transformed;
	transformed.mul_43(delta, K->LL_GetBoneInstance(bone_id).mTransform);
	SetBoneTransform(K, bone_id, transformed);
}

bool IsHandDescendant(IKinematics* K, u16 bone_id, u16 hand)
{
	for (u16 current = bone_id; current != BI_NONE; current = K->LL_GetData(current).GetParentID())
		if (current == hand)
			return true;
	return false;
}

// Moves the hand and its fingers by delta, with the hand ending at target_hand.
void TransformHandHierarchy(IKinematics* K, u16 hand, const Fmatrix& delta, const Fvector& target_hand)
{
	Fvector rigid_target = BonePosition(K, hand);
	delta.transform_tiny(rigid_target);
	Fvector correction;
	correction.sub(target_hand, rigid_target);
	for (u16 bone_id = 0; bone_id < K->LL_BoneCount(); ++bone_id)
	{
		if (!IsHandDescendant(K, bone_id, hand))
			continue;
		Fmatrix transformed;
		transformed.mul_43(delta, K->LL_GetBoneInstance(bone_id).mTransform);
		transformed.c.add(correction);
		SetBoneTransform(K, bone_id, transformed);
	}
}

// Moves the hand by hand_delta to target_hand, as far as the arm reaches, and
// bends the arm behind it toward pole_elbow. The shoulder stays where it is.
void SolveArmToHand(
	IKinematics* K, const HudArmChain& arm, const Fvector& target_hand, const Fvector& pole_elbow, const Fmatrix& hand_delta)
{
	const Fvector shoulder = BonePosition(K, arm.upperarm);
	const Fvector elbow = BonePosition(K, arm.forearm);
	const Fvector hand_pos = BonePosition(K, arm.hand);
	const bool has_twist = arm.twist != BI_NONE;
	const Fvector twist_pos = has_twist ? BonePosition(K, arm.twist) : elbow;
	const float upper_length = shoulder.distance_to(elbow);
	const float forearm_length = elbow.distance_to(hand_pos);

	Fvector solved_hand = target_hand;
	Fvector reach;
	reach.sub(solved_hand, shoulder);
	const float reach_length = reach.magnitude();
	const float max_reach = std::max(upper_length + forearm_length - kReachMargin, kReachMargin);
	const float min_reach = _abs(upper_length - forearm_length) + kReachMargin;
	if (reach_length > EPS_S && (reach_length > max_reach || reach_length < min_reach))
	{
		reach.mul(1.f / reach_length);
		reach.mul(std::clamp(reach_length, min_reach, max_reach));
		solved_hand.add(shoulder, reach);
	}

	const Fvector solved_elbow = SolveArmJoint(shoulder, solved_hand, pole_elbow, upper_length, forearm_length);
	RotateBoneTowardChild(K, arm.upperarm, elbow, shoulder, solved_elbow);
	RotateBoneTowardChild(K, arm.forearm, hand_pos, solved_elbow, solved_hand);

	if (has_twist)
	{
		Fvector old_forearm;
		old_forearm.sub(hand_pos, elbow);
		const float old_length_sq = old_forearm.square_magnitude();
		float twist_fraction = 0.f;
		if (old_length_sq > EPS_S)
		{
			Fvector old_twist;
			old_twist.sub(twist_pos, elbow);
			twist_fraction = std::clamp(old_twist.dotproduct(old_forearm) / old_length_sq, 0.f, 1.f);
		}
		Fvector solved_twist;
		solved_twist.lerp(solved_elbow, solved_hand, twist_fraction);
		RotateBoneTowardChild(K, arm.twist, hand_pos, solved_twist, solved_hand);
	}

	TransformHandHierarchy(K, arm.hand, hand_delta, solved_hand);
}

// The elbow pole of the weapon's own animation, smoothed by the walking
// response: weight 0 follows the animation, a larger weight follows the filter.
Fvector FilterAuthoredArmPole(HudArmChain& arm, const Fvector& current, float weight, float response, float dt)
{
	if (!arm.authored_pole_valid || weight < kIdleWeight)
	{
		arm.authored_pole.set(current);
		arm.authored_pole_valid = true;
		return current;
	}

	dt = std::clamp(dt, 0.f, kMaximumStepTime);
	const float alpha = 1.f - std::exp(-response * dt);
	Fvector filtered;
	filtered.lerp(arm.authored_pole, current, alpha);
	arm.authored_pole.set(filtered);
	Fvector result;
	result.lerp(current, filtered, weight);
	return result;
}

bool RelativeBoneTransform(IKinematics* K, u16 parent, u16 child, Fmatrix& result)
{
	if (parent == BI_NONE || child == BI_NONE)
		return false;

	Fmatrix parent_inverse;
	parent_inverse.invert(K->LL_GetBoneInstance(parent).mTransform);
	result.mul_43(parent_inverse, K->LL_GetBoneInstance(child).mTransform);
	return true;
}

float RotationDistance(const Fmatrix& reference, const Fmatrix& current)
{
	Fmatrix inverse;
	inverse.invert(reference);
	Fmatrix delta;
	delta.mul_43(inverse, current);
	const float cosine = std::clamp((delta.i.x + delta.j.y + delta.k.z - 1.f) * 0.5f, -1.f, 1.f);
	return std::acos(cosine);
}

float TranslationDistance(const Fmatrix& reference, const Fmatrix& current)
{
	return reference.c.distance_to(current.c);
}

// Replaces the nearer end of the range when the sample lies further from the
// other end than the range spans.
template <typename Distance>
void ExtendRange(MotionRange& range, const Fmatrix& sample, Distance distance)
{
	const float from_a = distance(range.a, sample);
	const float from_b = distance(range.b, sample);
	if (from_a >= from_b && from_a > range.extent)
	{
		range.b = sample;
		range.extent = from_a;
	}
	else if (from_b > range.extent)
	{
		range.a = sample;
		range.extent = from_b;
	}
}

u64 AuthoredMotionKey(u32 motion, bool ads)
{
	return (u64(motion) << 1) | u64(ads);
}

bool ReadActiveHandMotion(IKinematicsAnimated* model, const attachable_hud_item* item, u32& motion_key, float& phase, float& blend_weight)
{
	if (!model || !item || !item->m_active_hand_blend || !item->m_active_hand_motion.valid())
		return false;

	const u16 part_count = model->partitions().count();
	for (u16 part = 0; part < part_count; ++part)
	{
		const u32 blend_count = model->LL_PartBlendsCount(part);
		for (u32 blend_index = 0; blend_index < blend_count; ++blend_index)
		{
			CBlend* blend = model->LL_PartBlend(part, blend_index);
			if (blend != item->m_active_hand_blend)
				continue;
			if (!blend->playing || blend->motionID != item->m_active_hand_motion || blend->timeTotal <= EPS_S)
				return false;
			motion_key = blend->motionID.val;
			phase = std::clamp(blend->timeCurrent / blend->timeTotal, 0.f, 1.f);
			blend_weight = blend->blendAmount;
			return true;
		}
	}
	return false;
}

// The weapon states in which the controllers move the hands; in every other
// state, and during a script animation, the authored animation owns them.
bool AllowsControllerLayer(CWeapon* weapon, u8 script_anim_part)
{
	if (!weapon || script_anim_part != u8(-1))
		return false;

	switch (weapon->GetState())
	{
	case CWeapon::eIdle:
	case CWeapon::eFire:
	case CWeapon::eFire2:
	case CWeapon::eAimStart:
	case CWeapon::eAimEnd: return true;
	default: return false;
	}
}

void ClearAuthoredPoles(HudArmChain (&arms)[kArmCount])
{
	for (HudArmChain& arm : arms)
		arm.authored_pole_valid = false;
}

// The camera frame of the first-person camera, in world space. The axes are
// the camera's own and are not orthonormalised.
Fmatrix EyeFrame(const CCameraBase& camera)
{
	Fmatrix eye;
	eye.identity();
	eye.k = camera.vDirection;
	eye.j = camera.vNormal;
	eye.i.crossproduct(eye.j, eye.k);
	eye.c = camera.vPosition;
	return eye;
}

BodycamReference::Vector ToVector(const Fvector& value)
{
	return { value.x, value.y, value.z };
}

BodycamReference::Vector Position(const BodycamReference::Matrix& matrix)
{
	return { matrix[12], matrix[13], matrix[14] };
}

BodycamReference::Matrix ReferenceFromEngine(const BodycamReference::EngineBasis& basis, const Fmatrix& matrix)
{
	return basis.ToReference(ToRows(matrix));
}

Fmatrix EngineFromReference(const BodycamReference::EngineBasis& basis, const BodycamReference::Matrix& matrix)
{
	return FromRows(basis.ToEngine(matrix));
}

BodycamReference::Matrix ReferenceFromEnginePoint(const BodycamReference::EngineBasis& basis, const Fvector& point)
{
	Fmatrix translation;
	translation.identity();
	translation.c = point;
	return ReferenceFromEngine(basis, translation);
}

// The elbow pole after the arm takes up the twist of the gun between two gun
// poses about the shoulder-to-wrist axis. Engine points, Reference gun poses.
Fvector SettleArmPole(const BodycamReference::EngineBasis& basis, const Fvector& shoulder, const Fvector& wrist, const Fvector& pole,
	const BodycamReference::Matrix& baseline_gun, const BodycamReference::Matrix& animated_gun)
{
	auto settled = ReferenceFromEnginePoint(basis, pole);
	const auto point = BodycamReference::ClassicArmSettlePole(Position(ReferenceFromEnginePoint(basis, shoulder)),
		Position(ReferenceFromEnginePoint(basis, wrist)), Position(settled), baseline_gun, animated_gun);
	settled[12] = point[0];
	settled[13] = point[1];
	settled[14] = point[2];
	return EngineFromReference(basis, settled).c;
}

// What one frame's controller solve needs, in primary-model space.
struct ControllerFrame
{
	IKinematics* primary = nullptr;
	IKinematics* support = nullptr; // Left-arm model; null when the left arm is not solved.
	Fmatrix delta; // Controller motion of the gun and both grips.
	Fmatrix arm_follow; // Extra motion of the elbow poles only.
	float movement_weight = 0.f;
	float mouse_magnitude = 0.f; // Diagnostics only.
	float dt = 0.f;
	bool regrip = false, sway = false, recoil = false;
};

// The targets of one Classic solve, in primary-model space unless noted.
// Without a left arm the left goal and pole repeat the right ones and the
// other left fields are not set.
struct ClassicTargets
{
	Fmatrix combined; // Controller and handling motion, before the Reference layers.
	Fmatrix gun, right_goal, left_goal;
	Fvector right_pole, left_pole;
	// The poles before handling and the Reference layers move them.
	Fvector right_base_pole, left_base_pole;
	// The animated hands this frame. original_left is in support-model space.
	Fmatrix original_right, original_left, original_left_primary;
	Fmatrix left_to_primary, primary_to_left;
};

// Reference-space targets as engine goals and poles.
void ReadReferenceTargets(
	const BodycamReference::EngineBasis& basis, const BodycamReference::RegripTargets& source, ClassicTargets& targets)
{
	targets.gun = EngineFromReference(basis, source.gun);
	targets.right_goal.mul_43(targets.gun, EngineFromReference(basis, source.handR));
	targets.left_goal.mul_43(targets.gun, EngineFromReference(basis, source.handL));
	targets.right_pole = EngineFromReference(basis, source.poleR).c;
	targets.gun.transform_tiny(targets.right_pole);
	targets.left_pole = EngineFromReference(basis, source.poleL).c;
	targets.left_goal.transform_tiny(targets.left_pole);
}

Classic::Vec3 ToVec3(const Fvector& value)
{
	return { value.x, value.y, value.z };
}

constexpr LPCSTR kCharacterSection = "bodycam_reference_character";

// Reads the Reference asset profile from the game configuration; a missing
// entry throws std::invalid_argument with the given text.
BodycamReference::EngineProfileReader MakeProfileReader(LPCSTR missing)
{
	return BodycamReference::EngineProfileReader(
		[missing](const std::string& section, const std::string& key)
		{
			if (!pSettings->section_exist(section.c_str()) || !pSettings->line_exist(section.c_str(), key.c_str()))
				throw std::invalid_argument(missing);
			return std::string(pSettings->r_string(section.c_str(), key.c_str()));
		},
		{});
}
} // namespace

struct HudArms::State
{
	explicit State(player_hud& owner) : m_hud(owner) {}
	void Reset();
	void OnWeaponChanged(const attachable_hud_item* item);
	void UpdateAndApply(const ArmPose& pose, float dt);
	bool ClassicRegripCamera(Fmatrix& local_delta) const;
	bool ClassicRecoilFire(bool firing, bool aiming, const CWeapon* weapon);
	void DumpAuthoredMotionProfile() const;

	void ReadSimulationPose(const ArmPose& pose);
	void UpdateAuthoredMotionProfile(float dt);
	void UpdateClassicLayers(float dt, CWeapon* weapon, bool allowed);
	void UpdateClassicRegrip(float dt, CWeapon* weapon, bool allowed);
	void UpdateClassicRecoil(float dt, CWeapon* weapon, bool allowed);
	void UpdateClassicHandling(float dt, CWeapon* weapon, bool allowed);
	bool LoadReferenceRig();
	bool LoadRegripAssets();
	bool LoadClassicRecoil();
	bool FullLoweringInfluence() const;
	Fmatrix ClassicParentFrame() const;
	void SolveControllers(float dt);
	void SolveStalker2Controller(const ControllerFrame& frame);
	void SolveClassicController(const ControllerFrame& frame);
	void BuildClassicTargets(const ControllerFrame& frame, ClassicTargets& targets);
	void ApplyReferenceLayers(const ControllerFrame& frame, ClassicTargets& targets);
	BodycamReference::ClassicRigidLimit LimitClassicReach(const ControllerFrame& frame, const ClassicTargets& targets) const;
	void SolveClassicArms(const ControllerFrame& frame, ClassicTargets& targets, const BodycamReference::ClassicRigidLimit& limit);
	void LogRegrip(const ControllerFrame& frame, const BodycamReference::ClassicRigidLimit& limit);
	void LogContact(const ControllerFrame& frame, const ClassicTargets& targets);

	player_hud& m_hud;
	HudArmChain m_arms[kArmCount];
	u16 m_primary_anchor = BI_NONE;
	// Shared by every Classic layer.
	u32 m_layers_frame = u32(-1); // Layers update once per frame.
	float m_reach_alpha = 1.f; // Share of the layers' motion the arms can reach.
	Fvector m_item_position = { 0.f, 0.f, 0.f };
	Fvector m_item_rotation = { 0.f, 0.f, 0.f };
	Fvector m_fire_direction = { 0.f, 0.f, 1.f };
	bool m_weapon_geometry_valid = false;

	// STALKER 2 style controller, as read from the simulation.
	struct Stalker2Controller
	{
		Fvector rotation = { 0.f, 0.f, 0.f };
		Fvector arm_follow = { 0.f, 0.f, 0.f };
		float movement_weight = 0.f;
		float movement_response = kDefaultStalker2MovementResponse;
		float weight = 0.f;
		bool active = false;
	} m_stalker2;

	// Walk motion measured from the weapon's own animation, per motion.
	struct AuthoredMotion
	{
		xr_map<u64, AuthoredMotionProfile> profiles;
		AuthoredPhaseFollower phase;
		u64 phase_key = u64(-1); // The motion the follower tracks.
		float profile_weight = 0.f;
		AuthoredMotionGains gains;
	} m_authored;

	// Reference source rig: the calibration regrip, recoil and look settling share.
	// The regrip clip bank loads on its own, so recoil and sway do not need it.
	struct ReferenceRig
	{
		std::optional<BodycamReference::EngineBasis> basis;
		std::unique_ptr<BodycamReference::ClassicReferenceRegripPose> pose;
		std::vector<std::string> bone_names;
		bool load_attempted = false;
		std::optional<BodycamReference::RegripAssetBank> regrip_assets;
		bool regrip_attempted = false;
	} m_rig;

	struct Regrip
	{
		std::unique_ptr<BodycamReference::ClassicRegrip> player;
		BodycamReference::CameraIntensity intensity{ kInitialAnimationScale, kInitialRegrip };
		double alpha = 0;
		double held_alpha = 0; // The alpha a clip fading out under an authored animation keeps.
		std::vector<BodycamReference::RegripMontageSample> samples;
		std::vector<BodycamReference::RegripSample> pose_samples; // working storage of the pose pass
		BodycamReference::Rotation camera;
		u32 random = kRegripSeed;
		void Reset()
		{
			player.reset();
			intensity = BodycamReference::CameraIntensity{ kInitialAnimationScale, kInitialRegrip };
			alpha = held_alpha = 0;
			camera = {};
		}
	} m_regrip;

	struct Recoil
	{
		std::optional<BodycamReference::RecoilSetup> setup;
		bool load_attempted = false;
		std::unique_ptr<BodycamReference::ClassicRecoil> player;
		u32 random = kRecoilSeed;
		BodycamReference::RecoilPose pose;
		bool active = false;
		void Reset()
		{
			player.reset();
			pose = {};
			active = false;
		}
	} m_recoil;

	// Mouse-driven arm settling and movement-linked handling.
	struct Handling
	{
		Classic::HandlingState state;
		BodycamReference::ClassicLookSway sway;
		BodycamReference::ClassicSwayOutput sway_output;
		Fmatrix delta = Fidentity;
		bool delta_active = false;
		void ResetObservation()
		{
			state = {};
			sway.Reset();
		}
		void Reset()
		{
			ResetObservation();
			sway_output = {};
			delta.identity();
			delta_active = false;
		}
	} m_handling;

	// Diagnostics: how far the solved hands end from their targets.
	struct ContactDiagnostics
	{
		u32 samples = 0;
		u32 regrips = 0;
		float right_error = 0.f;
		float left_error = 0.f;
	} m_contact;
	u32 m_regrip_log_frame = u32(-1); // The regrip diagnostics write once per frame.
};

void HudArms::State::Reset()
{
	for (HudArmChain& arm : m_arms)
		arm = HudArmChain();
	m_stalker2 = {};

	IKinematics* right_model = m_hud.m_model->dcast_PKinematics();
	HudArmChain& right = m_arms[kRightArm];
	right.clavicle = ResolveArmBone(right_model, "r_clavicle", "bip01_r_clavicle");
	right.upperarm = ResolveArmBone(right_model, "r_upperarm", "bip01_r_upperarm");
	right.forearm = ResolveArmBone(right_model, "r_forearm", "bip01_r_forearm");
	right.twist = ResolveArmBone(right_model, "r_forearm_twist", "bip01_r_forearm_twist");
	right.hand = ResolveArmBone(right_model, "r_hand", "bip01_r_hand");
	m_primary_anchor = ResolveArmBone(right_model, "lead_gun", "ancor_0");

	IKinematics* left_model = m_hud.m_model_2->dcast_PKinematics();
	HudArmChain& left = m_arms[kLeftArm];
	left.clavicle = ResolveArmBone(left_model, "l_clavicle", "bip01_l_clavicle");
	left.upperarm = ResolveArmBone(left_model, "l_upperarm", "bip01_l_upperarm");
	left.forearm = ResolveArmBone(left_model, "l_forearm", "bip01_l_forearm");
	left.twist = ResolveArmBone(left_model, "l_forearm_twist", "bip01_l_forearm_twist");
	left.hand = ResolveArmBone(left_model, "l_hand", "bip01_l_hand");

	OnWeaponChanged(m_hud.attached_item(0));
}

void HudArms::State::ReadSimulationPose(const ArmPose& pose)
{
	m_stalker2.active = pose.active;
	m_stalker2.rotation.set(pose.wrist_rot);
	m_stalker2.arm_follow.set(pose.arm_follow_rot);
	m_stalker2.movement_weight = pose.movement_weight;
	m_stalker2.movement_response = pose.movement_response;
}

void HudArms::State::UpdateAuthoredMotionProfile(float dt)
{
	dt = std::clamp(dt, 0.f, kMaximumStepTime);
	const auto fade_profile = [&] { m_authored.profile_weight = std::max(0.f, m_authored.profile_weight - dt * kProfileFadeOutSpeed); };
	// Sampling stopped: the phase follower starts in phase with the clip next time.
	const auto stop_sampling = [&]
	{
		fade_profile();
		m_authored.phase.valid = false;
	};

	IKinematics* const models[kArmCount] = { m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr,
		m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr };
	attachable_hud_item* item = m_hud.attached_item(0);
	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool profile_allowed = weapon && weapon->GetState() == CWeapon::eIdle && m_hud.script_anim_part == u8(-1) && m_stalker2.active &&
		m_stalker2.movement_weight > kProfileMinimumMovement;
	if (!profile_allowed || !models[kRightArm] || !models[kLeftArm] || m_primary_anchor == BI_NONE)
	{
		stop_sampling();
		return;
	}

	u32 motion_key = u32(-1);
	float phase = 0.f;
	float blend_weight = 0.f;
	if (!ReadActiveHandMotion(m_hud.m_model, item, motion_key, phase, blend_weight) || blend_weight < kProfileMinimumBlend)
	{
		stop_sampling();
		return;
	}

	// Each joint relative to its parent, per arm.
	Fmatrix wrist[kArmCount], forearm[kArmCount], upperarm[kArmCount];
	for (unsigned side = 0; side < kArmCount; ++side)
	{
		const HudArmChain& arm = m_arms[side];
		if (!RelativeBoneTransform(models[side], arm.forearm, arm.hand, wrist[side]) ||
			!RelativeBoneTransform(models[side], arm.upperarm, arm.forearm, forearm[side]) ||
			!RelativeBoneTransform(models[side], arm.clavicle, arm.upperarm, upperarm[side]))
		{
			stop_sampling();
			return;
		}
	}

	const u64 profile_key = AuthoredMotionKey(motion_key, weapon->IsZoomed());
	if (profile_key != m_authored.phase_key)
		m_authored.phase.valid = false;
	m_authored.phase_key = profile_key;
	m_authored.phase.Update(phase, dt);
	AuthoredMotionProfile& profile = m_authored.profiles[profile_key];
	const Fmatrix& lead = models[kRightArm]->LL_GetBoneInstance(m_primary_anchor).mTransform;
	if (!profile.initialized)
	{
		profile.initialized = true;
		profile.previous_phase = phase;
		profile.lead_rotation.Seed(lead);
		profile.lead_translation.Seed(lead);
		for (unsigned side = 0; side < kArmCount; ++side)
		{
			profile.wrist[side].Seed(wrist[side]);
			profile.forearm[side].Seed(forearm[side]);
			profile.upperarm[side].Seed(upperarm[side]);
		}
		fade_profile();
		return;
	}

	if (!profile.complete)
	{
		const float phase_delta = SignedCycleDelta(phase - profile.previous_phase);
		if (phase_delta >= 0.f && phase_delta <= kProfileMaximumPhaseStep)
			profile.phase_travel += phase_delta;
		profile.previous_phase = phase;
		++profile.sample_count;

		ExtendRange(profile.lead_rotation, lead, RotationDistance);
		ExtendRange(profile.lead_translation, lead, TranslationDistance);
		for (unsigned side = 0; side < kArmCount; ++side)
		{
			ExtendRange(profile.wrist[side], wrist[side], RotationDistance);
			ExtendRange(profile.forearm[side], forearm[side], RotationDistance);
			ExtendRange(profile.upperarm[side], upperarm[side], RotationDistance);
		}

		if (profile.phase_travel >= kProfilePhaseTravel && profile.sample_count >= kProfileSampleCount)
		{
			profile.gains = CalculateAuthoredMotionGains(profile.Metrics());
			profile.complete = true;
		}
	}

	if (!profile.complete)
	{
		fade_profile();
		return;
	}

	m_authored.gains = profile.gains;
	m_authored.profile_weight += std::clamp(1.f - m_authored.profile_weight, -dt * kProfileFadeOutSpeed, dt * kProfileReadyFadeInSpeed);
}

void HudArms::State::DumpAuthoredMotionProfile() const
{
	attachable_hud_item* item = m_hud.attached_item(0);
	u32 motion_key = u32(-1);
	float phase = 0.f;
	float blend_weight = 0.f;
	const bool tracked = ReadActiveHandMotion(m_hud.m_model, item, motion_key, phase, blend_weight);
	Msg("* bodycam authored motion tracked=%d item=%s key=%u generation=%u phase=%0.4f blend=%0.3f output=%0.3f", tracked,
		item ? item->m_sect_name.c_str() : "<none>", motion_key, item ? item->m_active_hand_generation : 0, phase, blend_weight,
		m_authored.profile_weight);

	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool ads = weapon && weapon->IsZoomed();
	const auto found = m_authored.profiles.find(AuthoredMotionKey(motion_key, ads));
	if (found == m_authored.profiles.end())
	{
		Msg("* bodycam authored profile unavailable");
		return;
	}

	const AuthoredMotionProfile& profile = found->second;
	const AuthoredMotionMetrics metrics = profile.Metrics();
	Msg("* bodycam authored profile complete=%d samples=%u travel=%0.3f lead_deg=%0.3f lead_mm=%0.3f wrist_deg=%0.3f forearm_deg=%0.3f upperarm_deg=%0.3f",
		profile.complete, profile.sample_count, profile.phase_travel, rad2deg(metrics.lead_rotation), metrics.lead_translation * 1000.f,
		rad2deg(metrics.wrist_rotation), rad2deg(metrics.forearm_rotation), rad2deg(metrics.upperarm_rotation));
	Msg("* bodycam authored gains controller=%0.3f wrist=%0.3f arm=%0.3f", profile.gains.controller, profile.gains.wrist,
		profile.gains.arm);
}

void HudArms::State::UpdateAndApply(const ArmPose& pose, float dt)
{
	ReadSimulationPose(pose);
	UpdateAuthoredMotionProfile(dt);
	SolveControllers(dt);
}

void HudArms::State::OnWeaponChanged(const attachable_hud_item* item)
{
	m_layers_frame = u32(-1);
	m_reach_alpha = 1.f;
	m_regrip.Reset();
	m_recoil.Reset();
	m_handling.Reset();
	m_weapon_geometry_valid = false;
	m_item_position.set(0.f, 0.f, 0.f);
	m_item_rotation.set(0.f, 0.f, 0.f);
	m_fire_direction.set(0.f, 0.f, 1.f);
	m_authored = {};
	ClearAuthoredPoles(m_arms);

	if (!item)
		return;

	m_item_position.set(item->m_measures.m_item_attach[0]);
	m_item_rotation.set(item->m_measures.m_item_attach[1]);
	m_fire_direction.set(item->m_measures.m_fire_direction);
	m_weapon_geometry_valid = true;
}

bool HudArms::State::LoadReferenceRig()
{
	if (m_rig.pose)
		return true;
	if (m_rig.load_attempted)
		return false;
	m_rig.load_attempted = true;
	try
	{
		const auto reader = MakeProfileReader("Missing Classic reference rig profile");
		const auto paths = reader.Paths(kCharacterSection);
		const auto rifle = BodycamReference::RifleAssetBank::Load(BodycamReference::ReadEngineReferencePacket(paths.rifle));
		std::vector<std::string> names;
		std::vector<int> parents;
		std::optional<unsigned> head, source_gun, source_wrist;
		for (unsigned i = 0; i < BodycamReference::kPhysicalBoneCount; ++i)
		{
			const auto& track = rifle.Tracks()[i];
			names.push_back(track.name);
			parents.push_back(track.parent);
			if (track.name == "head")
				head = i;
			if (track.name == "ik_hand_gun")
				source_gun = i;
			if (track.name == "hand_r")
				source_wrist = i;
		}
		if (!head || !source_gun || !source_wrist)
			throw std::invalid_argument("Missing regrip source head/grip calibration");
		auto source_pose = rifle.RetargetedCclamp();
		source_pose.resize(BodycamReference::kPhysicalBoneCount);
		m_rig.basis = reader.Basis(kCharacterSection);
		const auto socket = rifle.CreateReference().CurrentCameraPoint(source_pose);
		const auto source_model = BodycamReference::HeadRelativeModelRoot({ socket[12], socket[13], socket[14] });
		const auto camera_from_source = m_rig.basis->ToReference(m_rig.basis->CameraRelativeToEngine(source_model));
		m_rig.pose = std::make_unique<BodycamReference::ClassicReferenceRegripPose>(
			parents, std::move(source_pose), *head, camera_from_source, *source_gun, *source_wrist);
		m_rig.bone_names = std::move(names);
		return true;
	}
	catch (const std::exception& e)
	{
		m_rig.basis.reset();
		Msg("! [CLASSIC-RIG] unavailable: %s", e.what());
		return false;
	}
}

bool HudArms::State::LoadRegripAssets()
{
	if (m_rig.regrip_assets)
		return true;
	if (m_rig.regrip_attempted || !LoadReferenceRig())
		return false;
	m_rig.regrip_attempted = true;
	try
	{
		const auto reader = MakeProfileReader("Missing Classic regrip asset profile");
		m_rig.regrip_assets = BodycamReference::RegripAssetBank::Load(
			BodycamReference::ReadEngineReferencePacket(reader.Paths(kCharacterSection).regrip), m_rig.bone_names);
		return true;
	}
	catch (const std::exception& e)
	{
		Msg("! [CLASSIC-REGRIP] unavailable: %s", e.what());
		return false;
	}
}

bool HudArms::State::LoadClassicRecoil()
{
	if (m_recoil.setup)
		return true;
	if (m_recoil.load_attempted || !LoadReferenceRig())
		return false;
	m_recoil.load_attempted = true;
	try
	{
		const auto reader = MakeProfileReader("Missing Classic recoil asset profile");
		m_recoil.setup =
			BodycamReference::LoadRecoilSetup(BodycamReference::ReadEngineReferencePacket(reader.Paths(kCharacterSection).recoil));
		return true;
	}
	catch (const std::exception& e)
	{
		Msg("! [CLASSIC-RECOIL] unavailable: %s", e.what());
		return false;
	}
}

// The weapon's shot events drive the recoil pose of the gun. The camera's aim
// recoil stays with the engine's shot effector.
bool HudArms::State::ClassicRecoilFire(bool firing, bool aiming, const CWeapon* weapon)
{
	if (!firing)
	{
		if (m_recoil.player)
			m_recoil.player->Stop();
		return false;
	}
	auto* item = m_hud.attached_item(0);
	if (!ClassicActive() || !GetConfig().recoil.enable || !weapon || !item ||
		item->m_parent_hud_item != static_cast<const CHudItem*>(weapon) || smart_cast<const CWeaponKnife*>(weapon) ||
		smart_cast<const CWeaponBinoculars*>(weapon) || !LoadClassicRecoil())
		return false;
	if (!m_recoil.player)
		m_recoil.player = std::make_unique<BodycamReference::ClassicRecoil>(
			*m_recoil.setup, [this] { return NextRandom(m_recoil.random) >> 17; },
			[this] { return double(NextRandom(m_recoil.random) >> 8) / 16777216.; });
	m_recoil.player->BeginShot(aiming, BodycamReference::EngineRecoilMetadata(*weapon));
	return true;
}

bool HudArms::State::FullLoweringInfluence() const
{
	return m_hud.BodycamLoweringInfluence() > kFullLoweringInfluence;
}

// The first-person camera frame in the primary HUD model's space. Reference
// motion is relative to the camera; this keeps the rotation of the HUD
// attachment out of it.
Fmatrix HudArms::State::ClassicParentFrame() const
{
	Fmatrix eye = EyeFrame(*Actor()->cam_FirstEye());
	eye.i.normalize_safe();
	eye.j.crossproduct(eye.k, eye.i);
	eye.j.normalize_safe();
	Fmatrix inverse, result;
	inverse.invert(m_hud.m_transform);
	result.mul_43(inverse, eye);
	return result;
}

void HudArms::State::UpdateClassicLayers(float dt, CWeapon* weapon, bool allowed)
{
	if (m_layers_frame == Device.dwFrame)
		return;
	m_layers_frame = Device.dwFrame;
	m_handling.delta.identity();
	m_handling.delta_active = false;
	m_handling.sway_output = {};
	UpdateClassicRegrip(dt, weapon, allowed);
	UpdateClassicRecoil(dt, weapon, allowed);
	UpdateClassicHandling(dt, weapon, allowed);
}

void HudArms::State::UpdateClassicRegrip(float dt, CWeapon* weapon, bool allowed)
{
	const auto& settings = GetConfig().regrip;
	const bool enabled = ClassicActive() && settings.enable;
	if (enabled && !m_regrip.player && LoadRegripAssets())
		m_regrip.player =
			std::make_unique<BodycamReference::ClassicRegrip>(*m_rig.regrip_assets, [this] { return NextRandom(m_regrip.random) >> 17; });
	if (!m_regrip.player)
		return;

	BodycamReference::ClassicRegripInput input;
	input.frame = Device.dwFrame;
	input.seconds = BodycamReference::ClampFrameDelta(dt);
	input.enabled = enabled;
	input.paused = Device.Paused();
	input.authoredOwnsHands = !allowed;
	input.aiming = weapon && weapon->IsZoomed();
	input.magnified = weapon && weapon->GetZoomType() == 0;
	input.firing = weapon && (weapon->GetState() == CWeapon::eFire || weapon->GetState() == CWeapon::eFire2);
	m_regrip.player->Update(input);
	BodycamReference::ScaleClassicRegripSamples(m_regrip.player->Samples(), settings.intensity, m_regrip.samples);
	m_regrip.alpha = 0;
	m_regrip.camera = {};
	if (!enabled || Device.Paused())
		return;

	BodycamReference::CameraIntensityInput intensity;
	intensity.scopeActive = input.aiming && input.magnified;
	intensity.aimpointSource = intensity.scopeActive ? kAimpointScope : kAimpointSights;
	Fvector direction = Actor()->cam_FirstEye()->vDirection;
	direction.mul(kIntensityTraceLength);
	direction.y += kIntensityTraceRise;
	const float range = direction.magnitude();
	direction.div(range);
	collide::rq_result trace;
	intensity.blockingHit = Level().ObjectSpace.RayPick(Device.vCameraPosition, direction, range, collide::rqtBoth, trace, Actor());
	intensity.distanceCm = intensity.blockingHit ? double(trace.range) * 100. : 0.;
	m_regrip.intensity.Update(intensity, input.seconds);
	// While an authored animation has the hands, a clip that is still fading
	// out keeps the last alpha; its own blend-out takes it away.
	if (allowed)
		m_regrip.held_alpha = input.aiming ? m_regrip.intensity.Regrip() : 1.;
	m_regrip.alpha = allowed || !m_regrip.samples.empty() ? m_regrip.held_alpha : 0.;
	m_regrip.camera =
		BodycamReference::EvaluateRegripCamera(*m_rig.regrip_assets, m_regrip.samples, m_regrip.alpha, m_regrip.intensity.AnimationScale());
}

// Reference gun recoil, on the source-rig calibration regrip loads.
void HudArms::State::UpdateClassicRecoil(float dt, CWeapon* weapon, bool allowed)
{
	const auto& settings = GetConfig().recoil;
	if (!ClassicActive() || !settings.enable)
	{
		m_recoil.Reset();
		return;
	}
	m_recoil.active = false;
	if (!m_recoil.player)
		return;

	BodycamReference::ClassicRecoilInput input;
	input.seconds = BodycamReference::ClampFrameDelta(dt);
	input.paused = Device.Paused();
	input.aiming = weapon && weapon->IsZoomed();
	input.magnified = weapon && weapon->GetZoomType() == 0;
	input.adsWeight = weapon ? weapon->GetZRotatingFactor() : 0.f;
	input.hipIntensity = settings.hip;
	input.adsIntensity = settings.ads;
	m_recoil.pose = m_recoil.player->Update(input);
	// A weapon action lets a running recoil settle out on the gun rather than
	// cutting it. A script or item animation owns the hands outright.
	const bool holds_gun = weapon && m_hud.script_anim_part == u8(-1) && FullLoweringInfluence();
	m_recoil.active = holds_gun && BodycamReference::ClassicRecoilPoseActive(m_recoil.pose);
	if (DiagnosticsEnabled(2) && (m_recoil.active || m_recoil.player->Firing()))
	{
		const auto& pose = m_recoil.pose;
		Msg("[BODYCAM-RECOIL] frame=%u time=%u firing=%u active=%u allowed=%u ads=%.4f pitch=%.5f yaw=%.5f roll=%.5f x=%.5f y=%.5f z=%.5f reach=%.5f",
			Device.dwFrame, Device.dwTimeGlobal, unsigned(m_recoil.player->Firing()), unsigned(m_recoil.active), unsigned(allowed),
			input.adsWeight, pose.rotation.pitch, pose.rotation.yaw, pose.rotation.roll, pose.location[0], pose.location[1],
			pose.location[2], m_reach_alpha);
	}
}

void HudArms::State::UpdateClassicHandling(float dt, CWeapon* weapon, bool allowed)
{
	const auto& settings = GetConfig().handling;
	// Weapon actions keep the handling offset while it settles out. A script or
	// item animation owns the hands outright, so there is no grip to carry.
	const bool script_owns_hands = m_hud.script_anim_part != u8(-1);
	if (!ClassicActive() || !weapon || script_owns_hands || (!settings.directional_weapon_inertia && !settings.movement_linked_handling))
	{
		m_handling.ResetObservation();
		return;
	}
	if (!m_arms[kRightArm].Solvable() || m_primary_anchor == BI_NONE)
		return;

	auto* physics = Actor()->character_physics_support()->movement();
	Fvector velocity;
	physics->GetCharacterVelocity(velocity);
	Classic::HandlingInput input;
	input.dt = dt;
	input.enabled = settings.movement_linked_handling;
	input.world_velocity = ToVec3(velocity);
	Fvector x = m_hud.m_transform.i, y = m_hud.m_transform.j, z = m_hud.m_transform.k;
	x.normalize_safe();
	y.normalize_safe();
	z.normalize_safe();
	input.right = ToVec3(x);
	input.up = ToVec3(y);
	input.forward = ToVec3(z);

	// The mouse-driven elbow settling runs only while the Classic layers have the hands.
	BodycamReference::ClassicSwayInput look;
	look.enabled = allowed && settings.directional_weapon_inertia && m_weapon_geometry_valid && LoadReferenceRig();
	if (look.enabled)
	{
		const auto pose = BodycamReference::Decompose(m_rig.basis->CameraWorldToReference(ToRows(EyeFrame(*Actor()->cam_FirstEye()))));
		look.control = BodycamReference::CameraPovFromWorld(pose.position, pose.rotation, kControlFov).rotation;
	}
	look.dt = dt;
	look.aiming = weapon->IsZoomed();
	look.magnified = weapon->GetZoomType() == 0;
	look.lookWeight = allowed ? 1. : 0.;
	m_handling.sway_output = m_handling.sway.Update(look);

	input.grounded = physics->Environment() != CPHMovementControl::peInAir;
	input.ads_weight = weapon->GetZRotatingFactor();
	input.suppressed = !allowed;
	input.contact_serial = Actor()->BodycamStepSerial();
	const auto output = Classic::Update(m_handling.state, input);
	m_handling.delta.c.set(float(output.x), float(output.y), float(output.z));
	m_handling.delta_active = m_handling.delta.c.square_magnitude() > kMinimumHandlingOffsetSq;
}

// Updates every controller for the frame and solves the arms once: the
// STALKER 2 controller alone, or combined with the Classic layers.
void HudArms::State::SolveControllers(float dt)
{
	IKinematics* primary = m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr;
	IKinematics* support = m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr;
	attachable_hud_item* item = m_hud.attached_item(0);
	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool layer_allowed = AllowsControllerLayer(weapon, m_hud.script_anim_part);
	UpdateClassicLayers(dt, weapon, layer_allowed && FullLoweringInfluence());
	const float target_weight = m_stalker2.active && layer_allowed ? 1.f : 0.f;
	const float blend_speed = target_weight > m_stalker2.weight ? kControllerEnterSpeed : kControllerExitSpeed;
	m_stalker2.weight += std::clamp(target_weight - m_stalker2.weight, -blend_speed * dt, blend_speed * dt);

	ControllerFrame frame;
	frame.regrip = m_regrip.player && !m_regrip.player->Samples().empty();
	frame.sway = m_handling.sway_output.ownsLook;
	frame.recoil = m_recoil.active;
	const bool classic_layers = m_handling.delta_active || frame.regrip || frame.sway || frame.recoil;
	// Arms that are not solved this frame put no reach limit on the regrip camera.
	if (!primary || (m_stalker2.weight < kIdleWeight && !classic_layers))
	{
		m_reach_alpha = 1.f;
		ClearAuthoredPoles(m_arms);
		return;
	}

	Fvector mouse_wrist_rot = m_stalker2.rotation;
	Fvector arm_follow_rot = m_stalker2.arm_follow;
	Fvector authored_walk_rot = { 0.f, 0.f, 0.f };
	Fvector authored_walk_pos = { 0.f, 0.f, 0.f };
	const float authored_weight = m_stalker2.movement_weight * m_authored.profile_weight;
	if (authored_weight > kIdleWeight)
	{
		const AuthoredWalkMotion authored = CalculateAuthoredWalkMotion(m_authored.phase.phase, authored_weight, m_authored.gains);
		authored_walk_rot.set(authored.rotation.x, authored.rotation.y, authored.rotation.z);
		authored_walk_pos.set(authored.translation.x, authored.translation.y, authored.translation.z);

		// The arms absorb part of the walking swing rejected by barrel stabilization.
		const float arm_follow = CalculateAuthoredArmFollow(m_authored.gains.arm);
		Fvector authored_arm_rot = authored_walk_rot;
		authored_arm_rot.mul(arm_follow);
		arm_follow_rot.add(authored_arm_rot);
	}
	mouse_wrist_rot.mul(m_stalker2.weight);
	authored_walk_rot.mul(m_stalker2.weight);
	authored_walk_pos.mul(m_stalker2.weight);
	arm_follow_rot.mul(m_stalker2.weight);

	if (!m_arms[kRightArm].Solvable() || m_primary_anchor == BI_NONE || (!m_hud.m_adjust_mode && !m_weapon_geometry_valid))
	{
		m_reach_alpha = 1.f;
		ClearAuthoredPoles(m_arms);
		return;
	}

	Fmatrix mouse_rotation;
	mouse_rotation.setHPB(mouse_wrist_rot.x, mouse_wrist_rot.y, mouse_wrist_rot.z);
	Fmatrix walk_rotation;
	walk_rotation.setHPB(authored_walk_rot.x, authored_walk_rot.y, authored_walk_rot.z);
	const Fvector& item_position = m_hud.m_adjust_mode ? m_hud.m_adjust_obj[0] : m_item_position;
	const Fvector& item_rotation = m_hud.m_adjust_mode ? m_hud.m_adjust_obj[1] : m_item_rotation;
	const Fvector authored_fire_axis = CalculateFireAxis(primary, m_primary_anchor, item_position, item_rotation, m_fire_direction);
	const Fmatrix stabilized_walk_rotation = ExtractTwistRotation(walk_rotation, authored_fire_axis);
	Fmatrix rotation;
	rotation.mul_43(mouse_rotation, stabilized_walk_rotation);
	Fmatrix arm_follow_rotation;
	arm_follow_rotation.setHPB(arm_follow_rot.x, arm_follow_rot.y, arm_follow_rot.z);
	const Fvector controller_pivot = BonePosition(primary, m_primary_anchor);

	frame.primary = primary;
	frame.support = support && m_arms[kLeftArm].Solvable() ? support : nullptr;
	frame.delta = RotationAboutPivot(controller_pivot, rotation);
	frame.delta.c.add(authored_walk_pos);
	frame.arm_follow = RotationAboutPivot(controller_pivot, arm_follow_rotation);
	frame.movement_weight = m_stalker2.movement_weight * m_stalker2.weight;
	frame.mouse_magnitude = mouse_wrist_rot.magnitude();
	frame.dt = dt;
	if (classic_layers)
		SolveClassicController(frame);
	else
		SolveStalker2Controller(frame);
}

// One combined controller for the STALKER 2 motion and every Classic layer;
// each arm is solved once.
void HudArms::State::SolveClassicController(const ControllerFrame& frame)
{
	ClassicTargets targets;
	BuildClassicTargets(frame, targets);
	if (frame.regrip || frame.sway || frame.recoil)
		ApplyReferenceLayers(frame, targets);
	const BodycamReference::ClassicRigidLimit limit = LimitClassicReach(frame, targets);
	if (DiagnosticsEnabled(2))
		LogRegrip(frame, limit);
	m_reach_alpha = float(limit.alpha);
	SolveClassicArms(frame, targets, limit);
	if (DiagnosticsEnabled(2))
		LogContact(frame, targets);
}

// The gun, both hands and both elbow poles after the controller and the handling offset.
void HudArms::State::BuildClassicTargets(const ControllerFrame& frame, ClassicTargets& targets)
{
	IKinematics* primary = frame.primary;
	HudArmChain& right = m_arms[kRightArm];
	targets.combined = frame.delta;
	if (m_handling.delta_active)
		targets.combined.mul_43(m_handling.delta, frame.delta);
	targets.original_right = primary->LL_GetBoneInstance(right.hand).mTransform;
	targets.gun.mul_43(targets.combined, primary->LL_GetBoneInstance(m_primary_anchor).mTransform);
	targets.right_goal.mul_43(targets.combined, targets.original_right);
	targets.right_pole =
		FilterAuthoredArmPole(right, BonePosition(primary, right.forearm), frame.movement_weight, m_stalker2.movement_response, frame.dt);
	frame.arm_follow.transform_tiny(targets.right_pole);
	targets.right_base_pole = targets.right_pole;
	if (m_handling.delta_active)
		m_handling.delta.transform_tiny(targets.right_pole);
	targets.left_goal = targets.right_goal;
	targets.left_pole = targets.right_pole;
	targets.left_base_pole = targets.right_base_pole;
	if (!frame.support)
		return;

	HudArmChain& left = m_arms[kLeftArm];
	Fmatrix inverse_primary;
	inverse_primary.invert(m_hud.m_transform);
	targets.left_to_primary.mul_43(inverse_primary, m_hud.m_transform_2);
	targets.primary_to_left.invert(targets.left_to_primary);
	targets.original_left = frame.support->LL_GetBoneInstance(left.hand).mTransform;
	targets.original_left_primary.mul_43(targets.left_to_primary, targets.original_left);
	targets.left_goal.mul_43(targets.combined, targets.original_left_primary);
	targets.left_pole = FilterAuthoredArmPole(
		left, BonePosition(frame.support, left.forearm), frame.movement_weight, m_stalker2.movement_response, frame.dt);
	targets.left_to_primary.transform_tiny(targets.left_pole);
	frame.arm_follow.transform_tiny(targets.left_pole);
	targets.left_base_pole = targets.left_pole;
	if (m_handling.delta_active)
		m_handling.delta.transform_tiny(targets.left_pole);
}

// Regrip, recoil and look settling, evaluated in Reference space on the built targets.
void HudArms::State::ApplyReferenceLayers(const ControllerFrame& frame, ClassicTargets& targets)
{
	const BodycamReference::EngineBasis& basis = *m_rig.basis;
	Fmatrix inverse_gun, inverse_left, relative_right, relative_left, pole_right, pole_left;
	inverse_gun.invert(targets.gun);
	inverse_left.invert(targets.left_goal);
	relative_right.mul_43(inverse_gun, targets.right_goal);
	relative_left.mul_43(inverse_gun, targets.left_goal);
	pole_right.identity();
	pole_right.c = targets.right_pole;
	pole_right.mulA_43(inverse_gun);
	pole_left.identity();
	pole_left.c = targets.left_pole;
	pole_left.mulA_43(inverse_left);
	const BodycamReference::RegripTargets engineTargets{ ReferenceFromEngine(basis, targets.gun),
		ReferenceFromEngine(basis, relative_right), ReferenceFromEngine(basis, relative_left), ReferenceFromEngine(basis, pole_right),
		ReferenceFromEngine(basis, pole_left) };
	auto result = engineTargets;
	const auto parent = ReferenceFromEngine(basis, ClassicParentFrame());
	if (frame.regrip)
	{
		m_rig.regrip_assets->SamplePose(m_regrip.samples, m_regrip.pose_samples);
		result = m_rig.pose->Apply(engineTargets, m_regrip.pose_samples, parent, m_regrip.alpha);
		ReadReferenceTargets(basis, result, targets);
	}
	// Reference order: recoil acts on the regripped gun.
	if (frame.recoil)
	{
		result = BodycamReference::ApplyClassicRecoil(*m_rig.pose, result, m_recoil.pose, parent);
		ReadReferenceTargets(basis, result, targets);
	}
	// Mouse sway turns the elbows only: the gun and the wrists keep the pose
	// regrip and recoil gave them.
	const auto settled_gun = result.gun;
	if (frame.sway)
		result.gun = BodycamReference::ApplyClassicLookSway(result.gun, parent, m_handling.sway_output);
	targets.right_pole = SettleArmPole(
		basis, BonePosition(frame.primary, m_arms[kRightArm].upperarm), targets.right_goal.c, targets.right_pole, settled_gun, result.gun);
	if (frame.support)
	{
		Fvector shoulder = BonePosition(frame.support, m_arms[kLeftArm].upperarm);
		targets.left_to_primary.transform_tiny(shoulder);
		targets.left_pole = SettleArmPole(basis, shoulder, targets.left_goal.c, targets.left_pole, settled_gun, result.gun);
	}
}

// Handling, regrip and recoil move the gun and both wrists as one rigid body.
// They share one reach limit, so the gun never goes where the fixed-shoulder
// arm solver would clamp a wrist. Look settling alone moves no wrist.
BodycamReference::ClassicRigidLimit HudArms::State::LimitClassicReach(const ControllerFrame& frame, const ClassicTargets& targets) const
{
	if (!m_handling.delta_active && !frame.regrip && !frame.recoil)
		return { ToPortable(targets.combined), 1. };

	IKinematics* primary = frame.primary;
	const HudArmChain& right = m_arms[kRightArm];
	const BodycamReference::ClassicArmReach right_reach{ ToVector(BonePosition(primary, right.upperarm)),
		ToVector(BonePosition(primary, right.forearm)), ToVector(targets.original_right.c) };
	BodycamReference::ClassicArmReach left_reach;
	if (frame.support)
	{
		const HudArmChain& left = m_arms[kLeftArm];
		Fvector shoulder = BonePosition(frame.support, left.upperarm), elbow = BonePosition(frame.support, left.forearm);
		targets.left_to_primary.transform_tiny(shoulder);
		targets.left_to_primary.transform_tiny(elbow);
		left_reach = { ToVector(shoulder), ToVector(elbow), ToVector(targets.original_left_primary.c) };
	}
	return BodycamReference::LimitClassicRigidController(ToPortable(primary->LL_GetBoneInstance(m_primary_anchor).mTransform),
		ToPortable(targets.gun), right_reach, frame.support ? &left_reach : nullptr, ToPortable(frame.delta));
}

// Regenerates every target from the one limited controller, so the gun and
// both wrists keep their grip, and solves the arms.
void HudArms::State::SolveClassicArms(
	const ControllerFrame& frame, ClassicTargets& targets, const BodycamReference::ClassicRigidLimit& limit)
{
	IKinematics* primary = frame.primary;
	const Fmatrix controller = ToEngine(limit.delta);
	targets.gun.mul_43(controller, primary->LL_GetBoneInstance(m_primary_anchor).mTransform);
	targets.right_goal.mul_43(controller, targets.original_right);
	if (frame.support)
		targets.left_goal.mul_43(controller, targets.original_left_primary);
	if (limit.alpha < 1)
	{
		targets.right_pole.lerp(targets.right_base_pole, targets.right_pole, float(limit.alpha));
		if (frame.support)
			targets.left_pole.lerp(targets.left_base_pole, targets.left_pole, float(limit.alpha));
	}

	Fmatrix inverse_right, right_delta;
	inverse_right.invert(targets.original_right);
	right_delta.mul_43(targets.right_goal, inverse_right);
	SolveArmToHand(primary, m_arms[kRightArm], targets.right_goal.c, targets.right_pole, right_delta);
	// The right hand may have carried the anchor along; it ends on the gun target either way.
	Fmatrix inverse_anchor, anchor_delta;
	inverse_anchor.invert(primary->LL_GetBoneInstance(m_primary_anchor).mTransform);
	anchor_delta.mul_43(targets.gun, inverse_anchor);
	TransformBone(primary, m_primary_anchor, anchor_delta);
	if (!frame.support)
		return;

	Fmatrix support_goal, inverse_original, support_delta;
	support_goal.mul_43(targets.primary_to_left, targets.left_goal);
	Fvector support_pole = targets.left_pole;
	targets.primary_to_left.transform_tiny(support_pole);
	inverse_original.invert(targets.original_left);
	support_delta.mul_43(support_goal, inverse_original);
	SolveArmToHand(frame.support, m_arms[kLeftArm], support_goal.c, support_pole, support_delta);
}

void HudArms::State::LogRegrip(const ControllerFrame& frame, const BodycamReference::ClassicRigidLimit& limit)
{
	if (m_regrip_log_frame == Device.dwFrame)
		return;
	m_regrip_log_frame = Device.dwFrame;
	if (m_regrip.player)
		for (const auto& sample : m_regrip.player->Samples())
			Msg("[BODYCAM-REGRIP-CLIP] frame=%u time=%u id=%u clip=%u phase=%.7f weight=%.7f retiring=%u", Device.dwFrame,
				Device.dwTimeGlobal, sample.id, sample.clip, sample.time, sample.weight, unsigned(sample.retiring));
	Msg("[BODYCAM-REGRIP] frame=%u time=%u active=%u clips=%u intensity=%.7f reach=%.7f cp=%.8f cy=%.8f cr=%.8f", Device.dwFrame,
		Device.dwTimeGlobal, unsigned(frame.regrip), m_regrip.player ? unsigned(m_regrip.player->Samples().size()) : 0, m_regrip.alpha,
		limit.alpha, m_regrip.camera.pitch, m_regrip.camera.yaw, m_regrip.camera.roll);
}

void HudArms::State::LogContact(const ControllerFrame& frame, const ClassicTargets& targets)
{
	constexpr u32 kSolvesPerLine = 60;
	m_contact.right_error = std::max(
		m_contact.right_error, frame.primary->LL_GetBoneInstance(m_arms[kRightArm].hand).mTransform.c.distance_to(targets.right_goal.c));
	if (frame.support)
	{
		Fvector actual = frame.support->LL_GetBoneInstance(m_arms[kLeftArm].hand).mTransform.c;
		targets.left_to_primary.transform_tiny(actual);
		m_contact.left_error = std::max(m_contact.left_error, actual.distance_to(targets.left_goal.c));
	}
	if (frame.regrip)
		++m_contact.regrips;
	if (++m_contact.samples < kSolvesPerLine)
		return;
	Msg("[BODYCAM-CONTACT] frame=%u samples=%u regrips=%u rightErrorM=%.9f leftErrorM=%.9f alpha=%.6f s2Weight=%.6f mouse=%.6f walk=%.6f",
		Device.dwFrame, m_contact.samples, m_contact.regrips, m_contact.right_error, m_contact.left_error, m_reach_alpha, m_stalker2.weight,
		frame.mouse_magnitude, frame.movement_weight);
	m_contact = {};
}

// The weapon and both authored grips form one rigid controller: each hand
// moves to the target the controller gives it and the arm is solved behind it.
void HudArms::State::SolveStalker2Controller(const ControllerFrame& frame)
{
	HudArmChain& right = m_arms[kRightArm];
	const bool anchor_follows_right_hand = IsHandDescendant(frame.primary, m_primary_anchor, right.hand);
	Fvector right_target = BonePosition(frame.primary, right.hand);
	frame.delta.transform_tiny(right_target);
	Fvector right_pole = FilterAuthoredArmPole(
		right, BonePosition(frame.primary, right.forearm), frame.movement_weight, m_stalker2.movement_response, frame.dt);
	frame.arm_follow.transform_tiny(right_pole);
	SolveArmToHand(frame.primary, right, right_target, right_pole, frame.delta);
	if (!anchor_follows_right_hand)
		TransformBone(frame.primary, m_primary_anchor, frame.delta);
	if (!frame.support)
		return;

	HudArmChain& left = m_arms[kLeftArm];
	const Fmatrix support_delta = ConvertModelDelta(m_hud.m_transform, m_hud.m_transform_2, frame.delta);
	const Fmatrix support_arm_follow = ConvertModelDelta(m_hud.m_transform, m_hud.m_transform_2, frame.arm_follow);
	Fvector left_target = BonePosition(frame.support, left.hand);
	support_delta.transform_tiny(left_target);
	Fvector left_pole = FilterAuthoredArmPole(
		left, BonePosition(frame.support, left.forearm), frame.movement_weight, m_stalker2.movement_response, frame.dt);
	support_arm_follow.transform_tiny(left_pole);
	SolveArmToHand(frame.support, left, left_target, left_pole, support_delta);
}

bool HudArms::State::ClassicRegripCamera(Fmatrix& local_delta) const
{
	if (!GetConfig().regrip.enable || !ClassicActive() || !m_regrip.player || m_layers_frame != Device.dwFrame)
		return false;
	auto* item = m_hud.attached_item(0);
	auto* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	if (!AllowsControllerLayer(weapon, m_hud.script_anim_part) || !FullLoweringInfluence())
		return false;
	const auto& rotation = m_regrip.camera;
	local_delta = ToEngine(BodycamReference::ClassicRegripCameraDelta(
		{ rotation.pitch * m_reach_alpha, rotation.yaw * m_reach_alpha, rotation.roll * m_reach_alpha }));
	return true;
}

HudArms::HudArms(player_hud& hud) : m_state(std::make_unique<State>(hud)) {}

HudArms::~HudArms() = default;

void HudArms::Reset()
{
	m_state->Reset();
}

void HudArms::UpdateAndApply(const ArmPose& pose, float dt)
{
	m_state->UpdateAndApply(pose, dt);
}

void HudArms::OnWeaponChanged(const attachable_hud_item* item)
{
	m_state->OnWeaponChanged(item);
}

bool HudArms::ClassicRegripCamera(Fmatrix& local_delta) const
{
	return m_state->ClassicRegripCamera(local_delta);
}

bool HudArms::ClassicRecoilFire(bool firing, bool aiming, const CWeapon* weapon)
{
	return m_state->ClassicRecoilFire(firing, aiming, weapon);
}

void HudArms::DumpAuthoredMotionProfile() const
{
	m_state->DumpAuthoredMotionProfile();
}
} // namespace Bodycam
