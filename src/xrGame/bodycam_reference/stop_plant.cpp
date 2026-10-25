#include "stop_plant.h"
#include "constants.h"
#include <stdexcept>

namespace BodycamReference
{
namespace
{
constexpr StopEvaluator kPlantEvaluators[2][6] = { { { 573, StopAsset::F, .133 }, { 572, StopAsset::B, .133 }, { 570, StopAsset::LF, .2 },
													   { 568, StopAsset::LB, .133 }, { 566, StopAsset::RF, .233 },
													   { 565, StopAsset::RB, .233 } },
	{ { 560, StopAsset::F, .7 }, { 559, StopAsset::B, .7 }, { 557, StopAsset::LF, .8 }, { 555, StopAsset::LB, .8 },
		{ 553, StopAsset::RF, .766 }, { 552, StopAsset::RB, .7 } } };
unsigned PlantIndex(unsigned state)
{
	if (state != 5 && state != 6)
		throw std::invalid_argument("Stop plant state must be 5 or 6");
	return state - 5;
}
}
const StopEvaluatorSet& StopPlantEvaluators(unsigned state)
{
	return kPlantEvaluators[PlantIndex(state)];
}
void StopPlantDispatch::InitializeState(unsigned state, const EventSink& sink)
{
	if (sink)
		sink({ StopEventKind::InitializeCache, state, kDetailCacheNode, {}, {} });
	if (state < 5)
		return;
	auto& plant = plants_[state - 5];
	plant.weights = FourWayWeights(plant.desired);
	if (sink)
		sink({ StopEventKind::InitializeMultiWay, state, state == 5 ? 571u : 558u, {}, {} });
	for (unsigned i = 0; i < 6; ++i)
	{
		if (i == 2)
			plant.left.Initialize();
		if (i == 4)
			plant.right.Initialize();
		const auto& e = kPlantEvaluators[state - 5][i];
		if (sink)
			sink({ StopEventKind::InitializeEvaluator, state, e.node, StopEvaluatorUpdate{ e, 0, false, {}, false }, {} });
	}
}
StopPlantTrace StopPlantDispatch::Update(
	const std::vector<LocomotionStateUpdate>& contexts, std::array<float, 4> velocity, unsigned hips, double dt, const EventSink& sink)
{
	StopPlantTrace out;
	out.weights = FourWayWeights(velocity);
	for (const auto& context : contexts)
	{
		if (context.state >= 5)
		{
			auto& plant = plants_[context.state - 5];
			plant.layerRelevant = true;
			plant.desired = velocity;
			plant.weights = out.weights;
			const auto emit = [&](unsigned index, float weight, bool active, std::optional<unsigned> child, bool zero)
			{
				auto e = kPlantEvaluators[context.state - 5][index];
				e.time = float(e.time);
				out.evaluators.push_back({ e, weight, active, child, zero });
				if (sink)
					sink({ StopEventKind::UpdateEvaluator, context.state, e.node, out.evaluators.back(), {} });
			};
			for (unsigned i = 0; i < 4; ++i)
				if (out.weights[i] > kBlendEpsilon)
				{
					const float weight = context.weight * out.weights[i];
					if (i < 2)
						emit(i, weight, context.activeBranch, {}, false);
					else
					{
						auto& blend = i == 2 ? plant.left : plant.right;
						const auto trace = blend.Update(hips, dt, weight, context.activeBranch);
						for (const auto& child : trace.updates)
							emit(2 + (i - 2) * 2 + child.child, child.weight, child.activeBranch, child.child, child.outgoingZero);
					}
				}
		}
		// The layer visits its relevant plant children before the shared Detail base.
		out.detailQueue.push_back(context);
		if (sink)
			sink({ StopEventKind::UpdateCache, context.state, kDetailCacheNode, {}, context });
	}
	return out;
}
StopPlantSnapshot StopPlantDispatch::EvaluationSnapshot(unsigned state) const
{
	const auto& p = plants_[PlantIndex(state)];
	return { p.weights.value(), p.left.Weights(), p.right.Weights(), p.layerRelevant };
}
}
