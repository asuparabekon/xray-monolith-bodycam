#include "locomotion_player_group.h"
#include "constants.h"
#include "cycle_layer.h"
#include <algorithm>
#include <stdexcept>
namespace BodycamReference
{
namespace
{
BoundLocomotionRecord RegistrationRecord(const PlayerAssetBinding& asset, PlayerGraphContext context, SyncPlayerKind kind)
{
	SyncRecord sync;
	sync.assetIdentity = asset.assetIdentity;
	sync.nodeMarkerIdentity = asset.nodeMarkerIdentity;
	sync.historyIdentity = asset.historyIdentity;
	sync.markerNames = asset.markerNames;
	sync.kind = kind;
	sync.weight = float(context.weight);
	return { asset.nodeId, std::move(sync), {}, 0, float(context.rootMotionWeight) };
}
void CheckRegistration(const PlayerAssetBinding& asset)
{
	if (asset.assetIdentity.empty() || asset.nodeMarkerIdentity.empty() || asset.historyIdentity.empty())
		throw std::invalid_argument("Player asset binding has an empty identity");
}
}
bool RegisterBlendPlayer(LocomotionPlayerState& state, const PlayerAssetBinding& asset, PlayerGraphContext context,
	const LocomotionPropertyState& properties, const std::function<void(BoundLocomotionRecord)>& emit)
{
	CheckRegistration(asset);
	state.blendWeight = float(context.weight);
	state.relevanceFlag = state.relevanceFlag || state.blendWeight >= kBlendFull;
	const float x = float(properties.strideBlend), y = float(properties.walkRunBlend), rate = float(properties.standingPlayRate);
	if (!asset.valid || !asset.skeletonValid)
		return false;
	auto record = RegistrationRecord(asset, context, SyncPlayerKind::BlendSpace);
	record.processedInputs = { x, y };
	record.recordRate = rate;
	emit(std::move(record));
	return true;
}
bool RegisterSprintPlayer(SprintPlayerState& state, const PlayerAssetBinding& asset, PlayerGraphContext context,
	const LocomotionPropertyState& properties, const std::function<void(BoundLocomotionRecord)>& emit)
{
	CheckRegistration(asset);
	state.blendWeight = float(context.weight);
	state.relevanceFlag = state.relevanceFlag || state.blendWeight >= kBlendFull;
	const float playRate = float(properties.standingPlayRate);
	if (!asset.valid || !asset.skeletonValid)
		return false;
	state.marker.time = std::clamp(state.marker.time, 0.f, kSprintCycleSeconds);
	const float rate = playRate / (asset.nodeId == kSprintPlayerNodes[1] ? .833f : 1.f);
	state.rateClampInitialized = true;
	auto record = RegistrationRecord(asset, context, SyncPlayerKind::Sequence);
	record.recordRate = rate;
	emit(std::move(record));
	return true;
}
void SprintPlayerState::Initialize(const std::function<void(SprintPlayerState&)>& exposed)
{
	auto next = *this;
	next.marker.previous.index = kNoMarker;
	next.marker.next.index = kNoMarker;
	next.relevanceFlag = false;
	exposed(next);
	next.marker.time = 0;
	next.rateClampInitialized = true;
	*this = next;
}
LocomotionPlayerGroup::LocomotionPlayerGroup(std::map<unsigned, LocomotionPlayer*> blend, std::map<unsigned, SprintPlayerState*> sprint,
	std::array<std::string, 2> names, std::array<float, 2> times)
	: blend_(std::move(blend)), sprint_(std::move(sprint)), names_(std::move(names)), times_(times)
{
}
void LocomotionPlayerGroup::BeginUpdate()
{
	bank_.BeginUpdate();
	records_.clear();
}
void LocomotionPlayerGroup::Register(BoundLocomotionRecord record)
{
	for (const auto& old : records_)
		if (old.sync.nodeMarkerIdentity == record.sync.nodeMarkerIdentity && old.nodeId != record.nodeId)
			throw std::invalid_argument("Two players share a marker identity");
	record.sync.registrationIndex = unsigned(records_.size());
	bank_.Register(record.sync);
	records_.push_back(std::move(record));
}
PlayerGroupTick LocomotionPlayerGroup::Tick(double dt, std::array<std::string, 2> identities)
{
	PlayerGroupTick out;
	if (records_.empty())
	{
		bank_.FinishTick();
		return out;
	}
	const auto view = [](const BlendMarkerNode& n)
	{
		return MarkerState{ n.normalizedTime, { n.previous.index, n.previous.offset.value_or(0) },
			{ n.next.index, n.next.offset.value_or(0) } };
	};
	std::map<std::string, MarkerState> nodes;
	for (const auto& r : records_)
		nodes[r.sync.nodeMarkerIdentity] =
			r.sync.kind == SyncPlayerKind::BlendSpace ? view(blend_.at(r.nodeId)->State().node) : sprint_.at(r.nodeId)->marker;
	out.preparation = bank_.Prepare(nodes, identities);
	nodes = out.preparation->preparation.nodeStates;
	out.candidates = TickLocomotionCandidates(out.preparation->current, out.preparation->previous, dt, nodes,
		[&](const SyncRecord& sync, CandidateContext context, std::map<std::string, MarkerState>& map)
		{
			// An output reports its context as the next player receives it, after the candidate loop has edited it.
			if (!out.outputs.empty())
				out.outputs.back().context = context;
			const auto& r = records_.at(sync.registrationIndex);
			const auto prepared = map.at(sync.nodeMarkerIdentity);
			if (sync.kind == SyncPlayerKind::BlendSpace)
			{
				auto& player = *blend_.at(r.nodeId);
				auto& node = player.State().node;
				// Group preparation resets indices only; unavailable offsets remain unavailable.
				node.previous.index = prepared.previous.index;
				node.next.index = prepared.next.index;
				const auto tick = player.Tick(r.processedInputs, r.recordRate, std::move(context));
				context = tick.markers.context;
				map[sync.nodeMarkerIdentity] = view(tick.markers.node);
			}
			else
			{
				auto& player = *sprint_.at(r.nodeId);
				player.marker = prepared;
				const auto tick = TickSprintMarkers(names_, times_, player.marker, r.recordRate, std::move(context));
				player.marker = tick.state;
				player.history = tick.history;
				context = tick.context;
				map[sync.nodeMarkerIdentity] = tick.state;
			}
			out.outputs.push_back({ r.nodeId, sync.registrationIndex, context });
			return context;
		});
	bank_.Commit(out.candidates->publication);
	bank_.FinishTick();
	out.group = bank_.Published();
	return out;
}
}
