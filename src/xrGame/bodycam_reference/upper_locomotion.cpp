#include "upper_locomotion.h"
#include "directional_cache.h"
#include <utility>

namespace BodycamReference
{
bool CurrentOrPreviousGraphCounter(GraphEpoch previous, GraphEpoch current, std::int64_t global)
{
	if (previous.counter != kNoGraphCounter && previous.frame != -1 && previous.counter == current.counter)
		return true;
	return global != -1 && NextGraphCounter(previous.counter) == current.counter;
}
namespace
{
std::optional<CachedLocomotionContext> WinningLocomotionContext(const std::vector<CachedLocomotionContext>& contexts)
{
	std::optional<CachedLocomotionContext> winner;
	for (const auto& c : contexts)
		if (!winner || c.weight > winner->weight)
			winner = c;
	return winner;
}
}
UpperLocomotionGraph::UpperLocomotionGraph(NormalMachineDefinition normal, ChildMachineDefinition stop, ChildMachineDefinition detail,
	UpperGraphHooks hooks, std::vector<LocomotionMachine*> extra)
	: hooks_(std::move(hooks)), normal_(std::move(normal), [this](const auto& change) { OnNormalStateChange(change); }),
	  stop_(std::move(stop)), detail_(std::move(detail)), owners_{ &normal_, &stop_, &detail_ }
{
	if (!hooks_.initializeStopState)
		hooks_.initializeStopState = [](unsigned, const auto& initialize) { initialize(); };
	owners_.insert(owners_.end(), extra.begin(), extra.end());
}
void UpperLocomotionGraph::OnNormalStateChange(const NormalStateChange& change)
{
	changeDuringUpdate_(change);
}
UpperGraphTrace UpperLocomotionGraph::Update(const UpperGraphInput& in)
{
	const float dt = float(in.dt);
	UpperGraphTrace trace;
	const auto emit = [&](UpperGraphEvent event)
	{
		if (in.recordEvents)
		{
			static const char* names[] = { "initialize-machine", "initialize-outer417", "cache-initialize-request", "normal-player-reset",
				"initialize-normal-state", "normal-state-update", "stop-state-update", "flush-cache", "initialize-detail-state",
				"detail-state-update", "update-outer417" };
			event.kind = names[static_cast<unsigned>(event.type)];
			trace.events.push_back(event);
		}
		if (hooks_.onEvent)
			hooks_.onEvent(event);
	};
	const auto curve = [&](const char* name) { return CurveValue(*in.priorCurves, name); };
	for (auto* owner : owners_)
	{
		owner->RecordDiagnostics(in.recordEvents);
		owner->PrepareProxyUpdate();
	}
	const std::array<const char*, 4> names{ { "normal", "stop", "detail", "cycles" } };
	std::function<void(unsigned, const std::string&)> initializeMachine;
	std::function<void(int, const std::string&)> cacheInitialize;
	std::function<void(unsigned, const std::string&)> initializeStopRoot;
	initializeMachine = [&](unsigned machine, const std::string& reason)
	{
		first_[machine] = true;
		UpperGraphEvent e;
		e.type = UpperGraphEventKind::InitializeMachine;
		e.machineIndex = machine;
		if (in.recordEvents)
			e.machine = names[machine];
		e.reason = reason;
		emit(e);
		if (machine == 3)
		{
			e.type = UpperGraphEventKind::InitializeCycles;
			e.machine.clear();
			emit(e);
			return;
		}
		owners_[machine]->Reinitialize();
		if (machine == 1)
			initializeStopRoot(0, reason + ":stop-initial-state");
		if (machine == 2)
			cacheInitialize(kCyclesCacheNode, reason + ":detail-initial-state");
	};
	cacheInitialize = [&](int id, const std::string& reason)
	{
		const bool initialized = hooks_.resolveCacheInitialize(id, reason, in.epoch);
		UpperGraphEvent e;
		e.type = UpperGraphEventKind::CacheInitializeRequest;
		e.cacheId = id;
		e.reason = reason;
		e.accepted = initialized;
		emit(e);
		if (initialized)
			initializeMachine(id == kDetailCacheNode ? 2 : 3, reason);
	};
	initializeStopRoot = [&](unsigned state, const std::string& reason)
	{ hooks_.initializeStopState(state, [&] { cacheInitialize(kDetailCacheNode, reason); }); };
	const auto machineUpdate = [&](unsigned machine)
	{
		if (!first_[machine] && last_[machine] && !CurrentOrPreviousGraphCounter(*last_[machine], in.epoch, in.globalFrameMarker) &&
			in.relevancyResetEnabled)
			initializeMachine(machine, "native-relevance-gap");
		last_[machine] = in.epoch;
		first_[machine] = false;
	};
	changeDuringUpdate_ = [&](const NormalStateChange& change)
	{
		UpperGraphEvent e;
		e.type = UpperGraphEventKind::NormalPlayerReset;
		e.change = change;
		emit(e);
		if (change.initialize)
		{
			e = {};
			e.type = UpperGraphEventKind::InitializeNormalState;
			e.state = int(change.state);
			emit(e);
			if (change.state == 1)
				cacheInitialize(kDetailCacheNode, "normal-enter-moving");
			else if (change.state == 2)
				initializeMachine(1, "normal-enter-stop");
		}
	};
	// The callback captures this frame's locals; drop it when the update ends.
	struct ClearChange
	{
		std::function<void(const NormalStateChange&)>& callback;
		~ClearChange() { callback = nullptr; }
	} clearChange{ changeDuringUpdate_ };
	if (in.normalContext)
	{
		machineUpdate(0);
		auto inputs = in.normalInputs();
		trace.normal = normal_.Update(dt, in.shouldMove, inputs.rotateLeft, inputs.rotateRight, std::move(inputs.automatic),
			in.normalContext->weight, in.normalContext->activeBranch);
		for (const auto& context : trace.normal->machine.contextUpdates)
		{
			CachedLocomotionContext c{ context };
			UpperGraphEvent e;
			e.type = UpperGraphEventKind::NormalStateUpdate;
			e.context = c;
			emit(e);
			if (c.state == 1)
			{
				c.source = "normal-moving";
				trace.detailQueue.push_back(c);
			}
			else if (c.state == 2)
			{
				machineUpdate(1);
				auto result = stop_.Update(dt, curve("Feet_Position"), c.weight, c.activeBranch);
				for (unsigned state : result.machine.initializedStates)
					initializeStopRoot(state, "stop-enter-" + std::to_string(state));
				for (const auto& stop : result.machine.contextUpdates)
				{
					CachedLocomotionContext sc{ stop };
					e = {};
					e.type = UpperGraphEventKind::StopStateUpdate;
					e.context = sc;
					emit(e);
					sc.source = "stop";
					trace.detailQueue.push_back(sc);
				}
				trace.stop.push_back(std::move(result));
			}
		}
	}
	trace.detailWinner = WinningLocomotionContext(trace.detailQueue);
	if (trace.detailWinner)
	{
		UpperGraphEvent e;
		e.type = UpperGraphEventKind::FlushCache;
		e.cacheId = kDetailCacheNode;
		e.context = *trace.detailWinner;
		emit(e);
		machineUpdate(2);
		auto inputs = in.detailInputs();
		inputs.publishedWeightGait = curve("Weight_Gait");
		trace.detail = detail_.Update(dt, inputs, trace.detailWinner->weight, trace.detailWinner->activeBranch);
		for (unsigned state : trace.detail->machine.initializedStates)
		{
			e = {};
			e.type = UpperGraphEventKind::InitializeDetailState;
			e.state = int(state);
			emit(e);
			cacheInitialize(kCyclesCacheNode, "detail-enter-" + std::to_string(state));
		}
		for (const auto& context : trace.detail->machine.contextUpdates)
		{
			CachedLocomotionContext c{ context };
			e = {};
			e.type = UpperGraphEventKind::DetailStateUpdate;
			e.context = c;
			emit(e);
			trace.cyclesQueue.push_back(c);
		}
	}
	trace.cyclesWinner = WinningLocomotionContext(trace.cyclesQueue);
	if (trace.cyclesWinner)
	{
		UpperGraphEvent e;
		e.type = UpperGraphEventKind::FlushCache;
		e.cacheId = kCyclesCacheNode;
		e.context = *trace.cyclesWinner;
		emit(e);
		machineUpdate(3);
		trace.outer417 = trace.cyclesWinner;
		e = {};
		e.type = UpperGraphEventKind::UpdateCycles;
		e.context = *trace.cyclesWinner;
		e.epoch = in.epoch;
		e.deltaTime = dt;
		emit(e);
	}
	pending_ = true;
	return trace;
}
bool UpperLocomotionGraph::Publish()
{
	if (!pending_)
		return false;
	for (auto* owner : owners_)
		owner->PublishProxyUpdate();
	pending_ = false;
	return true;
}
}
