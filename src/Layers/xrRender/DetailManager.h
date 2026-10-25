// DetailManager.h: interface for the CDetailManager class.
//
//////////////////////////////////////////////////////////////////////

#ifndef DetailManagerH
#define DetailManagerH
#pragma once

#include "../../xrCore/xrpool.h"
#include "../../xrCDB/Frustum.h"
#include "detailformat.h"
#include "detailmodel.h"
#include "light.h"

#ifdef _EDITOR
//.	#include	"ESceneClassList.h"
	const int	dm_max_decompress	= 14;
	class CCustomObject;
	typedef u32	ObjClassID;

    typedef xr_list<CCustomObject*> 		ObjectList;
    typedef ObjectList::iterator 			ObjectIt;
    typedef xr_map<ObjClassID,ObjectList> 	ObjectMap;
    typedef ObjectMap::iterator 			ObjectPairIt;

#else
const int dm_max_decompress = 7;
#endif
//const int		dm_size				= 24;								//!
const int dm_cache1_count = 4; // 
//const int 		dm_cache1_line		= dm_size*2/dm_cache1_count;		//! dm_size*2 must be div dm_cache1_count
const int dm_max_objects = 16383; // v4 14-bit id range (0x3FFF reserved for empty)
const int dm_obj_in_slot = 4;
//const int		dm_cache_line		= dm_size+1+dm_size;
//const int		dm_cache_size		= dm_cache_line*dm_cache_line;
//const float		dm_fade				= float(2*dm_size)-.5f;
const float dm_slot_size = DETAIL_SLOT_SIZE;


//AVO: detail radius
#include "../../build_config_defines.h"
#ifdef DETAIL_RADIUS
const u32 dm_max_cache_size = 62001 * 2; // assuming max dm_size = 124
extern u32 dm_size;
extern u32 dm_cache1_line;
extern u32 dm_cache_line;
extern u32 dm_cache_size;
extern float dm_fade;
extern u32 dm_current_size; //				= iFloor((float)ps_r__detail_radius/4)*2;				//!
extern u32 dm_current_cache1_line;
//		= dm_current_size*2/dm_cache1_count;		//! dm_current_size*2 must be div dm_cache1_count
extern u32 dm_current_cache_line; //		= dm_current_size+1+dm_current_size;
extern u32 dm_current_cache_size; //		= dm_current_cache_line*dm_current_cache_line;
extern float dm_current_fade; //				= float(2*dm_current_size)-.5f;
extern float ps_current_detail_density;
extern float ps_current_detail_height;
#else
const int		dm_size = 24;								//!
const int 		dm_cache1_line = dm_size * 2 / dm_cache1_count;		//! dm_size*2 must be div dm_cache1_count
const int		dm_cache_line = dm_size + 1 + dm_size;
const int		dm_cache_size = dm_cache_line * dm_cache_line;
const float		dm_fade = float(2 * dm_size) - .5f;
#endif


class ECORE_API CDetailManager
{
public:

	float fade_distance = 99999;
	Fvector light_position;

	void details_clear();

	struct SlotItem
	{
		// один кустик
		float scale;
		Fmatrix mRotY;
		Fmatrix mRotY_calculated;
		u32 vis_ID; // индекс в visibility списке он же тип [не качается, качается1, качается2]
		float c_hemi;
		float c_sun;
		float distance;
		Fvector position;
		Fvector normal;
		float alpha;
		float alpha_target;
#if RENDER==R_R1
		Fvector c_rgb;
#endif
	};

	DEFINE_VECTOR(SlotItem*, SlotItemVec, SlotItemVecIt);

	struct SlotPart
	{
		// 
		u32 id; // ID модельки
		SlotItemVec items; // список кустиков
		SlotItemVec r_items[3]; // список кустиков for render

		// Item box union, item origin box, largest item height off its origin
		Fbox occ_B, occ_O;
		float occ_H;
	};

	enum SlotType
	{
		stReady = 0,
		// Ready to use
		stPending,
		// Pending for decompression

		stFORCEDWORD = 0xffffffff
	};

	struct Slot
	{
		// распакованый слот размером DETAIL_SLOT_SIZE
		struct
		{
			u32 empty :1;
			u32 type :1;
			u32 frame :30;
		};

		int sx, sz; // координаты слота X x Y
		vis_data vis; // 
		SlotPart G[dm_obj_in_slot]; // 
		bool hidden;
		float cull_R;

		Slot()
		{
			frame = 0;
			empty = 1;
			type = stReady;
			sx = sz = 0;
			cull_R = 0;
			vis.clear();
		}
	};

	struct CacheSlot1
	{
		u32 empty;
		vis_data vis;
		Slot** slots[dm_cache1_count * dm_cache1_count];

		CacheSlot1()
		{
			empty = 1;
			vis.clear();
		}
	};

	typedef xr_vector<xr_vector<SlotItemVec*>> vis_list;
	typedef xr_vector<CDetail*> DetailVec; // dynamic; 14-bit id range enforced at Load()
	typedef DetailVec::iterator DetailIt;
	typedef poolSS<SlotItem, 4096> PSS;
public:
	int dither [16][16];
public:
	// swing values
	struct SSwingValue
	{
		float rot1;
		float rot2;
		float amp1;
		float amp2;
		float speed;
		void lerp(const SSwingValue& v1, const SSwingValue& v2, float factor);
	};

	SSwingValue swing_desc[2];
	SSwingValue swing_current;
	float m_time_rot_1;
	float m_time_rot_2;
	float m_time_pos;
	float m_global_time_old;
public:
	IReader* dtFS;
	DetailHeader dtH;
	DetailSlot* dtSlots; // note: pointer into VFS
	DetailSlot DS_empty;

public:
	DetailVec objects;
	vis_list m_visibles [3]; // 0=still, 1=Wave1, 2=Wave2

	xr_vector<xr_vector<Fsphere>> m_vis_bounds [3];
	u32 m_vis_bounds_frame = u32(-1);
	CFrustum* m_sun_cull = nullptr;

	// pip the scope gbuffer pass between svp_Begin and svp_End, it reuses the main pass spans, culls
	// them against the scope view and skips the main camera's occlusion, thinning and alpha step
	bool m_svp_pass = false;
	CFrustum m_svp_cull_frustum;
	CFrustum* m_svp_cull = nullptr;
	void svp_Begin(Fmatrix& full_xform);
	void svp_End();

	// slot spheres tested against the sun cascade or scope frustum, read by the pip stats overlay
	struct CullStats { u32 tested, kept, dropped, runs, run_max, draws; };
	CullStats m_cull_stats = {};
	bool cull_Span(Fsphere b, light* L, float grow, bool frustum, u32 count);

	bool m_occ_first_group = false;

	bool m_occ_first = false;

	// List j of a slot is part * 3 + vis id
	struct SlotRows
	{
		xr_vector<Fvector4> rows;
		xr_vector<Fvector4> ex;
		u32 first[dm_obj_in_slot * 3 + 1];
		Fvector P;
		float distance;
		u32 epoch = 0;
		u32 pack = 0;
		u16 ready = 0;
	};
	xr_vector<SlotRows> m_rows;
	xr_vector<xr_vector<u32>> m_vis_rows [3];
	u32 m_vis_rows_frame = u32(-1);
	u32 m_rows_epoch = 0;
	u32 m_rows_stamp = 0;
	u32 m_rows_mode = 0;
	bool m_rows_ex = false;

#ifdef USE_DX11
	dx10ConstantBuffer* m_cb_direct_target = nullptr;
	u8* m_cb_direct_image = nullptr;
	u32 m_cb_direct_array_off = 0;
	u32 m_cb_direct_ex_off = u32(-1);
	u32 m_cb_direct_seg[3][2];
	u32 m_cb_direct_segs = 0;
	dx10ConstantBuffer* cb_direct_begin(shared_str& array, shared_str& ex, Fvector4* ex_old);
	u8* cb_direct_map();
	void cb_direct_submit(u32 count);

	struct InstTwin { ref_selement E; u8 rows = 0xff, ex = 0xff; };
	xr_vector<InstTwin> m_inst_twins;
	bool m_inst_ex = false;

	struct MergeTwin { ref_selement E, thin; u8 verts = 0xff; ref_selement occ, occ_thin; u8 idx = 0xff; };
	xr_map<ShaderElement*, MergeTwin> m_merge;

	bool m_thin_on = false;
	Fvector4 m_thin_c[2] = {};
	xr_vector<u32> m_merge_first;
	xr_vector<u32> m_merge_base;
	ID3D11Buffer* m_merge_vb = nullptr;
	ID3D11ShaderResourceView* m_merge_srv = nullptr;
	ID3D11Buffer* m_merge_ib = nullptr;
	void merge_Load();

	xr_vector<float> m_occ_frac;

	// List j of var v starts at m_inst_first[v][j]
	struct InstSpan { SlotItemVec* items; u32 first, count, rows_id; Fvector P; float distance; };
	xr_vector<InstSpan> m_inst_spans;
	xr_vector<u32> m_inst_first[3];
	u32 m_inst_frame = u32(-1);
	u32 m_inst_total = 0;

	ID3D11Buffer* m_inst_buf = nullptr;
	ID3D11ShaderResourceView* m_inst_srv = nullptr;
	ID3D11Buffer* m_inst_ex_buf = nullptr;
	ID3D11ShaderResourceView* m_inst_ex_srv = nullptr;
	u32 m_inst_cap = 0;
	u32 m_inst_fail = 0;
	s8 m_inst_bound[4] = { -1, -1, -1, -1 };

	struct ResList { u32 first, pack, build; };
	struct ResSpan { u32 src, kind; };
	ID3D11Buffer* m_res_buf[2] = {};
	ID3D11ShaderResourceView* m_res_srv[2] = {};
	ID3D11UnorderedAccessView* m_res_uav[2] = {};
	ID3D11Buffer* m_res_ex_buf[2] = {};
	ID3D11ShaderResourceView* m_res_ex_srv[2] = {};
	ID3D11UnorderedAccessView* m_res_ex_uav[2] = {};
	ref_cs m_res_cs;
	xr_vector<ResList> m_res_list;
	xr_vector<ResSpan> m_res_span;
	u32 m_res_cap = 0;
	u32 m_res_cur = 0;
	u32 m_res_build = 1;
	u32 m_res_frame = u32(-1);
	bool m_res_off = false;
	bool res_On() const;
	bool res_Create(u32 need);
	void res_Release();

	ID3D11Buffer* m_res_up = nullptr;
	ID3D11ShaderResourceView* m_res_up_srv = nullptr;
	u32 m_res_up_cap = 0;
	u32 m_res_need[256] = {};
	u32 m_res_need_at = 0;
	bool res_Upload(u32 need);

	ID3D11Buffer* m_occ_span_buf = nullptr;
	ID3D11ShaderResourceView* m_occ_span_srv = nullptr;
	ID3D11Buffer* m_occ_obj_buf = nullptr;
	ID3D11ShaderResourceView* m_occ_obj_srv = nullptr;
	ID3D11Buffer* m_occ_vis_buf = nullptr;
	ID3D11UnorderedAccessView* m_occ_vis_uav = nullptr;
	ID3D11Buffer* m_occ_args_buf = nullptr;
	ID3D11UnorderedAccessView* m_occ_args_uav = nullptr;
	ID3D11Buffer* m_occ_idx_buf = nullptr;
	ID3D11ShaderResourceView* m_occ_idx_srv = nullptr;
	ID3D11UnorderedAccessView* m_occ_idx_uav = nullptr;
	ID3D11Buffer* m_occ_head_buf = nullptr;
	ID3D11ShaderResourceView* m_occ_head_srv = nullptr;
	u32 m_occ_cap = 0;
	u32 m_occ_span_cap = 0;
	bool m_occ_off = false;
	bool m_occ_logged = false;
	bool m_occ_fail_logged = false;
	bool occ_On(LPCSTR& reason) const;
	bool occ_Create(u32 need, u32 spans);
	void occ_Release();

	struct OccRec { u32 out, inst, forced; bool on; };
	ref_cs m_occ_pack;
	ref_cs m_occ_expand;
	xr_vector<OccRec> m_occ_rec;
	u32 m_occ_var[3][2] = {};
	u32 m_occ_frame = u32(-1);
	bool occ_Load();
	void occ_Build();
	void occ_Dispatch(u32 first, u32 end, u32 mode, u32 stamp);

	ref_shader m_occ_box_sh;
	ref_selement m_occ_box;
	ID3D11DepthStencilState* m_occ_box_ds = nullptr;
	ID3D11BlendState* m_occ_box_bs = nullptr;
	ID3D11Buffer* m_occ_box_ib = nullptr;
	u32 m_occ_test[3][2] = {};
	u32 m_occ_stamp = 0;
	bool occ_LoadBox();

	bool m_occ_first_on = false;
	void occ_Test(u32 var_id);

	void inst_Load();
	void inst_Unload();
	bool inst_Grow(u32 need);
	void inst_Build();
	void inst_Draw(CDetail& Object, u32 O, u32 var_id, const InstTwin& twin, const MergeTwin* merge, const OccRec* occ, light* L, bool cull, float cull_grow, bool cull_frustum, u32 vOffset, u32 iOffset);
#endif

#ifndef _EDITOR
	xrXRC xrc;
#endif
	//AVO: detail draw raius
	//CacheSlot1 					cache_level1[dm_cache1_line][dm_cache1_line];
	//Slot*							cache		[dm_cache_line][dm_cache_line];	// grid-cache itself
	//svector<Slot*,dm_cache_size>	cache_task;									// non-unpacked slots
	//Slot							cache_pool	[dm_cache_size];				// just memory for slots

#ifdef DETAIL_RADIUS
	CacheSlot1** cache_level1;
	Slot*** cache; // grid-cache itself
	svector<Slot*, dm_max_cache_size> cache_task; // non-unpacked slots
	Slot* cache_pool; // just memory for slots
#else
    CacheSlot1 						cache_level1[dm_cache1_line][dm_cache1_line];
    Slot*							cache[dm_cache_line][dm_cache_line];	// grid-cache itself
    svector<Slot*, dm_cache_size>	cache_task;									// non-unpacked slots
    Slot							cache_pool[dm_cache_size];				// just memory for slots*/
#endif

	int cache_cx;
	int cache_cz;

	PSS poolSI; // pool из которого выделяются SlotItem

	void UpdateVisibleM();
	void UpdateVisibleS();
public:
#ifdef _EDITOR
	virtual ObjectList* 			GetSnapList		()=0;
#endif

	IC bool UseVS() { return HW.Caps.geometry_major >= 1; }

	// Software processor
	ref_geom soft_Geom;
	void soft_Load();
	void soft_Unload();
	void soft_Render();

	// Hardware processor
	ref_geom hw_Geom;
	u32 hw_BatchSize;
	ID3DVertexBuffer* hw_VB;
	ID3DIndexBuffer* hw_IB;
	ref_constant hwc_consts;
	ref_constant hwc_wave;
	ref_constant hwc_wind;
	ref_constant hwc_array;
	ref_constant hwc_s_consts;
	ref_constant hwc_s_xform;
	ref_constant hwc_s_array;
	void hw_Load();
	void hw_Load_Geom();
	void hw_Load_Shaders();
	void hw_Unload();
	void hw_Render(light* L = nullptr);
#if defined(USE_DX10) || defined(USE_DX11)
	void hw_Render_dump(const Fvector4 &consts, const Fvector4 &wave, const Fvector4 &wind, const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id, light* L = nullptr);
#else	//	USE_DX10
	void hw_Render_dump(ref_constant array, u32 var_id, u32 lod_id, u32 c_base, light* L = nullptr);
#endif	//	USE_DX10

public:
	// get unpacked slot
	DetailSlot& QueryDB(int sx, int sz);

	void cache_Initialize();
	void cache_Update(int sx, int sz, Fvector& view, int limit);
	void cache_Task(int gx, int gz, Slot* D);
	Slot* cache_Query(int sx, int sz);
	void cache_Decompress(Slot* D);
	BOOL cache_Validate();
	// cache grid to world
	int cg2w_X(int x) { return cache_cx - dm_size + x; }
	int cg2w_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }
	// world to cache grid 
	int w2cg_X(int x) { return x - cache_cx + dm_size; }
	int w2cg_Z(int z) { return cache_cz - dm_size + (dm_cache_line - 1 - z); }

	void Load();
	void Unload();
	void Render();

	/// MT stuff
	u32 m_frame_calc;
	xr_atomic_u32 m_frame_rendered;
	xrCriticalSection m_mt_calc_guard;
	u32 m_mt_calc_registration_tid; // thread that queued MT_CALC, a same-thread calc run means it fell back inline
	void __stdcall MT_CALC();

	CDetailManager();
	virtual ~CDetailManager();
};

#endif //DetailManagerH
