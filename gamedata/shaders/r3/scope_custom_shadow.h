// PIP_COMPAT_PATCH scope_custom_shadow 20260715
/*
	=====================================================================
	Addon      : Shader 3D Scopes
	Link       : https://www.moddb.com/mods/stalker-anomaly/addons/shader-3d-scopes
	Authors    : LVutner, party_50
	PiP Author : m22

	God-Queen who fixed everything: MsPizza727

	All credit to original authors.
	=====================================================================
*/

#include "scope_3dss_common.h"
#include "svp_hooks_shadow.h"

float scope_custom_shadow(v_out I, Scope S) {
	float RETICLE_SIZE = s3ds_param_1.x;
	float EYE_RELIEF = s3ds_param_1.y;
	float EXIT_PUPIL = s3ds_param_1.z;
	int FFP = s3ds_param_1.w;

	float REFLECTION_HUE = s3ds_param_2.x;
	int MIN_ZOOM_1X = s3ds_param_2.z;

	float DIRT_INTENSITY = s3ds_param_3.z;
	float CHROMA_POWER = s3ds_param_3.w;

	float RETICLE_PROJECT = 10;
	float SHADOW_WIDTH = 0.15;

	if (RETICLE_TYPE == RT_FLAT_SCREEN)
		RETICLE_PROJECT = 0;

	float current_zoom = (curMag()) * 0.4 + 1.0;
	float zoom_part = zoomPercent();
	float3x3 TBN;
	TBN = cotangent_frame(S.v_N + 0.0039, S.v_P + 0.0039, S.tc0.xy);

	float3 V_tangent = normalize(float3(dot(-I.v_P, TBN[0]), dot(-I.v_P, TBN[1]), dot(-I.v_P, TBN[2])));

	if (!SETTING(SETTINGS, ST_CHROMATISM)) {
		CHROMA_POWER = 0;
	}

	float lum = current_lum();

	// Sight reticle
	float2 exit_pupil_tc = S.exit_pupil;

	// Specter switch shadow
	float4 zoom_switch_shadow = float4(0, 0, 0, 0);
	if (RETICLE_TYPE == RT_SPECTER)
	{
		zoom_switch_shadow = sample_zoom_switch_shadow(S.ffp);
	}

	float offset = distance(S.exit_pupil, S.sfp) + 1.0;
	offset = pow(offset,1.2);

	// True PiP uses mesh-independent lens coordinates and keeps the physical
	// transmission boundary separate from the persistent tube shadow.
	const bool true_pip = shader_scope_params.w < -1.5;
	float4 shadow_texture;
	if (svp_physical_optics_active())
	{
		exit_pupil_tc = svp_scope_lens_tc(I.w_P) + svp_scope_tunneling_offset();
		shadow_texture = svp_profiled_tunneling_shadow(exit_pupil_tc);
	}
	else
	{
		exit_pupil_tc = project(I.tc0, V_tangent.xy * mas_scale(), -EYE_RELIEF,
			EXIT_PUPIL * (SETTING(SETTINGS, ST_SEE_THROUGH) ? 1 : m_hud_params.x));
		shadow_texture = sample_shadow(exit_pupil_tc, SHADOW_WIDTH + 0.02 * (current_zoom - 1));
		if (!SETTING(SETTINGS, ST_PARALLAX_SHADOW))
			shadow_texture *= 1 - m_hud_params.x;
	}
	if (RETICLE_TYPE == RT_SCREEN || RETICLE_TYPE == RT_FLAT_SCREEN)
	{
		shadow_texture = float4(0, 0, 0, 0);
	}

	return rgba_blend( zoom_switch_shadow, shadow_texture).a;
}
