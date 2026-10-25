// Operation order and float width reproduce the source bit for bit, except that new shakes are capped at 30 per second.
#include "mouse_shake.h"
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
namespace
{
constexpr float kRandomPhaseScale = 0x1.9222dap-13f; // float(2 pi / 32767)
constexpr double kLookActuationSquared = 0x1.5798ecp-27;
constexpr float kBlendSeconds = .1f;
// New shakes start at most this often, so the stacked shake does not grow with frame rate.
// At 30 fps or less every motion frame still starts one, as in the source.
constexpr double kEmitSeconds = 1. / 30;
}
MouseShake::MouseShake(const std::function<unsigned()>& draw15)
{
	// The z oscillator starts at phase zero; every other axis starts at a random phase.
	for (unsigned i = 0; i < 7; ++i)
		if (i != 5)
			state_.phase[i] = float(double(draw15()) * kRandomPhaseScale);
}
const MouseShakeRaw& MouseShake::Update(double delta)
{
	const float dt = float(delta);
	if (state_.remaining > 0)
		state_.remaining = std::max(state_.remaining - dt, 0.f);
	if (inFlag_)
		inTime_ += dt;
	if (outFlag_)
		outTime_ += dt;
	if (state_.remaining > 0 && state_.remaining < kBlendSeconds)
	{
		outFlag_ = true;
		outTime_ = kBlendSeconds - state_.remaining;
	}
	if (inFlag_ && inTime_ > kBlendSeconds)
		inFlag_ = false;
	bool exhausted = state_.remaining <= 0;
	if (outFlag_ && outTime_ > kBlendSeconds)
	{
		outTime_ = kBlendSeconds;
		exhausted = true;
	}
	state_.envelope = exhausted ? 0 : std::min(outFlag_ ? 1.f - outTime_ / kBlendSeconds : 1.f, inFlag_ ? inTime_ / kBlendSeconds : 1.f);
	state_.location = {};
	state_.rotation = {};
	state_.finished = state_.remaining <= 0;
	if (state_.envelope > 0)
	{
		const auto oscillate = [&](unsigned i, float amplitude, float frequency, bool perlin)
		{
			state_.phase[i] += float(dt * frequency);
			const float wave = perlin ? Perlin1D(float(state_.phase[i])) : float(std::sin(state_.phase[i]));
			return wave * amplitude;
		};
		state_.location = { 0, double(oscillate(4, .2f, 0, false)) * state_.envelope,
			double(oscillate(5, 5, .3f, false)) * state_.envelope };
		state_.rotation = { float(oscillate(0, .3f, .1f, false) * state_.envelope), float(oscillate(1, .2f, 2, true) * state_.envelope),
			float(oscillate(2, .5f, 2, true) * state_.envelope) };
	}
	return state_;
}
MouseShakeLayer::MouseShakeLayer(std::function<unsigned()> draw15) : draw15_(std::move(draw15)) {}
void MouseShakeLayer::InputFrame(double x, double y, double dt, double speed)
{
	const bool nonzero = ((x * x) + (y * y)) + 0 >= kLookActuationSquared, event = nonzero || previousNonzero_;
	previousNonzero_ = nonzero;
	if (!event)
	{
		emitWait_ = 0; // The first motion frame starts a shake at once.
		return;
	}
	if (emitWait_ > 0)
	{
		emitWait_ -= dt;
		if (emitWait_ > 0)
			return;
	}
	emitWait_ = std::max(emitWait_ + kEmitSeconds, 0.);
	const double inverse = dt == 0 ? 0 : 1 / dt, yaw = (x * inverse) / 50, pitch = (y * inverse) / 50;
	const float eventScale =
		float(std::clamp(((std::abs(pitch) + std::abs(yaw)) - 1) / 3, 0., 1.) * 3 + std::clamp(speed / 600, 0., 1.) * 1.5);
	instances_.push_back({ ++events_, eventScale * .5f, MouseShake(draw15_) });
}
CameraPov MouseShakeLayer::Update(const CameraPov& base, double dt)
{
	auto result = base;
	instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
						 [&](MouseShakeEntry& entry)
						 {
							 const auto& raw = entry.shake.Update(dt);
							 if (raw.finished)
								 return true;
							 result = ApplyCameraShake(result, raw.location, raw.rotation, entry.scale);
							 return false;
						 }),
		instances_.end());
	return result;
}
}
