#pragma once

inline bool SvpOverlayActive(float optic_type, int markswitch)
{
	return optic_type >= 0.5f && ((optic_type < 1.5f) ? (markswitch == 0) : (markswitch < 2));
}

inline bool SvpThermalOverlayActive(float optic_type, int markswitch)
{
	return optic_type >= 1.5f && SvpOverlayActive(optic_type, markswitch);
}
