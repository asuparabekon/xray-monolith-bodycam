#pragma once

struct SSvpEyeSample
{
	Fvector2 raw_mm = {};
	Fvector2 residual_mm = {};
	float eye_relief_mm = 0.f;
	float entrance_scale = 1.f;
	bool valid = false;
};

// Samples the exit pupil and advances the virtual eye once per frame
SSvpEyeSample svp_update_eye_sample(const Fmatrix& eye_view);
