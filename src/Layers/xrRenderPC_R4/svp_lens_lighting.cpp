#include "stdafx.h"
#include "r4.h"
#include "svp_lens_lighting.h"
#include "../../xrEngine/xr_object.h"

#include "../xrRender/ResourceManager.h"
#include "../xrRender/svp_console.h"
#include "../xrRender/svp_lens_authoring.h"

extern Fvector4 ps_s3ds_param_2;
extern int ps_ssfx_is_underground;

namespace
{
constexpr u32 LensLightCount = 2;
constexpr u32 MaxInspectedLights = 128;

struct LensLight
{
	Fvector position = {};
	Fvector color = {};
	float range = 0.f;
	float score = 0.f;
	Fvector direction = {};
	float coneCosine = -1.f;
	float glassVisibility = 0.f;
	bool castsShadow = false;
};

LensLight selectedLights[LensLightCount];
u32 selectionFrame = u32(-1);
u32 inspectedLights = 0;

float ScoreLight(light& source, const Fvector& lensPosition)
{
	if (!source.flags.bActive || source.range <= EPS)
		return 0.f;

	Fvector toLens;
	toLens.sub(lensPosition, source.position);
	const float distance = toLens.magnitude();
	if (distance >= source.range)
		return 0.f;

	float visibility = 1.f - distance / source.range;
	visibility *= visibility;
	if (source.get_type() == IRender_Light::SPOT)
	{
		toLens.normalize_safe();
		const float outerCosine = cosf(source.cone * 0.5f);
		const float coneWidth = _max(_min(outerCosine + 0.08f, 1.f) - outerCosine, EPS);
		visibility *= clampr((source.direction.dotproduct(toLens) - outerCosine) / coneWidth, 0.f, 1.f);
	}

	const float luminance = 0.2126f * source.color.r + 0.7152f * source.color.g + 0.0722f * source.color.b;
	return _max(luminance, 0.f) * visibility;
}

void InsertLight(const LensLight& candidate)
{
	if (candidate.score <= selectedLights[1].score)
		return;

	if (candidate.score > selectedLights[0].score)
	{
		selectedLights[1] = selectedLights[0];
		selectedLights[0] = candidate;
	}
	else
		selectedLights[1] = candidate;
}

void ConsiderLight(light* source, const Fvector& lensPosition)
{
	if (!source)
		return;

	LensLight candidate;
	candidate.score = ScoreLight(*source, lensPosition);
	if (candidate.score <= selectedLights[1].score)
		return;

	candidate.position = source->position;
	candidate.color.set(source->color.r, source->color.g, source->color.b);
	candidate.range = source->range;
	candidate.direction = source->direction;
	candidate.coneCosine = source->get_type() == IRender_Light::SPOT ? cosf(source->cone * .5f) : -1.f;
	candidate.castsShadow = source->flags.bShadow;
	InsertLight(candidate);
}

void CaptureLights(const light_Package& normal, const light_Package& pending)
{
	selectionFrame = Device.dwFrame;
	selectedLights[0] = {};
	selectedLights[1] = {};
	inspectedLights = 0;
	const Fvector& lensPosition = Device.vCameraPosition;
	light* seenLights[MaxInspectedLights] = {};

	auto inspect = [&](const xr_vector<light*>& sources)
	{
		for (light* source : sources)
		{
			if (source && source->get_type() == IRender_Light::OMNIPART)
				source = source->omipart_parent;
			if (!source)
				continue;

			bool seen = false;
			for (u32 index = 0; index < inspectedLights; ++index)
				seen |= seenLights[index] == source;
			if (seen)
				continue;
			if (inspectedLights >= MaxInspectedLights)
				return;

			seenLights[inspectedLights++] = source;
			ConsiderLight(source, lensPosition);
		}
	};
	auto inspectPackage = [&](const light_Package& package)
	{
		inspect(package.v_point);
		inspect(package.v_spot);
		inspect(package.v_shadowed);
	};

	inspectPackage(normal);
	if (inspectedLights < MaxInspectedLights)
		inspectPackage(pending);

	// Two selected lights only; no extra scene capture or second-view light walk.
	// Coarse visibility at the eye matches the existing HUD light sampling domain.
	// Per-fragment direction, range and spotlight falloff are evaluated on glass.
	for (auto& selected : selectedLights)
	{
		if (selected.score <= 0.f)
			continue;
		selected.glassVisibility = 1.f;
		if (selected.castsShadow && g_pGameLevel)
		{
			Fvector direction;
			direction.sub(selected.position, lensPosition);
			const float distance = direction.magnitude();
			if (distance > EPS)
			{
				direction.div(distance);
				if (g_pGameLevel->ObjectSpace.RayTest(
						lensPosition, direction, distance, collide::rqtBoth, nullptr, g_pGameLevel->CurrentViewEntity()))
					selected.glassVisibility = 0.f;
			}
		}
	}

	if (ps_r__svp_lens_diagnostics && Device.dwTimeGlobal % 1000 < Device.dwTimeDelta)
	{
		Msg("[SVP-LENS] inspected=%u selected={%.6f, %.6f} debug=%d", inspectedLights, selectedLights[0].score, selectedLights[1].score,
			ps_r__svp_lens_debug);
		Msg("[GLASS-LIGHT] frame=%u eye=(%.4f,%.4f,%.4f) light0=(%.4f,%.4f,%.4f,%.4f) rgb0=(%.4f,%.4f,%.4f) vis0=%.1f light1=(%.4f,%.4f,%.4f,%.4f) rgb1=(%.4f,%.4f,%.4f) vis1=%.1f",
			Device.dwFrame, lensPosition.x, lensPosition.y, lensPosition.z, selectedLights[0].position.x, selectedLights[0].position.y,
			selectedLights[0].position.z, selectedLights[0].range, selectedLights[0].color.x, selectedLights[0].color.y,
			selectedLights[0].color.z, selectedLights[0].glassVisibility, selectedLights[1].position.x, selectedLights[1].position.y,
			selectedLights[1].position.z, selectedLights[1].range, selectedLights[1].color.x, selectedLights[1].color.y,
			selectedLights[1].color.z, selectedLights[1].glassVisibility);
	}
}

template <u32 LightIndex, bool Position>
class LensLightBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		const bool current = selectionFrame == Device.dwFrame;
		const LensLight source = current ? selectedLights[LightIndex] : LensLight{};
		if (Position)
			RCache.set_c(constant, source.position.x, source.position.y, source.position.z, source.range);
		else
			RCache.set_c(constant, source.color.x, source.color.y, source.color.z, source.score > 0.f ? 1.f : 0.f);
	}
};

// Separate constants leave the established native lens/ghost response intact.
template <u32 LightIndex, u32 Field>
class GlassLightBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		const LensLight source = selectionFrame == Device.dwFrame ? selectedLights[LightIndex] : LensLight{};
		if (Field == 0)
			RCache.set_c(constant, source.position.x, source.position.y, source.position.z, source.range);
		else if (Field == 1)
			RCache.set_c(constant, source.color.x, source.color.y, source.color.z, source.glassVisibility);
		else
			RCache.set_c(constant, source.direction.x, source.direction.y, source.direction.z, source.coneCosine);
	}
};
GlassLightBinder<0, 0> glass0Position;
GlassLightBinder<0, 1> glass0Color;
GlassLightBinder<0, 2> glass0Direction;
GlassLightBinder<1, 0> glass1Position;
GlassLightBinder<1, 1> glass1Color;
GlassLightBinder<1, 2> glass1Direction;

class LensTuningBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		RCache.set_c(constant, ps_r__svp_lens_environment, ps_r__svp_lens_direct, ps_r__svp_lens_ghosts, 0.f); // w reserved
	}
};

// The 3DSS lens reflection hue and brightness of the current optic, as offsets
// from neutral: x = coating hue shift in turns, y = reflection scale - 1.
class LensAuthoringBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		const SvpLensAuthoring authoring = svp_lens_authoring(ps_s3ds_param_2.x, ps_s3ds_param_2.y);
		RCache.set_c(constant, authoring.hue_shift, authoring.reflection_scale - 1.f, 0.f, 0.f);
	}
};

// The player's lens look adjustments, as offsets from neutral (scope_glass_authoring.h). PiP only:
// without true PiP every value is neutral, so the stock 3DSS lens keeps its look.
class LensAdjustBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		if (!Device.true_pip_on)
		{
			RCache.set_c(constant, 0.f, 0.f, 0.f, 0.f);
			return;
		}
		RCache.set_c(constant, ps_r__svp_lens_hue, ps_r__svp_lens_saturation - 1.f, ps_r__svp_lens_blur, ps_r__svp_lens_dirt - 1.f);
	}
};
// Environment reflection and glint strength for the PiP glass material passes; the classic lens
// takes the same settings through svp_lens_tuning.
class LensAdjustLightBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		if (!Device.true_pip_on)
		{
			RCache.set_c(constant, 0.f, 0.f, 0.f, 0.f);
			return;
		}
		RCache.set_c(constant, ps_r__svp_lens_environment - 1.f, ps_r__svp_lens_direct - 1.f, 0.f, 0.f);
	}
};

class LensControlBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override { RCache.set_c(constant, ps_r__svp_lens_opacity, float(ps_r__svp_lens_debug), 0.f, 0.f); }
};

class LensEnvironmentBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		if (!g_pGamePersistent || !g_pGamePersistent->Environment().CurrentEnv)
		{
			RCache.set_c(constant, 1.f, 1.f, 1.f, 0.f);
			return;
		}

		const CEnvDescriptorMixer& environment = *g_pGamePersistent->Environment().CurrentEnv;
		const float radianceScale = 4.f * ps_r2_sun_lumscale_hemi;
		static u32 diagnosticFrame = u32(-1);
		if (ps_r__svp_cop_diag > 1 && Device.dwFrame % 120 == 0 && diagnosticFrame != Device.dwFrame)
		{
			diagnosticFrame = Device.dwFrame;
			const CEnvironment& context = g_pGamePersistent->Environment();
			Msg("[Glass environment] frame=%u first=%s second=%s blend=%.6f scale=%.6f,%.6f,%.6f", Device.dwFrame,
				context.Current[0] ? context.Current[0]->sky_texture_env_name.c_str() : "none",
				context.Current[1] ? context.Current[1]->sky_texture_env_name.c_str() : "none", environment.weight,
				environment.hemi_color.x * radianceScale, environment.hemi_color.y * radianceScale,
				environment.hemi_color.z * radianceScale);
		}
		RCache.set_c(constant, environment.hemi_color.x * radianceScale + EPS, environment.hemi_color.y * radianceScale + EPS,
			environment.hemi_color.z * radianceScale + EPS, environment.weight);
	}
};

LensLightBinder<0, true> light0PositionBinder;
LensLightBinder<0, false> light0ColorBinder;
LensLightBinder<1, true> light1PositionBinder;
LensLightBinder<1, false> light1ColorBinder;
LensTuningBinder tuningBinder;
LensAuthoringBinder authoringBinder;
LensControlBinder controlBinder;
LensEnvironmentBinder environmentBinder;

// Weather cube convention and the underground flag for the lens (svp_lens_environment.h).
class LensEnvConventionBinder final : public R_constant_setup
{
	void setup(R_constant* constant) override
	{
		RCache.set_c(constant, float(ps_r__svp_lens_env_convention), ps_ssfx_is_underground ? 1.f : 0.f, 0.f, 0.f);
	}
};
LensEnvConventionBinder envConventionBinder;
LensAdjustBinder adjustBinder;
LensAdjustLightBinder adjustLightBinder;

}

void SvpLensLighting::RegisterConstants(CResourceManager& resources)
{
	resources.RegisterConstantSetup("scope_glass_light0_position", &glass0Position);
	resources.RegisterConstantSetup("scope_glass_light0_color", &glass0Color);
	resources.RegisterConstantSetup("scope_glass_light0_direction", &glass0Direction);
	resources.RegisterConstantSetup("scope_glass_light1_position", &glass1Position);
	resources.RegisterConstantSetup("scope_glass_light1_color", &glass1Color);
	resources.RegisterConstantSetup("scope_glass_light1_direction", &glass1Direction);
	resources.RegisterConstantSetup("svp_lens_light0_position", &light0PositionBinder);
	resources.RegisterConstantSetup("svp_lens_light0_color", &light0ColorBinder);
	resources.RegisterConstantSetup("svp_lens_light1_position", &light1PositionBinder);
	resources.RegisterConstantSetup("svp_lens_light1_color", &light1ColorBinder);
	resources.RegisterConstantSetup("svp_lens_tuning", &tuningBinder);
	resources.RegisterConstantSetup("svp_lens_authoring", &authoringBinder);
	resources.RegisterConstantSetup("svp_lens_control", &controlBinder);
	resources.RegisterConstantSetup("svp_lens_environment", &environmentBinder);
	resources.RegisterConstantSetup("svp_lens_env_convention", &envConventionBinder);
	resources.RegisterConstantSetup("svp_lens_adjust", &adjustBinder);
	resources.RegisterConstantSetup("svp_lens_adjust_light", &adjustLightBinder);
}

void SvpLensLighting::CaptureSceneLights(const light_Package& normal, const light_Package& pending)
{
	CaptureLights(normal, pending);
}

void SvpLensLighting::BindObjectLighting(IRenderable* owner)
{
	// a null owner must not apply the object bus, so apply_object runs only for a lit owner
	const bool hasLighting = owner && owner->renderable_ROS();
	if (hasLighting)
		RImplementation.apply_object(owner);
	else
		RImplementation.svp_clear_lens_object_light();

	const float hemi = hasLighting ? RImplementation.o_hemi : 0.f;
	const float sun = hasLighting ? RImplementation.o_sun : 0.f;

	static u32 lastDiagnostic = 0;
	if (ps_r__svp_lens_diagnostics && Device.dwTimeGlobal - lastDiagnostic >= 1000)
	{
		lastDiagnostic = Device.dwTimeGlobal;
		Msg("[SVP-LENS] owner=%p ros=%d hemi=%.3f sun=%.3f", owner, hasLighting ? 1 : 0, hemi, sun);
	}
}
