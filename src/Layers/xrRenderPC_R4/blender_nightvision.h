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

// Bodycam: redaction stamp (bodycam_redaction.ps)
class CBlender_bodycam_redaction : public IBlender
{
public:
	virtual LPCSTR getComment() { return "bodycam_redaction"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_bodycam_redaction();
	virtual ~CBlender_bodycam_redaction();
};

// Bodycam: in-scope identification marker sprite (bodycam_svp_marker.ps)
class CBlender_bodycam_svp_marker : public IBlender
{
public:
	virtual LPCSTR getComment() { return "bodycam_svp_marker"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_bodycam_svp_marker();
	virtual ~CBlender_bodycam_svp_marker();
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
