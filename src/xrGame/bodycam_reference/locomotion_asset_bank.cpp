#include "locomotion_asset_bank.h"
#include "asset_reader.h"
#include <stdexcept>
namespace BodycamReference
{
namespace
{
std::vector<std::string> Names(ReferenceAssetReader& r, unsigned maximum)
{
	std::vector<std::string> names(r.Count(maximum));
	for (auto& name : names)
		name = r.String();
	return names;
}
CompressedLocomotionCurves Curves(ReferenceAssetReader& r)
{
	const auto names = Names(r, 256);
	const auto bytes = r.Bytes(r.Count(1024 * 1024));
	return CompressedLocomotionCurves(bytes, names);
}
PlayerAssetBinding Binding(ReferenceAssetReader& r)
{
	PlayerAssetBinding b;
	b.nodeId = r.U32();
	b.assetIdentity = r.String();
	b.nodeMarkerIdentity = r.String();
	b.historyIdentity = r.String();
	b.markerNames = Names(r, 2);
	return b;
}
void Links(ReferenceAssetReader& r, std::vector<NormalStateLink>& links)
{
	links.resize(r.Count(32));
	for (auto& link : links)
	{
		link.transition = r.U32();
		link.delegate = r.I32();
		link.desired = r.Bool();
		link.automatic = r.Bool();
	}
}
template <class T>
void Transitions(ReferenceAssetReader& r, T& machine)
{
	machine.transitions.resize(r.Count(64));
	for (auto& t : machine.transitions)
	{
		t.previous = r.U32();
		t.next = r.U32();
		t.duration = r.F64();
		t.inertial = r.Bool();
	}
}
std::vector<LocomotionCurveKey> Keys(ReferenceAssetReader& r)
{
	std::vector<LocomotionCurveKey> out(r.Count(4096));
	for (auto& k : out)
	{
		k.time = r.F32();
		k.value = r.F32();
		k.arrive = r.F32();
		k.leave = r.F32();
		k.linear = r.Bool();
	}
	return out;
}
template <class Map, class Key, class Value>
void Insert(Map& map, Key key, Value value)
{
	if (!map.emplace(std::move(key), std::move(value)).second)
		throw std::invalid_argument("Duplicate locomotion asset binding");
}
RegionalValue Value(ReferenceAssetReader& r)
{
	RegionalValue value;
	value.constant = r.F64();
	value.property = r.String();
	return value;
}
RegionalManifest ReadRegional(ReferenceAssetReader& r)
{
	RegionalManifest m;
	m.standingAsset = r.String();
	m.assemblyBase = r.String();
	const auto properties = r.Count(64);
	for (unsigned i = 0; i < properties; ++i)
	{
		auto key = r.String();
		auto value = r.String();
		Insert(m.propertyInputs, std::move(key), std::move(value));
	}
	m.regions.resize(r.Count(64));
	for (auto& region : m.regions)
	{
		region.name = r.String();
		region.alphaCurve = r.String();
		region.mesh = r.Bool();
		region.additiveAlpha = Value(r);
	}
	m.assembly.resize(r.Count(64));
	for (auto& a : m.assembly)
	{
		a.node = r.U32();
		a.input = r.String();
		a.meshRotation = r.Bool();
		a.meshScale = r.Bool();
		a.weights.resize(r.Count(64));
		for (auto& weight : a.weights)
			weight = Value(r);
		a.mask.resize(r.Count(kSourceBoneCount));
		for (auto& e : a.mask)
		{
			e.skeletonIndex = r.U32();
			e.sourceIndex = r.U32();
			e.weight = r.F64();
		}
	}
	return m;
}
}
LocomotionAssetBank LocomotionAssetBank::Load(const std::vector<std::uint8_t>& bytes)
{
	ReferenceAssetReader r(bytes);
	r.ExpectHeader("BCRLOCO3", 3);
	LocomotionAssetBank out;
	auto& c = out.cycle_;
	auto& u = out.upper_;
	for (auto& k : c.changeDirection)
	{
		k.value = float(r.F64());
		k.arrive = float(r.F64());
		k.leave = float(r.F64());
	}
	for (auto& id : c.cacheOrder)
		id = r.U32();
	c.profileIndices.resize(r.Count(kSkeletonBoneCount));
	for (auto& v : c.profileIndices)
		v = r.I32();
	c.profileScales.resize(r.Count(179));
	for (auto& v : c.profileScales)
		v = r.F64();
	const auto sequenceCount = r.Count(128);
	for (unsigned i = 0; i < sequenceCount; ++i)
	{
		auto name = r.String();
		MarkerSequence s;
		s.length = float(r.F64());
		s.rateScale = float(r.F64());
		for (unsigned j = 0; j < 2; ++j)
		{
			s.names[j] = r.String();
			s.times[j] = float(r.F64());
		}
		Insert(c.sequences, std::move(name), std::move(s));
	}
	for (unsigned i = 0; i < 6; ++i)
	{
		CycleBlendAsset a;
		a.binding = Binding(r);
		a.startPosition = r.F32();
		for (auto& v : a.filterWindows)
			v = r.F32();
		for (auto& s : a.sequences)
		{
			s.identity = r.String();
			s.rateScale = r.F32();
		}
		for (auto& p : a.grid)
		{
			for (auto& v : p.indices)
				v = r.I32();
			for (auto& v : p.weights)
				v = r.F32();
		}
		const auto id = a.binding.nodeId;
		Insert(c.blend, id, std::move(a));
	}
	for (unsigned i = 0; i < 2; ++i)
	{
		auto b = Binding(r);
		const auto id = b.nodeId;
		Insert(c.sprint, id, std::move(b));
	}
	const auto curveCount = r.Count(128);
	for (unsigned i = 0; i < curveCount; ++i)
	{
		const auto id = r.U32();
		auto key = r.String();
		auto curves = Curves(r);
		const auto names = Names(r, 256);
		if (id)
			Insert(c.sprintCurves, id, std::move(curves));
		else
		{
			Insert(c.curves, key, std::move(curves));
			Insert(c.admittedCurves, std::move(key), std::set<std::string>(names.begin(), names.end()));
		}
	}
	for (auto& s : u.normal.states)
	{
		Links(r, s.links);
		s.playerNodes.resize(r.Count(32));
		for (auto& v : s.playerNodes)
			v = r.U32();
	}
	Transitions(r, u.normal);
	for (auto* m : { &u.stop, &u.detail })
	{
		for (auto& s : m->states)
		{
			s.conduit = r.Bool();
			r.I32(); // State start notify; nothing consumes it.
			Links(r, s.links);
		}
		Transitions(r, *m);
	}
	for (auto& p : u.leanGrid)
	{
		for (auto& v : p.indices)
			v = r.I32();
		for (auto& v : p.weights)
			v = r.F32();
	}
	for (auto& s : u.leanIdentities)
		s = r.String();
	for (unsigned i = 0; i < 4; ++i)
		Insert(u.detailCurves, i, Curves(r));
	for (unsigned i : { 3, 4 })
		Insert(u.rotateCurves, i, Curves(r));
	for (auto& s : u.walkIdentities)
		s = r.String();
	u.parents.resize(r.Count(kSkeletonBoneCount));
	for (auto& v : u.parents)
		v = r.I32();
	for (auto& mask : u.stopWeights)
	{
		mask.resize(u.parents.size());
		for (auto& v : mask)
			v = r.F32();
	}
	for (unsigned i = 0; i < 3; ++i)
	{
		const bool oscillate = r.Bool();
		out.properties_.emplace_back(Keys(r), oscillate);
	}
	for (auto& axis : out.axes_)
		axis = Keys(r);
	out.regional_ = ReadRegional(r);
	out.walking_.resize(r.Count(6));
	for (auto& curve : out.walking_)
	{
		curve.name = r.String();
		curve.keys.resize(r.Count(93));
		for (auto& key : curve.keys)
		{
			key.time = r.F32();
			key.value = r.F32();
		}
	}
	if (!r.Done() || u.parents.size() != kSkeletonBoneCount || c.profileIndices.size() != kSkeletonBoneCount)
		throw std::invalid_argument("Locomotion package has trailing data or not 89 bones");
	// The curve constructors validate the curve shapes; build them once so a bad package fails here.
	(void)out.Properties();
	(void)out.Rotations();
	(void)out.Walking();
	// The virtual bones sit after the source skeleton's non-virtual bones.
	std::vector<unsigned> mapping;
	for (unsigned i = 0; i < kSkeletonBoneCount; ++i)
		mapping.push_back(i < kPhysicalBoneCount ? i : i + (kSourceBoneCount - kSkeletonBoneCount));
	(void)RegionalPose(out.regional_, u.parents, std::move(mapping));
	return out;
}
GroundedLocomotionProperties LocomotionAssetBank::Properties() const
{
	return GroundedLocomotionProperties(properties_[0], properties_[1], properties_[2]);
}
}
