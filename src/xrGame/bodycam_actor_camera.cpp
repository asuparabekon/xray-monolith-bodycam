// Bodycam camera for the local actor. Classic: the engine side of the camera
// rig (its frame input, the wall check, applying the composed view, the HUD
// share) and the accessors other systems read. Reference: the final camera of
// a solved frame.
#include "stdafx.h"
#include "GamePersistent.h"
#include "../xrphysics/actorcameracollision.h"
#include "Actor.h"
#include "../xrEngine/CameraBase.h"
#include "Weapon.h"
#include "WeaponKnife.h"
#include "Inventory.h"
#include "ActorEffector.h"
#include "level.h"
#include "CharacterPhysicsSupport.h"
#include "PHMovementControl.h"
#include "player_hud.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "first_person_transform.h"
#include "bodycam_reference/reference_movement.h"
#include "bodycam_diagnostics.h"

namespace
{
// The leg cycle makes no steps in these states.
constexpr u32 kNoStrideStates = mcJump | mcFall | mcLanding | mcLanding2 | mcClimb;
// The feet are off the ground in these states; a landing is on the ground.
constexpr u32 kOffGroundStates = mcJump | mcFall | mcClimb;

std::uint32_t ConvertMoveFlags(u32 mstate)
{
	std::uint32_t flags = 0;
	if (mstate & mcFwd)
		flags |= Bodycam::smfForward;
	if (mstate & mcBack)
		flags |= Bodycam::smfBack;
	if (mstate & mcLStrafe)
		flags |= Bodycam::smfLeft;
	if (mstate & mcRStrafe)
		flags |= Bodycam::smfRight;
	if (mstate & mcCrouch)
		flags |= Bodycam::smfCrouch;
	if (mstate & mcSprint)
		flags |= Bodycam::smfSprint;
	if (mstate & mcFall)
		flags |= Bodycam::smfFall;
	if (mstate & mcJump)
		flags |= Bodycam::smfJump;
	return flags;
}

CWeapon* ActiveBodycamWeapon(const CActor& actor)
{
	return actor.inventory().ActiveItem() ? actor.inventory().ActiveItem()->cast_weapon() : nullptr;
}

bool HasActiveFirearm(const CActor& actor)
{
	const CWeapon* weapon = ActiveBodycamWeapon(actor);
	return weapon && !smart_cast<const CWeaponKnife*>(weapon);
}

Bodycam::AdsState GetBodycamAdsState(const CActor& actor)
{
	const CWeapon* weapon = ActiveBodycamWeapon(actor);
	if (!weapon)
		return {};

	// Logs an aim blend that jumps in one frame (an ADS snap), with the frame time.
	static u32 last_frame = 0;
	static float last_factor = 0.f;
	const float factor = weapon->GetZRotatingFactor();
	if (Device.dwFrame != last_frame)
	{
		if (Bodycam::DiagnosticsEnabled(2) && Device.dwFrame == last_frame + 1 && _abs(factor - last_factor) > .3f)
			Msg("[BODYCAM-ADS] aim blend %.2f -> %.2f in one frame, frame time %.1f ms", last_factor, factor, Device.fTimeDelta * 1000.f);
		last_frame = Device.dwFrame;
		last_factor = factor;
	}
	return Bodycam::ResolveAdsState(weapon->IsZoomed(), factor);
}

// The actor's measured motion this frame.
struct ActorMotion
{
	Fvector velocity; // m/s, world space.
	bool on_ground = true; // As the physics shell reports it.
	float planar_speed = 0.f; // m/s
	BodycamReference::GaitSpeedAnchors anchors;
	// The planar speed on Reference's gait speeds: presentation follows the
	// game's own gait speeds, so modded walk/run/sprint speeds keep the same shape.
	double reference_speed = 0.;
};

ActorMotion ReadActorMotion(CActor& actor)
{
	ActorMotion motion;
	auto* movement = actor.character_physics_support()->movement();
	movement->GetCharacterVelocity(motion.velocity);
	motion.on_ground = movement->Environment() != CPHMovementControl::peInAir;
	motion.planar_speed = _sqrt(motion.velocity.x * motion.velocity.x + motion.velocity.z * motion.velocity.z);
	motion.anchors = actor.BodycamGaitAnchors();
	motion.reference_speed = BodycamReference::ReferenceEquivalentSpeed(motion.planar_speed, motion.anchors);
	return motion;
}

// The footstep camera's wall check: static geometry only, the actor excluded.
class ActorCameraRigHost final : public Bodycam::CameraRigHost
{
public:
	explicit ActorCameraRigHost(CActor& actor) : m_actor(actor) {}

	std::optional<float> HorizontalClearance(const Fvector& origin, const Fvector& direction, float max_distance) override
	{
		collide::rq_result trace;
		if (!Level().ObjectSpace.RayPick(origin, direction, max_distance, collide::rqtStatic, trace, &m_actor))
			return std::nullopt;
		return float(trace.range);
	}

private:
	CActor& m_actor;
};

void LogFootstepCamera(const CActor& actor, const ActorMotion& motion, const Bodycam::CameraRigInput& input,
	const BodycamReference::FootstepCameraOutput& footstep)
{
	const auto& offset = footstep.offset;
	Msg("[BODYCAM-FOOTSTEP] frame=%u time=%u footPhase=%.4f phase=%.4f locked=%u speed=%.3f sprint=%u grounded=%u "
		"x=%.4f y=%.4f z=%.4f brake=%u steps=%u raw=%.3f run=%.3f sprintMax=%.3f",
		Device.dwFrame, Device.dwTimeGlobal, input.foot_phase ? float(*input.foot_phase) : -1.f, float(footstep.phase),
		unsigned(footstep.locked), float(input.reference_speed), unsigned(input.sprinting), unsigned(input.grounded), float(offset[0]),
		float(offset[1]), float(offset[2]), footstep.brakeContacts, actor.BodycamStepSerial(), motion.planar_speed,
		float(motion.anchors.engineRun), float(motion.anchors.engineSprint));
}
} // namespace

BodycamReference::SpeedCoefficients CActor::BodycamSpeedCoefficients() const
{
	return { m_fWalkAccel, m_fRunFactor, m_fRunBackFactor, m_fWalkBackFactor, m_fCrouchFactor, m_fSprintFactor, m_fWalk_StrafeFactor,
		m_fRun_StrafeFactor, m_fSprint_StrafeFactor };
}

BodycamReference::GaitSpeedAnchors CActor::BodycamGaitAnchors() const
{
	const u32 state = mstate_real;
	const bool crouched = !!(state & mcCrouch), aiming = IsZoomAimingMode();
	const BodycamReference::SpeedState flags{ true, aiming, !!(state & mcBack), crouched, false, !!(state & (mcLStrafe | mcRStrafe)) };
	// The current coefficients include GAMMA script buffs, weight and mods.
	return BodycamReference::GaitAnchors(BodycamSpeedCoefficients(), flags, BodycamReference::ReferenceMoveSpeed(crouched, aiming, false),
		BodycamReference::ReferenceMoveSpeed(crouched, aiming, true));
}

void CActor::cam_BodycamVisualReset(const CCameraBase* camera)
{
	m_bodycam.Reset(camera, GetBodycamAdsState(*this).blend);
}

bool CActor::cam_BodycamFootstepOffset(Fvector& offset, Fmatrix& roll) const
{
	return Bodycam::ClassicActive() && m_bodycam.CameraRig().HudShare(Device.dwFrame, offset, roll);
}

bool CActor::cam_BodycamFootstepComposed() const
{
	Fvector offset;
	Fmatrix roll;
	return Bodycam::FootstepCameraEnabled() && cam_BodycamFootstepOffset(offset, roll);
}

bool CActor::cam_BodycamVisualUpdate(const CCameraBase* camera, float dt, float viewport_near, bool apply_camera)
{
	m_bodycam_regrip_frame = u32(-1);
	if (!Bodycam::ClassicActive())
		return false;

	if (!m_bodycam.AnyFeatureEnabled() || !g_Alive() || m_holder || cam_freelook != eflDisabled)
	{
		cam_BodycamVisualReset(camera);
		return false;
	}

	const u32 state = MovingState();
	Bodycam::ActorFrame frame;
	Bodycam::CameraRigInput& view = frame.camera;
	view.frame = Device.dwFrame;
	view.apply = apply_camera;
	camera->vDirection.getHP(view.aim_yaw, view.aim_pitch);
	view.dt = dt;
	view.paused = !!Device.Paused();
	view.ads = GetBodycamAdsState(*this);
	view.strafe = ((state & mcRStrafe) ? 1. : 0.) - ((state & mcLStrafe) ? 1. : 0.);
	view.leaning = !!(state & (mcLLookout | mcRLookout));
	view.sprinting = !!(state & mcSprint);
	// Only the head motion and the Footstep Camera read the actor's motion,
	// and only on a frame that moves the view.
	const Bodycam::RuntimeConfig& config = Bodycam::GetConfig();
	const bool footstep_view = apply_camera && config.footstep.enable;
	ActorMotion motion;
	const bool motion_read = footstep_view || (apply_camera && config.camera.enable);
	if (motion_read)
	{
		motion = ReadActorMotion(*this);
		view.velocity = motion.velocity;
		view.grounded = motion.on_ground && !(state & kOffGroundStates);
		view.reference_speed = motion.reference_speed;
	}
	float phase;
	// Braking keeps the native leg phase (Bodycam::UpdateBrakingStride).
	if (footstep_view && ((state & mcAnyMove) || BodycamBraking()) && !(state & kNoStrideStates) && foot_phase(phase))
		view.foot_phase = phase;
	view.effector.set(Cameras().BaseRight(), Cameras().BaseUp(), Cameras().BaseDirection(), Cameras().BasePosition());

	frame.move_flags = ConvertMoveFlags(state);
	frame.accelerated = isActorAccelerated(state, view.ads.active);
	frame.weapon_lowered = is_safemode();
	frame.combat = m_sndShockEffector && m_sndShockEffector->InWork();
	frame.firearm_equipped = HasActiveFirearm(*this);
	// The Sprint Transition's gun step bob, once the start has decided that the
	// weapon plays no run-to-sprint clip of its own.
	if (Bodycam::SprintBobEnabled())
	{
		BodycamReference::SprintBobInput& bob = frame.sprint_bob;
		bob.starting =
			BodycamOwnsSprintTransition() && m_bodycam_sprint.starting && m_bodycam_sprint.entry_decided && !m_bodycam_sprint.entry_clip;
		if (bob.starting || m_bodycam.SprintBobActive())
		{
			bob.frame = Device.dwFrame;
			bob.dt = dt;
			bob.paused = view.paused;
			if ((state & mcAnyMove) && !(state & kNoStrideStates) && foot_phase(phase))
				bob.footPhase = phase;
			bob.strideSeconds = stride_time();
			bob.speed = motion_read ? motion.reference_speed : ReadActorMotion(*this).reference_speed;
		}
	}

	ActorCameraRigHost host(*this);
	const Bodycam::CameraComposition& composition = m_bodycam.Update(frame, host);
	// Regrip needs the current HUD solve (including its shared reach limit).
	if (apply_camera && config.regrip.enable)
	{
		m_bodycam_regrip_frame = Device.dwFrame;
		m_bodycam_regrip_near = viewport_near;
	}
	if (footstep_view && Bodycam::DiagnosticsEnabled(2))
		LogFootstepCamera(*this, motion, view, m_bodycam.CameraRig().Footstep());
	if (!composition.composes)
		return false;

	const Fmatrix& final_basis = composition.view;
	const float fov = std::clamp(Cameras().BaseFov(), 1.f, 175.f);
	Cameras().ApplyDeviceOverride(final_basis.c, final_basis.k, final_basis.j, fov, Cameras().BaseAspect(),
		g_pGamePersistent->Environment().CurrentEnv->far_plane, viewport_near);
	return true;
}

// The view was applied in cam_Finalize; only a regrip camera from this frame's
// HUD solve applies it again, turned. The camera manager still holds that
// apply's FOV and aspect.
void CActor::cam_BodycamApplyRegrip()
{
	if (m_bodycam_regrip_frame != Device.dwFrame)
		return;
	Fmatrix delta;
	if (!g_player_hud->ClassicRegripCamera(delta))
		return;
	Fmatrix native;
	native.set(Cameras().Right(), Cameras().Up(), Cameras().Direction(), Cameras().Position());
	const Fmatrix view = m_bodycam.CameraRig().RegripView(native, delta);
	Cameras().ApplyDeviceOverride(view.c, view.k, view.j, Cameras().Fov(), Cameras().Aspect(),
		g_pGamePersistent->Environment().CurrentEnv->far_plane, m_bodycam_regrip_near);
	FirstPerson::PublishCamera();
	if (auto* weapon = ActiveBodycamWeapon(*this))
		weapon->UpdateSvpWeaponPose();
}

void CActor::cam_BodycamNotifyShot(bool classic_recoil)
{
	if (this != Level().CurrentEntity())
		return;
	const float power = classic_recoil ? Bodycam::GetConfig().recoil.fire_impulse_share : 1.f;
	m_bodycam.NotifyShot(power, GetBodycamAdsState(*this).active);
	if (Bodycam::DiagnosticsEnabled(2))
		Msg("[BODYCAM-SHOT] frame=%u time=%u power=%.3f", Device.dwFrame, Device.dwTimeGlobal, power);
}

void CActor::cam_BodycamSetViewmodelProfile(const Fvector& pos, const Fvector& rot, float blend_speed)
{
	m_bodycam.SetViewmodelProfile(pos, rot, blend_speed);
}

void CActor::cam_BodycamClearViewmodelProfile(float blend_speed)
{
	m_bodycam.ClearViewmodelProfile(blend_speed);
}

void CActor::cam_BodycamDumpState()
{
	m_bodycam.Dump(GetBodycamAdsState(*this).active, MovingState());
	if (g_player_hud)
		g_player_hud->DumpAuthoredMotionProfile();
}

bool CActor::cam_BodycamGetHudOffset(Fvector& pos, Fvector& rot, float lowering_weight) const
{
	return Bodycam::ClassicActive() && m_bodycam.GetHudOffset(pos, rot, lowering_weight);
}

Bodycam::ArmPose CActor::BodycamArmPose() const
{
	Bodycam::ArmPose pose;
	if (!Bodycam::ClassicActive() || !m_bodycam.GetArmPose(pose))
		return {};
	return pose;
}

bool CActor::BodycamSprintAnimReady() const
{
	return !BodycamOwnsSprintTransition() || !(mstate_real & mcSprint) || m_bodycam_sprint.anim_ready;
}

bool CActor::BodycamOwnsSprintTransition() const
{
	return Bodycam::ClassicActive() && Bodycam::SprintTransitionEnabled() && HasActiveFirearm(*this);
}

void CActor::cam_FinalizeReference(const PreparedCamera& frame, Fmatrix& cameraWorld)
{
	VERIFY(frame.camera);
	VERIFY(_valid(cameraWorld));
	VERIFY(this == Level().CurrentEntity() && cam_active == eacFirstEye);
	VERIFY(!Level().Cameras().GetCamEffector(cefDemo));

	CCameraBase* C = frame.camera;
	const Fvector preparedEye = C->vPosition;
	const bool releaseContinuous = m_reference_camera_release_frame != u32(-1) && m_reference_camera_release_frame + 1 == Device.dwFrame;
	if (!releaseContinuous)
	{
		m_reference_camera_base_release.set(0, 0, 0);
		m_reference_camera_effect_release.set(0, 0, 0);
	}
	m_reference_camera_release_frame = Device.dwFrame;
	C->vPosition = cameraWorld.c;
	C->vDirection = cameraWorld.k;
	C->vNormal = cameraWorld.j;
	// Preserve local corner continuity while carrying history with locomotion
	// and stance. The native sweep bounds it to this frame's camera rig.
	Fvector baseSweepStart = preparedEye;
	if (releaseContinuous)
	{
		baseSweepStart.sub(preparedEye, m_reference_camera_base_carrier);
		baseSweepStart.add(m_reference_camera_base_safe_eye);
	}
	m_reference_camera_base_carrier = preparedEye;
	collide_camera_swept(*C, frame.viewport_near, this, baseSweepStart, preparedEye, &m_reference_camera_base_release, frame.delta,
		Bodycam::DiagnosticsEnabled(2));
	m_reference_camera_base_safe_eye = C->vPosition;
	// The caller must anchor all Reference HUD roots to this same corrected pose.
	cameraWorld.c = C->vPosition;

	const float far_plane = g_pGamePersistent->Environment().CurrentEnv->far_plane;

	Cameras().UpdateReference(cameraWorld, C->f_fov, C->f_aspect, far_plane);
	cam_UpdateAngularBookkeeping();
	Level().Cameras().UpdateReference(cameraWorld, C->f_fov, C->f_aspect, far_plane);
	// Effects can move the rendered eye after solved-base collision. Validate
	// the final viewport and share its translation with the cached HUD eye.
	const auto finalPosition = Cameras().Position();
	C->vPosition = finalPosition;
	C->vDirection = Cameras().Direction();
	C->vNormal = Cameras().Up();
	C->f_fov = Cameras().Fov();
	Fvector effectSweepStart = cameraWorld.c;
	if (releaseContinuous)
	{
		effectSweepStart.sub(cameraWorld.c, m_reference_camera_effect_carrier);
		effectSweepStart.add(m_reference_camera_effect_safe_eye);
	}
	m_reference_camera_effect_carrier = cameraWorld.c;
	collide_camera_swept(*C, frame.viewport_near, this, effectSweepStart, cameraWorld.c, &m_reference_camera_effect_release, frame.delta,
		Bodycam::DiagnosticsEnabled(2));
	m_reference_camera_effect_safe_eye = C->vPosition;
	Fvector finalCollisionDelta;
	finalCollisionDelta.sub(C->vPosition, finalPosition);
	Cameras().ApplyReferenceCollisionTranslation(finalCollisionDelta);

	// HUD-affecting native action/landing deltas move the paired arms and weapon
	// together. Camera-only effects retain their normal native distinction.
	Cameras().hud_camera_Matrix(cameraWorld);

	// Reference owns the solved pose, including any scoped surround freeze.
	// ApplyDevice also publishes native PP; do not apply PP a second time.
	Cameras().ApplyDevice(frame.viewport_near);
	FirstPerson::PublishCamera();
}
