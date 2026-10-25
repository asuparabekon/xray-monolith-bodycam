#ifndef SVP_LENS_OPTICS_INCLUDED
#define SVP_LENS_OPTICS_INCLUDED


#ifndef SVP_LENS_LIGHTING_DECLARED
#define SVP_LENS_LIGHTING_DECLARED
uniform float4 svp_lens_light0_position;
uniform float4 svp_lens_light0_color;
uniform float4 svp_lens_light1_position;
uniform float4 svp_lens_light1_color;
uniform float4 svp_lens_tuning; // environment, direct, ghosts, reserved
uniform float4 svp_lens_control; // opacity, debug channel, reserved, reserved
uniform float4 svp_lens_environment; // rgb radiance scale, w weather blend
uniform float4 svp_lens_probe_positive; // raw object probe: +X, +Y, +Z
uniform float4 svp_lens_probe_negative; // raw object probe: -X, -Y, -Z
uniform float4 svp_lens_deadzone;
#endif

#include "svp_lens_object_light.h"
#include "scope_glass_authoring.h"
#include "svp_lens_environment.h"

struct SvpLensChannels
{
	float4 environment;
	float4 direct;
	float4 ghosts;
};

// One profile drives both the standalone HUD glass and the 3DSS lens pass.
struct SvpLensProfile
{
	float roughness;
	float front_curvature;
	float refractive_index;
	float ar_residual_scale;
	float reflection_exposure;
	float environment_strength;
	float internal_depth;
	float internal_strength;
	float ghost_dispersion;
};

struct SvpLensGhostProfile
{
	float surface_curvature;
	float surface_depth;
	float roughness;
	float path_weight;
	float aperture_radius;
	float refractive_index;
	float ar_residual_scale;
	float pincushion;
};

struct SvpLensReflectionPath
{
	float intensity;
	float3 coating_near;
	float3 coating_middle;
	float3 coating_oblique;
	float coating_transition_start;
	float coating_transition_end;
	float coating_strength;
};

SvpLensProfile svp_default_lens_profile()
{
	SvpLensProfile profile;
	profile.roughness = 0.20;
	profile.front_curvature = 0.82;
	profile.refractive_index = 1.52;
	profile.ar_residual_scale = 0.30;
	profile.reflection_exposure = 7.5;
	profile.environment_strength = 0.48;
	profile.internal_depth = 0.34;
	profile.internal_strength = 0.78;
	profile.ghost_dispersion = 1.0;
	return profile;
}

// The default profile with the optic's authored reflection brightness.
SvpLensProfile svp_authored_lens_profile()
{
	SvpLensProfile profile = svp_default_lens_profile();
	profile.reflection_exposure *= max(1.0 + svp_lens_authoring.y, 0.0);
	return profile;
}

SvpLensGhostProfile svp_primary_environment_ghost_profile()
{
	SvpLensGhostProfile layer;
	layer.surface_curvature = -0.78;
	layer.surface_depth = 0.36;
	layer.roughness = 0.34;
	layer.path_weight = 0.20;
	layer.aperture_radius = 0.30;
	layer.refractive_index = 1.62;
	layer.ar_residual_scale = 0.35;
	layer.pincushion = 0.76;
	return layer;
}

SvpLensGhostProfile svp_secondary_environment_ghost_profile()
{
	SvpLensGhostProfile layer;
	layer.surface_curvature = -0.46;
	layer.surface_depth = 0.62;
	layer.roughness = 0.62;
	layer.path_weight = 0.16;
	layer.aperture_radius = 0.40;
	layer.refractive_index = 1.58;
	layer.ar_residual_scale = 0.45;
	layer.pincushion = 0.52;
	return layer;
}

SvpLensReflectionPath svp_front_reflection_path()
{
	SvpLensReflectionPath path;
	path.intensity = 1.00;
	path.coating_near = float3(0.32, 1.00, 0.58);
	path.coating_middle = float3(0.18, 0.62, 1.00);
	path.coating_oblique = float3(0.95, 0.22, 0.68);
	path.coating_transition_start = 0.015;
	path.coating_transition_end = 0.48;
	path.coating_strength = 0.62;
	return path;
}

SvpLensReflectionPath svp_middle_reflection_path()
{
	SvpLensReflectionPath path;
	path.intensity = 1.00;
	path.coating_near = float3(0.72, 0.34, 1.00);
	path.coating_middle = float3(0.48, 0.30, 1.00);
	path.coating_oblique = float3(0.92, 0.24, 0.70);
	path.coating_transition_start = 0.05;
	path.coating_transition_end = 0.48;
	path.coating_strength = 0.70;
	return path;
}

SvpLensReflectionPath svp_deep_reflection_path()
{
	SvpLensReflectionPath path;
	path.intensity = 0.92;
	path.coating_near = float3(0.30, 1.00, 0.62);
	path.coating_middle = float3(0.22, 0.78, 1.00);
	path.coating_oblique = float3(0.62, 0.34, 1.00);
	path.coating_transition_start = 0.05;
	path.coating_transition_end = 0.48;
	path.coating_strength = 0.72;
	return path;
}

float svp_optic_luminance(float3 colour)
{
	return dot(max(colour, 0.0), float3(0.2126, 0.7152, 0.0722));
}

float3 svp_optic_object_radiance()
{
	return max(L_ambient.rgb, 0.0)
		+ max(L_hemi_color.rgb, 0.0) * svp_lens_object_light.x
		+ max(L_sun_color.rgb, 0.0) * svp_lens_object_light.y;
}

float svp_optic_probe_visibility(float3 direction)
{
	direction = normalize(direction);
	float3 positive_weight = max(direction, 0.0);
	float3 negative_weight = max(-direction, 0.0);
	float weight = dot(positive_weight + negative_weight, 1.0);
	float directional = dot(positive_weight, svp_lens_probe_positive.xyz)
		+ dot(negative_weight, svp_lens_probe_negative.xyz);
	float probe_mean = dot(svp_lens_probe_positive.xyz
		+ svp_lens_probe_negative.xyz, 1.0) / 6.0;
	// CROS probe values are visibility weights, not scene radiance. Normalize
	// against the object's own mean so the probe shapes direction without
	// turning every normally dim HUD object into a black lens.
	return saturate(directional / max(probe_mean * weight, 0.0001));
}

float svp_optic_objective_aperture(float2 lens_position)
{
	return 1.0 - smoothstep(0.94, 1.0, length(lens_position));
}

float3 svp_optic_environment(float3 direction, float mip)
{
	// the active weather cubes times the weather colour and exposure; the callers apply the
	// probe (direction) and, for diffuse light, the object's hemi
	return svp_lens_weather(direction, mip, svp_lens_environment.w)
		* max(svp_lens_environment.rgb, 0.0);
}

float3 svp_optic_reflection_contrast(float3 radiance, float roughness)
{
	float contrast = lerp(3.20, 1.45, saturate(roughness));
	float luminance = svp_optic_luminance(radiance);
	float3 saturated = lerp(luminance.xxx, radiance, 1.28);
	return max((saturated - 0.14) * contrast + 0.14, 0.0);
}

float svp_optic_dielectric_fresnel(float cosine_incident, float incident_index,
	float transmitted_index)
{
	cosine_incident = saturate(abs(cosine_incident));
	float eta = incident_index / transmitted_index;
	float sine_transmitted2 = eta * eta * max(1.0 - cosine_incident * cosine_incident, 0.0);
	if (sine_transmitted2 >= 1.0)
		return 1.0;
	float cosine_transmitted = sqrt(max(1.0 - sine_transmitted2, 0.0));
	float rs_numerator = incident_index * cosine_incident
		- transmitted_index * cosine_transmitted;
	float rs_denominator = incident_index * cosine_incident
		+ transmitted_index * cosine_transmitted;
	float rp_numerator = transmitted_index * cosine_incident
		- incident_index * cosine_transmitted;
	float rp_denominator = transmitted_index * cosine_incident
		+ incident_index * cosine_transmitted;
	float rs = rs_numerator / max(abs(rs_denominator), 0.0001);
	float rp = rp_numerator / max(abs(rp_denominator), 0.0001);
	return saturate(0.5 * (rs * rs + rp * rp));
}

float svp_optic_coated_reflectance(float cosine_incident, float incident_index,
	float transmitted_index, float residual_scale)
{
	float bare = svp_optic_dielectric_fresnel(cosine_incident,
		incident_index, transmitted_index);
	float grazing = pow(1.0 - saturate(abs(cosine_incident)), 3.0);
	return bare >= 0.999 ? 1.0 : bare * lerp(saturate(residual_scale), 1.0, grazing);
}

bool svp_optic_sphere_surface(float2 position, float vertex_z, float curvature,
	out float3 surface_point, out float3 surface_normal)
{
	float signed_radius = 1.0 / curvature;
	float radius2 = signed_radius * signed_radius;
	float radial2 = dot(position, position);
	if (radial2 >= radius2)
	{
		surface_point = 0;
		surface_normal = float3(0, 0, 1);
		return false;
	}

	float3 centre = float3(0, 0, vertex_z - signed_radius);
	float branch = signed_radius >= 0.0 ? 1.0 : -1.0;
	surface_point = float3(position, centre.z + branch * sqrt(radius2 - radial2));
	surface_normal = normalize(surface_point - centre);
	return true;
}

bool svp_optic_intersect_sphere(float3 origin, float3 direction, float vertex_z,
	float curvature, float aperture_radius, out float3 surface_point, out float3 surface_normal)
{
	float signed_radius = 1.0 / curvature;
	float radius = abs(signed_radius);
	float3 centre = float3(0, 0, vertex_z - signed_radius);
	float3 offset = origin - centre;
	float b = dot(offset, direction);
	float c = dot(offset, offset) - radius * radius;
	float discriminant = b * b - c;
	surface_point = 0;
	surface_normal = float3(0, 0, 1);
	if (discriminant < 0.0)
		return false;

	float root = sqrt(discriminant);
	float distances[2] = { -b - root, -b + root };
	float best_distance = 1e20;
	float branch = signed_radius >= 0.0 ? 1.0 : -1.0;
	[unroll]
	for (int index = 0; index < 2; ++index)
	{
		float distance = distances[index];
		float3 candidate = origin + direction * distance;
		float3 candidate_normal = normalize(candidate - centre);
		bool valid = distance > 0.0001 && distance < best_distance
			&& dot(candidate.xy, candidate.xy) <= aperture_radius * aperture_radius
			&& candidate_normal.z * branch > 0.0;
		if (valid)
		{
			best_distance = distance;
			surface_point = candidate;
			surface_normal = candidate_normal;
		}
	}
	return best_distance < 1e19;
}

float3 svp_optic_local_to_world(float3 direction, float3 right, float3 up,
	float3 axis)
{
	return normalize(right * direction.x + up * direction.y + axis * direction.z);
}

float3 svp_optic_apply_pincushion(float3 direction, float strength)
{
	float axial = max(abs(direction.z), 0.05);
	float2 tangent = direction.xy / axial;
	float field_scale = max(0.16, 1.0 - strength * dot(tangent, tangent));
	return normalize(float3(direction.xy * field_scale, direction.z));
}

float3 svp_optic_path_coating(SvpLensReflectionPath path,
	float cosine_incident)
{
	float obliquity = 1.0 - saturate(abs(cosine_incident));
	float transition = smoothstep(path.coating_transition_start,
		path.coating_transition_end, obliquity);
	float inverse_transition = 1.0 - transition;
	float3 coating = path.coating_near * inverse_transition * inverse_transition
		+ path.coating_middle * (2.0 * inverse_transition * transition)
		+ path.coating_oblique * transition * transition;
	return saturate(svp_lens_authored_hue(coating));
}

float3 svp_optic_sample_reflection_path(float3 local_direction,
	float3 right, float3 up, float3 axis,
	float base_roughness, float cosine_incident,
	SvpLensReflectionPath path)
{
	float3 world_direction = svp_optic_local_to_world(local_direction,
		right, up, axis);
	base_roughness = svp_lens_blur_roughness(base_roughness);
	float3 radiance = svp_optic_environment(world_direction,
		lerp(0.8, 4.5, saturate(base_roughness)));
	// Apply object-probe visibility to the reflected world ray. This preserves
	// directional shadows without fading the complete optical stack.
	float probe_visibility = svp_optic_probe_visibility(world_direction);
	radiance *= lerp(0.45, 1.0, probe_visibility);
	radiance = svp_optic_reflection_contrast(radiance, base_roughness);
	float3 spectral_filter = lerp(float3(1.0, 1.0, 1.0),
		svp_optic_path_coating(path, cosine_incident), path.coating_strength);
	return radiance * spectral_filter * path.intensity;
}

float4 svp_optic_environment_ghost(float3 front_point, float3 front_normal,
	float3 view_direction, float3 right, float3 up, float3 axis,
	float front_reflectance, float3 front_filter,
	float front_curvature, float reflection_exposure,
	SvpLensGhostProfile layer, SvpLensReflectionPath path)
{
	float refractive_index = max(layer.refractive_index, 1.01);
	float3 inside_direction = refract(-view_direction, front_normal,
		1.0 / max(refractive_index, 1.01));
	if (dot(inside_direction, inside_direction) <= 0.0001)
		return 0;

	float3 layer_point;
	float3 layer_normal;
	if (!svp_optic_intersect_sphere(front_point + inside_direction * 0.001,
		inside_direction, -layer.surface_depth, layer.surface_curvature,
		layer.aperture_radius, layer_point, layer_normal))
		return 0;
	float layer_radius = length(layer_point.xy);
	float aperture = 1.0 - smoothstep(layer.aperture_radius - 0.06,
		layer.aperture_radius, layer_radius);
	if (aperture <= 0.0001)
		return 0;

	float3 return_inside = reflect(inside_direction, layer_normal);
	if (return_inside.z <= 0.0001)
		return 0;

	float3 exit_point;
	float3 exit_normal;
	if (!svp_optic_intersect_sphere(layer_point + return_inside * 0.001,
		return_inside, 0.0, front_curvature, 1.0, exit_point, exit_normal))
		return 0;
	float exit_radius = length(exit_point.xy);
	float exit_aperture = 1.0 - smoothstep(0.94, 1.0, exit_radius);
	if (exit_aperture <= 0.0001)
		return 0;

	float3 sample_direction = refract(return_inside, -exit_normal,
		max(refractive_index, 1.01));
	if (dot(sample_direction, sample_direction) <= 0.0001)
		return 0;

	sample_direction = svp_optic_apply_pincushion(sample_direction,
		layer.pincushion);
	float internal_cosine = abs(dot(-inside_direction, layer_normal));
	float3 radiance = svp_optic_sample_reflection_path(sample_direction,
		right, up, axis, layer.roughness,
		internal_cosine, path);
	float internal_reflectance = svp_optic_coated_reflectance(internal_cosine,
		refractive_index, 1.0, layer.ar_residual_scale);
	float front_transmission = 1.0 - saturate(front_reflectance);
	float interface_energy = internal_reflectance * layer.path_weight;
	// Internal reflections are optically faint, but their visibility must remain
	// linear in sampled radiance. Folding radiance into alpha as well as colour
	// suppresses every recessed return twice and hides the lens stack.
	float path_energy = front_transmission * front_transmission
		* lerp(interface_energy, sqrt(max(interface_energy, 0.0)), 0.18);
	float path_exposure = max(reflection_exposure, 0.0) / 7.5;
	float alpha = saturate(aperture * exit_aperture * path_energy
		* path_exposure);
	float3 colour = radiance * front_filter;
	return float4(colour * alpha, alpha);
}

float4 svp_optic_over(float4 front, float4 back)
{
	return front + back * (1.0 - saturate(front.a));
}

float svp_optic_ghost(float2 position, float2 source, float scale, float radius)
{
	float2 delta = position - source * scale;
	return exp2(-dot(delta, delta) / max(radius * radius, 0.0001));
}

float svp_optic_pupil_ghost(float2 position, float2 source, float scale, float radius)
{
	float distance_to_ghost = length(position - source * scale);
	float disc = 1.0 - smoothstep(radius * 0.70, radius, distance_to_ghost);
	float rim = exp2(-pow((distance_to_ghost - radius * 0.78) / max(radius * 0.16, 0.0001), 2.0));
	return disc * 0.58 + rim * 0.42;
}

void svp_optic_source(float3 direction, float3 colour, float intensity, float2 lens_position,
	float2 eye_shift, float3 normal, float3 right, float3 up, float3 view_direction,
	SvpLensProfile profile,
	out float4 direct_channel, out float4 ghost_channel)
{
	direct_channel = 0;
	ghost_channel = 0;
	if (intensity <= 0.0001)
		return;

	float entry = pow(saturate(dot(direction, -normal)), 1.5);
	float reflected = saturate(dot(reflect(-direction, normal), view_direction));
	float highlight = pow(reflected, 96.0);
	float flare = pow(reflected, 10.0) * 0.14;
	float3 axis = normalize(cross(right, up));
	float axial = max(abs(dot(direction, axis)), 0.20);
	float2 source = clamp(float2(dot(direction, right), dot(direction, up)) / axial, -1.5, 1.5) * 0.42;
	float ghost_spread = max(profile.ghost_dispersion, 0.05);
	float2 ghost_eye_shift = eye_shift * float2(1.0, -1.0);
	float near_ghost = svp_optic_pupil_ghost(lens_position + ghost_eye_shift * 0.20,
		source, 0.72 * ghost_spread, 0.060);
	float mid_ghost = svp_optic_pupil_ghost(lens_position + ghost_eye_shift * 0.48,
		source, 0.18 * ghost_spread, 0.105);
	float far_ghost = svp_optic_pupil_ghost(lens_position + ghost_eye_shift * 0.76,
		source, -0.42 * ghost_spread, 0.145);
	float veil = svp_optic_ghost(lens_position + ghost_eye_shift * 0.34,
		source, 0.06, 0.46) * 0.012;

	float direct_alpha = saturate((highlight * 0.28 + flare * 0.72) * intensity);
	float ghost_alpha = saturate(entry * (near_ghost * 0.13 + mid_ghost * 0.08
		+ far_ghost * 0.05 + veil * 1.7) * intensity);
	float3 direct_colour = colour * (0.85 + highlight * 1.2 + flare * 1.8);
	float ghost_sum = max(near_ghost + mid_ghost + far_ghost, 0.0001);
	float3 ghost_colour = colour * (near_ghost * float3(0.13, 0.20, 0.25)
		+ mid_ghost * float3(0.10, 0.18, 0.12)
		+ far_ghost * float3(0.22, 0.10, 0.07)) / ghost_sum;
	direct_channel = float4(direct_colour * direct_alpha, direct_alpha);
	ghost_channel = float4(ghost_colour * ghost_alpha, ghost_alpha);
}

void svp_optic_point_source(float4 position, float4 colour, float3 world_position,
	float2 lens_position, float2 eye_shift, float3 normal, float3 right, float3 up,
	float3 view_direction,
	SvpLensProfile profile,
	out float4 direct_channel, out float4 ghost_channel)
{
	direct_channel = 0;
	ghost_channel = 0;
	if (colour.w < 0.5 || position.w <= 0.0)
		return;

	float3 offset = position.xyz - world_position;
	float distance = length(offset);
	float attenuation = saturate(1.0 - distance / position.w);
	svp_optic_source(offset / max(distance, 0.001), colour.rgb, attenuation * attenuation,
		lens_position, eye_shift, normal, right, up, view_direction, profile,
		direct_channel, ghost_channel);
}

SvpLensChannels svp_evaluate_lens(float2 lens_position, float2 screen_position, float3 world_position,
	float3 right, float3 up, float3 surface_normal, float3 view_direction,
	SvpLensProfile profile, bool flat_lens, float reflection_visibility,
	float source_visibility)
{
	SvpLensChannels channels;
	channels.environment = 0;
	channels.direct = 0;
	channels.ghosts = 0;

	// Optical coordinates may move for parallax, but no simulated surface is
	// allowed to exist beyond the physical objective. The caller owns the one
	// soft edge used for compositing.
	if (!flat_lens && dot(lens_position, lens_position) >= 1.0)
		return channels;

	float3 axis = normalize(cross(right, up));
	axis *= dot(axis, surface_normal) < 0.0 ? -1.0 : 1.0;
	float3 front_point;
	float3 front_normal;
	if (!svp_optic_sphere_surface(lens_position, 0.0, profile.front_curvature,
		front_point, front_normal))
		return channels;
	float3 local_view_direction = normalize(float3(dot(view_direction, right),
		dot(view_direction, up), dot(view_direction, axis)));
	float3 normal = svp_optic_local_to_world(front_normal, right, up, axis);
	float view_normal = saturate(dot(normal, view_direction));
	float front_reflectance = svp_optic_coated_reflectance(view_normal, 1.0,
		profile.refractive_index, profile.ar_residual_scale);
	float normal_reflectance = svp_optic_coated_reflectance(1.0, 1.0,
		profile.refractive_index, profile.ar_residual_scale);
	float visible_front_reflectance = max(front_reflectance,
		normal_reflectance * 1.55);
	float3 reflected_local = reflect(-local_view_direction, front_normal);
	float3 environment = svp_optic_sample_reflection_path(reflected_local,
		right, up, axis, profile.roughness,
		view_normal, svp_front_reflection_path());
	float2 eye_shift = local_view_direction.xy;
	float3 front_filter = float3(1.0, 1.0, 1.0);
	float optical_return = lerp(visible_front_reflectance,
		sqrt(max(visible_front_reflectance, 0.0)), 0.35);
	float reflected_energy = optical_return * profile.reflection_exposure;
	float environment_alpha = saturate(profile.environment_strength
		* reflected_energy);
	float4 front_return = float4(environment * environment_alpha,
		environment_alpha);

	// The recessed stack contributes depth at the aperture edge. It must never
	// become a filled opaque disc over the objective.
	float2 stack_shift = eye_shift * profile.internal_depth * float2(1.0, -1.0);
	float2 tube_position = lens_position + stack_shift;
	float tube_radius = length(tube_position);
	float outer_baffle = smoothstep(0.80, 0.97, tube_radius);
	float baffle = saturate(outer_baffle);
	float4 primary_environment_ghost = svp_optic_environment_ghost(front_point, front_normal,
		local_view_direction, right, up, axis, front_reflectance, front_filter,
		profile.front_curvature, profile.reflection_exposure,
		svp_primary_environment_ghost_profile(), svp_middle_reflection_path());
	float4 secondary_environment_ghost = svp_optic_environment_ghost(front_point, front_normal,
		local_view_direction, right, up, axis, front_reflectance, front_filter,
		profile.front_curvature, profile.reflection_exposure,
		svp_secondary_environment_ghost_profile(), svp_deep_reflection_path());
	primary_environment_ghost *= profile.internal_strength;
	secondary_environment_ghost *= profile.internal_strength;
	float tube_transmission = 1.0 - baffle * 0.72;
	channels.environment = front_return * tube_transmission;
	channels.ghosts = svp_optic_over(primary_environment_ghost,
		secondary_environment_ghost)
		* tube_transmission;

	// Ambient returns appear as the eye leaves the usable optical axis. Direct
	// source ghosts use the same ownership but remain a separate channel.
	reflection_visibility = saturate(reflection_visibility);
	source_visibility = saturate(source_visibility);
	channels.environment *= reflection_visibility;
	channels.ghosts *= reflection_visibility;

	// Environment and internal returns are evaluated from the curved lens in
	// world space. Do not relight them with a weapon-wide brightness scalar;
	// directional source paths own their visibility independently.
	float4 direct_source;
	float4 ghost_source;
	svp_optic_source(normalize(-L_sun_dir_w), max(L_sun_color, 0.0),
		saturate(svp_optic_luminance(max(L_sun_color, 0.0)) * 1.5)
			* svp_optic_sun_visibility(), lens_position,
		eye_shift, normal, right, up, view_direction, profile, direct_source, ghost_source);
	channels.direct += direct_source * source_visibility;
	channels.ghosts += ghost_source * source_visibility;
	svp_optic_point_source(svp_lens_light0_position, svp_lens_light0_color, world_position,
		lens_position, eye_shift, normal, right, up, view_direction, profile,
		direct_source, ghost_source);
	channels.direct += direct_source * source_visibility;
	channels.ghosts += ghost_source * source_visibility;
	svp_optic_point_source(svp_lens_light1_position, svp_lens_light1_color, world_position,
		lens_position, eye_shift, normal, right, up, view_direction, profile,
		direct_source, ghost_source);
	channels.direct += direct_source * source_visibility;
	channels.ghosts += ghost_source * source_visibility;
	return channels;
}

float4 svp_lens_channel_debug(float4 channel)
{
	return float4(saturate(channel.rgb / max(channel.a, 0.0001)), 1.0);
}

float4 svp_composite_lens(SvpLensChannels channels)
{
	channels.environment *= svp_lens_tuning.x;
	channels.direct *= svp_lens_tuning.y;
	channels.ghosts *= svp_lens_tuning.z;

	int debug_channel = (int)round(svp_lens_control.y);
	float object_hemi = svp_optic_hemi_visibility();
	float object_sun = svp_optic_sun_visibility();
	float3 object_light = svp_optic_object_radiance();
	if (debug_channel == 1) return svp_lens_channel_debug(channels.environment);
	if (debug_channel == 2) return svp_lens_channel_debug(channels.direct);
	if (debug_channel == 3) return svp_lens_channel_debug(channels.ghosts);
	if (debug_channel == 6) return float4(0.0, 1.0, 0.0, 1.0);
	if (debug_channel == 7) return float4(saturate(object_light), 1.0);
	if (debug_channel == 8) return float4(1.0 - object_sun, object_hemi, object_sun, 1.0);
	if (debug_channel == 9)
		return float4(1.0 - object_sun, object_sun, 0.0, 1.0);

	float4 combined = 0;
	combined = svp_optic_over(channels.environment, combined);
	combined = svp_optic_over(channels.ghosts, combined);
	combined = svp_optic_over(channels.direct, combined);
	float opacity = saturate(combined.a * svp_lens_control.x);
	if (debug_channel == 5)
		return float4(opacity.xxx, 1.0);

	// Channels carry premultiplied optical energy. Existing scope materials use
	// straight-alpha blending, so unpremultiply once here instead of attenuating
	// faint reflections a second time in the blend stage.
	float3 straight_colour = combined.rgb / max(combined.a, 0.0001);
	straight_colour = straight_colour / (1.0 + straight_colour * 0.42);
	return float4(saturate(straight_colour), opacity);
}

#endif
