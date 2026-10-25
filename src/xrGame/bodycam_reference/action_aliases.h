#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BodycamReference
{
enum class ActionKind
{
	None,
	Reload,
	AmmoCheck,
	Inspect
};
struct ActionAlias
{
	std::uint64_t motion;
	std::string alias;
	bool resolved = true;
	std::string clip; // Authored hand motion name, shared by mirrored models.
};
inline std::size_t ActionAimWord(std::string_view alias)
{
	for (std::size_t at = 0; (at = alias.find("_aim", at)) != std::string_view::npos; ++at)
		if (at + 4 == alias.size() || alias[at + 4] == '_')
			return at;
	return std::string_view::npos;
}
inline std::string ActionHipIdentity(std::string alias)
{
	const auto at = ActionAimWord(alias);
	if (at != std::string_view::npos)
		alias.erase(at, 4);
	return alias;
}
inline bool ActionAliasAimed(const std::string& alias)
{
	return ActionAimWord(alias) != std::string_view::npos;
}

// Classifies a HUD animation alias. A prefix matches whole words only: "anm_reload" and
// "anm_reload_empty" match it, "anm_reloadx" does not. extraReloads lists further exact aliases the
// host game uses for reload steps.
inline ActionKind ClassifyActionAlias(const std::string& alias, const std::vector<std::string>& extraReloads = {})
{
	const auto family = [&](std::string_view base)
	{ return alias == base || (alias.size() > base.size() && alias.compare(0, base.size(), base) == 0 && alias[base.size()] == '_'); };
	if (family("anm_reload") || family("anm_open") || family("anm_add_cartridge") || family("anm_close"))
		return ActionKind::Reload;
	for (const auto& extra : extraReloads)
		if (alias == extra)
			return ActionKind::Reload;
	if (family("anm_ammo_check"))
		return ActionKind::AmmoCheck;
	if (family("anm_bore") || family("anm_inspect"))
		return ActionKind::Inspect;
	return ActionKind::None;
}

// "_empty" as a whole word of the alias: the empty-magazine variant of an action.
inline bool EmptyActionAlias(const std::string& alias)
{
	constexpr char word[] = "_empty";
	const auto at = alias.find(word);
	if (at == std::string::npos)
		return false;
	const auto end = at + sizeof(word) - 1;
	return end == alias.size() || alias[end] == '_';
}

// Clips of the action semantics package, in package order.
constexpr unsigned kAmmoCheckClip = 0, kReloadClip = 2, kEmptyReloadClip = 3;

// The semantics clip of an action. Inspection has no clip of its own and takes the reload one.
inline unsigned ActionSemanticsClip(ActionKind kind, bool empty)
{
	return kind == ActionKind::AmmoCheck ? kAmmoCheckClip : (empty ? kEmptyReloadClip : kReloadClip);
}

// Action kinds of a weapon's HUD motions, and the aimed aliases that may keep ADS.
class GenericActions
{
	struct Motion
	{
		ActionKind kind;
		std::string clip;
		std::map<std::string, ActionKind> aliases;
		bool aimed = false;
	};
	struct Selection
	{
		std::pair<std::uint64_t, std::uint64_t> identity;
		ActionKind kind;
		bool empty;
	};
	std::map<std::uint64_t, Motion> motions_;
	std::optional<Selection> selected_;
	std::optional<std::pair<std::uint64_t, std::uint64_t>> admitted_;
	std::map<std::string, std::string> aimedAliases_;

public:
	GenericActions() = default;
	explicit GenericActions(const std::vector<ActionAlias>& aliases, const std::vector<std::string>& extraReloads = {})
	{
		std::set<std::uint64_t> ambiguous;
		std::set<std::uint64_t> hipMotions;
		std::map<std::string, std::vector<std::uint64_t>> aliasMotions;
		std::set<std::string> unresolvedAliases;
		for (const auto& alias : aliases)
		{
			const auto kind = alias.resolved ? ClassifyActionAlias(alias.alias, extraReloads) : ActionKind::None;
			aliasMotions[alias.alias].push_back(alias.motion);
			if (!alias.resolved)
				unresolvedAliases.insert(alias.alias);
			const bool action = alias.resolved && kind != ActionKind::None;
			const auto result = motions_.try_emplace(alias.motion, Motion{ kind, alias.clip, {} });
			auto& motion = result.first->second;
			if (action)
				motion.aliases[alias.alias] = kind;
			if (action && ActionAliasAimed(alias.alias))
				motion.aimed = true;
			else
				hipMotions.insert(alias.motion);
			// Multiple action aliases can share one authored hand clip. An
			// idle/non-action collision is still unsafe to admit.
			if (!result.second && (kind == ActionKind::None || motion.kind == ActionKind::None))
				ambiguous.insert(alias.motion);
		}
		// A check sometimes aliases idle when the gun has no inspection clip; that shared motion is
		// not an action.
		for (auto it = motions_.begin(); it != motions_.end();)
		{
			if (it->second.kind == ActionKind::None || ambiguous.count(it->first))
				it = motions_.erase(it);
			else
			{
				it->second.aimed = it->second.aimed && !hipMotions.count(it->first);
				++it;
			}
		}
		// An alias selects randomly among its resolved variants. If even one
		// variant falls back or shares a hip/idle ID, do not retain ADS for its
		// other variants either. Propagate through aliases that share motions.
		const auto allAimed = [&](const std::vector<std::uint64_t>& motions)
		{
			bool all = !motions.empty();
			for (auto motion : motions)
				all = all && PreservesAim(motion);
			return all;
		};
		bool changed;
		do
		{
			changed = false;
			for (const auto& alias : aliasMotions)
			{
				if (!ActionAliasAimed(alias.first))
					continue;
				if (unresolvedAliases.count(alias.first) || !allAimed(alias.second))
					for (auto motion : alias.second)
					{
						const auto found = motions_.find(motion);
						if (found != motions_.end() && found->second.aimed)
						{
							found->second.aimed = false;
							changed = true;
						}
					}
			}
		} while (changed);
		for (const auto& alias : aliasMotions)
		{
			if (!ActionAliasAimed(alias.first) || unresolvedAliases.count(alias.first))
				continue;
			if (allAimed(alias.second))
				aimedAliases_.emplace(ActionHipIdentity(alias.first), alias.first);
		}
	}
	// The semantics clip of each action motion. Every alias of an action motion
	// is a resolved action alias; any of them that is an empty variant makes it one.
	std::map<std::uint64_t, unsigned> SemanticsClips() const
	{
		std::map<std::uint64_t, unsigned> clips;
		for (const auto& action : motions_)
		{
			bool empty = false;
			for (const auto& alias : action.second.aliases)
				empty |= EmptyActionAlias(alias.first);
			clips.emplace(action.first, ActionSemanticsClip(action.second.kind, empty));
		}
		return clips;
	}
	void Select(std::uint64_t motion, std::uint64_t generation, const std::string& alias)
	{
		selected_.reset();
		const auto it = motions_.find(motion);
		if (it == motions_.end())
			return;
		const auto found = it->second.aliases.find(alias);
		if (found == it->second.aliases.end())
			return;
		selected_ = Selection{ { motion, generation }, found->second, EmptyActionAlias(alias) };
	}
	ActionKind Kind(std::uint64_t motion) const
	{
		if (selected_ && selected_->identity.first == motion)
			return selected_->kind;
		const auto it = motions_.find(motion);
		return it == motions_.end() ? ActionKind::None : it->second.kind;
	}
	// The semantics clip of the motion, for the alias Select named.
	unsigned SemanticsCurve(std::uint64_t motion) const
	{
		return ActionSemanticsClip(Kind(motion), selected_ && selected_->identity.first == motion && selected_->empty);
	}
	bool HasMotion(std::uint64_t motion) const { return motions_.count(motion) != 0; }
	bool IsReload(std::uint64_t motion) const { return Kind(motion) == ActionKind::Reload; }
	bool SameClip(std::uint64_t a, std::uint64_t b) const
	{
		if (a == b)
			return true;
		const auto left = motions_.find(a), right = motions_.find(b);
		return left != motions_.end() && right != motions_.end() && !left->second.clip.empty() && left->second.clip == right->second.clip;
	}
	bool SameKind(std::uint64_t a, std::uint64_t b) const { return HasMotion(a) && HasMotion(b) && Kind(a) == Kind(b); }
	bool PreservesAim(std::uint64_t motion) const
	{
		const auto found = motions_.find(motion);
		return found != motions_.end() && found->second.aimed;
	}
	std::optional<std::string> AimedAliasFor(const std::string& requested) const
	{
		const auto found = aimedAliases_.find(ActionHipIdentity(requested));
		return found == aimedAliases_.end() ? std::nullopt : std::optional<std::string>(found->second);
	}
	bool Admit(std::uint64_t motion, std::uint64_t generation)
	{
		if (!HasMotion(motion))
			return false;
		if (selected_ && selected_->identity != std::make_pair(motion, generation))
			selected_.reset();
		const auto identity = std::make_pair(motion, generation);
		if (admitted_ && *admitted_ == identity)
			return false;
		admitted_ = identity;
		return true;
	}
};
}
