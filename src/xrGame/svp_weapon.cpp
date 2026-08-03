#include "stdafx.h"
#include "Weapon.h"
#include "actor.h"
#include "actoreffector.h"
#include "../xrEngine/CameraBase.h"
#include "../xrEngine/CameraManager.h"
#include "level.h"
#include "player_hud.h"
#include "../xrEngine/svp_gameplay_cvars.h"

// pip weapon raise settled threshold, GetZRotatingFactor at or above this reads as fully aimed
static const float SVP_SETTLED_ROT = 0.999f;

static bool svp_config_matches_weapon(const CSecondVPParams::OpticConfig& config,
	const CWeapon& weapon)
{
	const auto& viewport = Device.m_SecondViewport;
	if (!config.valid || !config.typed_route ||
		config.session != viewport.GetSVPSession() ||
		config.route_epoch != viewport.GetOpticRouteEpoch() ||
		xr_strcmp(config.weapon, weapon.cNameSect().c_str()) ||
		config.weapon_id != weapon.ID() || config.zoom_type != weapon.GetZoomType())
		return false;

	shared_str active_scope;
	if (weapon.IsScopeAttached())
		active_scope = weapon.GetScopeName();
	const LPCSTR scope = active_scope.c_str() ? active_scope.c_str() : "";
	return xr_strcmp(config.scope, scope) == 0;
}

static float svp_configured_zero(const CWeapon& weapon, const CSecondVPParams& viewport,
	CSecondVPParams::WeaponPoseSnapshot& pose)
{
	if (!svp_optic_api_active())
	{
		pose.optic_typed = false;
		return g_svp_zero;
	}

	CSecondVPParams::OpticConfig config;
	viewport.ReadOpticConfig(config);
	pose.optic_typed = true;
	pose.optic_config_valid = config.valid;
	pose.optic_context_token = config.context_token;
	pose.optic_config_generation = config.generation;
	pose.optic_route_epoch = config.route_epoch;
	return svp_config_matches_weapon(config, weapon) ? config.convergence_limit_m : 0.f;
}

static bool svp_current_sight(const CSecondVPParams& viewport,
	const CSecondVPParams::WeaponPoseSnapshot& pose,
	CSecondVPParams::SightSnapshot& sight)
{
	if (!viewport.ReadSight(sight)
		|| !viewport.SnapshotRecent(sight.frame, sight.session, Device.dwFrame)
		|| !CSecondVPParams::SameOpticConfig(pose, sight)
		|| sight.weapon_id != pose.weapon_id)
		return false;

	attachable_hud_item* root = nullptr;
	if (g_player_hud)
	{
		switch (sight.root_role)
		{
		case IDSGraphManager::hud_optic:
			root = g_player_hud->attached_item(SCOPE_ATTACH_IDX);
			break;
		case IDSGraphManager::hud_primary_item:
			root = g_player_hud->attached_item(0);
			break;
		default:
			break;
		}
	}
	if (!sight.root_local_valid || !root
		|| sight.root_token != reinterpret_cast<u64>(&root->m_item_transform))
		return false;

	root->m_item_transform.transform_tiny(
		sight.position, sight.root_local_position);
	root->m_item_transform.transform_dir(
		sight.direction, sight.root_local_direction);
	sight.direction.normalize_safe();
	return _valid(sight.position) && _valid(sight.direction)
		&& sight.direction.square_magnitude() > EPS;
}

static void svp_resolve_projectile_ray(const SPickParam& pick,
	const CSecondVPParams::SightSnapshot& sight, bool sight_valid,
	float zero_m, const Fvector& muzzle, Fvector& position, Fvector& direction)
{
	position.set(pick.defs.start);
	direction.set(pick.defs.dir);
	if (!sight_valid || zero_m <= 0.f)
		return;

	if (!pick.barrel_blocked && muzzle.square_magnitude() > EPS)
		position.set(muzzle);

	Fvector zero_point;
	zero_point.mad(sight.position, sight.direction, zero_m);
	Fvector zero_direction;
	zero_direction.sub(zero_point, position);
	if (zero_direction.magnitude() > 1.f)
	{
		zero_direction.normalize();
		direction.set(zero_direction);
	}
}

void CWeapon::UpdateSvpSwingEnvelope(CActor* pActor)
{
	auto& vp = Device.m_SecondViewport;
	SSvpSwingEnvelope& envelope = m_svpSwingEnvelope;
	const Fvector cr = pActor->cam_FirstEye()->vDirection;
	const int current_ammo = GetAmmoElapsed();
	const u32 session = vp.GetSVPSession();
	if (!envelope.initialized || envelope.session != session ||
		Device.dwFrame - envelope.frame > 1)
	{
		envelope.initialized = true;
		envelope.direction.set(cr);
		envelope.rate.set(0.f, 0.f);
		envelope.angular_rate = 0.f;
		envelope.acceleration = 0.f;
		envelope.ammo = current_ammo;
		envelope.log_time = Device.dwTimeGlobal;
	}
	envelope.session = session;
	envelope.frame = Device.dwFrame;

	// Angular acceleration catches recoil and hard swings while ignoring steady tracking.
	const float dt = _max(Device.fTimeDelta, 0.001f);
	Fvector turn_axis;
	turn_axis.crossproduct(envelope.direction, cr);
	float sine = turn_axis.magnitude();
	clamp(sine, 0.f, 1.f);
	const float angular_rate = asinf(sine) / dt;
	const float acceleration = _abs(angular_rate - envelope.angular_rate) / dt;
	envelope.acceleration += (acceleration - envelope.acceleration) *
		(1.f - expf(-dt / 0.04f));

	Fvector direction_delta;
	direction_delta.sub(cr, envelope.direction);
	Fvector up;
	up.set(pActor->cam_FirstEye()->vNormal);
	Fvector right;
	right.crossproduct(up, cr);
	right.normalize_safe();
	const float rate_blend = 1.f - expf(-dt / 0.12f);
	envelope.rate.x += (direction_delta.dotproduct(right) / dt - envelope.rate.x) * rate_blend;
	envelope.rate.y += (direction_delta.dotproduct(up) / dt - envelope.rate.y) * rate_blend;
	const float rate_magnitude = envelope.rate.magnitude();
	if (rate_magnitude > 0.05f)
	{
		vp.svp_swing_x = envelope.rate.x / rate_magnitude;
		vp.svp_swing_y = envelope.rate.y / rate_magnitude;
	}
	envelope.direction.set(cr);
	envelope.angular_rate = angular_rate;

	const bool shot = envelope.ammo != -1 && current_ammo < envelope.ammo;
	envelope.ammo = current_ammo;
	extern int g_svp_crescent;
	if (!g_svp_crescent || !vp.IsSVPActive() || GetZRotatingFactor() <= 0.999f)
		return;

	const float threshold = _max(24.f / _max(vp.svp_mag, 1.f), 12.f);
	float swing = (envelope.acceleration - threshold) / (0.5f * threshold);
	clamp(swing, 0.f, 1.f);
	if (shot)
	{
		vp.svp_shadow_gain += 0.45f;
		clamp(vp.svp_shadow_gain, 0.f, 1.f);
	}
	else if (swing > vp.svp_shadow_gain)
	{
		vp.svp_shadow_gain += (swing - vp.svp_shadow_gain) *
			(1.f - expf(-dt / 0.06f));
	}

	if ((envelope.acceleration > 2.f || vp.svp_shadow_gain > 0.05f) &&
		Device.dwTimeGlobal - envelope.log_time > 1000)
	{
		envelope.log_time = Device.dwTimeGlobal;
		PipMsg("[SVP-SWING] accel %.1f dz %.1f gain %.2f mag %.1f",
			envelope.acceleration, threshold, vp.svp_shadow_gain, vp.svp_mag);
	}
}

void CWeapon::UpdateSecondVP()
{
	SyncSvpZoomSeedMode();
	if (m_zoomtype == 0)
		RefreshSvpTypedMagnifications();
	if (!(ParentIsActor() && (m_pInventory != NULL) && (m_pInventory->ActiveItem() == this)))
		return;

	CActor* pActor = smart_cast<CActor*>(H_Parent());
	if (!scope_svp_enabled)
	{
		// legacy fake-SVP, stock activation condition unchanged
		Device.m_SecondViewport.SetSVPActive(m_zoomtype == 0 && pActor->cam_Active() == pActor->cam_FirstEye() && IsSecondVPZoomPresent() && m_zoom_params.m_fZoomRotationFactor > 0.05f);
		return;
	}

	const bool zoomed = IsZoomed();
	const bool svp_present = IsSecondVPZoomPresent();
	if (m_zoomtype == 0 && svp_present)
	{
		m_svpMainViewIdentity = SvpZoomIdentity();
		m_svpMainViewValid = true;
	}
	const bool svp_act = (scope_debug && svp_present && zoomed)
		|| (m_zoomtype == 0 && pActor->cam_Active() == pActor->cam_FirstEye()
			&& svp_present && zoomed);
	// pip activation and optic route changes log every term
	{
		auto& vp = Device.m_SecondViewport;
		CSecondVPParams::SightSnapshot sight;
		const bool sight_ok = vp.ReadSight(sight)
			&& vp.SnapshotRecent(sight.frame, sight.session, Device.dwFrame);
		CSecondVPParams::OpticConfig config;
		const bool typed = scope_svp_enabled >= 2 && svp_optic_api_active();
		const bool config_ok = !typed || (vp.ReadOpticConfig(config)
			&& svp_config_matches_weapon(config, *this));
		const u32 session = vp.GetSVPSession();
		const u32 config_generation = typed ? config.generation : 0;
		const u32 route_epoch = typed ? config.route_epoch : 0;
		static const CWeapon* s_prev_weapon = nullptr;
		static bool s_prev_act = false;
		static u32 s_prev_session = u32(-1);
		static u32 s_prev_config_generation = u32(-1);
		static u32 s_prev_route_epoch = u32(-1);
		if (svp_act != s_prev_act || this != s_prev_weapon || session != s_prev_session
			|| config_generation != s_prev_config_generation || route_epoch != s_prev_route_epoch)
		{
			s_prev_weapon = this;
			s_prev_act = svp_act;
			s_prev_session = session;
			s_prev_config_generation = config_generation;
			s_prev_route_epoch = route_epoch;
			PipMsg("[SVP-ACT] %d session=%u zt=%d cam=%d zoomed=%d rot=%.3f fresh=%d ready=%d typed=%d config=%d lens_r=%.3f stale=%u zf=%.1f sec=%s",
				(int)svp_act, session, m_zoomtype,
				(int)(pActor->cam_Active() == pActor->cam_FirstEye()), (int)zoomed,
				GetZRotatingFactor(),
				(int)sight_ok, (int)svp_present, (int)typed, (int)config_ok,
				sight.lens_radius,
				sight.frame != u32(-1) ? Device.dwFrame - sight.frame : u32(-1),
				GetZoomFactor(), cNameSect().c_str());
		}
	}
	// aim transitions keep eye tracking, a reload or other pending action breaks the cheek weld
	// so the virtual eye must not follow the animated scope
	const u32 weapon_state = GetState();
	const bool aim_transition = weapon_state == eAimStart || weapon_state == eAimEnd;
	Device.m_SecondViewport.svp_eye_tracking_suspended.store(
		IsPending() && !aim_transition, std::memory_order_release);
	Device.m_SecondViewport.SetSVPActive(svp_act);
	// the shadow swing envelope drains steadily, the charge site outruns it on real kicks
	Device.m_SecondViewport.svp_shadow_gain += (0.f - Device.m_SecondViewport.svp_shadow_gain)
		* (1.f - expf(-Device.fTimeDelta / 0.7f));
	// the lever throw holds the envelope up for the real throw time, the drain takes the tail
	{
		const float SVP_LEVER_THROW_S = 0.2f; // measured SpecterDR lever throw
		auto& vp = Device.m_SecondViewport;
		if (vp.svp_lever_ms)
		{
			const float t = (Device.dwTimeGlobal - vp.svp_lever_ms) / 1000.f;
			if (t >= 0.f && t < SVP_LEVER_THROW_S)
				vp.svp_shadow_gain = _max(vp.svp_shadow_gain, 1.f - t / SVP_LEVER_THROW_S);
			else
				vp.svp_lever_ms = 0;
		}
	}
	// aimed on irons or the launcher, the scope lens still renders but its pose is not the scope's
	Device.m_SecondViewport.svp_alt_sight = (m_zoomtype != 0 && IsZoomed());
	// authored recoil relax time publish, the recoil settle window derives from it
	Device.m_SecondViewport.svp_recoil_relax_s = (zoom_cam_recoil.RelaxSpeed > EPS)
		? zoom_cam_recoil.Dispersion / zoom_cam_recoil.RelaxSpeed : 0.f;

	// pip publish a raise transient free zoom for the svp camera, the raise holds the dialed
	// magnification and hands off to the live target after settle
	{
		extern int g_svp_zoom_sync;
		extern float scope_radius;
		auto& vp = Device.m_SecondViewport;
		// a selector, not an integrator, the raise publishes the steady dialed magnification and the
		// settled aim publishes the weapon's own glided factor so one integrator drives image + reticle
		float pub;
		if (!g_svp_zoom_sync || !vp.IsSVPActive() || !IsZoomed())
			pub = GetZoomFactor(); // fallback publishes current, the >1 guard falls back to hud_params.y when unset
		else if (GetZRotatingFactor() > SVP_SETTLED_ROT)
			pub = GetZoomFactor(); // settled, the weapon glide is the single integrator
		else
			pub = m_zoom_params.m_bUseDynamicZoom
				? (scope_radius > 0.0 ? m_fRTZoomFactor / scope_scrollpower : m_fRTZoomFactor)
				: CurrentZoomFactor(); // raise, hold the steady dialed magnification, no easing needed
		vp.svp_zoom_pub = pub;
		// config factors ride the 75 base and rescale to the live fov downstream, script
		// authored factors already carry the user fov and pass through
		extern float g_fov;
		vp.svp_fov_scale = m_zoom_params.m_bScriptedZoom ? 1.f : (g_fov / SVP_ZOOM_BASE_FOV);
		// the main view stays wide at g_fov through a pip scope, publish it as the punch free mag
		// reference so a recoil fov effector never reaches scope_magnification
		vp.svp_aim_fov = g_fov;
		// authored mag flat optics keep the clean optical mag, the panel subtense override stays off
		vp.svp_authored_mag = m_zoom_params.m_bSvpAuthoredMin;
		// authored mins and detent base mins are 75 base, the legacy optical model min rides g_fov
		vp.svp_min_75base = m_zoom_params.m_bSvpAuthoredMin || SvpDetentBase();
	}

}

void CWeapon::UpdateSvpWeaponPose()
{
	if (!Device.m_SecondViewport.IsSVPActive())
		return;

	UpdatePick();
	PublishSvpWeaponPose();
}

void CWeapon::PublishSvpWeaponPose()
{
	auto& vp = Device.m_SecondViewport;
	if (!vp.IsSVPActive() || !ParentIsActor() || !m_pInventory
		|| m_pInventory->ActiveItem() != this)
		return;

	CActor* actor = smart_cast<CActor*>(H_Parent());
	if (!actor)
		return;

	const SPickParam& pp = GetPick();
	CSecondVPParams::WeaponPoseSnapshot pose;
	pose.weapon_id = ID();
	const float configured_zero = svp_configured_zero(*this, vp, pose);
	CSecondVPParams::SightSnapshot sight;
	const bool sight_ok = svp_current_sight(vp, pose, sight);

	// player_hud has finalized the weapon, optic, and procedural transforms.
	// Publish that same transform for optics and 3D ballistics.
	pose.muzzle_pos.set(get_LastFP());
	pose.eye_ray_pos.set(actor->cam_FirstEye()->vPosition);
	pose.eye_ray_dir.set(actor->cam_FirstEye()->vDirection);
	pose.camera_pos.set(Device.vCameraPosition);
	pose.camera_right.set(Device.vCameraRight);
	pose.camera_up.set(Device.vCameraTop);
	pose.camera_forward.set(Device.vCameraDirection);
	svp_resolve_projectile_ray(pp, sight, sight_ok, configured_zero,
		pose.muzzle_pos, pose.fire_ray_pos, pose.fire_ray_dir);
	pose.fire_ray_zero = configured_zero;
	pose.frame = Device.dwFrame;
	pose.session = vp.GetSVPSession();
	vp.PublishWeaponPose(pose);
}

// pip main view fov ownership, latched per optic identity so a lens snapshot gap cannot zoom the main view
bool CWeapon::OwnsSvpMainView() const
{
	return scope_svp_enabled >= 2 && m_zoomtype == 0 && m_svpMainViewValid
		&& m_svpMainViewIdentity == SvpZoomIdentity();
}

// pip SVP readiness, true when a fresh captured lens exists, reads the stable sight publish
bool CWeapon::GetSVPCameraMatrix()
{
	auto& vp = Device.m_SecondViewport;
	CSecondVPParams::SightSnapshot sight;
	if (!vp.ReadSight(sight) || sight.lens_radius <= EPS
		|| !vp.SnapshotRecent(sight.frame, sight.session, Device.dwFrame))
		return false;
	if (!scope_svp_enabled || !svp_optic_api_active())
		return true;

	CSecondVPParams::OpticConfig config;
	return vp.ReadOpticConfig(config) && svp_config_matches_weapon(config, *this)
		&& sight.optic_typed
		&& CSecondVPParams::MatchesOpticConfig(sight, config);
}

// pip zeroing, the shot converges onto the sight line at the ranged zero, then the tracer
// ring records the final departure ray, called from CActor::g_fireParams
void svp_apply_zero_and_trace(const SPickParam& pp, u16 firing_weapon_id,
	Fvector& fire_pos, Fvector& fire_dir)
{
	// pip zeroing converges onto the sight line at the ranged zero
	auto& vp = Device.m_SecondViewport;
	CSecondVPParams::WeaponPoseSnapshot pose;
	const bool pose_ok = vp.ReadWeaponPose(pose)
		&& vp.SnapshotExact(pose.frame, pose.session, Device.dwFrame)
		&& pose.weapon_id == firing_weapon_id;
	if (Device.true_pip_on && vp.IsSVPActive() && pose_ok)
	{
		fire_pos.set(pose.fire_ray_pos);
		fire_dir.set(pose.fire_ray_dir);
	}
	else
	{
		fire_pos.set(pp.defs.start);
		fire_dir.set(pp.defs.dir);
	}

	// pip [3DB] tracer ring, records the final departure ray of every shot for the fading overlay
	{
		CSecondVPParams::FireTrace tr;
		tr.pos.set(fire_pos);
		tr.dir.set(fire_dir);
		tr.time_ms = Device.dwTimeGlobal;
		vp.AppendFireTrace(tr);
	}
}
