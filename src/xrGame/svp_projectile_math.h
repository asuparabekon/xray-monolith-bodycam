#pragma once

#include "../xrCore/_vector3d.h"

namespace SvpProjectile
{
constexpr float minimum_convergence_distance = 2.f;

inline float ResolveConvergenceDistance(float configured_distance,
	bool hit, float hit_distance)
{
	if (configured_distance <= 0.f || !hit)
		return configured_distance;

	return _max(hit_distance, minimum_convergence_distance);
}

inline bool ResolveRay(const Fvector& pick_position,
	const Fvector& pick_direction, bool barrel_blocked,
	const Fvector& sight_position, const Fvector& sight_direction,
	bool sight_valid, float convergence_distance, const Fvector& muzzle,
	Fvector& position, Fvector& direction,
	Fvector* convergence_target = nullptr)
{
	position.set(pick_position);
	direction.set(pick_direction);
	if (convergence_target)
		convergence_target->set(0.f, 0.f, 0.f);
	if (!sight_valid || convergence_distance <= 0.f)
		return false;

	if (!barrel_blocked && muzzle.square_magnitude() > EPS)
		position.set(muzzle);

	Fvector target;
	target.mad(sight_position, sight_direction, convergence_distance);
	Fvector resolved_direction;
	resolved_direction.sub(target, position);
	if (resolved_direction.magnitude() <= 1.f)
		return false;

	resolved_direction.normalize();
	direction.set(resolved_direction);
	if (convergence_target)
		convergence_target->set(target);
	return true;
}
}
