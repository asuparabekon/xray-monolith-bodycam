#include "stdafx.h"
#include "bodycam_math.h"

namespace Bodycam
{
void BuildCameraBasis(float yaw, float pitch, float roll, Fvector& dir, Fvector& up, Fvector& right)
{
	dir.setHP(yaw, pitch);
	dir.normalize_safe();
	Fvector::generate_orthonormal_basis_normalized(dir, up, right);
	if (_abs(roll) > EPS)
	{
		const float s = _sin(roll);
		const float c = _cos(roll);
		Fvector rolled_up, rolled_right;
		rolled_up.set(up).mul(c).mad(right, s);
		rolled_right.set(right).mul(c).mad(up, -s);
		up.set(rolled_up).normalize_safe();
		right.set(rolled_right).normalize_safe();
	}
}
} // namespace Bodycam
