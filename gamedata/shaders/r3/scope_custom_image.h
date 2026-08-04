// PIP_COMPAT_PATCH scope_custom_image 20260715 thinhook
/*
	=====================================================================
	Addon      : Shader 3D Scopes
	Link       : https://www.moddb.com/mods/stalker-anomaly/addons/shader-3d-scopes
	Authors    : LVutner, party_50

	All credit to original authors.
	=====================================================================
*/

#include "scope_3dss_common.h"


#define LCD_RES float2(640,640)
#include "svp_hooks_image.h"
#include "svp_hooks_shadow.h"

float2 SCOPECOORD_TO_TEXCOORD_SCREEN(float2 sc) {
	if (!isSVPActive() && scope_phase & SCOPE_PHASE_IMAGE) {
		// This is fake pip mode, so we have to correct the coordinates
		// FIXME: These coordinates need to be rotated to align with eye up
		float screen_delta  = length(ddy(scope.hpos.xy * output_res.zw));
		float texture_delta = length(ddy(scope.tc0.xy));
		float tc_multiplier = texture_delta / screen_delta;

		sc -= 0.5;

		float angle = hack_tex_angle;

		sc = float2( sc.x * cos(angle) - sc.y * sin(angle)
		           , sc.x * sin(angle) + sc.y * cos(angle));

		float f = (digitalZoom() * tc_multiplier);
		return ASPECT_UNCORRECT_TC(sc / f + 0.5);

	} else {
		return sc;
	}
}


float3 scope_custom_image(Scope s) {
	
	int FFP = s3ds_param_1.w;
	float CHROMA_POWER = s3ds_param_3.w;
	CHROMA_POWER = svp_img_chroma_power(CHROMA_POWER, s);
	float2 reticle_tc = FFP ? s.ffp : s.sfp;
	
	float current_zoom = (curMag()-minMag()) * 0.4 + 1.0;
	
	float3x3 TBN = cotangent_frame(s.v_N + 0.0039, s.v_P + 0.0039, s.tc0.xy);
	float3 V_tangent = normalize(float3(dot(-s.v_P, TBN[0]), dot(-s.v_P, TBN[1]), dot(-s.v_P, TBN[2])));
	
	float2 coord = s.sfp;
	if (RETICLE_TYPE == RT_SCREEN || RETICLE_TYPE == RT_FLAT_SCREEN) {
		coord = s.tc0;
	}

	float2 scope_tc = SCOPECOORD_TO_TEXCOORD(coord);
	
	if (RETICLE_TYPE != RT_FLAT_SCREEN)
	{
		// fisheye off under true PiP (w < -1.5), fake modes keep it
		float2 fish = fisheye(s.tc0, (V_tangent.xy * svp_effective_mas(mas_scale()))) / current_zoom * (shader_scope_params.w < -1.5 ? 0.0 : 1.0);
		scope_tc += fish;
		reticle_tc += fish;
	}

	svp_img_flat_window(scope_tc, s);

	svp_img_fix_flipped_lens(scope_tc, s);
	
	// A passive thermal sight is ordinary glass; preserve sensor treatment only
	// while its thermal display is selected.
	bool thermal_live = (IMAGE_TYPE == IT_THERMAL || IMAGE_TYPE == IT_THERMAL_COLOR)
		&& markswitch_current.x < 2;
	bool pip_glass_strip = shader_scope_params.w < -1.5 && !thermal_live;
	bool blur = SETTING(SETTINGS, ST_NVG_BLUR) && !(pip_glass_strip && svp_control.z > 0.5) && floor(shader_param_8.x) != 0 || zoomRotateFactor() == 0;

	float3 back = back_image_sample(s.tc0, scope_tc, CHROMA_POWER, blur, SETTING(SETTINGS, ST_CHROMATISM) && !(pip_glass_strip && svp_control.y > 0.5));
	if ((IMAGE_TYPE == IT_THERMAL || IMAGE_TYPE == IT_THERMAL_COLOR) && markswitch_current.x < 2) {
		// fake mode authored pixelation (true PiP re-applies it on the filled tc below)
		if (SETTING(SETTINGS, ST_THERMAL_PIXELATION) && shader_scope_params.w >= -1.5) {
			float pixelate = curMag();

			float2 sfp = (floor(coord * LCD_RES / pixelate) * pixelate + 0.5) / LCD_RES;
			scope_tc = SCOPECOORD_TO_TEXCOORD_SCREEN(sfp);
		}

		svp_img_thermal_fill(scope_tc, s);

		float2 rez = screen_res.xy;

		gbuffer_data gbd = gbuffer_load_data(scope_tc, scope_tc * rez.xy, 0);
		back = infrared(gbd, scope_tc * rez.xy, scope_tc);
		svp_img_sensor_noise(back, scope_tc);
		if (markswitch_current.x == 1) {
			back = 1 - back;
		}
		if (IMAGE_TYPE == IT_THERMAL_COLOR) {
		    back = s_heat_map.Sample(smp_base, float2(back.x, 0.5));
		}
		svp_img_thermal_veil(back, gbd);
		svp_img_lcd_mask(back);
	} else {
		// FIXME: Incorrect colorspace
		back *= LENS_COLOR;
		svp_image_glass_fx(back, scope_tc, s);
	}

	if (false) {
		if (IMAGE_TYPE == IT_THERMAL || IMAGE_TYPE == IT_THERMAL_COLOR) {
			float SUBPIXELS = (LCD_RES * 4.0);
			float digitalZoom = SETTING(SETTINGS, ST_THERMAL_PIXELATION)
				? curMag()
				: 1.0;

			float multiplier = (SUBPIXELS / digitalZoom);

			float2 c = (coord - s.center) * multiplier + SUBPIXELS*10;

			// The LCD effect is designed for 1:1 pixel ratio, and LCD effect in general requires high output resolution to look correct.
			// It is absolutely necessary to multisample to get even a barely passable output.
			float2 o = float2(0.25,-0.25);
			float3 s = lcd_effect(c)
					+ lcd_effect(c + o.xx)
					+ lcd_effect(c + o.yy)
					+ lcd_effect(c + o.xy)
					+ lcd_effect(c + o.yx);

			back *= s / 5.0;
		}
	}

	if (IMAGE_TYPE == IT_NV && markswitch_current.x == 0) {
		// FIXME: Incorrect colorspace
		if (shader_scope_params.w < -1.5 && svp_glass4.x > 0.001)
			back = apply_nvg_bleach(scope_tc, back, svp_glass4.x, max(svp_glass4.y, 0.1));
		else
			back = apply_nvg(scope_tc, back);
	}

	if (svp_physical_optics_active() && RETICLE_TYPE != RT_FLAT_SCREEN)
		back *= svp_exit_pupil_transmission(svp_scope_lens_tc(s.w_P));

	return back;
}
