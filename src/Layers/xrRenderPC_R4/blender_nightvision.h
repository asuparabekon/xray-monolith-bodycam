#pragma once


class CBlender_nightvision : public IBlender
{
public:
	virtual LPCSTR getComment() { return "nightvision"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_nightvision();
	virtual ~CBlender_nightvision();
};

//crookr
class CBlender_fakescope : public IBlender
{
public:
	virtual LPCSTR getComment() { return "fakescope"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_fakescope();
	virtual ~CBlender_fakescope();
};

// Immersive Identification: dead-head glitch stamp (ii_glitch.ps)
class CBlender_ii_glitch : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ii_glitch"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ii_glitch();
	virtual ~CBlender_ii_glitch();
};

// Immersive Identification: in-scope identification marker sprite (ii_svp_marker.ps)
class CBlender_ii_svp_marker : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ii_svp_marker"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ii_svp_marker();
	virtual ~CBlender_ii_svp_marker();
};

//--DSR-- HeatVision_start
class CBlender_heatvision : public IBlender
{
public:
	virtual LPCSTR getComment() { return "heatvision"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_heatvision();
	virtual ~CBlender_heatvision();
};
//--DSR-- HeatVision_end
