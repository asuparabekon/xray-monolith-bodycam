#pragma once
#include "movement_vibration.h"
#include <vector>
namespace BodycamReference
{
struct MouseShakeRaw
{
	Vector location{};
	Rotation rotation;
	std::array<double, 7> phase{}; // pitch, yaw, roll, x, y, z, fov: random draw order.
	float remaining = .2f, envelope = 0;
	bool finished = false;
};
// One camera shake started by mouse movement.
class MouseShake
{
public:
	explicit MouseShake(const std::function<unsigned()>& draw15);
	const MouseShakeRaw& Update(double deltaSeconds);
	const MouseShakeRaw& State() const { return state_; }

private:
	MouseShakeRaw state_;
	float inTime_ = 0, outTime_ = 0;
	bool inFlag_ = true, outFlag_ = false;
};
struct MouseShakeEntry
{
	unsigned id;
	float scale;
	MouseShake shake;
};
class MouseShakeLayer
{
public:
	explicit MouseShakeLayer(std::function<unsigned()> draw15);
	void InputFrame(double lookX, double lookY, double worldDelta, double speedCm);
	CameraPov Update(const CameraPov& base, double deltaSeconds);
	unsigned Events() const { return events_; }
	const std::vector<MouseShakeEntry>& Instances() const { return instances_; }

private:
	std::function<unsigned()> draw15_;
	std::vector<MouseShakeEntry> instances_;
	bool previousNonzero_ = false;
	unsigned events_ = 0;
	double emitWait_ = 0; // Seconds until the next shake may start.
};
}
