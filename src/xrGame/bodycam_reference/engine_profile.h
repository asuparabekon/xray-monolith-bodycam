#pragma once
#include "engine_basis.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace BodycamReference
{
inline void ValidateModelTransform(const Matrix& transform)
{
	auto orientation = transform;
	orientation[12] = orientation[13] = orientation[14] = 0;
	EngineBasis validatedOrientation(orientation);
}
struct EngineCharacterAssetPaths
{
	std::string upper, locomotion, rifle, recoil, movement, equipment, idle, regrip;
};
struct NamedArmBone
{
	unsigned reference;
	std::string engine;
};
struct ArmCalibration
{
	std::string motion;
	float seconds = 0;
	std::vector<NamedArmBone> bones;
	Matrix referenceFromModel{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
};
struct EngineCharacterProfile
{
	EngineCharacterAssetPaths assets;
	EngineBasis basis;
	ArmCalibration right, left;
};

// The VFS/INI adapter supplies strings. Validate the complete profile here,
// before touching an active rig. Bone names come from the decoded rifle bank.
class EngineProfileReader
{
public:
	using Value = std::function<std::string(const std::string&, const std::string&)>;
	using Entries = std::function<std::vector<std::pair<std::string, std::string>>(const std::string&)>;
	EngineProfileReader(Value value, Entries entries) : value_(std::move(value)), entries_(std::move(entries)) {}
	EngineCharacterAssetPaths Paths(const std::string& section) const
	{
		return { Text(section, "upper"), Text(section, "locomotion"), Text(section, "rifle"), Text(section, "recoil"),
			Text(section, "movement"), Text(section, "equipment"), Text(section, "idle"), Text(section, "regrip") };
	}
	EngineCharacterProfile Read(const std::string& section, const std::vector<std::string>& physicalBones) const
	{
		return { Paths(section), Basis(section), Arm(section, "right", physicalBones), Arm(section, "left", physicalBones) };
	}
	EngineBasis Basis(const std::string& section) const
	{
		const auto x = Numbers<3>(section, "basis_x"), y = Numbers<3>(section, "basis_y"), z = Numbers<3>(section, "basis_z");
		return EngineBasis({ x[0], x[1], x[2], 0, y[0], y[1], y[2], 0, z[0], z[1], z[2], 0, 0, 0, 0, 1 });
	}
	Matrix ModelTransform(const std::string& section, const std::string& key) const
	{
		const auto result = Numbers<16>(section, key);
		ValidateModelTransform(result);
		return result;
	}

private:
	static bool Blank(const std::string& text) { return text.find_first_not_of(" \t\r\n") == std::string::npos; }
	std::string Text(const std::string& section, const std::string& key) const
	{
		auto text = value_(section, key);
		if (Blank(text))
			throw std::invalid_argument("Empty reference profile field: " + section + "." + key);
		return text;
	}
	template <std::size_t N>
	std::array<double, N> Numbers(const std::string& section, const std::string& key) const
	{
		auto text = Text(section, key);
		std::replace(text.begin(), text.end(), ',', ' ');
		std::istringstream stream(text);
		stream.imbue(std::locale::classic());
		std::array<double, N> result;
		for (auto& value : result)
			if (!(stream >> value) || !std::isfinite(value))
				throw std::invalid_argument("Invalid numeric reference profile field: " + section + "." + key);
		stream >> std::ws;
		if (!stream.eof())
			throw std::invalid_argument("Trailing numeric reference profile values: " + section + "." + key);
		return result;
	}
	ArmCalibration Arm(const std::string& section, const std::string& side, const std::vector<std::string>& names) const
	{
		ArmCalibration result;
		result.motion = Text(section, side + "_motion");
		const double time = Numbers<1>(section, side + "_seconds")[0];
		if (time < 0 || time > std::numeric_limits<float>::max())
			throw std::invalid_argument("Calibration time outside native float range: " + side);
		result.seconds = float(time);
		result.referenceFromModel = Numbers<16>(section, side + "_model_transform");
		ValidateModelTransform(result.referenceFromModel);
		for (const auto& entry : entries_(Text(section, side + "_bones")))
		{
			const auto found = std::find(names.begin(), names.end(), entry.first);
			if (found == names.end())
				throw std::invalid_argument("Unknown physical reference bone: " + entry.first);
			if (Blank(entry.second))
				throw std::invalid_argument("Empty engine bone for reference bone: " + entry.first);
			const auto index = unsigned(found - names.begin());
			for (const auto& bone : result.bones)
				if (bone.reference == index || bone.engine == entry.second)
					throw std::invalid_argument("Duplicate arm bone correspondence: " + entry.first);
			result.bones.push_back({ index, entry.second });
		}
		if (result.bones.empty())
			throw std::invalid_argument("Empty arm calibration: " + side);
		return result;
	}
	Value value_;
	Entries entries_;
};
}
