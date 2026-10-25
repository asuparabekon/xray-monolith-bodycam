// The two outputs of a reticle atlas emission pass (svp_reticle_emission.cpp binds the glow targets there): the lit
// core, and the same light weighted by the atlas's glow strength, which the engine spreads into the glow.
#ifndef RETICLE_EMISSION_OUT_H
#define RETICLE_EMISSION_OUT_H
struct ReticleEmissionOut
{
	float4 core : SV_Target0;
	float4 glow : SV_Target1;
};
#endif
