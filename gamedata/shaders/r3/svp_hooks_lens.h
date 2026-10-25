// relocated true-PiP flat-screen lens cull, included by scope_custom_lens.h after scope_3dss_common.h
#ifndef SVP_HOOKS_LENS_INCLUDED
#define SVP_HOOKS_LENS_INCLUDED

#include "svp_hooks_common.h"

// a flat screen is a see-through window, not coated glass, drop the whole lens layer under
// true PiP so the world fills the panel (reflections/specular/dirt/vignette would ellipse it)
bool svp_lens_flat_window_cull()
{
	return RETICLE_TYPE == RT_FLAT_SCREEN && SVP_TRUE_PIP;
}

float svp_lens_ads_reflection_visibility()
{
	if (shader_scope_params.w >= -1.5)
		return 1.0;

	if (svp_scope_alignment.z <= 0.5 || svp_lens_deadzone.z <= 0.5)
		return 0.0;

	return saturate(svp_lens_deadzone.w);
}

#endif
