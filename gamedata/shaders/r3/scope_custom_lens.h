// PIP_COMPAT_PATCH scope_custom_lens 20260715 thinhook
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
#include "svp_hooks_lens.h"


float4 scope_custom_lens(v_out I, Scope s) {

	if (svp_lens_flat_window_cull())
		return float4(0, 0, 0, 0);

	float DIRT_INTENSITY = s3ds_param_3.z;
	float REFLECTION_HUE = s3ds_param_2.x;
	float LENS_BRIGHTNESS = s3ds_param_2.y;

	float lum = current_lum() * 2;

	float3x3 TBNw_inv = float3x3(I.w_T, I.w_B, I.w_N);
	TBNw_inv = transpose(TBNw_inv);
	float3 normalmap = sample_lens_normalmap(s.tc0, 3);
	float3 lensnormal = normalize(float3(dot(normalmap, TBNw_inv[0]), dot(normalmap, TBNw_inv[1]), dot(normalmap, TBNw_inv[2])));
	float4 dirt = s_dirt.Sample(smp_base, s.tc0);
	dirt = saturate(float4((1 - dirt.rgb) * 1.5 * calc_model_lq_lighting(lensnormal), dirt.a * DIRT_INTENSITY));

	// Reflections
	float3 layer1_color = HSVtoRGB(float3(REFLECTION_HUE, 0.16, 1));
	float3 layer2_color = HSVtoRGB(float3(REFLECTION_HUE - 0.1, 0.7, 1));
	float3 layer3_color = HSVtoRGB(float3(REFLECTION_HUE + 0.1, 0.4, 1));

	float4 reflections;
	reflections.a = sample_reflections_weight(dirt.a, s.w_P, s.w_N, lum, SETTING(SETTINGS, ST_SEE_THROUGH));
	bool is_flat = RETICLE_TYPE == RT_FLAT_SCREEN;
	reflections.rgb = sample_reflections(s.tc0.xy, 0, 3, dirt.a, TBNw_inv, s.w_P, s.w_N, lum, is_flat) * layer1_color;
	reflections.rgb += sample_reflections(s.tc0.xy, 100, 2, dirt.a, TBNw_inv, s.w_P, s.w_N, lum, is_flat) * layer2_color;
	reflections.rgb += sample_reflections(s.tc0.xy, 40, -1, dirt.a, TBNw_inv, s.w_P, s.w_N, lum, is_flat) * layer3_color;
	reflections *= LENS_BRIGHTNESS;

	// Specular
	float3 specular = sample_specular(s.tc0.xy, 0, 3, SPECULAR_FACTOR, dirt.a, TBNw_inv, s.w_P, s.w_N, SETTING(SETTINGS, ST_SEE_THROUGH)) * layer1_color;
	specular += sample_specular(s.tc0.xy, 100, 2, SPECULAR_FACTOR * 0.1, dirt.a, TBNw_inv, s.w_P, s.w_N, SETTING(SETTINGS, ST_SEE_THROUGH)) * layer2_color;
	specular += sample_specular(s.tc0.xy, 40, -1, SPECULAR_FACTOR * 0.1, dirt.a, TBNw_inv, s.w_P, s.w_N, SETTING(SETTINGS, ST_SEE_THROUGH)) * layer3_color;

	// The legacy lens vignette is an authored cosmetic mask. Under true PiP it
	// duplicates the physical tube/pupil boundary and appears as a fixed dark
	// annulus once the lens reaches the eye during ADS.
	const bool true_pip = shader_scope_params.w < -1.5;
	float4 vignette = true_pip ? float4(0, 0, 0, 0)
		: float4(0, 0, 0, smoothstep(0.4, 2, 2 * length(s.tc0.xy - float2(0.5, 0.5))));

	float4 final_scope_2 = float4(0,0,0,0);

	final_scope_2 = rgba_blend(final_scope_2, vignette);
	final_scope_2 = rgba_blend(final_scope_2, reflections);

	float4 final_scope_3 = float4(0,0,0,0);

	final_scope_3 = rgba_blend(final_scope_3, dirt);

	final_scope_2.rgb = tonemap(final_scope_2.rgb, s.v_P);
	final_scope_3.rgb = tonemap(final_scope_3.rgb, s.v_P);

	float4 final_scope = rgba_blend(final_scope_2, final_scope_3);
	float4 specular_white = float4(1,1,1,specular.r);
	final_scope = rgba_blend(final_scope, specular_white);
	final_scope.rgb += specular;

    return final_scope;
}
