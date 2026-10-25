#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace BodycamReference
{
// Script layers (.anm names) the host skips while Reference drives the hands, because Reference poses
// what they would add itself. Every other layer, and every layer outside Reference, applies.
struct ScriptLayerPolicy
{
	std::vector<std::string> referenceSkipped;

	bool Apply(bool reference, std::string_view name) const
	{
		if (!reference)
			return true;
		for (const auto& skipped : referenceSkipped)
			if (name == skipped)
				return false;
		return true;
	}
};
}
