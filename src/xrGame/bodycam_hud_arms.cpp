#include "stdafx.h"
#include "bodycam_hud_arms.h"
#include "bodycam_camera.h"
#include "bodycam_simulation.h"
#include "player_hud.h"
#include "Weapon.h"

#include <algorithm>
#include <cmath>

namespace Bodycam
{
namespace
{
enum EHudArmSide
{
	hud_arm_right = 0,
	hud_arm_left,
	hud_arm_side_count
};

struct HudArmChain
{
	u16 clavicle = BI_NONE;
	u16 upperarm = BI_NONE;
	u16 forearm = BI_NONE;
	u16 twist = BI_NONE;
	u16 hand = BI_NONE;
	Fvector upper_pose = { 0.f, 0.f, 0.f };
	Fvector forearm_pose = { 0.f, 0.f, 0.f };
	Fvector twist_pose = { 0.f, 0.f, 0.f };
	Fvector authored_pole = { 0.f, 0.f, 0.f };
	bool authored_pole_valid = false;
	void ResetBones()
	{
		clavicle = upperarm = forearm = twist = hand = BI_NONE;
		authored_pole.set(0.f, 0.f, 0.f);
		authored_pole_valid = false;
	}
};

struct AuthoredMotionProfile
{
	bool initialized = false;
	bool complete = false;
	float previous_phase = 0.f;
	float phase_travel = 0.f;
	u32 sample_count = 0;
	Fmatrix lead_reference;
	Fmatrix lead_opposite;
	Fmatrix lead_translation_reference;
	Fmatrix lead_translation_opposite;
	Fmatrix right_wrist_reference;
	Fmatrix right_wrist_opposite;
	Fmatrix left_wrist_reference;
	Fmatrix left_wrist_opposite;
	Fmatrix right_forearm_reference;
	Fmatrix right_forearm_opposite;
	Fmatrix left_forearm_reference;
	Fmatrix left_forearm_opposite;
	Fmatrix right_upperarm_reference;
	Fmatrix right_upperarm_opposite;
	Fmatrix left_upperarm_reference;
	Fmatrix left_upperarm_opposite;
	float lead_rotation = 0.f;
	float lead_translation = 0.f;
	float wrist_rotation = 0.f;
	float forearm_rotation = 0.f;
	float upperarm_rotation = 0.f;
	float right_wrist_rotation = 0.f;
	float left_wrist_rotation = 0.f;
	float right_forearm_rotation = 0.f;
	float left_forearm_rotation = 0.f;
	float right_upperarm_rotation = 0.f;
	float left_upperarm_rotation = 0.f;
	float controller_gain = 0.f;
	float wrist_gain = 0.f;
	float arm_gain = 0.f;
};

u16 ResolveArmBone(IKinematics* model, LPCSTR primary, LPCSTR fallback)
{
	if (!model)
		return BI_NONE;
	const u16 primary_id = model->LL_BoneID(primary);
	return primary_id != BI_NONE ? primary_id : model->LL_BoneID(fallback);
}
constexpr float kElbowUpperarmScale = 0.18f;
constexpr float kElbowForearmScale = 0.12f;
constexpr float kWristForearmScale = 0.10f;
constexpr float kWristTwistScale = 0.06f;
constexpr float kProfileFadeOutSpeed = 6.f;
constexpr float kProfileSamplingFadeInSpeed = 3.f;
constexpr float kProfileReadyFadeInSpeed = 2.5f;
constexpr float kControllerEnterSpeed = 10.f;
constexpr float kControllerExitSpeed = 14.f;

bool IsValidBone(IKinematics* K, u16 bone_id)
{
	return K && bone_id != BI_NONE && bone_id < K->LL_BoneCount();
}

Fvector BonePosition(IKinematics* K, u16 bone_id)
{
	return K->LL_GetBoneInstance(bone_id).mTransform.c;
}

Fvector PoseOffset(const Fvector& pose, const Fmatrix& reference, float scale)
{
	Fvector offset;
	offset.set(reference.j);
	offset.mul(pose.x * scale);
	offset.mad(reference.i, pose.y * scale);
	offset.mad(reference.k, pose.z * scale);
	return offset;
}

Fvector SolveMidpoint(const Fvector& start, const Fvector& end, const Fvector& current_mid, const Fvector& desired_mid)
{
	const Bodycam::SVec3 bodycam_start = { start.x, start.y, start.z };
	const Bodycam::SVec3 bodycam_end = { end.x, end.y, end.z };
	const Bodycam::SVec3 bodycam_current = { current_mid.x, current_mid.y, current_mid.z };
	const Bodycam::SVec3 bodycam_desired = { desired_mid.x, desired_mid.y, desired_mid.z };
	const Bodycam::SVec3 solved = Bodycam::SolveArmMidpoint(bodycam_start, bodycam_end, bodycam_current, bodycam_desired);
	return Fvector().set(solved.x, solved.y, solved.z);
}

void RotateBoneTowardChild(IKinematics* K, u16 bone_id, const Fvector& old_child, const Fvector& new_origin, const Fvector& new_child)
{
	if (!IsValidBone(K, bone_id))
		return;

	CBoneInstance& bone = K->LL_GetBoneInstance(bone_id);
	Fvector old_dir;
	old_dir.sub(old_child, bone.mTransform.c);
	Fvector new_dir;
	new_dir.sub(new_child, new_origin);
	if (old_dir.magnitude() < EPS_S || new_dir.magnitude() < EPS_S)
		return;
	old_dir.normalize_safe();
	new_dir.normalize_safe();

	float dot = old_dir.dotproduct(new_dir);
	clamp(dot, -1.f, 1.f);
	Fvector axis;
	axis.crossproduct(old_dir, new_dir);
	const float cross_magnitude = axis.magnitude();
	const float angle = Bodycam::ArmCorrectionAngle(dot, cross_magnitude);

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

	bone.mTransform.mulA_43(rotation);
	bone.mTransform.c.set(new_origin);
	bone.mRenderTransform.mul_43(bone.mTransform, K->LL_GetData(bone_id).m2b_transform);
}

void ApplyAdditiveArmFollow(IKinematics* K, u16 upper, u16 forearm, u16 twist, u16 hand, const Fvector& upper_pose, const Fvector& forearm_pose, const Fvector& twist_pose)
{
	if (!IsValidBone(K, upper) || !IsValidBone(K, forearm) || !IsValidBone(K, hand))
		return;

	const bool has_twist = IsValidBone(K, twist);
	const Fvector shoulder = BonePosition(K, upper);
	const Fvector elbow = BonePosition(K, forearm);
	const Fvector wrist = has_twist ? BonePosition(K, twist) : elbow;
	const Fvector hand_pos = BonePosition(K, hand);

	Fvector elbow_desired = elbow;
	elbow_desired.add(PoseOffset(upper_pose, K->LL_GetBoneInstance(upper).mTransform, kElbowUpperarmScale));
	elbow_desired.add(PoseOffset(forearm_pose, K->LL_GetBoneInstance(forearm).mTransform, kElbowForearmScale));

	const Fvector solved_elbow = SolveMidpoint(shoulder, hand_pos, elbow, elbow_desired);
	Fvector solved_wrist = wrist;
	if (has_twist)
	{
		Fvector wrist_desired = wrist;
		wrist_desired.add(PoseOffset(forearm_pose, K->LL_GetBoneInstance(forearm).mTransform, kWristForearmScale));
		wrist_desired.add(PoseOffset(twist_pose, K->LL_GetBoneInstance(twist).mTransform, kWristTwistScale));
		solved_wrist = SolveMidpoint(solved_elbow, hand_pos, wrist, wrist_desired);
	}

	RotateBoneTowardChild(K, upper, elbow, shoulder, solved_elbow);
	RotateBoneTowardChild(K, forearm, has_twist ? wrist : hand_pos, solved_elbow, has_twist ? solved_wrist : hand_pos);
	if (has_twist)
		RotateBoneTowardChild(K, twist, hand_pos, solved_wrist, hand_pos);

	CBoneInstance& hand_bone = K->LL_GetBoneInstance(hand);
	hand_bone.mTransform.c.set(hand_pos);
	hand_bone.mRenderTransform.mul_43(hand_bone.mTransform, K->LL_GetData(hand).m2b_transform);
}

Fvector SolveArmJoint(const Fvector& start, const Fvector& end, const Fvector& current_joint,
	float first_length, float second_length, float pole_angle)
{
	Fvector line;
	line.sub(end, start);
	const float distance = line.magnitude();
	if (distance < EPS_S)
		return current_joint;
	line.mul(1.f / distance);

	const float along = (first_length * first_length - second_length * second_length + distance * distance) /
		(2.f * distance);
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
	Fvector tangent;
	tangent.crossproduct(line, radial);
	if (tangent.magnitude() > EPS_S)
	{
		tangent.normalize();
		Fvector rotated_radial;
		rotated_radial.mul(radial, _cos(pole_angle));
		rotated_radial.mad(tangent, _sin(pole_angle));
		radial = rotated_radial;
	}
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

Fvector CalculateFireAxis(IKinematics* hands, u16 anchor_id, const Fvector& item_position,
	const Fvector& item_rotation_degrees, const Fvector& fire_direction)
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

Fmatrix ConvertModelDelta(const Fmatrix& source_to_world, const Fmatrix& target_to_world,
	const Fmatrix& source_delta)
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
	if (!IsValidBone(K, bone_id))
		return;

	CBoneInstance& bone = K->LL_GetBoneInstance(bone_id);
	Fmatrix transformed;
	transformed.mul_43(delta, bone.mTransform);
	bone.mTransform.set(transformed);
	bone.mRenderTransform.mul_43(bone.mTransform, K->LL_GetData(bone_id).m2b_transform);
}

bool IsHandDescendant(IKinematics* K, u16 bone_id, u16 hand)
{
	for (u16 current = bone_id; IsValidBone(K, current); current = K->LL_GetData(current).GetParentID())
	{
		if (current == hand)
			return true;
		const u16 parent = K->LL_GetData(current).GetParentID();
		if (parent == current || parent == BI_NONE)
			break;
	}
	return false;
}

void TransformHandHierarchy(IKinematics* K, u16 hand, const Fmatrix& delta,
	const Fvector& target_hand)
{
	if (!IsValidBone(K, hand))
		return;

	Fvector rigid_target = BonePosition(K, hand);
	delta.transform_tiny(rigid_target);
	Fvector correction;
	correction.sub(target_hand, rigid_target);
	for (u16 bone_id = 0; bone_id < K->LL_BoneCount(); ++bone_id)
	{
		if (!IsHandDescendant(K, bone_id, hand))
			continue;
		TransformBone(K, bone_id, delta);
		CBoneInstance& bone = K->LL_GetBoneInstance(bone_id);
		bone.mTransform.c.add(correction);
		bone.mRenderTransform.mul_43(bone.mTransform, K->LL_GetData(bone_id).m2b_transform);
	}
}

void SolveArmToHand(IKinematics* K, u16 upper, u16 forearm, u16 twist, u16 hand,
	const Fvector& target_hand, const Fvector& pole_elbow, const Fmatrix& hand_delta)
{
	if (!IsValidBone(K, upper) || !IsValidBone(K, forearm) || !IsValidBone(K, hand))
		return;

	const Fvector shoulder = BonePosition(K, upper);
	const Fvector elbow = BonePosition(K, forearm);
	const Fvector hand_pos = BonePosition(K, hand);
	const bool has_twist = IsValidBone(K, twist);
	const Fvector twist_pos = has_twist ? BonePosition(K, twist) : elbow;
	const float upper_length = shoulder.distance_to(elbow);
	const float forearm_length = elbow.distance_to(hand_pos);

	Fvector solved_hand = target_hand;
	Fvector reach;
	reach.sub(solved_hand, shoulder);
	const float reach_length = reach.magnitude();
	const float max_reach = std::max(upper_length + forearm_length - 0.0001f, 0.0001f);
	const float min_reach = _abs(upper_length - forearm_length) + 0.0001f;
	if (reach_length > EPS_S && (reach_length > max_reach || reach_length < min_reach))
	{
		reach.mul(1.f / reach_length);
		reach.mul(clampr(reach_length, min_reach, max_reach));
		solved_hand.add(shoulder, reach);
	}

	const Fvector solved_elbow = SolveArmJoint(
		shoulder, solved_hand, pole_elbow, upper_length, forearm_length, 0.f);
	RotateBoneTowardChild(K, upper, elbow, shoulder, solved_elbow);
	RotateBoneTowardChild(K, forearm, hand_pos, solved_elbow, solved_hand);

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
			twist_fraction = clampr(old_twist.dotproduct(old_forearm) / old_length_sq, 0.f, 1.f);
		}
		Fvector solved_twist;
		solved_twist.lerp(solved_elbow, solved_hand, twist_fraction);
		RotateBoneTowardChild(K, twist, hand_pos, solved_twist, solved_hand);
	}

	TransformHandHierarchy(K, hand, hand_delta, solved_hand);
}

Fvector FilterAuthoredArmPole(HudArmChain& arm, const Fvector& current,
	float weight, float response, float dt)
{
	weight = clampr(weight, 0.f, 4.f);
	if (!arm.authored_pole_valid || weight < 0.0001f)
	{
		arm.authored_pole.set(current);
		arm.authored_pole_valid = true;
		return current;
	}

	dt = clampr(dt, 0.f, 0.05f);
	const float alpha = 1.f - std::exp(-std::max(response, 0.1f) * dt);
	Fvector filtered;
	filtered.lerp(arm.authored_pole, current, alpha);
	arm.authored_pole.set(filtered);
	Fvector result;
	result.lerp(current, filtered, weight);
	return result;
}

bool RelativeBoneTransform(IKinematics* K, u16 parent, u16 child, Fmatrix& result)
{
	if (!IsValidBone(K, parent) || !IsValidBone(K, child))
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
	const float cosine = clampr((delta.i.x + delta.j.y + delta.k.z - 1.f) * 0.5f, -1.f, 1.f);
	return std::acos(cosine);
}

void UpdateRotationRange(Fmatrix& endpoint_a, Fmatrix& endpoint_b, float& range, const Fmatrix& sample)
{
	const float from_a = RotationDistance(endpoint_a, sample);
	const float from_b = RotationDistance(endpoint_b, sample);
	if (from_a >= from_b && from_a > range)
	{
		endpoint_b = sample;
		range = from_a;
	}
	else if (from_b > range)
	{
		endpoint_a = sample;
		range = from_b;
	}
}

void UpdateTranslationRange(Fmatrix& endpoint_a, Fmatrix& endpoint_b, float& range, const Fmatrix& sample)
{
	const float from_a = endpoint_a.c.distance_to(sample.c);
	const float from_b = endpoint_b.c.distance_to(sample.c);
	if (from_a >= from_b && from_a > range)
	{
		endpoint_b = sample;
		range = from_a;
	}
	else if (from_b > range)
	{
		endpoint_a = sample;
		range = from_b;
	}
}

u64 AuthoredMotionKey(u32 motion, bool ads)
{
	return (u64(motion) << 1) | u64(ads);
}

bool ReadActiveHandMotion(IKinematicsAnimated* model, const attachable_hud_item* item,
	u32& motion_key, float& phase, float& blend_weight)
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
			phase = clampr(blend->timeCurrent / blend->timeTotal, 0.f, 1.f);
			blend_weight = blend->blendAmount;
			return true;
		}
	}
	return false;
}
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
	case CWeapon::eAimEnd:
		return true;
	default:
		return false;
	}
}

void ClearAuthoredPoles(HudArmChain (&arms)[hud_arm_side_count])
{
	for (HudArmChain& arm : arms)
		arm.authored_pole_valid = false;
}

}

struct HudArms::State
{
	explicit State(player_hud& owner) : m_hud(owner) {}
	void Reset();
	void ReadSimulationPose(const ArmPose& pose);
	void UpdateAuthoredMotionProfile(float dt);
	void UpdateAndApply(const ArmPose& pose, float dt);
	void ApplyBodycamArmPose(IKinematics* model, const HudArmChain& arm);
	void ApplyStalker2Controller(float dt);
	void OnWeaponChanged(const attachable_hud_item* item);
	void DumpAuthoredMotionProfile() const;
	player_hud& m_hud;
	HudArmChain m_arms[hud_arm_side_count];
	u16 m_primary_anchor = BI_NONE;
	Fvector m_stalker2_controller_rotation = { 0.f, 0.f, 0.f };
	Fvector m_stalker2_arm_follow_rotation = { 0.f, 0.f, 0.f };
	float m_stalker2_movement_weight = 0.f;
	float m_stalker2_movement_response = kDefaultStalker2MovementResponse;
	float m_stalker2_weight = 0.f;
	bool m_stalker2_active = false;
	xr_map<u64, AuthoredMotionProfile> m_motion_profiles;
	u64 m_active_profile = u64(-1);
	float m_authored_phase = 0.f;
	float m_profile_weight = 0.f;
	float m_controller_gain = 0.f;
	float m_wrist_gain = 0.f;
	float m_arm_gain = 0.f;
	Fvector m_item_position = { 0.f, 0.f, 0.f };
	Fvector m_item_rotation = { 0.f, 0.f, 0.f };
	Fvector m_fire_direction = { 0.f, 0.f, 1.f };
	bool m_weapon_geometry_valid = false;
};
void HudArms::State::Reset()
{
	for (u32 i = 0; i < hud_arm_side_count; ++i)
		m_arms[i].ResetBones();
	m_authored_phase = 0.f;
	m_stalker2_controller_rotation.set(0.f, 0.f, 0.f);
	m_stalker2_arm_follow_rotation.set(0.f, 0.f, 0.f);
	m_stalker2_movement_weight = 0.f;
	m_stalker2_movement_response = kDefaultStalker2MovementResponse;
	m_stalker2_weight = 0.f;
	m_stalker2_active = false;
	m_item_position.set(0.f, 0.f, 0.f);
	m_item_rotation.set(0.f, 0.f, 0.f);
	m_fire_direction.set(0.f, 0.f, 1.f);

	IKinematics* right_model = m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr;
	IKinematics* left_model = m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr;
	m_primary_anchor = BI_NONE;
	if (right_model)
	{
		HudArmChain& arm = m_arms[hud_arm_right];
		arm.clavicle = ResolveArmBone(right_model, "r_clavicle", "bip01_r_clavicle");
		arm.upperarm = ResolveArmBone(right_model, "r_upperarm", "bip01_r_upperarm");
		arm.forearm = ResolveArmBone(right_model, "r_forearm", "bip01_r_forearm");
		arm.twist = ResolveArmBone(right_model, "r_forearm_twist", "bip01_r_forearm_twist");
		arm.hand = ResolveArmBone(right_model, "r_hand", "bip01_r_hand");
		m_primary_anchor = ResolveArmBone(right_model, "lead_gun", "ancor_0");
	}

	if (left_model)
	{
		HudArmChain& arm = m_arms[hud_arm_left];
		arm.clavicle = ResolveArmBone(left_model, "l_clavicle", "bip01_l_clavicle");
		arm.upperarm = ResolveArmBone(left_model, "l_upperarm", "bip01_l_upperarm");
		arm.forearm = ResolveArmBone(left_model, "l_forearm", "bip01_l_forearm");
		arm.twist = ResolveArmBone(left_model, "l_forearm_twist", "bip01_l_forearm_twist");
		arm.hand = ResolveArmBone(left_model, "l_hand", "bip01_l_hand");
	}

	m_weapon_geometry_valid = false;
	OnWeaponChanged(m_hud.attached_item(0));
}

void HudArms::State::ReadSimulationPose(const ArmPose& pose)
{
	HudArmChain& right = m_arms[hud_arm_right];
	right.upper_pose.set(pose.upperarm);
	right.forearm_pose.set(pose.forearm);
	right.twist_pose.set(pose.twist);

	HudArmChain& left = m_arms[hud_arm_left];
	left.upper_pose.set(pose.left_upperarm);
	left.forearm_pose.set(pose.left_forearm);
	left.twist_pose.set(pose.left_twist);
	m_stalker2_active = pose.stalker2_active;
	m_stalker2_controller_rotation.set(pose.stalker2_wrist_rot);
	m_stalker2_arm_follow_rotation.set(pose.stalker2_arm_follow_rot);
	m_stalker2_movement_weight = pose.stalker2_movement_weight;
	m_stalker2_movement_response = pose.stalker2_movement_response;
}

void HudArms::State::UpdateAuthoredMotionProfile(float dt)
{
	dt = clampr(dt, 0.f, 0.05f);
	auto fade_profile = [&]()
	{
		m_profile_weight += clampr(-m_profile_weight, -dt * kProfileFadeOutSpeed,
			dt * kProfileSamplingFadeInSpeed);
	};

	IKinematics* primary = m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr;
	IKinematics* support = m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr;
	attachable_hud_item* item = m_hud.attached_item(0);
	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool profile_allowed = weapon && weapon->GetState() == CWeapon::eIdle && m_hud.script_anim_part == u8(-1) &&
		m_stalker2_active && m_stalker2_movement_weight > 0.001f;
	if (!profile_allowed || !primary || !support || !IsValidBone(primary, m_primary_anchor))
	{
		fade_profile();
		return;
	}

	u32 motion_key = u32(-1);
	float phase = 0.f;
	float blend_weight = 0.f;
	if (!ReadActiveHandMotion(m_hud.m_model, item, motion_key, phase, blend_weight) ||
		blend_weight < 0.95f)
	{
		fade_profile();
		return;
	}

	HudArmChain& right = m_arms[hud_arm_right];
	HudArmChain& left = m_arms[hud_arm_left];
	Fmatrix right_wrist;
	Fmatrix left_wrist;
	Fmatrix right_forearm;
	Fmatrix left_forearm;
	Fmatrix right_upperarm;
	Fmatrix left_upperarm;
	if (!RelativeBoneTransform(primary, right.forearm, right.hand, right_wrist) ||
		!RelativeBoneTransform(support, left.forearm, left.hand, left_wrist) ||
		!RelativeBoneTransform(primary, right.upperarm, right.forearm, right_forearm) ||
		!RelativeBoneTransform(support, left.upperarm, left.forearm, left_forearm) ||
		!RelativeBoneTransform(primary, right.clavicle, right.upperarm, right_upperarm) ||
		!RelativeBoneTransform(support, left.clavicle, left.upperarm, left_upperarm))
	{
		fade_profile();
		return;
	}

	const bool ads = weapon->IsZoomed();
	const u64 profile_key = AuthoredMotionKey(motion_key, ads);
	if (profile_key != m_active_profile)
	{
		m_active_profile = profile_key;
		m_profile_weight = 0.f;
	}
	m_authored_phase = phase;
	AuthoredMotionProfile& profile = m_motion_profiles[profile_key];
	const Fmatrix& lead = primary->LL_GetBoneInstance(m_primary_anchor).mTransform;
	if (!profile.initialized)
	{
		profile.initialized = true;
		profile.previous_phase = phase;
		profile.lead_reference = lead;
		profile.lead_opposite = lead;
		profile.lead_translation_reference = lead;
		profile.lead_translation_opposite = lead;
		profile.right_wrist_reference = right_wrist;
		profile.right_wrist_opposite = right_wrist;
		profile.left_wrist_reference = left_wrist;
		profile.left_wrist_opposite = left_wrist;
		profile.right_forearm_reference = right_forearm;
		profile.right_forearm_opposite = right_forearm;
		profile.left_forearm_reference = left_forearm;
		profile.left_forearm_opposite = left_forearm;
		profile.right_upperarm_reference = right_upperarm;
		profile.right_upperarm_opposite = right_upperarm;
		profile.left_upperarm_reference = left_upperarm;
		profile.left_upperarm_opposite = left_upperarm;
		fade_profile();
		return;
	}

	if (!profile.complete)
	{
		float phase_delta = phase - profile.previous_phase;
		if (phase_delta < -0.5f)
			phase_delta += 1.f;
		if (phase_delta >= 0.f && phase_delta <= 0.25f)
			profile.phase_travel += phase_delta;
		profile.previous_phase = phase;
		++profile.sample_count;

		UpdateRotationRange(profile.lead_reference, profile.lead_opposite, profile.lead_rotation, lead);
		UpdateTranslationRange(profile.lead_translation_reference, profile.lead_translation_opposite,
			profile.lead_translation, lead);
		UpdateRotationRange(profile.right_wrist_reference, profile.right_wrist_opposite,
			profile.right_wrist_rotation, right_wrist);
		UpdateRotationRange(profile.left_wrist_reference, profile.left_wrist_opposite,
			profile.left_wrist_rotation, left_wrist);
		UpdateRotationRange(profile.right_forearm_reference, profile.right_forearm_opposite,
			profile.right_forearm_rotation, right_forearm);
		UpdateRotationRange(profile.left_forearm_reference, profile.left_forearm_opposite,
			profile.left_forearm_rotation, left_forearm);
		UpdateRotationRange(profile.right_upperarm_reference, profile.right_upperarm_opposite,
			profile.right_upperarm_rotation, right_upperarm);
		UpdateRotationRange(profile.left_upperarm_reference, profile.left_upperarm_opposite,
			profile.left_upperarm_rotation, left_upperarm);
		profile.wrist_rotation = std::max(profile.right_wrist_rotation, profile.left_wrist_rotation);
		profile.forearm_rotation = std::max(profile.right_forearm_rotation, profile.left_forearm_rotation);
		profile.upperarm_rotation = std::max(profile.right_upperarm_rotation, profile.left_upperarm_rotation);

		if (profile.phase_travel >= 0.95f && profile.sample_count >= 20)
		{
			Bodycam::AuthoredMotionMetrics metrics;
			metrics.lead_rotation = profile.lead_rotation;
			metrics.lead_translation = profile.lead_translation;
			metrics.wrist_rotation = profile.wrist_rotation;
			metrics.forearm_rotation = profile.forearm_rotation;
			metrics.upperarm_rotation = profile.upperarm_rotation;
			const Bodycam::AuthoredMotionGains gains = Bodycam::CalculateAuthoredMotionGains(metrics);
			profile.controller_gain = gains.controller;
			profile.wrist_gain = gains.wrist;
			profile.arm_gain = gains.arm;
			profile.complete = true;
		}
	}

	if (!profile.complete)
	{
		fade_profile();
		return;
	}

	m_controller_gain = profile.controller_gain;
	m_wrist_gain = profile.wrist_gain;
	m_arm_gain = profile.arm_gain;
	m_profile_weight += clampr(1.f - m_profile_weight, -dt * kProfileFadeOutSpeed,
		dt * kProfileReadyFadeInSpeed);
}

void HudArms::State::DumpAuthoredMotionProfile() const
{
	attachable_hud_item* item = m_hud.attached_item(0);
	u32 motion_key = u32(-1);
	float phase = 0.f;
	float blend_weight = 0.f;
	const bool tracked = ReadActiveHandMotion(m_hud.m_model, item, motion_key, phase, blend_weight);
	Msg("* bodycam authored motion tracked=%d item=%s key=%u generation=%u phase=%0.4f blend=%0.3f output=%0.3f",
		tracked, item ? item->m_sect_name.c_str() : "<none>", motion_key,
		item ? item->m_active_hand_generation : 0, phase, blend_weight, m_profile_weight);

	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool ads = weapon && weapon->IsZoomed();
	const auto profile = m_motion_profiles.find(AuthoredMotionKey(motion_key, ads));
	if (profile == m_motion_profiles.end())
	{
		Msg("* bodycam authored profile unavailable");
		return;
	}

	const AuthoredMotionProfile& value = profile->second;
	Msg("* bodycam authored profile complete=%d samples=%u travel=%0.3f lead_deg=%0.3f lead_mm=%0.3f wrist_deg=%0.3f forearm_deg=%0.3f upperarm_deg=%0.3f",
		value.complete, value.sample_count, value.phase_travel,
		angle_normalize_signed(value.lead_rotation) * 180.f / PI,
		value.lead_translation * 1000.f,
		angle_normalize_signed(value.wrist_rotation) * 180.f / PI,
		angle_normalize_signed(value.forearm_rotation) * 180.f / PI,
		angle_normalize_signed(value.upperarm_rotation) * 180.f / PI);
	Msg("* bodycam authored gains controller=%0.3f wrist=%0.3f arm=%0.3f",
		value.controller_gain, value.wrist_gain, value.arm_gain);
}

void HudArms::State::ApplyBodycamArmPose(IKinematics* K, const HudArmChain& arm)
{
	if (!K)
		return;

	ApplyAdditiveArmFollow(K,
		arm.upperarm,
		arm.forearm,
		arm.twist,
		arm.hand,
		arm.upper_pose,
		arm.forearm_pose,
		arm.twist_pose);
}

void HudArms::State::UpdateAndApply(const ArmPose& pose, float dt)
{
	ReadSimulationPose(pose);
	UpdateAuthoredMotionProfile(dt);
	ApplyBodycamArmPose(m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr, m_arms[hud_arm_right]);
	ApplyBodycamArmPose(m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr, m_arms[hud_arm_left]);
	ApplyStalker2Controller(dt);
}

void HudArms::State::OnWeaponChanged(const attachable_hud_item* item)
{
	m_weapon_geometry_valid = false;
	m_item_position.set(0.f, 0.f, 0.f);
	m_item_rotation.set(0.f, 0.f, 0.f);
	m_fire_direction.set(0.f, 0.f, 1.f);
	m_motion_profiles.clear();
	m_active_profile = u64(-1);
	m_profile_weight = 0.f;
	m_controller_gain = 0.f;
	m_wrist_gain = 0.f;
	m_arm_gain = 0.f;
	ClearAuthoredPoles(m_arms);

	if (!item)
		return;

	m_item_position.set(item->m_measures.m_item_attach[0]);
	m_item_rotation.set(item->m_measures.m_item_attach[1]);
	m_fire_direction.set(item->m_measures.m_fire_direction);
	m_weapon_geometry_valid = true;
}

void HudArms::State::ApplyStalker2Controller(float dt)
{
	IKinematics* primary = m_hud.m_model ? m_hud.m_model->dcast_PKinematics() : nullptr;
	IKinematics* support_model = m_hud.m_model_2 ? m_hud.m_model_2->dcast_PKinematics() : nullptr;
	attachable_hud_item* item = m_hud.attached_item(0);
	CWeapon* weapon = item ? smart_cast<CWeapon*>(item->m_parent_hud_item) : nullptr;
	const bool layer_allowed = AllowsControllerLayer(weapon, m_hud.script_anim_part);
	const float target_weight = m_stalker2_active && layer_allowed ? 1.f : 0.f;
	const float blend_speed = target_weight > m_stalker2_weight ? kControllerEnterSpeed : kControllerExitSpeed;
	m_stalker2_weight += clampr(target_weight - m_stalker2_weight,
		-blend_speed * dt, blend_speed * dt);
	HudArmChain& right = m_arms[hud_arm_right];
	HudArmChain& left = m_arms[hud_arm_left];
	if (!primary || m_stalker2_weight < 0.0001f)
	{
		ClearAuthoredPoles(m_arms);
		return;
	}

	Fvector mouse_wrist_rot = m_stalker2_controller_rotation;
	Fvector arm_follow_rot = m_stalker2_arm_follow_rotation;
	Fvector authored_walk_rot = { 0.f, 0.f, 0.f };
	Fvector authored_walk_pos = { 0.f, 0.f, 0.f };
	const float movement_input = clampr(m_stalker2_movement_weight, 0.f, 4.f);
	const float movement_weight = movement_input * m_stalker2_weight;
	const float authored_weight = movement_input * m_profile_weight;
	if (authored_weight > 0.0001f)
	{
		Bodycam::AuthoredMotionGains gains;
		gains.controller = m_controller_gain;
		gains.wrist = m_wrist_gain;
		gains.arm = m_arm_gain;
		const Bodycam::SVec3 authored = Bodycam::CalculateAuthoredWalkRotation(
			m_authored_phase, authored_weight, gains);
		authored_walk_rot.set(authored.x, authored.y, authored.z);
		const Bodycam::SVec3 authored_position = Bodycam::CalculateAuthoredWalkTranslation(
			m_authored_phase, authored_weight, gains);
		authored_walk_pos.set(authored_position.x, authored_position.y, authored_position.z);

		// The arms absorb part of the walking swing rejected by barrel stabilization.
		const float arm_follow = Bodycam::CalculateAuthoredArmFollow(m_arm_gain);
		Fvector authored_arm_rot = authored_walk_rot;
		authored_arm_rot.mul(arm_follow);
		arm_follow_rot.add(authored_arm_rot);
	}
	mouse_wrist_rot.mul(m_stalker2_weight);
	authored_walk_rot.mul(m_stalker2_weight);
	authored_walk_pos.mul(m_stalker2_weight);
	arm_follow_rot.mul(m_stalker2_weight);

	if (!IsValidBone(primary, right.hand) || !IsValidBone(primary, m_primary_anchor) ||
		(!m_hud.m_adjust_mode && !m_weapon_geometry_valid))
	{
		ClearAuthoredPoles(m_arms);
		return;
	}

	Fmatrix mouse_rotation;
	mouse_rotation.setHPB(mouse_wrist_rot.x, mouse_wrist_rot.y, mouse_wrist_rot.z);
	Fmatrix walk_rotation;
	walk_rotation.setHPB(authored_walk_rot.x, authored_walk_rot.y, authored_walk_rot.z);
	const Fvector& item_position = m_hud.m_adjust_mode ? m_hud.m_adjust_obj[0] : m_item_position;
	const Fvector& item_rotation = m_hud.m_adjust_mode ? m_hud.m_adjust_obj[1] : m_item_rotation;
	const Fvector authored_fire_axis = CalculateFireAxis(primary, m_primary_anchor,
		item_position, item_rotation, m_fire_direction);
	const Fmatrix stabilized_walk_rotation = ExtractTwistRotation(walk_rotation, authored_fire_axis);
	Fmatrix rotation;
	rotation.mul_43(mouse_rotation, stabilized_walk_rotation);
	Fmatrix arm_follow_rotation;
	arm_follow_rotation.setHPB(arm_follow_rot.x, arm_follow_rot.y, arm_follow_rot.z);
	const Fvector right_hand = BonePosition(primary, right.hand);
	const Fvector controller_pivot = BonePosition(primary, m_primary_anchor);
	Fmatrix primary_delta = RotationAboutPivot(controller_pivot, rotation);
	primary_delta.c.add(authored_walk_pos);
	const Fmatrix primary_arm_follow_delta = RotationAboutPivot(controller_pivot, arm_follow_rotation);
	const bool anchor_follows_right_hand =
		IsHandDescendant(primary, m_primary_anchor, right.hand);

	Fmatrix support_delta;
	Fmatrix support_arm_follow_delta;
	if (support_model && IsValidBone(support_model, left.hand))
	{
		support_delta = ConvertModelDelta(m_hud.m_transform, m_hud.m_transform_2, primary_delta);
		support_arm_follow_delta = ConvertModelDelta(m_hud.m_transform, m_hud.m_transform_2, primary_arm_follow_delta);
	}

	// The weapon and both authored grips form one rigid controller. Move each
	// hand to the target produced by that controller, then solve the arm behind
	// the fixed hand instead of using an arm bone to fake weapon motion.
	Fvector right_target = right_hand;
	primary_delta.transform_tiny(right_target);
	Fvector right_pole = FilterAuthoredArmPole(right, BonePosition(primary, right.forearm),
		movement_weight, m_stalker2_movement_response, dt);
	primary_arm_follow_delta.transform_tiny(right_pole);
	SolveArmToHand(primary, right.upperarm, right.forearm, right.twist, right.hand,
		right_target, right_pole, primary_delta);
	if (!anchor_follows_right_hand)
		TransformBone(primary, m_primary_anchor, primary_delta);

	if (support_model && IsValidBone(support_model, left.hand))
	{
		Fvector left_target = BonePosition(support_model, left.hand);
		support_delta.transform_tiny(left_target);
		Fvector left_pole = FilterAuthoredArmPole(left, BonePosition(support_model, left.forearm),
			movement_weight, m_stalker2_movement_response, dt);
		support_arm_follow_delta.transform_tiny(left_pole);
		SolveArmToHand(support_model, left.upperarm, left.forearm, left.twist, left.hand,
			left_target, left_pole, support_delta);
	}
}

HudArms::HudArms(player_hud& hud) : m_state(xr_new<State>(hud))
{
}

HudArms::~HudArms()
{
	xr_delete(m_state);
}

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

void HudArms::DumpAuthoredMotionProfile() const
{
	m_state->DumpAuthoredMotionProfile();
}
} // namespace Bodycam
