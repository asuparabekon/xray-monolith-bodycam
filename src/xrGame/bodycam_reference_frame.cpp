#include "bodycam_reference_host.h"
#include "bodycam_reference/ads_pose.h"
#include "bodycam_reference/camera_lean.h"
#include "bodycam_reference/constants.h"
#include "pch_script.h"
#include "bodycam_reference_frame.h"
#include "bodycam_settings.h"
#include "bodycam_math.h"
#include "Actor.h"
#include "ActorEffector.h"
#include "Weapon.h"
#include "WeaponKnife.h"
#include "WeaponBinoculars.h"
#include "player_hud.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "characterphysicssupport.h"
#include "Level.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/bodycam_reference_projection.h"
#include "../xrPhysics/PHWorld.h"
#include "../xrPhysics/PhysicsCommon.h"
#include "../xrPhysics/ActorCameraCollision.h"
#include "bodycam_diagnostics.h"
#include <memory>
#include <algorithm>
#include <cmath>
#include <stdexcept>

extern float psHUD_FOV_def;
extern int g_nearwall;
extern float g_fov;

namespace BodycamReference
{
namespace
{
// GetHudFov is in the engine's HUD FOV unit; this many degrees make one.
// The camera trace runs this many metres along the aim direction and this many up.
constexpr float kCameraTraceForward = 15.f, kCameraTraceUp = .5f;

Fmatrix BlendTransforms(const Fmatrix& from, const Fmatrix& to, double weight)
{
	if (weight <= 0)
		return from;
	if (weight >= 1)
		return to;
	return Bodycam::ToEngine(BlendTransform(Bodycam::ToPortable(from), Bodycam::ToPortable(to), weight));
}
Fmatrix CameraMatrix(const CCameraBase& camera)
{
	Fmatrix result;
	result.identity();
	result.k = camera.vDirection;
	result.j = camera.vNormal;
	result.i.crossproduct(result.j, result.k);
	result.c = camera.vPosition;
	return result;
}
// The direction of the engine's first-person camera as a Reference control rotation.
Rotation EyeControl(const CCameraBase& eye, const EngineBasis& basis)
{
	const auto camera = Decompose(basis.CameraWorldToReference(Bodycam::ToRows(CameraMatrix(eye))));
	return CameraPovFromWorld(camera.position, camera.rotation, g_fov).rotation;
}
// Reference X/Z metres as the library's planar centimetres.
PlanarVector PlanarCentimetres(const Vector& metres)
{
	return { metres[2] * kCentimetresPerMetre, -metres[0] * kCentimetresPerMetre };
}
// One linear congruential stream behind both generators the library asks for.
CharacterRandomSource RandomSource(unsigned seed)
{
	auto state = std::make_shared<std::uint32_t>(seed);
	const auto next = [state] { return *state = *state * 1664525u + 1013904223u; };
	return { [next] { return next() >> 17; }, [next] { return double(next()) / 4294967296.; } };
}
// Throws when the character profile or one of its assets is missing or invalid.
std::unique_ptr<EngineCharacterHost> CreateHost(const CCameraBase& eye)
{
	const EngineProfileReader profile(ReadEngineReferenceSetting, {});
	const auto basis = profile.Basis(kCharacterSection);
	// The adapter exchanges world coordinates with the engine assuming a
	// shared up axis and opposite handedness.
	const auto up = basis.DirectionToEngine({ 0, 1, 0 });
	const auto x = basis.DirectionToEngine({ 1, 0, 0 }), z = basis.DirectionToEngine({ 0, 0, 1 });
	if (up != Vector{ 0, 1, 0 } || x[0] * z[2] - x[2] * z[0] > 0)
		throw std::invalid_argument("Reference character basis must be Y-up and opposite-handed to the engine");
	auto session =
		LoadEngineCharacterSession(profile.Paths(kCharacterSection), RandomSource(Device.dwTimeGlobal), EyeControl(eye, basis), g_fov);
	return std::make_unique<EngineCharacterHost>(std::move(session), basis);
}
ReloadPhase WeaponReloadPhase(CWeapon& weapon)
{
	if (!weapon.IsTriStateReload() || weapon.GetState() != CWeapon::eReload)
		return ReloadPhase::None;
	switch (weapon.GetReloadState())
	{
	case CWeapon::eSubstateReloadBegin: return ReloadPhase::Open;
	case CWeapon::eSubstateReloadInProcess: return ReloadPhase::Insert;
	default: return ReloadPhase::Close;
	}
}
struct HudFov
{
	float actual = psHUD_FOV_def, withoutNearwall = psHUD_FOV_def;
};
// As cam_Prepare selects it: the smaller HUD FOV of the active item and the
// detector, each minimum taken on its own.
HudFov AttachedHudFov(CActor& actor)
{
	HudFov fov;
	bool found = false;
	for (auto* item :
		{ smart_cast<CHudItem*>(actor.inventory().ActiveItem()), smart_cast<CHudItem*>(actor.inventory().ItemFromSlot(DETECTOR_SLOT)) })
	{
		if (!item || !item->IsAttachedToHUD())
			continue;
		fov.actual = found ? fminf(fov.actual, item->GetHudFov()) : item->GetHudFov();
		fov.withoutNearwall = found ? fminf(fov.withoutNearwall, item->GetHudFovWithoutNearWall()) : item->GetHudFovWithoutNearWall();
		found = true;
	}
	return fov;
}
u16 HandBone(IKinematics& model, LPCSTR name, LPCSTR fallback)
{
	const u16 bone = model.LL_BoneID(name);
	return bone != BI_NONE ? bone : model.LL_BoneID(fallback);
}
}

struct EngineFrameAdapter::Frame
{
	CActor& actor;
	CWeapon* weapon;
	player_hud& hud;
	// Both arm models exist.
	bool hudReady;
	// How far each stage got, for Update and FinishWithoutReference.
	bool hostReady = false, cameraPrepared = false, handsPrepared = false, cameraFinalized = false, handsFinalized = false;
	CActor::PreparedCamera camera;
	Fmatrix preparedCamera;
	// The Reference aim direction the engine's camera was pointed along.
	Fvector aimDirection;
	player_hud::HudPoseFrame hands;
	double motionInfluence = 1;
	HudFov hudFov;
};

bool EngineFrameAdapter::Update(CActor& actor, CWeapon* weapon)
{
	// The HUD projection of the last update, which a repeated update of the same frame keeps.
	const bool hadWeaponProjection = Device.reference_hud_projection_frame != u32(-1);
	Device.reference_hud_projection_frame = u32(-1);
	auto& hud = *g_player_hud;
	const auto& selection = Bodycam::GetMotionSelection();
	if (!actor.IsFocused())
		return false;
	if (Bodycam::ClassicActive() && selection.Requested() == MotionMode::Classic)
		return false;
	if (Device.Paused())
	{
		if (Bodycam::ReferenceActive() && hadWeaponProjection)
			Device.reference_hud_projection_frame = Device.dwFrame;
		return Bodycam::ReferenceActive();
	}
	const bool cameraEligible = actor.g_Alive() && !actor.Holder() && !actor.m_FPCam && actor.active_cam() == eacFirstEye &&
		actor.cam_freelook == eflDisabled && !Level().Cameras().GetCamEffector(cefDemo) && !ph_world->Processing();
	if (!cameraEligible)
	{
		Leave(actor, hud);
		return false;
	}

	if (!hud.m_reference_host && hud.m_reference_failed_request == selection.Requests())
		return false;

	Frame frame{ actor, weapon, hud, hud.m_model && hud.m_model_2 };
	try
	{
		return Run(frame, hadWeaponProjection);
	}
	catch (const std::exception& failure)
	{
		Msg("! Reference frame disabled: %s", failure.what());
	}
	Leave(actor, hud);
	// Classic until the next mode request; the weapon is not bound again until
	// it is equipped again.
	if (frame.hostReady)
		hud.m_reference_profile_attempted = true;
	hud.m_reference_failed_request = selection.Requests();
	return FinishWithoutReference(frame);
}

// False when the frame is not a Reference frame. Throws when it fails; the
// stages it completed are recorded in the frame.
bool EngineFrameAdapter::Run(Frame& frame, bool hadWeaponProjection)
{
	auto& actor = frame.actor;
	auto* weapon = frame.weapon;
	auto& hud = frame.hud;
	if (hud.m_reference_host && hud.m_reference_actor != &actor)
		hud.ResetReferenceHost();
	if (!hud.m_reference_host)
	{
		hud.m_reference_host = CreateHost(*actor.cam_FirstEye()).release();
		hud.m_reference_actor = &actor;
	}
	frame.hostReady = true;
	auto* host = hud.m_reference_host;

	// A request to leave Reference waits for the weapon action in progress.
	auto& selection = Bodycam::GetMotionSelection();
	const bool retiring = Bodycam::ReferenceActive() && selection.Requested() == MotionMode::Classic;
	const bool actionBusy = retiring &&
		((weapon &&
			 (weapon->IsPending() || weapon->GetState() == CWeapon::eFire || weapon->GetState() == CWeapon::eReload ||
				 weapon->GetState() == CWeapon::eBore)) ||
			hud.ReferenceActionBlending());
	const auto transition = selection.CompleteBoundary(actionBusy, true);
	if (!Bodycam::ReferenceActive())
	{
		if (transition == ModeTransition::Changed)
			Leave(actor, hud);
		return false;
	}
	if (transition == ModeTransition::Changed)
		host->SetAimControl(EyeControl(*actor.cam_FirstEye(), host->Basis()));
	if (host->CompletedFrame(Device.dwFrame))
	{
		if (hadWeaponProjection && host->HasWeaponBinding())
			Device.reference_hud_projection_frame = Device.dwFrame;
		return true;
	}

	const bool weaponPose = BindWeapon(frame, *host);
	host->SetReloadPhase(weaponPose ? WeaponReloadPhase(*weapon) : ReloadPhase::None);
	if (weaponPose)
		AdmitAction(frame, *host);

	const auto movement = ObserveMovement(actor, weapon, *host);
	const bool aiming = weapon && weapon->IsZoomed(), primarySight = !weapon || weapon->GetZoomType() == 0;
	ApplyScriptCameraDeltas(actor, *host);
	host->BeginFrame(Device.dwFrame, Device.ApplicationDelta(), aiming, primarySight, movement);

	PrepareCameraAndHud(frame, *host, weaponPose, aiming);
	EngineCharacterView view;
	ObserveFrame(frame, *host, weaponPose, aiming && primarySight, view);
	host->Solve(view, [host](const AnimationCurves& curves) { return host->ActionCurves(curves); });
	SyncLegs(actor, *host);
	const auto& preparedCamera = frame.preparedCamera;
	const auto& pose = *host->Pose();
	// The engine's lean, already limited by collision, moves and rolls the
	// camera; the arms and the optic follow because they hang on it.
	Fmatrix world = Bodycam::FromRows(
		ApplyLean(pose.cameraWorld, actor.r_torso.roll, preparedCamera.c.x - actor.Position().x, preparedCamera.c.z - actor.Position().z));
	frame.cameraFinalized = true;
	actor.cam_FinalizeReference(frame.camera, world);
	PublishHud(frame, *host, world, weaponPose, float(view.horizontalFov));
	if (Bodycam::DiagnosticsEnabled(2))
		Msg("[REFERENCE-HUD] frame=%u weapon_pose=%u influence=%.6f clip=%.6f item=%s", Device.dwFrame, unsigned(weaponPose),
			frame.motionInfluence, Device.reference_hud_clip_scale, weapon ? weapon->cNameSect().c_str() : "none");
	return true;
}

// Fills the view the solve reads from the engine's state after the camera
// and the HUD were prepared, and gives the host the action clips playing.
void EngineFrameAdapter::ObserveFrame(
	Frame& frame, EngineCharacterHost& host, bool weaponPose, bool primarySightAimed, EngineCharacterView& view)
{
	auto& actor = frame.actor;
	auto* weapon = frame.weapon;
	const auto& preparedCamera = frame.preparedCamera;
	if (weaponPose)
	{
		ObserveHands(frame, view);
		std::vector<ActionSample> samples;
		for (const auto& playback : frame.hud.ReferenceActionPlaybacks())
			samples.push_back({ HandMotionKey(playback.model, playback.motion.val), playback.seconds, playback.duration, playback.weight });
		host.SetActionSamples(std::move(samples));
		const auto section = weapon->cNameSect().c_str();
		if (Bodycam::HasReferenceWeaponPositionBaseline(section))
		{
			const auto& position = Bodycam::GetReferenceWeaponPosition(section);
			view.weaponPosition = EngineCharacterView::WeaponPosition{ { position.hip.x, position.hip.y, position.hip.z },
				{ position.ads.x, position.ads.y, position.ads.z } };
		}
	}
	else
		host.SetActionSamples({});
	view.motionInfluence = frame.motionInfluence;
	view.crouched = !!(actor.mstate_real & mcCrouch);
	view.lowCrouched = view.crouched && !!(actor.mstate_real & mcAccel);
	view.lookDownRadians = std::asin(std::clamp(-double(preparedCamera.k.y), -1., 1.));
	view.aimBlend = weapon ? weapon->GetZRotatingFactor() : 0;
	const auto position = host.Basis().DirectionToReference({ actor.Position().x, actor.Position().y, actor.Position().z });
	view.actor = { position[0], position[2], position[1], 0 };
	// The engine's smoothed stance and stair eye height, relative to the
	// standing eye. The actor's own height carries a jump.
	auto* physics = actor.character_physics_support()->movement();
	Fvector standingSize, currentSize;
	physics->Boxes()[0].getsize(standingSize);
	physics->Box().getsize(currentSize);
	const double standingEyeHeight = actor.CameraHeight() + actor.m_fCamHeightFactor * (standingSize.y - currentSize.y);
	view.actor.floorClearance = preparedCamera.c.y - actor.Position().y - standingEyeHeight;
	view.horizontalFov = frame.camera.camera->f_fov;
	view.scopeCapture = weaponPose && primarySightAimed && Device.m_SecondViewport.IsSVPActive();
	Fvector traceDirection = frame.aimDirection;
	traceDirection.mul(kCameraTraceForward);
	traceDirection.y += kCameraTraceUp;
	const float traceRange = traceDirection.magnitude();
	traceDirection.div(traceRange);
	collide::rq_result trace;
	view.cameraHit = Level().ObjectSpace.RayPick(Device.vCameraPosition, traceDirection, traceRange, collide::rqtBoth, trace, &actor);
	view.cameraHitDistanceCm = view.cameraHit ? double(trace.range) * kCentimetresPerMetre : 0.;
}

void EngineFrameAdapter::Leave(CActor& actor, player_hud& hud)
{
	Device.reference_hud_projection_frame = u32(-1);
	const bool wasReference = Bodycam::ReferenceActive();
	hud.ResetReferenceHost();
	Bodycam::GetMotionSelection().DetachActor();
	if (wasReference)
		actor.cam_BodycamVisualReset(actor.cam_FirstEye());
}

// Completes the camera and the HUD the way the engine does, from wherever the
// failed frame stopped, without advancing the animations a second time. False
// when the frame had not prepared the camera and the caller has to update it.
bool EngineFrameAdapter::FinishWithoutReference(Frame& frame)
{
	auto& actor = frame.actor;
	auto& hud = frame.hud;
	frame.hands.reference = false;
	frame.hands.reference_weapon_pose = false;
	if (!frame.cameraPrepared)
		return false;
	if (!frame.cameraFinalized)
	{
		collide_camera(*frame.camera.camera, frame.camera.viewport_near, &actor);
		actor.cam_Finalize(frame.camera);
	}
	if (frame.handsFinalized)
		return true;
	if (frame.handsPrepared)
	{
		Fmatrix world, inversePrepared, delta;
		actor.Cameras().hud_camera_Matrix(world);
		inversePrepared.invert(frame.preparedCamera);
		delta.mul_43(world, inversePrepared);
		hud.m_transform.mulA_43(delta);
		hud.m_transform_2.mulA_43(delta);
		hud.finalize_hud_pose(frame.hands);
	}
	else if (frame.hudReady)
	{
		Fmatrix world;
		actor.Cameras().hud_camera_Matrix(world);
		hud.update(world);
	}
	return true;
}

// Keeps the binding in step with the item in the hands. True when the frame poses a bound weapon.
bool EngineFrameAdapter::BindWeapon(Frame& frame, EngineCharacterHost& host)
{
	auto* weapon = frame.weapon;
	auto& hud = frame.hud;
	// Knives and binoculars, which also stand for empty hands, keep the
	// engine's HUD and only ride the Reference camera.
	const bool tool = weapon && (smart_cast<CWeaponKnife*>(weapon) || smart_cast<CWeaponBinoculars*>(weapon));
	const bool primaryWeapon =
		frame.hudReady && weapon && !tool && hud.m_attached_items[0] && hud.m_attached_items[0]->m_parent_hud_item == weapon;
	if (!primaryWeapon && host.HasWeaponBinding())
		hud.UnbindReferenceWeapon();
	if (primaryWeapon && !host.HasWeaponBinding() && !hud.m_reference_profile_attempted)
		hud.BindReferenceWeapon(*weapon);
	hud.RefreshReferenceEquipment();
	return primaryWeapon && host.HasWeaponBinding();
}

// Admits the action clip that started playing since the last frame.
void EngineFrameAdapter::AdmitAction(Frame& frame, EngineCharacterHost& host)
{
	auto* weapon = frame.weapon;
	HudHandPlayback playback;
	if (!frame.hud.HandPlaybackSnapshot(playback))
		return;
	const auto motion = HandMotionKey(playback.model, playback.motion.val);
	if (!host.AdmitAction(
			motion, playback.generation, !!(frame.actor.mstate_real & mcCrouch), weapon->GetAmmoElapsed() == 0, weapon->IsZoomed()))
		return;
	if (!host.ActionPreservesAim(motion))
		weapon->CancelReferenceActionAim();
	weapon->CancelReferenceActionFire();
	host.StopFire(true);
}

CharacterMovementInput EngineFrameAdapter::ObserveMovement(CActor& actor, CWeapon* weapon, EngineCharacterHost& host)
{
	const auto& basis = host.Basis();
	Fvector velocity;
	actor.character_physics_support()->movement()->GetCharacterVelocity(velocity);
	const auto observed = basis.DirectionToReference({ velocity.x, velocity.y, velocity.z });
	host.ObserveVelocity(ph_world->m_steps_num, observed[0], observed[2]);
	const auto position = basis.DirectionToReference({ actor.Position().x, actor.Position().y, actor.Position().z });
	CharacterMovementInput movement;
	const auto& features = Bodycam::GetConfig().reference;
	movement.cameraShake = features.camera_shake;
	movement.weaponSway = features.weapon_sway;
	movement.stressMotion = features.stress;
	movement.sightRegrip = features.sight_regrip;
	// The engine clears the direction bits of mstate_real below 0.2 m/s, so
	// the animation starts from the wished direction.
	const auto inputFlags = actor.CanMove() ? actor.mstate_wishful : 0u;
	movement.right = !!(inputFlags & mcRStrafe) - !!(inputFlags & mcLStrafe);
	movement.forward = !!(inputFlags & mcFwd) - !!(inputFlags & mcBack);
	movement.sprintRequested = !!(actor.mstate_real & mcSprint);
	movement.canAccelerate = actor.CanAccelerate() && actor.CanSprint();
	movement.desiredGait = movement.sprintRequested ? 1 : 0;
	movement.stance = !!(actor.mstate_real & mcCrouch);
	movement.engineGaitPresentation = false;
	movement.position = PlanarCentimetres(position);
	movement.engineVelocity = PlanarCentimetres(observed);
	movement.reloading = weapon && weapon->GetState() == CWeapon::eReload;
	return movement;
}

// A script's queued camera turn becomes the aim direction.
void EngineFrameAdapter::ApplyScriptCameraDeltas(CActor& actor, EngineCharacterHost& host)
{
	if (!actor.m_script_camera_delta_pending_count)
		return;
	actor.cam_ApplyScriptCameraDeltas(actor.cam_FirstEye());
	const auto* eye = actor.cam_FirstEye();
	Fquaternion rotation;
	rotation.rotationYawPitchRoll(eye->roll, eye->yaw, eye->pitch);
	Fmatrix orientation;
	orientation.rotation(rotation);
	orientation.transpose();
	if (eye->m_Flags.test(CCameraBase::flRelativeLink))
		orientation.mulA_43(actor.XFORM());
	const auto updated = Decompose(host.Basis().CameraWorldToReference(Bodycam::ToRows(orientation)));
	host.SetAimControl(CameraPovFromWorld({}, updated.rotation, actor.currentFOV()).rotation);
}

// Points the engine's camera along the Reference aim direction and runs the
// engine's own camera and HUD preparation, each once.
void EngineFrameAdapter::PrepareCameraAndHud(Frame& frame, const EngineCharacterHost& host, bool weaponPose, bool aiming)
{
	auto& actor = frame.actor;
	auto& hud = frame.hud;
	// Actor::UpdateCL sets the aiming mode after this adapter has run, and the
	// HUD preparation below reads it; left clear, the weapon's zoom progress
	// would fall back every frame.
	actor.SetZoomAimingMode(aiming);
	LocalPose control;
	control.rotation = WorldCameraQuaternion(host.Motion().control);
	frame.aimDirection = Bodycam::FromRows(host.Basis().CameraWorldToEngine(Compose(control))).k;
	actor.cam_FirstEye()->yaw = -frame.aimDirection.getH();
	actor.cam_FirstEye()->pitch = -frame.aimDirection.getP();
	if (!actor.cam_Prepare(float(Device.dwTimeDelta) / 1000.f, weaponPose ? g_fov : actor.currentFOV(), frame.camera, true))
		throw std::runtime_error("Reference camera preparation failed");
	frame.cameraPrepared = true;
	frame.motionInfluence = frame.hudReady ? std::clamp(double(hud.BodycamLoweringInfluence()), 0., 1.) : 1.;
	frame.hands.reference = true;
	frame.hands.reference_motion_influence = float(frame.motionInfluence);
	frame.hands.reference_weapon_pose = weaponPose;
	frame.preparedCamera = CameraMatrix(*frame.camera.camera);
	if (frame.hudReady)
	{
		hud.prepare_hud_pose(frame.preparedCamera, frame.hands);
		frame.handsPrepared = true;
	}
	frame.hudFov = AttachedHudFov(actor);
	if (weaponPose)
		Device.reference_hud_projection_frame = Device.dwFrame;
	Device.reference_hud_nearwall_scale = g_nearwall == NW_FOV
		? NearwallProjectionScale(frame.hudFov.actual * kHudFovDegrees, frame.hudFov.withoutNearwall * kHudFovDegrees)
		: 1.f;
}

// Reads the hands the engine has animated: the right hand bone and the
// weapon's grip on it for the HUD, and the left hand relative to the right for the solve.
void EngineFrameAdapter::ObserveHands(Frame& frame, EngineCharacterView& view)
{
	auto& hud = frame.hud;
	auto& hands = frame.hands;
	auto* rightModel = hud.m_model->dcast_PKinematics();
	hands.reference_hand_bone = HandBone(*rightModel, "r_hand", "bip01_r_hand");
	if (hands.reference_hand_bone == BI_NONE)
		throw std::runtime_error("Right hand model has no hand bone");
	auto* item = hud.m_attached_items[0];
	item->update_attach_offset();
	Fmatrix rightHand, weaponWorld, inverseRightHand;
	rightHand.mul_43(hud.m_transform, rightModel->LL_GetTransform(hands.reference_hand_bone));
	hud.calc_transform(0, item->m_attach_offset, weaponWorld, item->m_measures.m_bLeadGunLeftHand);
	inverseRightHand.invert(rightHand);
	hands.reference_hand_grip.mul_43(inverseRightHand, weaponWorld);
	auto* leftModel = hud.m_model_2->dcast_PKinematics();
	const u16 leftBone = HandBone(*leftModel, "l_hand", "bip01_l_hand");
	if (leftBone == BI_NONE)
		throw std::runtime_error("Left hand model has no hand bone");
	Fmatrix leftHand, leftFromRight;
	leftHand.mul_43(hud.m_transform_2, leftModel->LL_GetTransform(leftBone));
	leftFromRight.mul_43(inverseRightHand, leftHand);
	view.leftHandFromRight = Bodycam::ToRows(leftFromRight);
}

// Places both arm model roots on the solved camera and finishes the engine's HUD update.
void EngineFrameAdapter::PublishHud(Frame& frame, const EngineCharacterHost& host, const Fmatrix& world, bool weaponPose, float sceneFov)
{
	auto& hud = frame.hud;
	const auto& pose = *host.Pose();
	const double influence = frame.motionInfluence;
	if (frame.handsPrepared)
	{
		// The roots the engine prepared, carried from the prepared camera to the solved one.
		Fmatrix inversePrepared, cameraDelta, engineRight, engineLeft;
		inversePrepared.invert(frame.preparedCamera);
		cameraDelta.mul_43(world, inversePrepared);
		engineRight.mul_43(cameraDelta, hud.m_transform);
		engineLeft.mul_43(cameraDelta, hud.m_transform_2);
		if (weaponPose)
		{
			// hud_adjust, which scripts also drive, moves the attachment
			// from its configured place; the whole rig takes that movement.
			Fmatrix rigWorld = world;
			if (hud.m_adjust_mode)
			{
				auto* item = hud.m_attached_items[0];
				const auto configured = Bodycam::AttachTransform(item->m_measures.m_hands_attach[0], item->m_measures.m_hands_attach[1]);
				const auto current = Bodycam::AttachTransform(item->hands_attach_pos(), item->hands_attach_rot());
				Fmatrix inverseConfigured, adjustment;
				inverseConfigured.invert(configured);
				adjustment.mul_43(current, inverseConfigured);
				rigWorld.mul_43(world, adjustment);
			}
			hud.m_transform.mul_43(rigWorld, Bodycam::FromRows(pose.rightModelCamera));
			hud.m_transform_2.mul_43(rigWorld, Bodycam::FromRows(pose.leftModelCamera));
			hud.m_transform = BlendTransforms(engineRight, hud.m_transform, influence);
			hud.m_transform_2 = BlendTransforms(engineLeft, hud.m_transform_2, influence);
		}
		else
		{
			// An unbound item keeps the engine's animation and grip; both
			// roots take the weapon's Reference motion away from its neutral pose.
			Fmatrix inverseNeutral, carrier, identity, inverseWorld, worldCarrier, temp;
			inverseNeutral.invert(Bodycam::FromRows(pose.neutralHudCamera));
			carrier.mul_43(Bodycam::FromRows(pose.hudMotion), inverseNeutral);
			identity.identity();
			carrier = BlendTransforms(identity, carrier, influence);
			inverseWorld.invert(world);
			temp.mul_43(world, carrier);
			worldCarrier.mul_43(temp, inverseWorld);
			hud.m_transform.mul_43(worldCarrier, engineRight);
			hud.m_transform_2.mul_43(worldCarrier, engineLeft);
		}
	}
	// Aiming scales the HUD projection in clip space toward the weapon's own
	// HUD FOV, which keeps every depth of the rig in place.
	const float hudFovDegrees = (g_nearwall == NW_FOV ? frame.hudFov.withoutNearwall : frame.hudFov.actual) * kHudFovDegrees;
	Device.reference_hud_clip_scale =
		weaponPose ? 1.f + float(influence) * (AdsClipScale(sceneFov, hudFovDegrees, host.Motion().adsAlpha) - 1.f) : 1.f;
	frame.handsFinalized = true;
	if (frame.handsPrepared)
		hud.finalize_hud_pose(frame.hands);
}

// Sets the actor's leg animation to the Reference foot phase, so that the
// visible legs and the engine's footsteps keep step with the camera.
void EngineFrameAdapter::SyncLegs(CActor& actor, const EngineCharacterHost& host)
{
	const u32 state = actor.mstate_real;
	if (!(state & mcAnyMove) || (state & (mcJump | mcFall | mcLanding | mcLanding2 | mcClimb)) || !actor.m_current_legs_blend ||
		actor.m_current_legs_blend->stop_at_end)
		return;
	const auto phase = host.LocomotionPhase();
	if (!phase)
		return;
	auto* animated = smart_cast<IKinematicsAnimated*>(actor.Visual());
	// Advance the tracks now: a later bone calculation must not move past this phase.
	animated->UpdateTracks();
	if (!actor.m_current_legs.valid() || !actor.synchronize_locomotion(float(*phase)))
		return;
	actor.Visual()->dcast_PKinematics()->CalculateBones_Invalidate();
	if (actor.m_current_torso_blend)
	{
		const auto* torso = animated->LL_GetMotionDef(actor.m_current_torso);
		const auto* legs = animated->LL_GetMotionDef(actor.m_current_legs);
		if (torso && legs && (torso->flags & esmSyncPart) && (legs->flags & esmSyncPart))
			actor.m_current_torso_blend->timeCurrent =
				actor.m_current_legs_blend->timeCurrent / actor.m_current_legs_blend->timeTotal * actor.m_current_torso_blend->timeTotal;
	}
}
}
