#pragma once
#include <cstdint>
#include <cmath>

namespace BodycamReference
{
// Half of an angle in degrees, in radians.
constexpr double kHalfDegreesToRadians=3.14159265358979323846/360.;
// The HUD FOV the engine draws at hud_fov 1, in degrees; psHUD_FOV scales it.
constexpr float kHudFovDegrees=83.f;
// How much larger the HUD is drawn at engineFov than at unobstructedFov: what
// the engine's near-wall FOV change does to the picture.
inline float NearwallProjectionScale(float engineFov,float unobstructedFov)
{
    if(engineFov<=0||unobstructedFov<=0||engineFov>=179||unobstructedFov>=179)return 1.f;
    return float(std::tan(engineFov*kHalfDegreesToRadians)/std::tan(unobstructedFov*kHalfDegreesToRadians));
}
// The clip-space scale that makes a HUD projected at sceneFov look as it does
// at engineHudFov, faded in by adsAlpha (0..1). It applies to X and Y at every depth.
inline float AdsClipScale(float sceneFov,float engineHudFov,float adsAlpha)
{
    return 1.f+adsAlpha*(1.f/NearwallProjectionScale(engineHudFov,sceneFov)-1.f);
}
// The Reference projection applies only in the frame the game marked with
// referenceFrame; any other frame keeps the engine's HUD projection.
inline bool IsReferenceFrame(std::uint32_t frame,std::uint32_t referenceFrame)
{
    return referenceFrame!=UINT32_MAX&&referenceFrame==frame;
}
inline float HudProjectionClipScale(std::uint32_t frame,std::uint32_t referenceFrame,float scale)
{
    return IsReferenceFrame(frame,referenceFrame)?scale:1.f;
}
inline float HudProjectionFov(std::uint32_t frame, std::uint32_t referenceFrame,
    float sceneFov, float stockHudFov, bool truePipFov,float nearwallScale=1.f)
{
    if(IsReferenceFrame(frame,referenceFrame)){
        if(nearwallScale==1)return sceneFov;
        return float(std::atan(std::tan(sceneFov*kHalfDegreesToRadians)*nearwallScale)/kHalfDegreesToRadians);
    }
    return truePipFov?sceneFov:stockHudFov;
}
}
