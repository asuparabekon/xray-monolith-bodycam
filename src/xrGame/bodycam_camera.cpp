#include "stdafx.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "level.h"
#include "../xrEngine/CameraBase.h"

namespace Bodycam
{
namespace
{
DebugSnapshot g_debug_snapshot;
std::uint32_t ConvertMoveFlags(u32 mstate)
{
	std::uint32_t flags = 0;
	if (mstate & mcFwd)
		flags |= smfForward;
	if (mstate & mcBack)
		flags |= smfBack;
	if (mstate & mcLStrafe)
		flags |= smfLeft;
	if (mstate & mcRStrafe)
		flags |= smfRight;
	if (mstate & mcCrouch)
		flags |= smfCrouch;
	if (mstate & mcSprint)
		flags |= smfSprint;
	if (mstate & mcFall)
		flags |= smfFall;
	if (mstate & mcJump)
		flags |= smfJump;
	if (mstate & (mcLanding | mcLanding2))
		flags |= smfLanding;
	return flags;
}

Fvector ToFvector(const SVec3& value)
{
	Fvector result;
	result.set(value.x, value.y, value.z);
	return result;
}

ArmPose BuildArmPose(const SimulationOutput& output)
{
	ArmPose pose;
	pose.active = output.arm_active;
	pose.stalker2_active = output.stalker2_arm_active;
	pose.clavicle = ToFvector(output.arm_clavicle);
	pose.upperarm = ToFvector(output.arm_upperarm);
	pose.forearm = ToFvector(output.arm_forearm);
	pose.twist = ToFvector(output.arm_twist);
	pose.hand = ToFvector(output.arm_hand);
	pose.left_clavicle = ToFvector(output.arm_left_clavicle);
	pose.left_upperarm = ToFvector(output.arm_left_upperarm);
	pose.left_forearm = ToFvector(output.arm_left_forearm);
	pose.left_twist = ToFvector(output.arm_left_twist);
	pose.left_hand = ToFvector(output.arm_left_hand);
	pose.stalker2_wrist_rot = ToFvector(output.stalker2_wrist_rot);
	pose.stalker2_arm_follow_rot = ToFvector(output.stalker2_arm_follow_rot);
	pose.stalker2_movement_weight = output.stalker2_movement_weight;
	pose.stalker2_movement_response = output.stalker2_movement_response;
	return pose;
}
} // namespace

void CBodycam::ClearViewmodelOutput()
{
	m_viewmodel_active = FALSE;
	m_viewmodel_pos.set(0.f, 0.f, 0.f);
	m_viewmodel_rot.set(0.f, 0.f, 0.f);
}

void CBodycam::ClearHudOutput()
{
	ClearViewmodelOutput();
	m_arm_pose = ArmPose();
	g_debug_snapshot.active = false;
}

void CBodycam::SetMovementDebug(float target_speed, float actual_speed, float speed_fraction)
{
	m_movement_target_speed = target_speed;
	m_movement_actual_speed = actual_speed;
	m_movement_speed_fraction = speed_fraction;
}

void CBodycam::Reset(const CCameraBase* camera, u32 mstate, float ads_blend)
{
	m_mouse_aim = MouseAimState();

	if (!camera)
	{
		m_state = SimulationState();
		ClearHudOutput();
		return;
	}

	float yaw = 0.f;
	float pitch = 0.f;
	camera->vDirection.getHP(yaw, pitch);
	ResetMouseAim(m_mouse_aim, yaw, pitch);
	ResetSimulation(m_state, yaw, pitch, ConvertMoveFlags(mstate), ads_blend);
	ClearHudOutput();
}

void CBodycam::AddMouseLookDelta(float yaw_delta, float pitch_delta)
{
	AddMouseAimDelta(m_mouse_aim, yaw_delta, pitch_delta);
}

PipView CBodycam::UpdatePipView(const PipInput& input)
{
	return m_pip_adapter.Update(input);
}

void CBodycam::Update(const UpdateInput& input, VisualOutput& output)
{
	const SimulationSettings settings = GetSimulationSettings();
	const MouseAimOutput mouse_aim = ResolveMouseAim(m_mouse_aim, input.target_yaw, input.target_pitch);

	SimulationInput sim_input;
	sim_input.target_yaw = input.target_yaw;
	sim_input.target_pitch = input.target_pitch;
	sim_input.dt = input.dt;
	sim_input.move_flags = ConvertMoveFlags(input.mstate);
	sim_input.ads = input.ads;
	sim_input.ads_blend = input.ads_blend;
	sim_input.weapon_lowered = input.weapon_lowered;
	sim_input.combat = input.combat;
	sim_input.firearm_equipped = input.firearm_equipped;
	sim_input.actor_speed_fraction = input.actor_speed_fraction;
	sim_input.accelerated = isActorAccelerated(input.mstate, input.ads);
	sim_input.visual_aim_available = mouse_aim.available;
	sim_input.visual_aim_yaw = mouse_aim.yaw;
	sim_input.visual_aim_pitch = mouse_aim.pitch;

	SimulationOutput sim_output;
	UpdateSimulation(settings, m_state, sim_input, sim_output);
	if (settings.features.impulse_debug && (sim_output.impulse_pos_clamped || sim_output.impulse_rot_clamped))
	{
		Msg("* bodycam impulse clamped pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f]",
			m_state.viewmodel.impulse_pos.x, m_state.viewmodel.impulse_pos.y, m_state.viewmodel.impulse_pos.z,
			m_state.viewmodel.impulse_rot.x, m_state.viewmodel.impulse_rot.y, m_state.viewmodel.impulse_rot.z);
	}

	if (sim_output.viewmodel_active)
	{
		m_viewmodel_active = TRUE;
		m_viewmodel_pos = ToFvector(sim_output.viewmodel_pos);
		m_viewmodel_rot = ToFvector(sim_output.viewmodel_rot);
	}
	else
	{
		ClearViewmodelOutput();
	}

	m_arm_pose = BuildArmPose(sim_output);

	output.yaw = sim_output.yaw;
	output.pitch = sim_output.pitch;
	output.roll = sim_output.roll;
	output.pos = ToFvector(sim_output.camera_pos);
	output.fov_offset = sim_output.fov_offset;
	CaptureDebugSnapshot(input.ads, input.ads_blend, input.mstate);
}

void CBodycam::AddFireImpulse(float power, bool ads)
{
	Bodycam::AddFireImpulse(GetSimulationSettings(), m_state, power, ads);
}

void CBodycam::AddImpulse(LPCSTR kind, float power, bool ads)
{
	if (!AddNamedImpulse(GetSimulationSettings(), m_state, kind, power, ads))
		Msg("! bodycam.add_impulse: unknown impulse kind '%s'", kind ? kind : "<null>");
}

void CBodycam::CaptureDebugSnapshot(bool ads, float ads_blend, u32 mstate) const
{
	const RuntimeConfig& config = GetConfig();
	g_debug_snapshot.active = true;
	g_debug_snapshot.camera_enabled = !!config.features.camera_enable;
	g_debug_snapshot.vm_enabled = !!config.features.vm_enable;
	g_debug_snapshot.lower_enabled = !!config.features.lower_enable;
	g_debug_snapshot.arm_enabled = !!config.features.bodycam_arm_enable || !!config.features.stalker2_arm_enable;
	g_debug_snapshot.ads = ads || ads_blend > 0.f;
	g_debug_snapshot.mstate = mstate;
	g_debug_snapshot.camera_yaw = RadToDeg(m_state.camera.yaw);
	g_debug_snapshot.camera_pitch = RadToDeg(m_state.camera.pitch);
	g_debug_snapshot.camera_roll = RadToDeg(m_state.camera.roll);
	g_debug_snapshot.camera_pos = ToFvector(m_state.camera.pos);
	g_debug_snapshot.vm_pos.set(m_viewmodel_pos);
	g_debug_snapshot.vm_rot.set(m_viewmodel_rot);
	g_debug_snapshot.movement_target_speed = m_movement_target_speed;
	g_debug_snapshot.movement_actual_speed = m_movement_actual_speed;
	g_debug_snapshot.movement_speed_fraction = m_movement_speed_fraction;
	g_debug_snapshot.lower_target = m_state.lowering.target;
	g_debug_snapshot.lower_amount = m_state.lowering.amount;
	g_debug_snapshot.lower_holster = m_state.lowering.holster;
	g_debug_snapshot.arm_clavicle = m_arm_pose.clavicle;
	g_debug_snapshot.arm_upperarm = m_arm_pose.upperarm;
	g_debug_snapshot.arm_forearm = m_arm_pose.forearm;
	g_debug_snapshot.arm_twist = m_arm_pose.twist;
	g_debug_snapshot.arm_hand = m_arm_pose.hand;
}

bool GetDebugSnapshot(DebugSnapshot& snapshot)
{
	snapshot = g_debug_snapshot;
	return snapshot.active;
}

void CBodycam::Dump(bool ads, u32 mstate) const
{
	const RuntimeConfig& config = GetConfig();
	const float ads_blend = m_state.ads_blend;
	CaptureDebugSnapshot(ads, ads_blend, mstate);
	Msg("* bodycam dump enabled=%d viewmodel=%d active=%d ads=%d ads_blend=%0.3f mstate=0x%08x", config.features.camera_enable, config.features.vm_enable, m_viewmodel_active, ads, ads_blend, mstate);
	Msg("* bodycam camera yaw=%0.3f pitch=%0.3f roll=%0.3f pos[%0.4f %0.4f %0.4f]", RadToDeg(m_state.camera.yaw), RadToDeg(m_state.camera.pitch), RadToDeg(m_state.camera.roll), m_state.camera.pos.x, m_state.camera.pos.y, m_state.camera.pos.z);
	Msg("* bodycam mouse speed[%0.3f %0.3f] accel[%0.3f %0.3f] move[%0.3f %0.3f %0.3f]", RadToDeg(m_state.viewmodel.mouse_speed.x), RadToDeg(m_state.viewmodel.mouse_speed.y), RadToDeg(m_state.viewmodel.mouse_accel.x), RadToDeg(m_state.viewmodel.mouse_accel.y), m_state.viewmodel.move_intent.x, m_state.viewmodel.move_intent.y, m_state.viewmodel.move_intent.z);
	Msg("* bodycam impulse pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f] airborne=%0.3f caps pos=%0.3f rot=%0.3f", m_state.viewmodel.impulse_pos.x, m_state.viewmodel.impulse_pos.y, m_state.viewmodel.impulse_pos.z, m_state.viewmodel.impulse_rot.x, m_state.viewmodel.impulse_rot.y, m_state.viewmodel.impulse_rot.z, m_state.viewmodel.airborne_time, config.impulse.impulse_pos_cap, config.impulse.impulse_rot_cap);
	Msg("* bodycam viewmodel pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f] ads_mult mouse=%0.3f impulse=%0.3f", m_state.viewmodel.pos.x, m_state.viewmodel.pos.y, m_state.viewmodel.pos.z, m_state.viewmodel.rot.x, m_state.viewmodel.rot.y, m_state.viewmodel.rot.z, config.viewmodel.ads_mouse_mult, config.viewmodel.ads_impulse_mult);
	Msg("* bodycam movement target=%0.3f actual=%0.3f fraction=%0.3f accel enabled=%d ads_disable=%d accel=%0.3f decel=%0.3f",
		m_movement_target_speed, m_movement_actual_speed, m_movement_speed_fraction, config.movement.enable, config.movement.ads_disable,
		config.movement.accel_time, config.movement.decel_time);
	Msg("* bodycam lowering enabled=%d target=%0.3f droop=%0.3f holster=%0.3f timers fire=%0.3f ads=%0.3f combat=%0.3f lower=%0.3f return=%0.3f pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f]",
		config.features.lower_enable, m_state.lowering.target, m_state.lowering.amount, m_state.lowering.holster, m_state.lowering.fire_recovery, m_state.lowering.ads_recovery, m_state.lowering.combat_timer,
		config.lowering.speed, config.lowering.return_speed, m_state.lowering.pos.x, m_state.lowering.pos.y, m_state.lowering.pos.z, m_state.lowering.rot.x, m_state.lowering.rot.y, m_state.lowering.rot.z);
	Msg("* bodycam arm bodycam_style=%d stalker2_style=%d active=%d clavicle[%0.3f %0.3f %0.3f] upper[%0.3f %0.3f %0.3f] forearm[%0.3f %0.3f %0.3f] twist[%0.3f %0.3f %0.3f] hand[%0.3f %0.3f %0.3f]",
		config.features.bodycam_arm_enable, config.features.stalker2_arm_enable, m_arm_pose.active,
		RadToDeg(m_arm_pose.clavicle.x), RadToDeg(m_arm_pose.clavicle.y), RadToDeg(m_arm_pose.clavicle.z),
		RadToDeg(m_arm_pose.upperarm.x), RadToDeg(m_arm_pose.upperarm.y), RadToDeg(m_arm_pose.upperarm.z),
		RadToDeg(m_arm_pose.forearm.x), RadToDeg(m_arm_pose.forearm.y), RadToDeg(m_arm_pose.forearm.z),
		RadToDeg(m_arm_pose.twist.x), RadToDeg(m_arm_pose.twist.y), RadToDeg(m_arm_pose.twist.z),
		RadToDeg(m_arm_pose.hand.x), RadToDeg(m_arm_pose.hand.y), RadToDeg(m_arm_pose.hand.z));
	Msg("* bodycam stalker2 active=%d wrist_rot_deg[%0.3f %0.3f %0.3f] arm_follow_deg[%0.3f %0.3f %0.3f] authored_move[%0.3f response=%0.2f]",
		m_arm_pose.stalker2_active,
		RadToDeg(m_arm_pose.stalker2_wrist_rot.x), RadToDeg(m_arm_pose.stalker2_wrist_rot.y), RadToDeg(m_arm_pose.stalker2_wrist_rot.z),
		RadToDeg(m_arm_pose.stalker2_arm_follow_rot.x), RadToDeg(m_arm_pose.stalker2_arm_follow_rot.y), RadToDeg(m_arm_pose.stalker2_arm_follow_rot.z),
		m_arm_pose.stalker2_movement_weight, m_arm_pose.stalker2_movement_response);
	DumpConfigBindings();
}

bool CBodycam::GetHudOffset(Fvector& pos, Fvector& rot) const
{
	if (!m_viewmodel_active)
		return false;

	pos.set(m_viewmodel_pos);
	rot.set(m_viewmodel_rot);
	return true;
}

bool CBodycam::GetArmPose(ArmPose& pose) const
{
	if (!m_arm_pose.active && !m_arm_pose.stalker2_active)
		return false;

	pose = m_arm_pose;
	return true;
}

bool CBodycam::CameraEnabled() const
{
	return Bodycam::CameraEnabled();
}

bool CBodycam::HudEnabled() const
{
	return HudEffectsEnabled();
}

bool CBodycam::AnyFeatureEnabled() const
{
	return AnyEffectEnabled();
}
} // namespace Bodycam
