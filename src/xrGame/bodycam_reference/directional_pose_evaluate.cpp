// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "directional_pose_evaluate.h"
#include "constants.h"
#include "cycle_layer.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace BodycamReference
{
namespace
{
template <class Make>
const PoseCurveBundle& Memoized(std::map<unsigned, PoseCurveBundle>& memo, unsigned key, const Make& make)
{
	auto it = memo.find(key);
	if (it == memo.end())
		it = memo.emplace(key, make()).first;
	return it->second;
}
}
NamedCurves TwoWayCurves(const NamedCurves& a, const NamedCurves& b, float alpha)
{
	const float factor = 1.f - (1.f - alpha);
	if (std::abs(factor) <= kBlendEpsilon)
		return a;
	if (std::abs(factor - 1.f) <= kBlendEpsilon)
		return b;
	NamedCurves out;
	for (const auto& left : a)
	{
		const auto right = std::find_if(b.begin(), b.end(), [&](const NamedCurveEntry& e) { return e.name == left.name; });
		if (right == b.end())
			out.push_back({ left.name, left.value - left.value * factor, left.flags });
		else
			out.push_back({ left.name, (right->value - left.value) * factor + left.value, std::uint8_t(left.flags | right->flags) });
	}
	for (const auto& right : b)
		if (std::none_of(a.begin(), a.end(), [&](const NamedCurveEntry& e) { return e.name == right.name; }))
			out.push_back({ right.name, right.value * factor, right.flags });
	return out;
}
NamedCurves MultiWayCurves(const std::vector<WeightedNamedCurves>& samples)
{
	std::vector<WeightedNamedCurves> admitted;
	for (const auto& s : samples)
		if (s.weight > kBlendEpsilon)
			admitted.push_back(s);
	return admitted.empty() ? NamedCurves{} : BlendWeightedCurves(admitted);
}
NamedCurves TransitionCurves(const NamedCurves& a, const NamedCurves& b, float alpha)
{
	auto out = a;
	const float factor = 1.f - alpha;
	if (std::abs(factor - 1.f) > 1e-8f)
		for (auto& e : out)
			e.value *= factor;
	if (alpha > kBlendEpsilon)
		for (const auto& e : b)
		{
			auto& found = FindOrAddCurve(out, e.name);
			found.value = alpha * e.value + found.value;
			found.flags |= e.flags;
		}
	return out;
}
PoseCurveBundle EvaluateDirectionalInput(unsigned state, const DirectionEvaluationSnapshot& snapshot, TwoWayPoseState sprint,
	const std::function<PoseCurveBundle(unsigned)>& player, const PoseReadCache& cache, const std::function<PoseCurveBundle()>& reference)
{
	const auto twoWay = [](TwoWayPoseState node, const std::function<PoseCurveBundle()>& a, const std::function<PoseCurveBundle()>& b)
	{
		std::map<unsigned, PoseCurveBundle> memo;
		const auto child = [&](unsigned i) -> const PoseCurveBundle& { return Memoized(memo, i, [&] { return i == 0 ? a() : b(); }); };
		auto pose = TwoWayPackedPose(node.alpha, node.relevant[0], node.relevant[1], [&](unsigned i) { return child(i).pose; });
		NamedCurves curves;
		if (!node.relevant[1])
			curves = child(0).curves;
		else if (!node.relevant[0])
			curves = child(1).curves;
		else
			curves = TwoWayCurves(child(0).curves, child(1).curves, node.alpha);
		return PoseCurveBundle{ std::move(pose), std::move(curves) };
	};
	const auto forward = [&]() { return cache(kForwardCache, [&]() { return player(kDirectionPlayerNodes[0]); }); };
	const auto sprintPose = [&]()
	{
		return cache(kSprintCache, [&]()
			{ return twoWay(sprint, [&]() { return player(kSprintPlayerNodes[0]); }, [&]() { return player(kSprintPlayerNodes[1]); }); });
	};
	const auto gait = [&]()
	{
		std::map<unsigned, PoseCurveBundle> memo;
		const auto child = [&](unsigned i) -> const PoseCurveBundle&
		{ return Memoized(memo, i, [&] { return i == 0 ? forward() : sprintPose(); }); };
		auto pose = GaitPackedPose(snapshot.gaitWeights, [&](unsigned i) { return child(i).pose; }, [&]() { return reference().pose; });
		std::vector<WeightedNamedCurves> samples;
		for (unsigned i = 0; i < 2; ++i)
			if (snapshot.gaitWeights[i] > kBlendEpsilon)
				samples.push_back({ snapshot.gaitWeights[i], child(i).curves });
		auto curves = samples.size() == 1 && samples[0].weight >= kBlendFull ? samples[0].curves : MultiWayCurves(samples);
		return PoseCurveBundle{ std::move(pose), std::move(curves) };
	};
	std::map<unsigned, PoseCurveBundle> memo;
	const auto child = [&](unsigned i) -> const PoseCurveBundle&
	{
		const auto direction = kStateDirections[state][i];
		return Memoized(memo, i,
			[&]
			{
				return cache(kFirstDirectionCache + direction,
					[&]()
					{
						return direction == 0 ? twoWay({ snapshot.maskAlpha, snapshot.maskRelevant }, gait, forward)
											  : player(kDirectionPlayerNodes[direction]);
					});
			});
	};
	const auto weights = snapshot.multiWay[state].value();
	auto pose = MultiWayPackedPose(weights, [&](unsigned i) { return child(i).pose; }, [&]() { return reference().pose; });
	std::vector<WeightedNamedCurves> samples;
	for (unsigned i = 0; i < 4; ++i)
	{
		const auto found = memo.find(i);
		samples.push_back({ weights[i], found == memo.end() ? NamedCurves{} : found->second.curves });
	}
	return { std::move(pose), MultiWayCurves(samples) };
}
PoseCurveBundle EvaluateStatePoseStack(unsigned, unsigned current, const std::vector<LocomotionTransitionRecord>& records,
	const std::function<PoseCurveBundle(unsigned)>& source,
	const std::function<TransitionPoseProfile(const LocomotionTransitionRecord&, unsigned, std::size_t)>& profile)
{
	if (records.empty())
		return source(current);
	std::map<unsigned, PoseCurveBundle> memo;
	const auto root = [&](unsigned state) -> const PoseCurveBundle& { return Memoized(memo, state, [&] { return source(state); }); };
	PoseCurveBundle out;
	for (unsigned i = 0; i < records.size(); ++i)
	{
		const auto& r = records[i];
		if (i == 0)
			out = root(r.previous);
		const auto& next = root(r.next);
		const auto resolved = profile(r, i, out.pose.size());
		out.pose = TransitionPackedPose(out.pose, next.pose, r.alpha, resolved.indices, resolved.weights);
		out.curves = TransitionCurves(out.curves, next.curves, r.alpha);
	}
	out.pose = NormalizePackedPose(std::move(out.pose));
	return out;
}
}
