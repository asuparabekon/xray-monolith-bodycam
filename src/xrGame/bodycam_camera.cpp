#include "stdafx.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "level.h"
#include "../xrEngine/CameraBase.h"
#include "bodycam_diagnostics.h"

namespace Bodycam
{
namespace
{
Fvector ToFvector(const SVec3& value)
{
	Fvector result;
	result.set(value.x, value.y, value.z);
	return result;
}
} // namespace

CActor* CurrentActor()
{
	return g_pGameLevel ? smart_cast<CActor*>(Level().CurrentEntity()) : nullptr;
}

void DumpState()
{
	if (CActor* actor = CurrentActor())
		actor->cam_BodycamDumpState();
	else
		Msg("! bodycam dump: no active actor");
}

void CBodycam::ClearViewmodelOutput()
{
	m_viewmodel_active = false;
	m_viewmodel_pos.set(0.f, 0.f, 0.f);
	m_viewmodel_rot.set(0.f, 0.f, 0.f);
	m_lowering_pos.set(0.f, 0.f, 0.f);
	m_lowering_rot.set(0.f, 0.f, 0.f);
}

void CBodycam::ClearHudOutput()
{
	ClearViewmodelOutput();
	m_arm_pose = ArmPose();
}

void CBodycam::SetMovementSpeed(float target_speed, float filtered_speed, float speed_fraction)
{
	m_movement_target_speed = target_speed;
	m_movement_filtered_speed = filtered_speed;
	m_movement_speed_fraction = speed_fraction;
}

void CBodycam::Reset(const CCameraBase* camera, float ads_blend)
{
	ClearHudOutput();
	m_sprint_bob.Reset();
	if (!camera)
	{
		m_mouse_aim = MouseAimState();
		m_state = SimulationState();
		m_camera_rig.Reset();
		return;
	}

	float yaw = 0.f;
	float pitch = 0.f;
	camera->vDirection.getHP(yaw, pitch);
	ResetMouseAim(m_mouse_aim, yaw, pitch);
	ResetSimulation(m_state, yaw, pitch, ads_blend);
	m_camera_rig.Reset(yaw, pitch);
}

// The deltas are camera angles; the aim, simulation and rig keep the negated
// heading and pitch of the camera direction.
void CBodycam::AddMouseLookDelta(float yaw_delta, float pitch_delta)
{
	AddMouseAimDelta(m_mouse_aim, -yaw_delta, -pitch_delta);
}

void CBodycam::RebaseLookDelta(float yaw_delta, float pitch_delta)
{
	RebaseMouseAim(m_mouse_aim, -yaw_delta, -pitch_delta);
	RebaseSimulationLook(m_state, -yaw_delta, -pitch_delta);
	m_camera_rig.Rebase(-yaw_delta, -pitch_delta);
}

void CBodycam::SetViewmodelProfile(const Fvector& pos, const Fvector& rot, float blend_speed)
{
	Bodycam::SetViewmodelProfile(m_viewmodel_profile, { pos.x, pos.y, pos.z }, { rot.x, rot.y, rot.z }, blend_speed);
}

void CBodycam::ClearViewmodelProfile(float blend_speed)
{
	Bodycam::ClearViewmodelProfile(m_viewmodel_profile, blend_speed);
}

void CBodycam::ApplyViewmodelProfile(float dt, float ads_blend)
{
	const ViewmodelProfileOutput profile = UpdateViewmodelProfile(m_viewmodel_profile, dt, ads_blend);
	if (!profile.active)
		return;

	m_viewmodel_active = true;
	m_viewmodel_pos.add(ToFvector(profile.pos));
	m_viewmodel_rot.add(ToFvector(profile.rot));
}

// The grip in the HUD offset's space (metres right, up, forward of the hands'
// origin): the gun step bob sways the gun about it.
const Fvector kSprintBobPivot = { 0.f, -0.12f, 0.35f };

// Sprint Transition: through the sprint start the gun takes the step bob,
// added to the viewmodel offset like the fire impulse.
void CBodycam::ApplySprintBob(BodycamReference::SprintBobInput input)
{
	const RuntimeConfig& config = GetConfig();
	if (!SprintBobEnabled())
	{
		m_sprint_bob.Reset();
		return;
	}
	if (!input.starting && !m_sprint_bob.Output().active)
		return;
	input.scale = config.sprint_start.gun_bob;
	const BodycamReference::SprintBobOutput& bob = m_sprint_bob.Update(input);
	if (!bob.active)
		return;
	m_viewmodel_active = true;
	m_viewmodel_pos.add(Fvector().set(float(bob.offset[0]), float(bob.offset[1]), float(bob.offset[2])));
	// The HUD turns its offset about the hands' origin; this keeps the grip in
	// place so the gun sways about it instead of orbiting the camera.
	const Fvector rotation = Fvector().set(float(bob.rotation[1]), float(bob.rotation[0]), float(bob.rotation[2]));
	Fmatrix turn;
	turn.setHPB(DegToRad(rotation.x), DegToRad(rotation.y), DegToRad(rotation.z));
	Fvector turned;
	turn.transform_dir(turned, kSprintBobPivot);
	m_viewmodel_pos.add(kSprintBobPivot).sub(turned);
	m_viewmodel_rot.add(rotation);
	if (DiagnosticsEnabled(2))
		Msg("[BODYCAM-SPRINT-BOB] frame=%u time=%u starting=%u weight=%.4f phase=%.4f footPhase=%.4f stride=%.4f speed=%.3f x=%.5f y=%.5f z=%.5f pitch=%.3f yaw=%.3f roll=%.3f",
			Device.dwFrame, Device.dwTimeGlobal, unsigned(input.starting), bob.weight, bob.phase, input.footPhase ? *input.footPhase : -1.,
			input.strideSeconds, input.speed, bob.offset[0], bob.offset[1], bob.offset[2], bob.rotation[0], bob.rotation[1],
			bob.rotation[2]);
}

PipView CBodycam::UpdatePipView(const PipInput& input)
{
	return m_pip_adapter.Update(input, ReadPipRuntime());
}

const CameraComposition& CBodycam::Update(const ActorFrame& frame, CameraRigHost& host)
{
	const RuntimeConfig& config = GetConfig();
	const CameraRigSettings camera_settings{ config.camera, config.head, config.footstep, config.sim.sprint.enable };
	const bool follow_lag = camera_settings.FollowLag();
	CameraRigInput camera = frame.camera;
	camera.inertia_speed = m_movement_filtered_speed;
	const MouseAim mouse_aim = ResolveMouseAim(m_mouse_aim, camera.aim_yaw, camera.aim_pitch);

	SimulationInput input;
	input.dt = camera.dt;
	input.move_flags = frame.move_flags;
	input.ads = camera.ads.active;
	input.ads_blend = camera.ads.blend;
	input.weapon_lowered = frame.weapon_lowered;
	input.combat = frame.combat;
	input.accelerated = frame.accelerated;
	input.firearm_equipped = frame.firearm_equipped;
	input.actor_speed_fraction = m_movement_speed_fraction;
	input.gun_first_follow = follow_lag;
	input.sprint_camera_owned_elsewhere = config.footstep.enable;
	input.mouse_aim_yaw = mouse_aim.yaw;
	input.mouse_aim_pitch = mouse_aim.pitch;

	SimulationOutput sim_output;
	UpdateSimulation(config.sim, m_state, input, sim_output);

	if (sim_output.viewmodel_active)
	{
		m_viewmodel_active = true;
		m_viewmodel_pos = ToFvector(sim_output.viewmodel_pos);
		m_viewmodel_rot = ToFvector(sim_output.viewmodel_rot);
		m_lowering_pos = ToFvector(sim_output.lowering_pos);
		m_lowering_rot = ToFvector(sim_output.lowering_rot);
	}
	else
	{
		ClearViewmodelOutput();
	}
	ApplyViewmodelProfile(input.dt, input.ads_blend);
	ApplySprintBob(frame.sprint_bob);

	m_arm_pose.active = sim_output.stalker2_arm_active;
	m_arm_pose.wrist_rot = ToFvector(sim_output.stalker2_wrist_rot);
	m_arm_pose.arm_follow_rot = ToFvector(sim_output.stalker2_arm_follow_rot);
	m_arm_pose.movement_weight = sim_output.stalker2_movement_weight;
	m_arm_pose.movement_response = sim_output.stalker2_movement_response;

	const CameraComposition& composition = m_camera_rig.Compose(camera_settings, camera, m_state.signals, sim_output.sprint_sway, host);
	const VisualOutput& output = composition.lag;

	if (DiagnosticsEnabled(2))
	{
		Msg("[BODYCAM-CAMERA] frame=%u time=%u yaw=%.8f pitch=%.8f outYaw=%.8f outPitch=%.8f posX=%.9f posY=%.9f posZ=%.9f sprintPitch=%.8f follow=%u",
			Device.dwFrame, Device.dwTimeGlobal, camera.aim_yaw, camera.aim_pitch, output.yaw, output.pitch, output.pos.x, output.pos.y,
			output.pos.z, output.sprint_pitch, unsigned(follow_lag));
		Msg("[BODYCAM-VIEWMODEL] frame=%u time=%u active=%u lower=%.6f kick=%.6f", Device.dwFrame, Device.dwTimeGlobal,
			unsigned(sim_output.viewmodel_active), sim_output.lower_amount,
			m_state.fire.pos.Magnitude() + DegToRad(m_state.fire.rot.Magnitude()));
		Msg("[BODYCAM-ARM] frame=%u time=%u active=%u wristX=%.8f wristY=%.8f wristZ=%.8f walk=%.6f mouseX=%.6f mouseY=%.6f",
			Device.dwFrame, Device.dwTimeGlobal, unsigned(m_arm_pose.active), m_arm_pose.wrist_rot.x, m_arm_pose.wrist_rot.y,
			m_arm_pose.wrist_rot.z, m_arm_pose.movement_weight, m_state.signals.mouse_speed.x, m_state.signals.mouse_speed.y);
	}
	return composition;
}

void CBodycam::NotifyShot(float power, bool ads)
{
	Bodycam::NotifyShot(GetConfig().sim, m_state, power, ads);
}

void CBodycam::Dump(bool ads, u32 mstate) const
{
	const RuntimeConfig& config = GetConfig();
	const SimulationState& state = m_state;
	const CameraState& camera = m_camera_rig.Camera();
	Msg("* bodycam dump camera=%d viewmodel_active=%d ads=%d mstate=0x%08x", config.camera.enable, m_viewmodel_active, ads, mstate);
	Msg("* bodycam camera yaw=%0.3f pitch=%0.3f pos[%0.4f %0.4f %0.4f]", RadToDeg(camera.yaw), RadToDeg(camera.pitch), camera.pos.x,
		camera.pos.y, camera.pos.z);
	Msg("* bodycam signals mouse[%0.3f %0.3f] move[%0.3f %0.3f %0.3f]", RadToDeg(state.signals.mouse_speed.x),
		RadToDeg(state.signals.mouse_speed.y), state.signals.move_intent.x, state.signals.move_intent.y, state.signals.move_intent.z);
	Msg("* bodycam fire impulse enabled=%d target pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f] offset pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f]",
		config.sim.fire.enable, state.fire.impulse_pos.x, state.fire.impulse_pos.y, state.fire.impulse_pos.z, state.fire.impulse_rot.x,
		state.fire.impulse_rot.y, state.fire.impulse_rot.z, state.fire.pos.x, state.fire.pos.y, state.fire.pos.z, state.fire.rot.x,
		state.fire.rot.y, state.fire.rot.z);
	Msg("* bodycam weapon profile enabled=%d target_pos[%0.4f %0.4f %0.4f] target_rot[%0.3f %0.3f %0.3f] blend=%0.2f",
		m_viewmodel_profile.enabled, m_viewmodel_profile.target_pos.x, m_viewmodel_profile.target_pos.y, m_viewmodel_profile.target_pos.z,
		m_viewmodel_profile.target_rot.x, m_viewmodel_profile.target_rot.y, m_viewmodel_profile.target_rot.z,
		m_viewmodel_profile.blend_speed);
	Msg("* bodycam movement target=%0.3f filtered=%0.3f fraction=%0.3f inertia=%d ads_disable=%d accel=%0.3f decel=%0.3f",
		m_movement_target_speed, m_movement_filtered_speed, m_movement_speed_fraction, config.movement_inertia.enable,
		config.movement_inertia.disable_ads, config.movement_inertia.accel_time, config.movement_inertia.decel_time);
	Msg("* bodycam lowering enabled=%d target=%0.3f amount=%0.3f holster=%0.3f timers fire=%0.3f ads=%0.3f combat=%0.3f pos[%0.4f %0.4f %0.4f] rot[%0.3f %0.3f %0.3f]",
		config.sim.lowering.enable, state.lowering.target, state.lowering.amount, state.lowering.holster, state.lowering.fire_recovery,
		state.lowering.ads_recovery, state.lowering.combat_timer, state.lowering.pos.x, state.lowering.pos.y, state.lowering.pos.z,
		state.lowering.rot.x, state.lowering.rot.y, state.lowering.rot.z);
	Msg("* bodycam stalker2 arm enabled=%d active=%d wrist_deg[%0.3f %0.3f %0.3f] arm_follow_deg[%0.3f %0.3f %0.3f] walk[%0.3f response=%0.2f]",
		config.sim.stalker2_arm.enable, m_arm_pose.active, RadToDeg(m_arm_pose.wrist_rot.x), RadToDeg(m_arm_pose.wrist_rot.y),
		RadToDeg(m_arm_pose.wrist_rot.z), RadToDeg(m_arm_pose.arm_follow_rot.x), RadToDeg(m_arm_pose.arm_follow_rot.y),
		RadToDeg(m_arm_pose.arm_follow_rot.z), m_arm_pose.movement_weight, m_arm_pose.movement_response);
	DumpConfigBindings();
}

bool CBodycam::GetHudOffset(Fvector& pos, Fvector& rot, float lowering_weight) const
{
	if (!m_viewmodel_active)
		return false;

	pos.set(m_viewmodel_pos);
	rot.set(m_viewmodel_rot);
	pos.mad(m_lowering_pos, lowering_weight - 1.f);
	rot.mad(m_lowering_rot, lowering_weight - 1.f);
	return true;
}

bool CBodycam::GetArmPose(ArmPose& pose) const
{
	if (!m_arm_pose.active)
		return false;

	pose = m_arm_pose;
	return true;
}

bool CBodycam::AnyFeatureEnabled() const
{
	return AnyEffectEnabled() || ViewmodelProfileActive(m_viewmodel_profile);
}
} // namespace Bodycam
