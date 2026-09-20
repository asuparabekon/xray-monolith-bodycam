#include "stdafx.h"

#include "blender_nightvision.h"

CBlender_nightvision::CBlender_nightvision() { description.CLS = 0; }
CBlender_fakescope::CBlender_fakescope() { description.CLS = 0; } //crookr
CBlender_bodycam_redaction::CBlender_bodycam_redaction() { description.CLS = 0; } // Bodycam
CBlender_bodycam_redaction::~CBlender_bodycam_redaction() {}
CBlender_bodycam_svp_marker::CBlender_bodycam_svp_marker() { description.CLS = 0; } // Bodycam
CBlender_bodycam_svp_marker::~CBlender_bodycam_svp_marker() {}
CBlender_heatvision::CBlender_heatvision() { description.CLS = 0;  } //--DSR-- Heatvision

CBlender_nightvision::~CBlender_nightvision()
{
}

CBlender_fakescope::~CBlender_fakescope() //crookr
{
}

//--DSR-- HeatVision_start
CBlender_heatvision::~CBlender_heatvision()
{
}
//--DSR-- HeatVision_end

void CBlender_nightvision::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);
	switch (C.iElement)
	{
	case 0: //Dummy shader - because IDK what gonna happen when r2_nightvision will be 0
		C.r_Pass("stub_screen_space", "copy_nomsaa", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_generic", r2_RT_generic0);

		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1:	
		C.r_Pass("stub_screen_space", "nightvision_gen_1", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_position", r2_RT_P);	
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);	
		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
		C.r_dx10Texture("s_blur_8", r2_RT_blur_8);		

		C.r_dx10Texture("s_heat", r2_RT_heat); //--DSR-- HeatVision

			
		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;	
	case 2:	
		C.r_Pass("stub_screen_space", "nightvision_gen_2", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_position", r2_RT_P);	
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);	
		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
		C.r_dx10Texture("s_blur_8", r2_RT_blur_8);		
		
		C.r_dx10Texture("s_heat", r2_RT_heat); //--DSR-- HeatVision

		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;	
	case 3:	
		C.r_Pass("stub_screen_space", "nightvision_gen_3", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_position", r2_RT_P);	
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);	
		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
		C.r_dx10Texture("s_blur_8", r2_RT_blur_8);		
		
		C.r_dx10Texture("s_heat", r2_RT_heat); //--DSR-- HeatVision

		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;		
	}
}

void CBlender_fakescope::Compile(CBlender_Compile& C) //crookr
{
	IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "fakescope", FALSE, FALSE, FALSE);
	C.r_dx10Texture("s_position", r2_RT_P);
	C.r_dx10Texture("s_image", r2_RT_generic0);
	C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);
	C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
	C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
	C.r_dx10Texture("s_blur_8", r2_RT_blur_8);
	//C.r_dx10Texture("s_scope", "wpn\\wpn_crosshair_pso1");
	//C.r_dx10Texture("s_scope", scope_fake_texture);
	C.r_dx10Texture("s_scope", r2_RT_scopert);


	C.r_dx10Sampler("smp_base");
	C.r_dx10Sampler("smp_nofilter");
	C.r_dx10Sampler("smp_rtlinear");
	C.r_End();

}

//--DSR-- HeatVision_start
// Bodycam: dead-head redaction stamp. Binds the scene colour RT
// (r2_RT_generic0) as s_image and selects bodycam_redaction.ps over the fullscreen quad
// vertex shader; region restriction is done engine-side via a scissor rect in
// phase_redaction(). Mirrors CBlender_fakescope above, minus the extra scope inputs.
void CBlender_bodycam_redaction::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "bodycam_redaction", FALSE, FALSE, FALSE);
	C.r_dx10Texture("s_image", r2_RT_generic0);
	C.r_dx10Texture("s_position", r2_RT_P);   // view-space position/depth, to mask the viewmodel + foreground
	C.r_dx10Sampler("smp_base");
	C.r_dx10Sampler("smp_nofilter");
	C.r_End();
}

// Bodycam: in-scope marker sprite. Solid dot/ring/spinner drawn
// per marker over the scope image -- no scene sampling, alpha-blended so it
// overlays translucently. r_Pass alpha-blend params: SRCALPHA / INVSRCALPHA.
void CBlender_bodycam_svp_marker::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	C.r_Pass("stub_screen_space", "bodycam_svp_marker", FALSE, FALSE, FALSE,
		TRUE, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
	C.r_End();
}

void CBlender_heatvision::Compile(CBlender_Compile& C)
{
	IBlender::Compile(C);

	switch (C.iElement)
	{
	case 0: //Dummy shader - because IDK what gonna happen when r2_nightvision will be 0
		C.r_Pass("stub_screen_space", "copy_nomsaa", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_generic", r2_RT_generic0);

		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	case 1:
		C.r_Pass("stub_screen_space", "heatvision", FALSE, FALSE, FALSE);
		C.r_dx10Texture("s_position", r2_RT_P);
		C.r_dx10Texture("s_image", r2_RT_generic0);
		C.r_dx10Texture("s_bloom_new", r2_RT_pp_bloom);
		C.r_dx10Texture("s_blur_2", r2_RT_blur_2);
		C.r_dx10Texture("s_blur_4", r2_RT_blur_4);
		C.r_dx10Texture("s_blur_8", r2_RT_blur_8);

		C.r_dx10Texture("s_heat", r2_RT_heat); //--DSR-- HeatVision


		C.r_dx10Sampler("smp_base");
		C.r_dx10Sampler("smp_nofilter");
		C.r_dx10Sampler("smp_rtlinear");
		C.r_End();
		break;
	}
}
//--DSR-- HeatVision_end
