// Classic bodycam HUD playback: the sprint-entry clip of a HUD item, driving a
// one-shot clip by progress, pacing the movement loop, the script-only
// movement notification, the lowering influence of script animations, and the
// player_hud forwarders to the arm controller.
#include "stdafx.h"
#include "bodycam_hud_arms.h"
#include "bodycam_sprint_start.h"
#include "player_hud.h"
#include "HudItem.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../xrEngine/motion.h"
#include "../../xrServerEntities/script_engine.h"

#include <algorithm>

void CHudItem::BodycamPlaySprintEntryClip()
{
	if (GetState() != eIdle || m_bStopAtEndAnimIsRunning || !IsAttachedToHUD() || !MovingAnimAllowedNow())
		return;
	m_bodycam_entry_clip = false;
	m_bodycam_entry_probe = true;
	PlayAnimIdleSprint();
	m_bodycam_entry_probe = false;
}

void CHudItem::BodycamDriveSprintEntry(float progress, Bodycam::SprintEntryDrive drive)
{
	if (!m_bodycam_entry_clip)
		return;
	// Another clip took over (action, weapon state): it keeps its own timing.
	if (!m_bStopAtEndAnimIsRunning || !g_player_hud || m_current_motion != m_bodycam_entry_motion)
	{
		m_bodycam_entry_clip = false;
		return;
	}
	if (drive == Bodycam::SprintEntryDrive::Hold)
	{
		g_player_hud->BodycamScrubOneShot(progress);
		// The item ends one-shots by time; keep the end ahead while held.
		constexpr u32 kHeldClipEndMarginMs = 1000;
		m_dwMotionEndTm = Device.dwTimeGlobal + kHeldClipEndMarginMs;
		return;
	}
	const float remaining = g_player_hud->BodycamReleaseOneShot();
	m_dwMotionEndTm = Device.dwTimeGlobal + (drive == Bodycam::SprintEntryDrive::Finish ? u32(remaining * 1000.f) : 0);
	m_bodycam_entry_clip = false;
}

void player_hud::BodycamNotifyScriptMovement(ACTOR_DEFS::EMoveCommand cmd)
{
	::luabind::functor<void> func;
	if (ai().script_engine().functor("_g.player_hud__OnMovementChanged", func))
	{
		func(cmd);
	}
}

namespace
{
// Playback rate of a held one-shot. Not zero: the engine reads a stopped blend
// as a finished clip.
constexpr float kHeldBlendSpeed = 0.0001f;
// Share of a one-shot a hold may reach; the rest plays on release.
constexpr float kMaximumHeldPhase = 0.97f;
// Smaller changes of the loop pace are not applied.
constexpr float kPaceTolerance = 0.002f;

// Visits live blends of the arm models and of the primary weapon. With a
// left-hand item (detector) the second arm model plays that item's clips and
// is skipped; other attached items (3D scopes) play their own clips.
template <typename Visit>
void VisitHudBlends(player_hud& hud, Visit&& visit)
{
	const auto model = [&](IKinematicsAnimated* ka)
	{
		if (!ka)
			return;
		xrCriticalSectionGuard lock(&ka->dcast_PKinematics()->UCalc_Mutex);
		for (u16 part = 0, parts = ka->partitions().count(); part < parts; ++part)
			for (u32 i = 0, count = ka->LL_PartBlendsCount(part); i < count; ++i)
				if (CBlend* blend = ka->LL_PartBlend(part, i))
					visit(*blend);
	};
	model(hud.m_model);
	if (!hud.attached_item(1))
		model(hud.m_model_2);
	if (attachable_hud_item* item = hud.attached_item(0))
		model(item->m_model ? item->m_model->dcast_PKinematicsAnimated() : nullptr);
}

bool IsOneShot(const CBlend& blend)
{
	return blend.stop_at_end && blend.timeTotal > EPS_S;
}
}

void player_hud::BodycamScrubOneShot(float phase)
{
	VisitHudBlends(*this,
		[&](CBlend& blend)
		{
			if (blend.blend_state() != CBlend::eAccrue || !IsOneShot(blend))
				return;
			auto held = m_bodycam_held_blends.find(&blend);
			if (held == m_bodycam_held_blends.end())
				m_bodycam_held_blends.emplace(&blend, blend.speed);
			else if (blend.speed != kHeldBlendSpeed)
				held->second = blend.speed; // The engine reused this blend for a new clip.
			blend.speed = kHeldBlendSpeed;
			blend.timeCurrent = std::clamp(phase, 0.f, kMaximumHeldPhase) * blend.timeTotal;
		});
}

float player_hud::BodycamReleaseOneShot()
{
	float remaining = 0.f;
	VisitHudBlends(*this,
		[&](CBlend& blend)
		{
			const auto held = m_bodycam_held_blends.find(&blend);
			if (!IsOneShot(blend) || held == m_bodycam_held_blends.end() || blend.speed != kHeldBlendSpeed)
				return;
			blend.speed = held->second;
			if (blend.blend_state() == CBlend::eAccrue && blend.speed > 0.f)
				remaining = std::max(remaining, (blend.timeTotal - blend.timeCurrent) / blend.speed);
		});
	m_bodycam_held_blends.clear();
	return remaining;
}

void player_hud::BodycamPaceMovementLoop(float factor)
{
	if (_abs(factor - m_bodycam_loop_pace) < kPaceTolerance)
		return;
	// Only the current loop is paced; fading and one-shot clips keep their
	// authored timing.
	const float ratio = factor / m_bodycam_loop_pace;
	VisitHudBlends(*this,
		[ratio](CBlend& blend)
		{
			if (blend.blend_state() == CBlend::eAccrue && !blend.stop_at_end)
				blend.speed *= ratio;
		});
	m_bodycam_loop_pace = factor;
}

void player_hud::DumpAuthoredMotionProfile() const
{
	m_bodycam_hud_arms->DumpAuthoredMotionProfile();
}

float player_hud::BodycamLoweringInfluence() const
{
	float influence = 1.f + (script_anim_bodycam_lowering_influence - 1.f) * script_anim_offset_factor;

	for (const script_layer* layer : m_script_layers)
	{
		if (!layer || !layer->anm || layer->blend_amount <= 0.f)
			continue;

		const float layer_influence = 1.f + (layer->m_bodycam_lowering_influence - 1.f) * layer->blend_amount;
		influence = std::min(influence, layer_influence);
	}
	return influence;
}

bool player_hud::ClassicRegripCamera(Fmatrix& local_delta) const
{
	return m_bodycam_hud_arms->ClassicRegripCamera(local_delta);
}

bool player_hud::ClassicRecoilFire(bool firing, bool aiming, const CWeapon* weapon)
{
	return m_bodycam_hud_arms->ClassicRecoilFire(firing, aiming, weapon);
}
