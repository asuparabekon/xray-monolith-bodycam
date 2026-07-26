#ifndef svp_magsH
#define svp_magsH
#pragma once

// pip authored magnifications derive 75 base zoom endpoints from a scope section

enum svp_mag_mode { svp_mag_none, svp_mag_fixed, svp_mag_dynamic, svp_mag_stepped };

struct svp_mags_data
{
	svp_mag_mode mode = svp_mag_none;
	float f_top = 0.f;
	float f_floor = 0.f;
};

// svp_magnifications wins, then standard 3DSS fields, none keeps legacy zoom
svp_mags_data svp_mags_resolve(LPCSTR sect, float zoom_multiple);

#endif
