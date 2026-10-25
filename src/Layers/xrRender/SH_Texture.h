#ifndef SH_TEXTURE_H
#define SH_TEXTURE_H
#pragma once

#include "../../xrCore/xr_resource.h"

class ENGINE_API CAviPlayerCustom;
class CTheoraSurface;
class CGIFAnimationPlayer;

class ECORE_API CTexture : public xr_resource_named
{
public:
	//	Since DX10 allows up to 128 unique textures, 
	//	distance between enum values should be at leas 128
	enum ResourceShaderType //	Don't change this since it's hardware-dependent
	{
		rstPixel = 0,
		//	Default texture offset
		rstVertex = D3DVERTEXTEXTURESAMPLER0,
		rstGeometry = rstVertex + 256,
		rstHull = rstGeometry + 256,
		rstDomain = rstHull + 256,
		rstCompute = rstDomain + 256,
		rstInvalid = rstCompute + 256
	};

public:
	void __stdcall apply_load(u32 stage);
	void __stdcall apply_theora(u32 stage);
	void __stdcall apply_avi(u32 stage);
	void __stdcall apply_seq(u32 stage);
    void __stdcall apply_gif(u32 stage);
	void __stdcall apply_normal(u32 stage);

#if defined(USE_DX10) || defined(USE_DX11)
    std::atomic<bool> loadQueued{false};
#endif
    bool IsLoading() const {
#if defined(USE_DX10) || defined(USE_DX11)
        if(loadQueued.load(std::memory_order_acquire)) return true;
#endif
        return flags.bLoading != 0;
    }
	void Preload();
	void Load();
	void PostLoad();
	void Unload(void);
	//	void								Apply			(u32 dwStage);

	void surface_set(ID3DBaseTexture* surf);
	ID3DBaseTexture* surface_get();

	IC BOOL isUser() { return flags.bUser; }
	IC u32 get_Width()
	{
		desc_enshure();
		return desc.Width;
	}

	IC u32 get_Height()
	{
		desc_enshure();
		return desc.Height;
	}

	void video_Sync(u32 _time) { m_play_time = _time; }
	void video_Play(BOOL looped, u32 _time = 0xFFFFFFFF);
	void video_Pause(BOOL state);
	void video_Stop();
	BOOL video_IsPlaying();

	CTexture();
	virtual ~CTexture();

#if defined(USE_DX10) || defined(USE_DX11)
	ID3DShaderResourceView*				get_SRView() {return m_pSRView;}
#endif	//	USE_DX10

private:
	IC void wait_for_loading() const { while (IsLoading()){SwitchToThread();} }
	IC BOOL desc_valid() { wait_for_loading(); return pSurface==desc_cache; }
	IC void desc_enshure() { wait_for_loading(); if (!desc_valid()) desc_update(); }
	void desc_update();
#if defined(USE_DX10) || defined(USE_DX11)
	void								Apply			(u32 dwStage);
	void								ProcessStaging();
	D3D_USAGE							GetUsage();
#endif	//	USE_DX10

	//	Class data
public: //	Public class members (must be encapsulated furthur)
	struct
	{
		u32 bLoaded : 1;
		u32 bLoading : 1;
		u32 bUser : 1;
		u32 seqCycles : 1;
		u32 MemoryUsage : 27;
#if defined(USE_DX10) || defined(USE_DX11)
		u32					bLoadedAsStaging: 1;
#endif	//	USE_DX10
	} flags;

    u32 dwLastUsedFrame = 0; // frame index of last Apply() call — used for eviction

	xr_delegate<void(u32)> bind;


	CAviPlayerCustom* pAVI;
	CTheoraSurface* pTheora;
	float m_material;
	shared_str m_bumpmap;

	bool m_is_hot = false;		//--DSR-- HeatVision
	bool m_is_glowing = false;	//--DSR-- SilencerOverheat

#if RENDER == R_R4
	// Reticle atlas metadata follows this resource's lifetime; -1 means unread, 0 means no atlas.
	float reticle_glow_size = -1.f;
#endif

	union
	{
		u32 m_play_time; // sync theora time
		u32 seqMSPF; // Sequence data milliseconds per frame
	};

private:
	ID3DBaseTexture* pSurface;
	// Sequence data
	xr_vector<ID3DBaseTexture*> seqDATA;

	// Description
	ID3DBaseTexture* desc_cache;
	D3D_TEXTURE2D_DESC desc;

    CGIFAnimationPlayer* gifPlayer;

#if defined(USE_DX10) || defined(USE_DX11)
	ID3DShaderResourceView*			m_pSRView;
	// Sequence view data
	xr_vector<ID3DShaderResourceView*>m_seqSRView;
#endif	//	USE_DX10
};

struct resptrcode_texture : public resptr_base<CTexture>
{
	void create(LPCSTR _name);
	void destroy() { _set(NULL); }
	shared_str bump_get() { while (_get() && _get()->IsLoading()) { SwitchToThread(); }return _get()->m_bumpmap; }
	bool bump_exist() { while (_get() && _get()->IsLoading()) { SwitchToThread(); }return 0!=bump_get().size(); }
};

typedef resptr_core<CTexture, resptrcode_texture>
ref_texture;

#endif
