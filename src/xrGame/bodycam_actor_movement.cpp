// Bodycam movement of the local actor. Classic presentation: the decisions
// live in bodycam_sprint_start.h (no engine types); this file gathers their
// inputs from the actor and carries out their effects. Reference movement:
// the gait and speed rules that g_cl_CheckControls applies in Reference mode.
#include "stdafx.h"
#include "Actor.h"
#include "Inventory.h"
#include "Weapon.h"
#include "HudItem.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "player_hud.h"
#include "xr_level_controller.h"
#include "bodycam_movement_response.h"
#include "bodycam_settings.h"
#include "bodycam_sprint_start.h"
#include "bodycam_reference/speed_curve.h"
#include "bodycam_reference/reference_movement.h"
#include "bodycam_diagnostics.h"

namespace
{
// The engine side of the sprint start for one frame.
class ActorSprintStartHost final : public Bodycam::SprintStartHost
{
public:
	ActorSprintStartHost(CActor& actor, CHudItem* item, float actual_speed) : m_actor(actor), m_item(item), m_actual_speed(actual_speed) {}

	double ActualSprintProgress() override { return BodycamReference::SprintProgress(m_actual_speed, m_actor.BodycamGaitAnchors()); }
	void NotifyScriptsOfSprint() override
	{
		if (g_player_hud)
			g_player_hud->BodycamNotifyScriptMovement(mcSprint);
	}
	bool PlayEntryClip() override
	{
		if (!m_item)
			return false;
		m_item->BodycamPlaySprintEntryClip();
		return m_item->m_bodycam_entry_clip;
	}
	void DriveEntryClip(float progress, Bodycam::SprintEntryDrive drive) override
	{
		if (m_item)
			m_item->BodycamDriveSprintEntry(progress, drive);
	}

private:
	CActor& m_actor;
	CHudItem* m_item;
	float m_actual_speed;
};

Bodycam::SprintLoweringInput ReadSprintLoweringInput(CActor& actor, bool owned, u32 move_state)
{
	CWeapon* weapon = smart_cast<CWeapon*>(actor.inventory().ActiveItem());
	Bodycam::SprintLoweringInput input;
	input.sprinting = !!(move_state & mcSprint);
	input.owned = owned;
	input.lowered = actor.is_safemode();
	input.can_lower = weapon && weapon->m_bCanBeLowered;
	input.pending = weapon && weapon->IsPending();
	input.weapon = weapon ? weapon->ID() : Bodycam::kNoWeapon;
	return input;
}
}

void CActor::BodycamUpdateMovement(Fvector& control_accel, float dt)
{
	m_bodycam_sprint.hud_changed = false;
	const bool moving = !!(mstate_real & mcAnyMove), sprinting = !!(mstate_real & mcSprint), aiming = IsZoomAimingMode();
	Bodycam::MovementInertiaInput response_input;
	response_input.dt = dt;
	response_input.move_flags = mstate_real;
	response_input.ads = aiming;
	response_input.target_accel.set(control_accel);

	const Bodycam::MovementInertiaOutput response =
		Bodycam::UpdateMovementInertia(Bodycam::GetConfig().movement_inertia, m_bodycam_movement_inertia, response_input);
	control_accel.set(response.accel);
	m_bodycam.SetMovementSpeed(response.target_speed, response.filtered_speed, response.speed_fraction);

	Fvector velocity;
	character_physics_support()->movement()->GetCharacterVelocity(velocity);
	velocity.y = 0.f;
	const float actual_speed = velocity.magnitude();

	if (Bodycam::GetConfig().movement_inertia.enable)
	{
		Bodycam::BrakingInput braking;
		braking.moving = moving;
		braking.was_moving = !!(mstate_old & mcAnyMove);
		braking.airborne_or_climbing = !!(mstate_real & (mcJump | mcFall | mcLanding | mcLanding2 | mcClimb));
		braking.grounded = is_on_ground();
		braking.speed = actual_speed;
		braking.forward = velocity.dotproduct(XFORM().k);
		braking.right = velocity.dotproduct(XFORM().i);
		braking.run_speed = float(BodycamGaitAnchors().engineRun);
		Bodycam::UpdateBrakingStride(m_bodycam_brake, braking);
	}
	else
		m_bodycam_brake.active = false;
	if (Bodycam::DiagnosticsEnabled(2) && m_bodycam_brake.active)
		Msg("[BODYCAM-BRAKE] frame=%u time=%u speed=%.3f run=%u dir=%u steps=%u", Device.dwFrame, Device.dwTimeGlobal, actual_speed,
			u32(m_bodycam_brake.run), u32(m_bodycam_brake.dir), BodycamStepSerial());

	const bool owned = BodycamOwnsSprintTransition();
	CHudItem* item = smart_cast<CHudItem*>(inventory().ActiveItem());
	Bodycam::SprintStartInput start;
	start.sprinting = sprinting;
	start.owned = owned;
	start.aiming = aiming;
	start.speed_fraction = response.speed_fraction;
	start.dt = dt;
	start.weapon = inventory().ActiveItem() ? inventory().ActiveItem()->object().ID() : Bodycam::kNoWeapon;
	ActorSprintStartHost host(*this, item, actual_speed);
	Bodycam::UpdateSprintStart(m_bodycam_sprint, start, host);

	const Bodycam::SprintHudState sprint_hud =
		Bodycam::ResolveSprintHudState(m_bodycam_sprint.hud_active, sprinting, owned, aiming, moving, m_bodycam_sprint.full);
	m_bodycam_sprint.anim_ready = sprint_hud.ready;
	m_bodycam_sprint.hud_active = sprint_hud.active;

	Bodycam::RunPaceInput pace;
	pace.stride = CStepManager::stride_time();
	pace.running = moving && !sprinting && !(mstate_real & mcCrouch) && isActorAccelerated(mstate_real, aiming);
	pace.sprinting = sprinting;
	pace.owned = owned;
	const float pace_factor = Bodycam::UpdateRunLoopPace(m_bodycam_sprint, pace);
	if (g_player_hud)
		g_player_hud->BodycamPaceMovementLoop(pace_factor);

	// A held lowering commits before the HUD is told about the sprint, so the
	// authored sprint clip is never shown for a frame before GAMMA lowers. The
	// lowering itself restarts the HUD clip selection.
	const bool lowered = BodycamCommitSprintLowering(owned);
	if (Bodycam::DiagnosticsEnabled(2) && (sprinting || lowered))
		Msg("[BODYCAM-SPRINT-START] frame=%u time=%u full=%u ready=%u queued=%u lowered=%u safemode=%u fraction=%.3f run=%.3f pace=%.3f progress=%.3f clip=%u",
			Device.dwFrame, Device.dwTimeGlobal, u32(m_bodycam_sprint.full), u32(sprint_hud.ready),
			u32(m_bodycam_sprint.lower_queued != Bodycam::kNoWeapon), u32(lowered), u32(is_safemode()), response.speed_fraction,
			m_bodycam_sprint.run_stride, g_player_hud ? g_player_hud->m_bodycam_loop_pace : 1.f, m_bodycam_sprint.progress,
			u32(item && item->m_bodycam_entry_clip));
	if (sprint_hud.notify && g_player_hud && !lowered)
	{
		m_bodycam_sprint.hud_changed = true;
		g_player_hud->OnMovementChanged(mcSprint);
	}
}

void CActor::BodycamResetMovement()
{
	m_bodycam_movement_inertia = {};
	m_bodycam.SetMovementSpeed(0.f, 0.f, 0.f);
	Bodycam::ResetSprintStart(m_bodycam_sprint);
	m_bodycam_brake.active = false;
}

bool CActor::BodycamDeferSprintLowering()
{
	// Manual lowering and already-lowered weapons are untouched.
	return Bodycam::DeferSprintLowering(m_bodycam_sprint, ReadSprintLoweringInput(*this, BodycamOwnsSprintTransition(), mstate_real));
}

bool CActor::BodycamCommitSprintLowering(bool owned)
{
	if (!Bodycam::CommitSprintLowering(m_bodycam_sprint, ReadSprintLoweringInput(*this, owned, mstate_real)))
		return false;
	// The same transaction as actor_set_safemode(true).
	smart_cast<CWeapon*>(inventory().ActiveItem())->Action(kSAFEMODE, CMD_START);
	set_safemode(true);
	return true;
}

bool CActor::BodycamSprintLowerProgress(const CWeapon* weapon, float& progress) const
{
	if (m_bodycam_sprint.lower_queued == Bodycam::kNoWeapon || weapon->ID() != m_bodycam_sprint.lower_queued)
		return false;
	progress = m_bodycam_sprint.progress;
	return true;
}

bool CActor::BodycamReferenceMovement() const
{
	return Bodycam::ReferenceMovementActive() && Local() && g_Alive() && IsFocused();
}

// Reference has one walk gait and a sprint: every move is accelerated unless
// sprinting or crouched. Stamina and the CanMove/CanSprint gates stay the engine's.
void CActor::BodycamReferenceGait(u32& wished_state)
{
	if (wished_state & (mcSprint | mcCrouch))
		wished_state &= ~mcAccel;
	else
		wished_state |= mcAccel;
}

// Reference sprints in every direction but backwards.
bool CActor::BodycamReferenceSprintDirection() const
{
	return (mstate_real & mcAnyMove) && !(mstate_real & mcBack);
}

// A refused sprint falls back to the walk gait, not to the engine's slower walk.
void CActor::BodycamReferenceFallbackGait()
{
	if (!(mstate_real & (mcSprint | mcCrouch | mcClimb)))
		mstate_real |= mcAccel;
}

// The acceleration scale that moves the actor at the Reference speed of its
// gait, changed by as much as scripts and load have changed the actor's speed
// coefficients from their configured values. Climbing keeps the engine's scale.
float CActor::BodycamReferenceMoveScale(float scale, const Fvector& control_accel, bool accelerated) const
{
	if (mstate_real & mcClimb)
		return scale;
	BodycamReference::SpeedState state;
	state.accelerated = accelerated;
	state.aiming = IsZoomAimingMode();
	state.back = !!(mstate_real & mcBack);
	state.crouched = !!(mstate_real & mcCrouch);
	state.sprinting = !!(mstate_real & mcSprint);
	state.strafe = !!(mstate_real & (mcLStrafe | mcRStrafe));
	const float magnitude = control_accel.magnitude();
	const float loaded = BodycamReference::SpeedBudget(m_reference_loaded_speed, state);
	const float speed = float(BodycamReference::ReferenceMoveSpeed(state.crouched, state.aiming, state.sprinting));
	return BodycamReference::ReferenceAcceleration(speed, scale * magnitude, loaded) / magnitude;
}
