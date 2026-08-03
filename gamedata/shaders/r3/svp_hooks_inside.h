// Physical-optics handling for the authored 3DSS scope-interior mask.
#ifndef SVP_HOOKS_INSIDE_H
#define SVP_HOOKS_INSIDE_H

#include "svp_hooks_common.h"

Texture2D s_inside_blurred;

float4 svp_sample_authored_inside(float2 inside_tc)
{
	float4 inside = 0;
	[branch]
	if (svp_physical_optics_active())
		inside = s_inside_blurred.SampleLevel(smp_base, inside_tc, 0.0);
	else
		inside = s_inside.Sample(smp_base, inside_tc);

	return inside;
}

#endif
