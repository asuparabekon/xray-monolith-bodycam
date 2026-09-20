#ifndef IRENDERABLE_H_INCLUDED
#define IRENDERABLE_H_INCLUDED

#include "render.h"

//////////////////////////////////////////////////////////////////////////
// definition ("Renderable")
class ENGINE_API IRenderable:
	public ISpatialOwner
{
public:
	struct
	{
		Fmatrix xform;
		IRenderVisual* visual;
		IRender_ObjectSpecific* pROS;
		BOOL pROS_Allowed;
	} renderable;

public:
	IRenderable();
	virtual ~IRenderable();
	IRender_ObjectSpecific* renderable_ROS();
	BENCH_SEC_SCRAMBLEVTBL2
	virtual void renderable_Render(IDSGraphManager* DM) = 0;
	virtual BOOL renderable_ShadowGenerate() { return FALSE; };
	virtual BOOL renderable_ShadowReceive() { return FALSE; };

	// Bodycam: when true, this object's main-pass g-buffer pixels
	// are tagged with the redaction stencil bit so the redaction post-effect hugs
	// its exact silhouette. Default false = unaffected. Set on the mod's corpse set.
	virtual bool renderable_bodycam_redact() const { return false; }

	virtual float GetHotness() { return 0.0; }			//--DSR-- HeatVision
	virtual float GetTransparency() { return 0.0; }		//--DSR-- HeatVision
	virtual float GetGlowing() { return 0.0; }			//--DSR-- SilencerOverheat

	virtual IRenderable* dcast_Renderable() override { return this; }
};

#endif // IRENDERABLE_H_INCLUDED
