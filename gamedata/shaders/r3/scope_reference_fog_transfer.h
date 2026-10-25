#ifndef SCOPE_REFERENCE_FOG_TRANSFER_H
#define SCOPE_REFERENCE_FOG_TRANSFER_H
#include "scope_reference_scene_tone.h"

// SSFX fog scattering works on the display image, so the blur and the mix happen there.
// imageDisplay is scope_reference_scene_tone(raw, gamma, meter).
float3 scope_reference_fog_mix(float3 raw, float3 imageDisplay, float3 blurredDisplay, float weight,
    float gamma, float meter)
{
    if (weight <= 1e-6) return raw;
    float3 display = lerp(imageDisplay, max(imageDisplay, blurredDisplay), weight);
    return scope_reference_scene_tone_inverse(display, gamma, meter);
}

#endif
