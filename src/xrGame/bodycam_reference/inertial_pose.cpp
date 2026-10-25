// Operation order and float width reproduce the source bit for bit; the replay tests cover them.
#include "inertial_pose.h"
#include "constants.h"
#include "transform.h"
#include <algorithm>
#include <cmath>
namespace BodycamReference
{
namespace
{
constexpr float kTwoPiF = 2 * kPiF;
// QuaternionProduct in the source operation order.
Quaternion SourceQuaternionProduct(Quaternion a, Quaternion b)
{
	const double x = b[0], y = b[1], z = b[2], w = b[3], px = a[0], py = a[1], pz = a[2], pw = a[3];
	return { ((w * px + x * pw) + z * py) - y * pz, ((y * pw - z * px) + w * py) + x * pz, ((y * px + z * pw) - x * py) + w * pz,
		((w * pw - x * px) - y * py) - z * pz };
}
template <class T>
T Wrap(T value)
{
	while (value > kPiF)
		value = T(value - kTwoPiF);
	while (value < -kPiF)
		value = T(value + kTwoPiF);
	return value;
}
std::array<double, 3> ReadVector(const PackedTransform& t, unsigned offset)
{
	return { t[offset], t[offset + 1], t[offset + 2] };
}
Quaternion Rotation(const PackedTransform& t)
{
	return { t[0], t[1], t[2], t[3] };
}
}
InertialVectorDelta InertialVectorDifference(
	std::array<double, 3> current, std::array<double, 3> recent, std::array<double, 3> older, float dt)
{
	std::array<double, 3> d;
	for (unsigned i = 0; i < 3; ++i)
		d[i] = recent[i] - current[i];
	const float magnitude = float(std::sqrt((d[1] * d[1] + d[0] * d[0]) + d[2] * d[2]));
	const double inverse = magnitude > 1e-4f ? 1 / double(magnitude) : 0;
	std::array<double, 3> direction{};
	if (magnitude > 1e-4f)
		for (unsigned i = 0; i < 3; ++i)
			direction[i] = d[i] * inverse;
	float velocity = 0;
	if (dt > 1e-4f && magnitude > 1e-4f)
	{
		std::array<double, 3> old;
		for (unsigned i = 0; i < 3; ++i)
			old[i] = older[i] - current[i];
		const float projection = float((old[1] * direction[1] + old[0] * direction[0]) + old[2] * direction[2]);
		velocity = (magnitude - projection) / dt;
	}
	return { magnitude, { float(direction[0]), float(direction[1]), float(direction[2]) }, velocity };
}
InertialRotationDelta InertialRotationDifference(Quaternion current, Quaternion recent, Quaternion older, float dt)
{
	const auto q = SourceQuaternionProduct(recent, Conjugate(current));
	const double length = (q[0] * q[0] + q[2] * q[2]) + q[1] * q[1];
	std::array<double, 3> axis = { 1, 0, 0 };
	if (length >= 1e-8)
	{
		const double reciprocal = 1 / std::sqrt(length);
		for (unsigned i = 0; i < 3; ++i)
			axis[i] = q[i] * reciprocal;
	}
	float angle = Wrap(float(2 * std::acos(std::clamp(q[3], -1.0, 1.0))));
	if (angle < 0)
	{
		angle = -angle;
		for (auto& a : axis)
			a = -a;
	}
	float velocity = 0;
	if (dt > 1e-4f && angle > 1e-4f)
	{
		const auto old = SourceQuaternionProduct(older, Conjugate(current));
		const double dot = (axis[1] * old[1] + axis[0] * old[0]) + axis[2] * old[2];
		const float projected = float(dot == 0 && old[3] == 0 ? 0 : Wrap(2 * std::atan2(dot, old[3])));
		velocity = Wrap(angle - projected) / dt;
	}
	return { { float(axis[0]), float(axis[1]), float(axis[2]) }, angle, velocity };
}
InertialPoseDeltas BuildInertialPose(const PackedPose& current, const PackedPose& recent, const PackedPose& older, float dt,
	Quaternion currentComponent, Quaternion recentComponent, Quaternion olderComponent)
{
	const auto a = SourceQuaternionProduct(Conjugate(currentComponent), recentComponent),
			   b = SourceQuaternionProduct(Conjugate(currentComponent), olderComponent);
	InertialPoseDeltas out;
	out.records.reserve(current.size());
	out.rootMode = std::abs(a[3]) < double(.999f) || std::abs(b[3]) < double(.999f) ? 2 : 0;
	for (unsigned i = 0; i < current.size(); ++i)
	{
		const auto rq = i == 0 && out.rootMode == 2 ? SourceQuaternionProduct(a, Rotation(recent[i])) : Rotation(recent[i]);
		const auto oq = i == 0 && out.rootMode == 2 ? SourceQuaternionProduct(b, Rotation(older[i])) : Rotation(older[i]);
		out.records.push_back({ InertialVectorDifference(ReadVector(current[i], 4), ReadVector(recent[i], 4), ReadVector(older[i], 4), dt),
			InertialVectorDifference(ReadVector(current[i], 8), ReadVector(recent[i], 8), ReadVector(older[i], 8), dt),
			InertialRotationDifference(Rotation(current[i]), rq, oq, dt) });
	}
	return out;
}
PackedPose ApplyInertialPose(PackedPose pose, const std::vector<InertialBoneDelta>& deltas, float time, float duration)
{
	for (unsigned i = 0; i < pose.size(); ++i)
	{
		auto& t = pose[i];
		const auto& d = deltas[i];
		const auto& r = d.rotation;
		const float half = InertialDecay(r.angle, r.velocity, time, duration) * .5f;
		const double s = std::sin(double(half));
		const auto q = SourceQuaternionProduct({ r.axis[0] * s, r.axis[1] * s, r.axis[2] * s, std::cos(double(half)) }, Rotation(t));
		std::copy(q.begin(), q.end(), t.begin());
		const float translation = InertialDecay(d.translation.magnitude, d.translation.velocity, time, duration),
					scale = InertialDecay(d.scale.magnitude, d.scale.velocity, time, duration);
		for (unsigned j = 0; j < 3; ++j)
		{
			t[4 + j] += double(d.translation.direction[j]) * double(translation);
			t[8 + j] += double(d.scale.direction[j]) * double(scale);
		}
	}
	return NormalizePackedPose(std::move(pose));
}
void MovementInertialReceiver::Update(double dt, const std::vector<InertialRequest>& requests)
{
	curves_.Update(dt);
	for (const auto& r : requests)
		curves_.Request(r);
}
PoseCurveBundle MovementInertialReceiver::Evaluate(const PoseCurveBundle& input, Quaternion component, bool recordDiagnostics)
{
	std::vector<InertialCurve> entries;
	entries.reserve(input.curves.size());
	for (const auto& e : input.curves)
	{
		auto found = names_.find(e.name);
		if (found == names_.end())
		{
			const unsigned id = unsigned(namesById_.size());
			namesById_.push_back(e.name);
			found = names_.emplace(e.name, id).first;
		}
		entries.push_back({ found->second, e.value, e.flags });
	}
	std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
	const auto result = curves_.Evaluate(entries, unsigned(input.pose.size()));
	const auto& event = result.event;
	if (event.initializeOffsets)
	{
		const auto& older = older_ ? *older_ : *recent_;
		auto d = BuildInertialPose(input.pose, recent_->pose, older.pose, recent_->dt, component, recent_->component, older.component);
		deltas_ = std::move(d.records);
		rootMode_ = d.rootMode;
	}
	if (!event.applyOffsets)
		deltas_.clear();
	PoseCurveBundle out;
	out.pose = event.applyOffsets ? ApplyInertialPose(input.pose, deltas_, event.elapsed, event.duration) : input.pose;
	out.curves.reserve(result.curves.size());
	for (const auto& e : result.curves)
		out.curves.push_back({ namesById_.at(e.name), e.value, e.flags });
	older_ = std::move(recent_);
	recent_ = History{ out.pose, component, event.accumulatedDelta };
	last_ = { event, event.applyOffsets ? rootMode_ : 0, 0 };
	if (recordDiagnostics && event.applyOffsets)
		for (unsigned i = 0; i < out.pose.size(); ++i)
			last_.maximumTranslationOffsetCm = std::max(last_.maximumTranslationOffsetCm,
				std::hypot(out.pose[i][4] - input.pose[i][4], out.pose[i][5] - input.pose[i][5], out.pose[i][6] - input.pose[i][6]));
	return out;
}
}
