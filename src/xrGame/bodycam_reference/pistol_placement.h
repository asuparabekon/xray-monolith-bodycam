#pragma once
#include "transform.h"

namespace BodycamReference
{
// Hip translation of a pistol: the host's pistol hip offset (camera-local metres, -Z forward) for a
// pistol, none otherwise. Only the hip translation changes; sights and action clips keep their own.
inline Vector PistolHipTranslation(bool pistol, const Vector& pistolHipOffset)
{
	const double weight = pistol ? 1. : 0.;
	return { pistolHipOffset[0] * weight, pistolHipOffset[1] * weight, pistolHipOffset[2] * weight };
}
}
