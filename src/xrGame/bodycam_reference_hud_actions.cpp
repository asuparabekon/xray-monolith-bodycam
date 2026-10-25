// Which action clips the engine's hand models are playing, as the Reference
// host and the weapon's aim input need to know it.
#include "stdafx.h"
#include "player_hud.h"
#include "HudItem.h"
#include "weapon.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "bodycam_reference_host.h"
#include "bodycam_reference/blend_contribution.h"

namespace
{
// Calls visit(model, part, blend) for every playing blend of the two arm
// models (model 0 is the right arm) until it returns true. The engine reuses
// a blend slot when its motion ends, so a blend pointer kept from an earlier
// frame is only good if this still visits it.
template <class Visit>
void VisitHandBlends(IKinematicsAnimated* right, IKinematicsAnimated* left, Visit visit)
{
	IKinematicsAnimated* models[] = { right, left };
	for (unsigned model = 0; model < 2; ++model)
	{
		auto* animated = models[model];
		if (!animated)
			continue;
		xrCriticalSectionGuard lock(&animated->dcast_PKinematics()->UCalc_Mutex);
		for (u16 part = 0; part < animated->partitions().count(); ++part)
			for (u32 index = 0; index < animated->LL_PartBlendsCount(part); ++index)
			{
				const auto* blend = animated->LL_PartBlend(part, index);
				if (blend && blend->blend_state() != CBlend::eFREE_SLOT && visit(model, part, *blend))
					return;
			}
	}
}
}

// True while an action clip has any weight, which includes one that is blending out behind the idle.
bool player_hud::ReferenceActionBlending() const
{
	bool blending = false;
	if (m_reference_host)
		VisitHandBlends(m_model, m_model_2,
			[&](unsigned model, u16, const CBlend& blend)
			{
				blending =
					blend.blendAmount > 0.f && m_reference_host->IsAction(BodycamReference::HandMotionKey(model, blend.motionID.val));
				return blending;
			});
	return blending;
}

// The hand motion the primary item started last, if it is still playing.
bool player_hud::HandPlaybackSnapshot(HudHandPlayback& output) const
{
	output = {};
	const auto* item = m_attached_items[0];
	if (!item || !item->m_active_hand_motion.valid() || !item->m_active_hand_blend)
		return false;
	bool playing = false;
	VisitHandBlends(m_model, m_model_2,
		[&](unsigned model, u16, const CBlend& blend)
		{
			if (&blend != item->m_active_hand_blend)
				return false;
			if (blend.motionID == item->m_active_hand_motion)
			{
				output.motion = blend.motionID;
				output.generation = item->m_active_hand_generation;
				output.seconds = blend.timeCurrent;
				output.duration = blend.timeTotal;
				output.weight = blend.blendAmount;
				output.model = model;
				playing = true;
			}
			return true;
		});
	return playing;
}

// Lets a weapon action that starts while aiming keep the aim when the weapon
// has an aimed variant of it, and plays that variant.
player_hud_motion* player_hud::ReferenceSelectActionMotion(attachable_hud_item& item, player_hud_motion* requested)
{
	if (item.m_attach_place_idx != 0 || !ReferenceOwnsMotion(item.m_parent_hud_item) || requested->m_animations.empty())
		return requested;
	auto* weapon = item.m_parent_hud_item->object().cast_weapon();
	const auto kind =
		BodycamReference::ClassifyActionAlias(requested->m_alias_name.c_str(), BodycamReference::ReferenceTuning().extraReloadAliases);
	if (weapon && kind != BodycamReference::ActionKind::None && weapon->IsZoomed())
	{
		const auto aimed = m_reference_host->AimedAlias(requested->m_alias_name.c_str());
		if (!aimed)
			weapon->CancelReferenceActionAim();
		else if (auto* paired = item.m_hand_motions->find_motion(aimed->c_str()); paired && !paired->m_animations.empty())
			requested = paired;
	}
	// An action that keeps the aim, whichever of its variants plays, ends the burst being fired.
	const auto motion = BodycamReference::HandMotionKey(0, requested->m_animations.front().mid.val);
	for (const auto& variant : requested->m_animations)
		if (!m_reference_host->SameActionKind(motion, BodycamReference::HandMotionKey(0, variant.mid.val)))
			return requested;
	if (m_reference_host->ActionPreservesAim(motion))
	{
		if (weapon)
			weapon->CancelReferenceActionFire();
		m_reference_host->StopFire(true);
	}
	return requested;
}

HudActionState player_hud::ReferenceActionStatus() const
{
	const auto playbacks = ReferenceActionPlaybacks();
	HudActionState state;
	state.playing = !playbacks.empty();
	state.preservesAim = state.playing;
	for (const auto& playback : playbacks)
	{
		const auto motion = BodycamReference::HandMotionKey(playback.model, playback.motion.val);
		state.reload = state.reload || m_reference_host->ActionIsReload(motion);
		state.preservesAim = state.preservesAim && m_reference_host->ActionPreservesAim(motion);
	}
	return state;
}

bool player_hud::ReferenceSemanticActionPlaying() const
{
	return !ReferenceActionPlaybacks().empty();
}

// The action clips with weight on the base channel, one entry per motion. Their
// weight is the share of the pose they contribute within their partition.
std::vector<HudHandPlayback> player_hud::ReferenceActionPlaybacks() const
{
	std::vector<HudHandPlayback> output;
	if (!m_reference_host)
		return output;
	// The base channel blends of the partition being visited; weight holds the blend amount.
	std::vector<HudHandPlayback> blends;
	unsigned currentModel = 0;
	u16 currentPart = 0;
	const auto finishPart = [&]
	{
		std::vector<float> amounts;
		for (const auto& blend : blends)
			amounts.push_back(blend.weight);
		const auto contributions = BodycamReference::BlendContributions(amounts, EPS_S);
		for (unsigned index = 0; index < blends.size(); ++index)
		{
			auto playback = blends[index];
			playback.weight = contributions[index];
			if (playback.weight <= 0.f || !m_reference_host->IsAction(BodycamReference::HandMotionKey(playback.model, playback.motion.val)))
				continue;
			// Model-local IDs need not match for a mirrored clip, or may name
			// unrelated clips. Binding resolves their shared authored identity.
			// The right model's entry stands, at its strongest partition.
			auto found = std::find_if(output.begin(), output.end(),
				[&](const HudHandPlayback& p)
				{
					return m_reference_host->SameActionClip(BodycamReference::HandMotionKey(p.model, p.motion.val),
						BodycamReference::HandMotionKey(playback.model, playback.motion.val));
				});
			if (found == output.end())
				output.push_back(playback);
			else if (playback.model == 0 && found->weight < playback.weight)
				*found = playback;
		}
		blends.clear();
	};
	VisitHandBlends(m_model, m_model_2,
		[&](unsigned model, u16 part, const CBlend& blend)
		{
			if (model != currentModel || part != currentPart)
			{
				finishPart();
				currentModel = model;
				currentPart = part;
			}
			// The other channels add to the pose and do not take a share of it.
			if (blend.channel == 0)
			{
				HudHandPlayback playback;
				playback.motion = blend.motionID;
				playback.seconds = blend.timeCurrent;
				playback.duration = blend.timeTotal;
				playback.weight = blend.blendAmount;
				playback.model = model;
				blends.push_back(playback);
			}
			return false;
		});
	finishPart();
	return output;
}
