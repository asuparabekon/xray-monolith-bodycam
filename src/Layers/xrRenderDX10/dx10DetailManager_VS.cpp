#include "stdafx.h"
#include "../xrRender/DetailManager.h"

#include "../../xrEngine/igame_persistent.h"
#include "../../xrEngine/environment.h"

#include "../xrRenderDX10/dx10BufferUtils.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include <xmmintrin.h>
#ifdef USE_DX11
#include "../xrRenderPC_R4/blender_light_occq.h"
#endif

// Vars to store wind prev frame data ( Motion vectors )
static u32 prev_frame = -1;
static float prev_time = 0;
static Fvector4	prev_dir1 = { 0, 0, 0 }, prev_dir2 = { 0, 0, 0 };

const int quant = 16384;
const int c_hdr = 10;
const int c_size = 4;

static D3DVERTEXELEMENT9 dwDecl[] =
{
	{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, // pos
	{0, 12, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0}, // uv
	D3DDECL_END()
};

#pragma pack(push,1)
struct vertHW
{
	float x, y, z;
	short u, v, t, mid;
};
#pragma pack(pop)

short QC(float v);
//{
//	int t=iFloor(v*float(quant)); clamp(t,-32768,32767);
//	return short(t&0xffff);
//}

float GoToValue(float& current, float go_to)
{
	float diff = abs(current - go_to);

	float r_value = Device.fTimeDelta;

	if (diff - r_value <= 0)
	{
		current = go_to;
		return 0;
	}

	return current < go_to ? r_value : -r_value;
}

#ifdef USE_DX11
dx10ConstantBuffer* CDetailManager::cb_direct_begin(shared_str& array, shared_str& ex, Fvector4* ex_old)
{
	m_cb_direct_target = nullptr;
	R_constant* C = RCache.get_c(array);
	if (!C || !(C->destination & RC_dest_vertex))
		return nullptr;
	const u32 slot = (C->destination & RC_dest_vertex_cb_index_mask) >> RC_dest_vertex_cb_index_shift;
	dx10ConstantBuffer* B = RCache.m_aVertexConstants[slot]._get();
	const u32 rows_bytes = hw_BatchSize * sizeof(Fvector4) * 4;
	if (!B || u32(C->vs.index) + rows_bytes > B->GetSize())
		return nullptr;

	u32 span[2][2] = { { u32(C->vs.index), rows_bytes } };
	u32 spans = 1;
	m_cb_direct_array_off = C->vs.index;
	m_cb_direct_ex_off = u32(-1);
	R_constant* E = ex_old ? RCache.get_c(ex) : nullptr;
	if (E && (E->destination & RC_dest_vertex) &&
		((E->destination & RC_dest_vertex_cb_index_mask) >> RC_dest_vertex_cb_index_shift) == slot)
	{
		const u32 ex_bytes = hw_BatchSize * sizeof(Fvector4);
		if (u32(E->vs.index) + ex_bytes > B->GetSize())
			return nullptr;
		m_cb_direct_ex_off = E->vs.index;
		span[1][0] = E->vs.index;
		span[1][1] = ex_bytes;
		spans = 2;
		if (span[1][0] < span[0][0])
			std::swap(span[0], span[1]);
		if (span[0][0] + span[0][1] > span[1][0])
			return nullptr;
	}

	u32 at = 0;
	m_cb_direct_segs = 0;
	for (u32 s = 0; s < spans; s++)
	{
		if (span[s][0] > at)
		{
			m_cb_direct_seg[m_cb_direct_segs][0] = at;
			m_cb_direct_seg[m_cb_direct_segs++][1] = span[s][0] - at;
		}
		at = span[s][0] + span[s][1];
	}
	if (B->GetSize() > at)
	{
		m_cb_direct_seg[m_cb_direct_segs][0] = at;
		m_cb_direct_seg[m_cb_direct_segs++][1] = B->GetSize() - at;
	}

	m_cb_direct_target = B;
	return B;
}

u8* CDetailManager::cb_direct_map()
{
	D3D11_MAPPED_SUBRESOURCE sub;
	CHK_DX(HW.pContext->Map(m_cb_direct_target->GetBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &sub));
	m_cb_direct_image = (u8*)sub.pData;
	return m_cb_direct_image;
}

void CDetailManager::cb_direct_submit(u32 count)
{
	dx10ConstantBuffer& B = *m_cb_direct_target;
	const u8* shadow = (const u8*)B.GetData();
	for (u32 s = 0; s < m_cb_direct_segs; s++)
		CopyMemory(m_cb_direct_image + m_cb_direct_seg[s][0], shadow + m_cb_direct_seg[s][0], m_cb_direct_seg[s][1]);
	HW.pContext->Unmap(B.GetBuffer(), 0);
	B.MarkFlushed();
}

// 0xff when absent, 0xfe when outside the free slots
static u8 inst_slot(R_constant_table& T, LPCSTR name)
{
	R_constant* C = T.get(name);
	if (!C)
		return 0xff;
	const u32 slot = u32(C->samp.index) - CTexture::rstVertex;
	if (C->destination != RC_dest_sampler || C->type != RC_dx10texture || slot < CBackend::mtMaxVertexShaderTextures || slot >= 128)
		return 0xfe;
	return u8(slot);
}

void CDetailManager::inst_Load()
{
	m_inst_twins.clear();
	m_inst_twins.resize(objects.size() * 2);
	m_inst_ex = false;
	for (u32 O = 0; O < objects.size(); O++)
	{
		for (u32 lod = 0; lod < 2; lod++)
		{
			ShaderElement* E = objects[O]->shader->E[lod]._get();
			if (!E || E->passes.empty())
				continue;
			ShaderElement T;
			T.flags = E->flags;
			LPCSTR status = "ok";
			u8 rows = 0xff, ex = 0xff;
			string_path name = "";
			for (u32 p = 0; p < E->passes.size(); p++)
			{
				SPass& P = *E->passes[p];
				xr_strcpy(name, P.vs->cName.c_str());
				if (P.vs->skinning > 0)
				{
					LPSTR tail = strrchr(name, '_');
					if (tail)
						*tail = 0;
				}
				xr_strcat(name, "_inst");

				string_path file;
				strconcat(sizeof(file), file, ::Render->getShaderPath(), name, ".vs");
				if (!FS.exist("$game_shaders$", file))
				{
					status = "missing";
					break;
				}

				ref_vs V = DEV->_CreateVS(name);
				R_constant_table table;
				table.merge(&P.ps->constants);
				table.merge(&V->constants);
				if (P.gs)
					table.merge(&P.gs->constants);
				if (P.hs)
					table.merge(&P.hs->constants);
				if (P.ds)
					table.merge(&P.ds->constants);

				const u8 r = inst_slot(table, "dt_rows"), e = inst_slot(table, "dt_ex");
				R_constant* D = table.get("dt_draw");
				if (r >= 0xfe || e == 0xfe || !D || !(D->destination & RC_dest_vertex) || table.get("array") ||
					(p && (r != rows || e != ex)))
				{
					status = "old";
					break;
				}
				rows = r;
				ex = e;

				for (ref_constant& C : table.table)
				{
					R_constant* src = P.constants ? P.constants->get(C->name) : nullptr;
					if (src)
						C->handler = src->handler;
				}

				SPass proto;
				proto.state = P.state;
				proto.ps = P.ps;
				proto.vs = V;
				proto.gs = P.gs;
				proto.hs = P.hs;
				proto.ds = P.ds;
				proto.cs = P.cs;
				proto.constants = DEV->_CreateConstantTable(table);
				proto.T = P.T;
				proto.C = P.C;
				T.passes.push_back(DEV->_CreatePass(proto));
			}
			if (xr_strcmp(status, "ok"))
				Msg("[DT-INST] %s %s", name, status);
			if (T.passes.size() != E->passes.size())
				continue;
			InstTwin& twin = m_inst_twins[O * 2 + lod];
			twin.E = DEV->_CreateElement(T);
			twin.rows = rows;
			twin.ex = ex;
			m_inst_ex = m_inst_ex || ex != 0xff;
		}
	}

	// Cached rows keep the ex data so ex twins can reuse them
	if (m_inst_ex && !m_rows_ex)
	{
		m_rows_ex = true;
		m_rows_epoch++;
	}
}

void CDetailManager::inst_Unload()
{
	m_inst_twins.clear();
	m_inst_spans.clear();
	for (u32 v = 0; v < 3; v++)
		m_inst_first[v].clear();
	_RELEASE(m_inst_srv);
	_RELEASE(m_inst_buf);
	_RELEASE(m_inst_ex_srv);
	_RELEASE(m_inst_ex_buf);
	m_inst_cap = m_inst_fail = 0;
	m_inst_frame = u32(-1);
	res_Release();
	m_res_cs._set((SCS*)nullptr);
	m_res_off = false;
	occ_Release();
	m_occ_off = false;
	m_occ_logged = false;
	m_occ_fail_logged = false;
	m_occ_pack._set((SCS*)nullptr);
	m_occ_expand._set((SCS*)nullptr);
	m_occ_box = ref_selement();
	m_occ_box_sh.destroy();
	_RELEASE(m_occ_box_ds);
	_RELEASE(m_occ_box_bs);
	_RELEASE(m_occ_box_ib);
	m_occ_rec.clear();
	m_occ_frac.clear();
	m_merge.clear();
	m_merge_first.clear();
	m_merge_base.clear();
	_RELEASE(m_merge_srv);
	_RELEASE(m_merge_vb);
	_RELEASE(m_merge_ib);
}

// Detail vertex as dt_vertex pulls it
struct vertMerge
{
	float x, y, z, w;
	short u, v, t, mid;
	u32 pad[2];
};
static_assert(sizeof(vertMerge) == 32, "two uint4 elements");
static const u32 merge_K = 64;

// The loaded VS must bind every slot the source VS binds, plus the index list with idx
static ref_vs merge_VS(SVS* src, SDeclaration* decl, u8& verts, LPCSTR suffix = "_merge", u8* idx = nullptr)
{
	string_path name, file;
	xr_strcpy(name, src->cName.c_str());
	if (src->skinning > 0)
	{
		LPSTR tail = strrchr(name, '_');
		if (tail)
			*tail = 0;
	}
	xr_strcat(name, suffix);

	string128 status = "ok";
	ref_vs V;
	strconcat(sizeof(file), file, ::Render->getShaderPath(), name, ".vs");
	if (!FS.exist("$game_shaders$", file))
		xr_strcpy(status, "failed missing");
	else
	{
		V = DEV->_CreateVS(name);

		// Lookups go through merged copies since get searches by pointer
		R_constant_table st, vt;
		st.merge(&src->constants);
		vt.merge(&V->constants);
		const u8 rows = inst_slot(st, "dt_rows"), ex = inst_slot(st, "dt_ex");
		const u8 mrows = inst_slot(vt, "dt_rows"), mex = inst_slot(vt, "dt_ex");
		R_constant* D = vt.get("dt_draw");
		verts = inst_slot(vt, "dt_verts");
		if (rows >= 0xfe || mrows != rows)
			xr_sprintf(status, "failed slots dt_rows source %u merge %u", rows, mrows);
		else if (mex != ex)
			xr_sprintf(status, "failed slots dt_ex source %u merge %u", ex, mex);
		else if (verts >= 0xfe)
			xr_sprintf(status, "failed slots dt_verts merge %u", verts);
		else if (!D || !(D->destination & RC_dest_vertex))
			xr_sprintf(status, "failed slots dt_draw destination %u", D ? u32(D->destination) : 0u);
		else if (idx && verts != inst_slot(st, "dt_verts"))
			xr_sprintf(status, "failed slots dt_verts source %u occ %u", inst_slot(st, "dt_verts"), verts);
		else if (idx && (*idx = inst_slot(vt, "dt_idx")) >= 0xfe)
			xr_sprintf(status, "failed slots dt_idx %u", *idx);
		else
		{
			// The detail declaration layout must accept a VS without vertex inputs
			ID3D11InputLayout* layout = nullptr;
			ID3DBlob* sig = V->signature->signature;
			if (FAILED(HW.pDevice->CreateInputLayout(&decl->dx10_dcl_code[0], decl->dx10_dcl_code.size() - 1, sig->GetBufferPointer(), sig->GetBufferSize(), &layout)))
				xr_strcpy(status, "failed layout");
			_RELEASE(layout);
		}
	}
	if (xr_strcmp(status, "ok"))
		Msg("%s %s %s", idx ? "[DT-OCC]" : "[DT-MERGE]", name, status);
	return xr_strcmp(status, "ok") ? ref_vs() : V;
}

// The _thin VS must bind the source slots plus dt_thin
static ref_vs thin_VS(SVS* src, SDeclaration* decl)
{
	string_path name, file;
	strconcat(sizeof(name), name, src->cName.c_str(), "_thin");

	string128 status = "ok";
	ref_vs V;
	strconcat(sizeof(file), file, ::Render->getShaderPath(), name, ".vs");
	if (!FS.exist("$game_shaders$", file))
		xr_strcpy(status, "failed missing");
	else
	{
		V = DEV->_CreateVS(name);
		R_constant_table st, vt;
		st.merge(&src->constants);
		vt.merge(&V->constants);
		LPCSTR slots[] = { "dt_rows", "dt_ex", "dt_verts", "dt_idx" };
		LPCSTR bad = nullptr;
		for (LPCSTR s : slots)
		{
			if (!bad && inst_slot(vt, s) != inst_slot(st, s))
				bad = s;
		}
		R_constant* D = vt.get("dt_draw");
		R_constant* T = vt.get("dt_thin");
		if (bad)
			xr_sprintf(status, "failed slots %s source %u thin %u", bad, inst_slot(st, bad), inst_slot(vt, bad));
		else if (!D || !(D->destination & RC_dest_vertex))
			xr_sprintf(status, "failed slots dt_draw destination %u", D ? u32(D->destination) : 0u);
		else if (!T || !(T->destination & RC_dest_vertex))
			xr_sprintf(status, "failed slots dt_thin destination %u", T ? u32(T->destination) : 0u);
		else
		{
			ID3D11InputLayout* layout = nullptr;
			ID3DBlob* sig = V->signature->signature;
			if (FAILED(HW.pDevice->CreateInputLayout(&decl->dx10_dcl_code[0], decl->dx10_dcl_code.size() - 1, sig->GetBufferPointer(), sig->GetBufferSize(), &layout)))
				xr_strcpy(status, "failed layout");
			_RELEASE(layout);
		}
	}
	if (xr_strcmp(status, "ok"))
		Msg("[DT-THIN] %s %s", name, status);
	return xr_strcmp(status, "ok") ? ref_vs() : V;
}

static ref_pass thin_Pass(SPass& P, SPass proto, const ref_vs& V)
{
	R_constant_table table;
	table.merge(&P.ps->constants);
	table.merge(&V->constants);
	if (P.gs)
		table.merge(&P.gs->constants);
	if (P.hs)
		table.merge(&P.hs->constants);
	if (P.ds)
		table.merge(&P.ds->constants);
	for (ref_constant& C : table.table)
	{
		R_constant* src = P.constants ? P.constants->get(C->name) : nullptr;
		if (src)
			C->handler = src->handler;
	}
	proto.vs = V;
	proto.constants = DEV->_CreateConstantTable(table);
	return DEV->_CreatePass(proto);
}

void CDetailManager::merge_Load()
{
	m_merge.clear();
	m_merge_first.assign(objects.size(), u32(-1));
	m_merge_base.assign(objects.size(), 0);
	m_occ_frac.assign(objects.size(), 0.f);

	// One vertex copy per object, merge_K index copies offset by the vertex count
	xr_vector<vertMerge> verts;
	xr_vector<u16> indices;
	for (u32 O = 0; O < objects.size(); O++)
	{
		const CDetail& D = *objects[O];
		if (D.number_vertices * merge_K > 65536)
			continue;
		m_merge_base[O] = u32(verts.size());
		m_merge_first[O] = u32(indices.size());
		for (u32 v = 0; v < D.number_vertices; v++)
		{
			const Fvector& vP = D.vertices[v].P;
			vertMerge M = {};
			M.x = vP.x;
			M.y = vP.y;
			M.z = vP.z;
			M.w = 1.f;
			M.u = QC(D.vertices[v].u);
			M.v = QC(D.vertices[v].v);
			M.t = QC(vP.y / (D.bv_bb.max.y - D.bv_bb.min.y));
			verts.push_back(M);
			m_occ_frac[O] = _max(m_occ_frac[O], _abs(float(M.t)) / float(quant));
		}
		for (u32 copy = 0; copy < merge_K; copy++)
			for (u32 i = 0; i < D.number_indices; i++)
				indices.push_back(u16(D.indices[i] + copy * D.number_vertices));
	}

	bool ok = !verts.empty();
	if (ok)
	{
		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.ByteWidth = u32(verts.size() * sizeof(vertMerge));
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = verts.data();
		D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
		view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		view.Buffer.NumElements = u32(verts.size() * 2);
		ok = SUCCEEDED(HW.pDevice->CreateBuffer(&desc, &data, &m_merge_vb)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_merge_vb, &view, &m_merge_srv));
		desc.ByteWidth = u32(indices.size() * sizeof(u16));
		desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		data.pSysMem = indices.data();
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, &data, &m_merge_ib));
	}
	if (!ok)
	{
		Msg("[DT-MERGE] buffers failed, merge off");
		_RELEASE(m_merge_srv);
		_RELEASE(m_merge_vb);
		_RELEASE(m_merge_ib);
		return;
	}

	xr_map<shared_str, std::pair<ref_vs, u8>> shaders;
	xr_map<shared_str, ref_vs> thin_shaders;
	SDeclaration* decl = hw_Geom->dcl._get();
	auto add = [&](ShaderElement* E)
	{
		if (!E || m_merge.count(E))
			return;
		ShaderElement T, TT;
		T.flags = E->flags;
		TT.flags = E->flags;
		bool thin = true;
		u8 slot = 0xff;
		for (u32 p = 0; p < E->passes.size(); p++)
		{
			SPass& P = *E->passes[p];
			auto it = shaders.find(P.vs->cName);
			if (it == shaders.end())
			{
				u8 s = 0xff;
				ref_vs V = merge_VS(P.vs._get(), decl, s);
				it = shaders.emplace(P.vs->cName, std::make_pair(V, s)).first;
			}
			if (!it->second.first || (p && it->second.second != slot))
				return;
			slot = it->second.second;

			R_constant_table table;
			table.merge(&P.ps->constants);
			table.merge(&it->second.first->constants);
			if (P.gs)
				table.merge(&P.gs->constants);
			if (P.hs)
				table.merge(&P.hs->constants);
			if (P.ds)
				table.merge(&P.ds->constants);

			for (ref_constant& C : table.table)
			{
				R_constant* src = P.constants ? P.constants->get(C->name) : nullptr;
				if (src)
					C->handler = src->handler;
			}

			SPass proto;
			proto.state = P.state;
			proto.ps = P.ps;
			proto.vs = it->second.first;
			proto.gs = P.gs;
			proto.hs = P.hs;
			proto.ds = P.ds;
			proto.cs = P.cs;
			proto.constants = DEV->_CreateConstantTable(table);
			proto.T = P.T;
			proto.C = P.C;
			T.passes.push_back(DEV->_CreatePass(proto));

			auto th = thin_shaders.find(proto.vs->cName);
			if (th == thin_shaders.end())
				th = thin_shaders.emplace(proto.vs->cName, thin_VS(proto.vs._get(), decl)).first;
			thin = thin && th->second;
			if (thin)
				TT.passes.push_back(thin_Pass(P, proto, th->second));
		}
		MergeTwin& M = m_merge[E];
		M.E = DEV->_CreateElement(T);
		M.verts = slot;
		if (thin)
			M.thin = DEV->_CreateElement(TT);
	};
	for (InstTwin& T : m_inst_twins)
		add(T.E._get());
	u32 thin = 0;
	for (auto& it : m_merge)
		thin += it.second.thin ? 1 : 0;
	if (thin != m_merge.size())
		Msg("[DT-THIN] elements %u of %u, thin twins failed", thin, u32(m_merge.size()));
}

bool CDetailManager::inst_Grow(u32 need)
{
	if (need <= m_inst_cap)
		return true;
	if (m_inst_fail && need >= m_inst_fail)
		return false;

	_RELEASE(m_inst_srv);
	_RELEASE(m_inst_buf);
	_RELEASE(m_inst_ex_srv);
	_RELEASE(m_inst_ex_buf);
	m_inst_cap = 0;
	const u32 cap = _min(need + need / 4, 1u << 24);

	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
	view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;

	desc.ByteWidth = cap * sizeof(Fvector4) * 4;
	view.Buffer.NumElements = cap * 4;
	bool ok = SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_inst_buf)) &&
		SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_inst_buf, &view, &m_inst_srv));
	if (ok && m_inst_ex)
	{
		desc.ByteWidth = cap * sizeof(Fvector4);
		view.Buffer.NumElements = cap;
		ok = SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_inst_ex_buf)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_inst_ex_buf, &view, &m_inst_ex_srv));
	}
	if (!ok)
	{
		Msg("[DT-INST] buffer create failed need=%u cap=%u, legacy draw", need, cap);
		_RELEASE(m_inst_srv);
		_RELEASE(m_inst_buf);
		_RELEASE(m_inst_ex_srv);
		_RELEASE(m_inst_ex_buf);
		m_inst_fail = need;
		return false;
	}
	m_inst_cap = cap;
	return true;
}

bool CDetailManager::res_On() const
{
	return ps_r__detail_inst_res && ps_r__detail_inst && ps_r__detail_rows;
}

bool CDetailManager::res_Create(u32 need)
{
	if (need <= m_res_cap)
		return true;

	LPCSTR reason = nullptr;
	if (!m_res_cs)
	{
		LPCSTR cs = m_inst_ex ? "dt_res_copy_ex" : "dt_res_copy";
		string_path file;
		strconcat(sizeof(file), file, ::Render->getShaderPath(), cs, ".cs");
		UINT support = 0;
		if (!FS.exist("$game_shaders$", file))
			reason = "shader";
		else if (FAILED(HW.pDevice->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_UINT, &support)) ||
			!(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
			reason = "format";
		else
		{
			m_res_cs = DEV->_CreateCS(cs);
			if (!m_res_cs)
				reason = "cs";
		}
	}

	for (u32 k = 0; k < 2; k++)
	{
		_RELEASE(m_res_uav[k]);
		_RELEASE(m_res_srv[k]);
		_RELEASE(m_res_buf[k]);
		_RELEASE(m_res_ex_uav[k]);
		_RELEASE(m_res_ex_srv[k]);
		_RELEASE(m_res_ex_buf[k]);
	}
	m_res_cap = 0;
	const u32 cap = _min(need + need / 4, 1u << 24);

	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.ByteWidth = cap * sizeof(Fvector4) * 4;
	D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
	view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	view.Buffer.NumElements = cap * 4;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
	uav.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uav.Buffer.NumElements = cap * 4;
	for (u32 k = 0; k < 2 && !reason; k++)
	{
		if (FAILED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_res_buf[k])) ||
			FAILED(HW.pDevice->CreateShaderResourceView(m_res_buf[k], &view, &m_res_srv[k])) ||
			FAILED(HW.pDevice->CreateUnorderedAccessView(m_res_buf[k], &uav, &m_res_uav[k])))
			reason = "create";
	}

	// One normal and alpha entry per instance beside the rows
	if (m_inst_ex && !reason)
	{
		desc.ByteWidth = cap * sizeof(Fvector4);
		view.Buffer.NumElements = cap;
		uav.Buffer.NumElements = cap;
		for (u32 k = 0; k < 2 && !reason; k++)
		{
			if (FAILED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_res_ex_buf[k])) ||
				FAILED(HW.pDevice->CreateShaderResourceView(m_res_ex_buf[k], &view, &m_res_ex_srv[k])) ||
				FAILED(HW.pDevice->CreateUnorderedAccessView(m_res_ex_buf[k], &uav, &m_res_ex_uav[k])))
				reason = "ex";
		}
	}
	if (reason)
	{
		Msg("[DT-RES] off reason=%s", reason);
		res_Release();
		m_res_off = true;
		return false;
	}

	m_res_cap = cap;
	m_res_cur = 0;
	if (!++m_res_build)
		m_res_build = 1;
	if (m_inst_ex)
		Msg("[DT-RES] ex on cap=%u", cap);
	return true;
}

void CDetailManager::res_Release()
{
	for (u32 k = 0; k < 2; k++)
	{
		_RELEASE(m_res_uav[k]);
		_RELEASE(m_res_srv[k]);
		_RELEASE(m_res_buf[k]);
		_RELEASE(m_res_ex_uav[k]);
		_RELEASE(m_res_ex_srv[k]);
		_RELEASE(m_res_ex_buf[k]);
	}
	_RELEASE(m_res_up_srv);
	_RELEASE(m_res_up);
	m_res_up_cap = 0;
	ZeroMemory(m_res_need, sizeof(m_res_need));
	m_res_list.clear();
	m_res_span.clear();
	m_res_cap = 0;
	m_res_frame = u32(-1);
}

bool CDetailManager::res_Upload(u32 need)
{
	m_res_need[m_res_need_at++ & 255] = need;
	u32 cap = 0;
	if (need > m_res_up_cap)
		cap = _min(need + need / 4, 1u << 24);
	else if (m_res_up_cap > (1u << 20))
	{
		u32 peak = 0;
		for (u32 k = 0; k < 256; k++)
			peak = _max(peak, m_res_need[k]);
		if (m_res_up_cap > peak * 4)
			cap = _min(peak + peak / 4, 1u << 24);
	}
	if (!cap)
		return true;

	_RELEASE(m_res_up_srv);
	_RELEASE(m_res_up);
	m_res_up_cap = 0;
	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.ByteWidth = cap * sizeof(Fvector4) * 4;
	D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
	view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
	view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	view.Buffer.NumElements = cap * 4;
	if (FAILED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_res_up)) ||
		FAILED(HW.pDevice->CreateShaderResourceView(m_res_up, &view, &m_res_up_srv)))
	{
		Msg("[DT-RES] off reason=upload");
		res_Release();
		m_res_off = true;
		return false;
	}
	m_res_up_cap = cap;
	return true;
}

bool CDetailManager::occ_On(LPCSTR& reason) const
{
	reason = nullptr;
	if (!ps_r__detail_inst || m_inst_twins.empty())
		reason = "inst";
	else if (!ps_r__detail_merge || m_merge.empty())
		reason = "merge";
	else if (!ps_r__detail_rows)
		reason = "rows";
	else if (RImplementation.o.dx10_msaa)
		reason = "msaa";
	else if (m_inst_frame != Device.dwFrame)
		reason = "frame";
	return !reason;
}

bool CDetailManager::occ_Create(u32 need, u32 spans)
{
	if (need <= m_occ_cap && spans <= m_occ_span_cap)
		return true;

	const u32 ranges = u32(objects.size()) * 3;
	const u32 pad = merge_K * ranges;
	occ_Release();
	LPCSTR reason = nullptr;
	UINT support = 0;
	if (!m_occ_pack && !occ_Load())
		reason = "shader";
	else if (u64(need) + pad > (1u << 24) || spans > (1u << 24))
		reason = "capacity";
	else if (FAILED(HW.pDevice->CheckFormatSupport(DXGI_FORMAT_R32_UINT, &support)) ||
		!(support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW))
		reason = "format";

	u32 cap = 0, span_cap = 0;
	if (!reason)
	{
		cap = _min(need + need / 4, (1u << 24) - pad);
		span_cap = _min(spans + spans / 4, 1u << 24);

		D3D11_BUFFER_DESC desc = {};
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SHADER_RESOURCE_VIEW_DESC view = {};
		view.Format = DXGI_FORMAT_R32G32B32A32_UINT;
		view.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format = DXGI_FORMAT_R32_UINT;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		desc.ByteWidth = span_cap * 48;
		view.Buffer.NumElements = span_cap * 3;
		bool ok = SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_span_buf)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_occ_span_buf, &view, &m_occ_span_srv));
		desc.ByteWidth = ranges * 32;
		view.Buffer.NumElements = ranges * 2;
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_obj_buf)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_occ_obj_buf, &view, &m_occ_obj_srv));

		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.ByteWidth = 32;
		view.Buffer.NumElements = 2;
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_head_buf)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_occ_head_buf, &view, &m_occ_head_srv));

		// Span stamps then span out offsets
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.ByteWidth = span_cap * 8;
		uav.Buffer.NumElements = span_cap * 2;
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_vis_buf)) &&
			SUCCEEDED(HW.pDevice->CreateUnorderedAccessView(m_occ_vis_buf, &uav, &m_occ_vis_uav));
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		view.Format = DXGI_FORMAT_R32_UINT;
		desc.ByteWidth = (cap + pad) * 4;
		view.Buffer.NumElements = uav.Buffer.NumElements = cap + pad;
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_idx_buf)) &&
			SUCCEEDED(HW.pDevice->CreateShaderResourceView(m_occ_idx_buf, &view, &m_occ_idx_srv)) &&
			SUCCEEDED(HW.pDevice->CreateUnorderedAccessView(m_occ_idx_buf, &uav, &m_occ_idx_uav));
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		desc.ByteWidth = ranges * 20;
		uav.Buffer.NumElements = ranges * 5;
		ok = ok && SUCCEEDED(HW.pDevice->CreateBuffer(&desc, nullptr, &m_occ_args_buf)) &&
			SUCCEEDED(HW.pDevice->CreateUnorderedAccessView(m_occ_args_buf, &uav, &m_occ_args_uav));
		if (!ok)
			reason = "create";
	}
	if (reason)
	{
		if (!m_occ_fail_logged)
			Msg("[DT-OCC] off reason=%s", reason);
		m_occ_fail_logged = true;
		occ_Release();
		m_occ_off = true;
		return false;
	}

	// Stamps start at zero, a value no frame writes
	const UINT zero[4] = {};
	HW.pContext->ClearUnorderedAccessViewUint(m_occ_vis_uav, zero);
	m_occ_cap = cap;
	m_occ_span_cap = span_cap;
	return true;
}

bool CDetailManager::occ_Load()
{
	if (!occ_LoadBox())
		return false;

	string_path file;
	LPCSTR kernels[2] = { "dt_occ_pack", "dt_occ_expand" };
	ref_cs* dest[2] = { &m_occ_pack, &m_occ_expand };
	for (u32 k = 0; k < 2; k++)
	{
		strconcat(sizeof(file), file, ::Render->getShaderPath(), kernels[k], ".cs");
		if (!FS.exist("$game_shaders$", file))
			return false;
		*dest[k] = DEV->_CreateCS(kernels[k]);
		if (!*dest[k])
			return false;
	}

	xr_map<shared_str, std::pair<ref_vs, u8>> shaders;
	xr_map<shared_str, ref_vs> thin_shaders;
	SDeclaration* decl = hw_Geom->dcl._get();
	u32 twins = 0, thins = 0;
	for (auto& it : m_merge)
	{
		MergeTwin& M = it.second;
		ShaderElement* E = M.E._get();
		M.occ = ref_selement();
		M.occ_thin = ref_selement();
		M.idx = 0xff;
		if (!E)
			continue;
		ShaderElement T, TT;
		T.flags = E->flags;
		TT.flags = E->flags;
		bool thin = true;
		u8 slot = 0xff;
		for (u32 p = 0; p < E->passes.size(); p++)
		{
			SPass& P = *E->passes[p];
			auto sh = shaders.find(P.vs->cName);
			if (sh == shaders.end())
			{
				u8 verts = 0xff, idx = 0xff;
				ref_vs V = merge_VS(P.vs._get(), decl, verts, "_occ", &idx);
				sh = shaders.emplace(P.vs->cName, std::make_pair(V, idx)).first;
			}
			if (!sh->second.first || (p && sh->second.second != slot))
				break;
			slot = sh->second.second;

			R_constant_table table;
			table.merge(&P.ps->constants);
			table.merge(&sh->second.first->constants);
			if (P.gs)
				table.merge(&P.gs->constants);
			if (P.hs)
				table.merge(&P.hs->constants);
			if (P.ds)
				table.merge(&P.ds->constants);

			for (ref_constant& C : table.table)
			{
				R_constant* src = P.constants ? P.constants->get(C->name) : nullptr;
				if (src)
					C->handler = src->handler;
			}

			SPass proto;
			proto.state = P.state;
			proto.ps = P.ps;
			proto.vs = sh->second.first;
			proto.gs = P.gs;
			proto.hs = P.hs;
			proto.ds = P.ds;
			proto.cs = P.cs;
			proto.constants = DEV->_CreateConstantTable(table);
			proto.T = P.T;
			proto.C = P.C;
			T.passes.push_back(DEV->_CreatePass(proto));

			auto th = thin_shaders.find(proto.vs->cName);
			if (th == thin_shaders.end())
				th = thin_shaders.emplace(proto.vs->cName, thin_VS(proto.vs._get(), decl)).first;
			thin = thin && th->second;
			if (thin)
				TT.passes.push_back(thin_Pass(P, proto, th->second));
		}
		if (T.passes.empty() || T.passes.size() != E->passes.size())
			continue;
		M.occ = DEV->_CreateElement(T);
		M.idx = slot;
		twins++;
		if (thin)
		{
			M.occ_thin = DEV->_CreateElement(TT);
			thins++;
		}
	}
	if (twins != m_merge.size())
		Msg("[DT-OCC] elements %u of %u", twins, u32(m_merge.size()));
	if (thins != twins)
		Msg("[DT-THIN] occ elements %u of %u", thins, twins);
	return twins != 0;
}

bool CDetailManager::occ_LoadBox()
{
	string_path file;
	LPCSTR ext[2] = { ".vs", ".ps" };
	for (u32 k = 0; k < 2; k++)
	{
		strconcat(sizeof(file), file, ::Render->getShaderPath(), "dt_occ_box", ext[k]);
		if (!FS.exist("$game_shaders$", file))
			return false;
	}
	if (!m_occ_box_sh)
	{
		CBlender_light_occq occq;
		m_occ_box_sh.create(&occq, "r2\\occq");
	}
	SPass& P = *m_occ_box_sh->E[0]->passes[0];
	ref_vs V = DEV->_CreateVS("dt_occ_box");
	ref_ps S = DEV->_CreatePS("dt_occ_box");
	if (!V || !S)
		return false;

	// The box shaders bind the span table at t12 and the stamps at u7
	R_constant_table table;
	table.merge(&S->constants);
	table.merge(&V->constants);
	for (ref_constant& C : table.table)
	{
		R_constant* src = P.constants ? P.constants->get(C->name) : nullptr;
		if (src)
			C->handler = src->handler;
	}
	R_constant* B = table.get("dt_occ_box");
	R_constant* W = table.get("m_WVP");
	R_constant* U = table.get("occ_vis");
	if (inst_slot(table, "occ_spans") != 12 || !B || !(B->destination & RC_dest_vertex) || !W || !W->handler ||
		!U || U->type != RC_dx11UAV || U->samp.index != 7 + CTexture::rstPixel)
		return false;

	SDeclaration* decl = hw_Geom->dcl._get();
	ID3D11InputLayout* layout = nullptr;
	ID3DBlob* sig = V->signature->signature;
	if (FAILED(HW.pDevice->CreateInputLayout(&decl->dx10_dcl_code[0], decl->dx10_dcl_code.size() - 1, sig->GetBufferPointer(), sig->GetBufferSize(), &layout)))
		return false;
	_RELEASE(layout);

	static const u16 faces[36] = { 0, 2, 6, 0, 6, 4, 1, 3, 7, 1, 7, 5, 0, 1, 5, 0, 5, 4, 2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 5, 7, 4, 7, 6 };
	if (!m_occ_box_ib && FAILED(dx10BufferUtils::CreateIndexBuffer(&m_occ_box_ib, faces, sizeof(faces))))
		return false;

	if (!m_occ_box_ds)
	{
		D3D11_DEPTH_STENCIL_DESC ds = {};
		ds.DepthEnable = TRUE;
		ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		ds.StencilEnable = FALSE;
		ds.StencilReadMask = ds.StencilWriteMask = 0xff;
		ds.FrontFace.StencilFailOp = ds.FrontFace.StencilDepthFailOp = ds.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		ds.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		ds.BackFace = ds.FrontFace;

		D3D11_BLEND_DESC bs = {};
		for (u32 i = 0; i < 8; i++)
		{
			D3D11_RENDER_TARGET_BLEND_DESC& rt = bs.RenderTarget[i];
			rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
			rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
			rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = 0;
		}
		if (FAILED(HW.pDevice->CreateDepthStencilState(&ds, &m_occ_box_ds)) || FAILED(HW.pDevice->CreateBlendState(&bs, &m_occ_box_bs)))
		{
			_RELEASE(m_occ_box_ds);
			_RELEASE(m_occ_box_bs);
			return false;
		}
	}

	SPass proto;
	proto.state = P.state;
	proto.ps = S;
	proto.vs = V;
	proto.gs = P.gs;
	proto.hs = P.hs;
	proto.ds = P.ds;
	proto.cs = P.cs;
	proto.constants = DEV->_CreateConstantTable(table);
	proto.T = P.T;
	proto.C = P.C;
	ShaderElement T;
	T.flags = m_occ_box_sh->E[0]->flags;
	T.passes.push_back(DEV->_CreatePass(proto));
	m_occ_box = DEV->_CreateElement(T);
	return true;
}

static bool occ_span_box(Fbox& box, const CDetailManager::SlotPart& part, float a, float wave, const Fmatrix& vp, float half_px)
{
	box.min.lerp(part.occ_O.min, part.occ_B.min, a);
	box.max.lerp(part.occ_O.max, part.occ_B.max, a);
	const float d = wave * a * part.occ_H;
	box.min.x -= d;
	box.max.x += d;
	box.min.z -= d;
	box.max.z += d;

	Fvector4 p, clip;
	float far_w = 0.f;
	for (u32 k = 0; k < 8; k++)
	{
		p.set(k & 1 ? box.max.x : box.min.x, k & 2 ? box.max.y : box.min.y, k & 4 ? box.max.z : box.min.z, 1.f);
		vp.transform(clip, p);
		far_w = _max(far_w, clip.w);
	}
	box.grow(far_w * half_px);
	if (box.contains(Device.vCameraPosition))
		return false;
	for (u32 k = 0; k < 8; k++)
	{
		p.set(k & 1 ? box.max.x : box.min.x, k & 2 ? box.max.y : box.min.y, k & 4 ? box.max.z : box.min.z, 1.f);
		vp.transform(clip, p);
		if (clip.z <= 0.f)
			return false;
	}
	return true;
}

void CDetailManager::occ_Build()
{
	const u32 N = u32(objects.size());
	D3D11_MAPPED_SUBRESOURCE sub_span, sub_rec;
	if (FAILED(HW.pContext->Map(m_occ_span_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &sub_span)))
		return;
	if (FAILED(HW.pContext->Map(m_occ_obj_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &sub_rec)))
	{
		HW.pContext->Unmap(m_occ_span_buf, 0);
		return;
	}
	u32* table = (u32*)sub_span.pData;
	u32* recs = (u32*)sub_rec.pData;
	m_occ_rec.assign(N * 3, OccRec{ 0, 0, 0, false });

	// half_px is half a pixel per unit of clip w
	const Fmatrix& vp = Device.mFullTransform;
	const float half_px = 1.f / (Device.mProject._22 * float(RImplementation.Target->get_height()));
	const float fade_start = 1.f, fade_range = dm_fade * dm_fade - fade_start;
	const bool no_scale = !!psDeviceFlags2.test(rsNoScale);
	const float amp[3] = { 0.f, _abs(swing_current.amp1), _abs(swing_current.amp2) };
	m_occ_stamp = Device.dwFrame % 16777215u + 1;

	m_occ_first_on = ps_r__detail_occ_first != 0;

	u32 at = 0, tested = 0;
	for (u32 v = 0; v < 3; v++)
	{
		const u32 lod = v ? 0 : 1;
		m_occ_var[v][0] = at;
		m_occ_test[v][0] = tested;
		for (u32 O = 0; O < N; O++)
		{
			const u32 s0 = m_inst_first[v][O], s1 = m_inst_first[v][O + 1];
			const InstTwin& twin = m_inst_twins[O * 2 + lod];
			if (s0 == s1 || !twin.E || m_merge_first[O] == u32(-1))
				continue;
			auto it = m_merge.find(twin.E._get());
			if (it == m_merge.end() || !it->second.occ)
				continue;

			bool faded = false;
			for (u32 s = s0; s < s1 && !faded; s++)
			{
				const InstSpan& span = m_inst_spans[s];
				float scale = 1.f;
				if (fade_distance <= -1)
					scale *= 1.0f - span.P.distance_to_xz_sqr(light_position) * 0.005f;
				else if (span.distance > fade_distance)
					scale *= 1.0f - abs(span.distance - fade_distance) * 0.005f;
				faded = span.count && scale != 1.f;
			}
			if (faded)
				continue;

			// Flags 1 kept, 2 record start, 4 record end, as dt_occ_pack reads them
			const u32 r = v * N + O;
			OccRec& rec = m_occ_rec[r];
			rec.out = m_inst_spans[s0].first + r * merge_K;
			const float wave = amp[v] * m_occ_frac[O];
			for (u32 s = s0; s < s1; s++, at++)
			{
				const InstSpan& span = m_inst_spans[s];
				u32* e = table + at * 12;
				e[0] = span.first;
				e[1] = span.count;
				e[2] = (s == s0 ? 2 : 0) | (s + 1 == s1 ? 4 : 0);
				e[3] = r;
				ZeroMemory(e + 4, 8 * sizeof(u32));
				rec.inst += span.count;

				const SlotPart* part = span.rows_id != u32(-1) ? &cache_pool[span.rows_id / (dm_obj_in_slot * 3)].G[span.rows_id % (dm_obj_in_slot * 3) / 3] : nullptr;
				const float fade = span.distance < fade_start ? 0.f : (span.distance - fade_start) / fade_range;
				Fbox box;
				const bool test = span.count && span.distance >= _sqr(25.f) && part && part->occ_B.min.x <= part->occ_B.max.x &&
					occ_span_box(box, *part, no_scale ? 1.f : 1.f - fade, wave, vp, half_px);
				if (test)
				{
					// Box min then max, the w of entry k names the k-th tested span
					CopyMemory(e + 4, &box.min, sizeof(Fvector));
					CopyMemory(e + 8, &box.max, sizeof(Fvector));
					table[tested * 12 + 7] = at;
					tested++;
				}
				else
				{
					e[2] |= 1;
					rec.forced += span.count;
				}
			}
			u32* d = recs + r * 8;
			d[0] = rec.out;
			d[1] = merge_K * objects[O]->number_indices;
			d[2] = m_merge_first[O];
			d[3] = 0;
			d[4] = at - (s1 - s0);
			d[5] = at;
			d[6] = d[7] = 0;
			rec.on = true;
		}
		m_occ_var[v][1] = at;
		m_occ_test[v][1] = tested;
	}
	HW.pContext->Unmap(m_occ_obj_buf, 0);
	HW.pContext->Unmap(m_occ_span_buf, 0);
	m_occ_frame = Device.dwFrame;
}

// The kernels leave every slot they bound empty
void CDetailManager::occ_Dispatch(u32 a, u32 b, u32 mode, u32 stamp)
{
	if (a == b)
		return;

	// The index list leaves its vertex slot before the kernels write it
	ID3D11ShaderResourceView* none_srv = nullptr;
	if (m_inst_bound[3] >= 0)
	{
		HW.pContext->VSSetShaderResources(m_inst_bound[3], 1, &none_srv);
		m_inst_bound[3] = -1;
	}

	const u32 n = b - a, gx = _min(n, 65535u);
	const u32 head[8] = { a, b, gx, m_occ_span_cap, stamp, mode, 0, 0 };
	HW.pContext->UpdateSubresource(m_occ_head_buf, 0, nullptr, head, 0, 0);
	ID3D11UnorderedAccessView* uav[3] = { m_occ_vis_uav, m_occ_idx_uav, m_occ_args_uav };
	ID3D11UnorderedAccessView* none_uav[3] = {};
	SRVSManager.SetCSResource(0, m_occ_span_srv);
	SRVSManager.SetCSResource(1, m_occ_obj_srv);
	SRVSManager.SetCSResource(2, m_occ_head_srv);
	HW.pContext->CSSetUnorderedAccessViews(0, 3, uav, nullptr);
	RCache.set_CS(m_occ_pack);
	RCache.Compute(1, 1, 1);
	RCache.set_CS(m_occ_expand);
	RCache.Compute(gx, (n + gx - 1) / gx, 1);
	HW.pContext->CSSetUnorderedAccessViews(0, 3, none_uav, nullptr);
	SRVSManager.SetCSResource(0, nullptr);
	SRVSManager.SetCSResource(1, nullptr);
	SRVSManager.SetCSResource(2, nullptr);
	SRVSManager.Apply();
}

void CDetailManager::occ_Test(u32 var_id)
{
	static shared_str strBox("dt_occ_box");
	const u32 a = m_occ_test[var_id][0], n = m_occ_test[var_id][1] - a;
	if (!n)
		return;

	RCache.set_Element(m_occ_box._get());
	RCache.set_CullMode(CULL_NONE);
	StateManager.SetDepthStencilState(m_occ_box_ds);
	StateManager.SetBlendState(m_occ_box_bs);
	RCache.set_c(strBox, float(a), float(m_occ_stamp), 0.f, 0.f);
	RCache.set_Indices(m_occ_box_ib);
	HW.pContext->VSSetShaderResources(12, 1, &m_occ_span_srv);
	RCache.RenderInstancedUAV(D3DPT_TRIANGLELIST, 0, 8, 0, 12, n, 7, m_occ_vis_uav);
	ID3D11ShaderResourceView* none = nullptr;
	HW.pContext->VSSetShaderResources(12, 1, &none);
	RCache.set_Indices(hw_IB);
	RCache.set_CullMode(CULL_NONE);
}

void CDetailManager::occ_Release()
{
	_RELEASE(m_occ_span_srv);
	_RELEASE(m_occ_span_buf);
	_RELEASE(m_occ_obj_srv);
	_RELEASE(m_occ_obj_buf);
	_RELEASE(m_occ_vis_uav);
	_RELEASE(m_occ_vis_buf);
	_RELEASE(m_occ_args_uav);
	_RELEASE(m_occ_args_buf);
	_RELEASE(m_occ_idx_uav);
	_RELEASE(m_occ_idx_srv);
	_RELEASE(m_occ_idx_buf);
	_RELEASE(m_occ_head_srv);
	_RELEASE(m_occ_head_buf);
	m_occ_cap = m_occ_span_cap = 0;
	m_occ_frame = u32(-1);
}

void CDetailManager::inst_Build()
{
	const u32 frame = Device.dwFrame;

	m_inst_spans.clear_not_free();
	u32 total = 0;
	const bool records = ps_r__detail_rows != 0 && m_vis_rows_frame == frame && Device.fTimeDelta >= 0;
	for (u32 v = 0; v < 3; v++)
	{
		const u32 lod = v ? 0 : 1;
		vis_list& list = m_visibles[v];
		m_inst_first[v].resize(objects.size() + 1);
		for (u32 O = 0; O < objects.size(); O++)
		{
			m_inst_first[v][O] = u32(m_inst_spans.size());
			if (!m_inst_twins[O * 2 + lod].E)
				continue;
			xr_vector<SlotItemVec*>& vis = list[O];
			const bool rows_obj = records && m_vis_rows[v].size() == list.size() && m_vis_rows[v][O].size() == vis.size();
			for (u32 k = 0; k < vis.size(); k++)
			{
				SlotItemVec* items = vis[k];
				InstSpan span = { items, total, 0, rows_obj ? m_vis_rows[v][O][k] : u32(-1), { 0, 0, 0 }, 0 };
				if (!items->empty())
				{
					span.P = items->front()->position;
					span.distance = items->front()->distance;
				}
				m_inst_spans.push_back(span);
				total += u32(items->size());
			}
		}
		m_inst_first[v][objects.size()] = u32(m_inst_spans.size());
	}

	const u32 spans = u32(m_inst_spans.size());
	if (res_On() && !m_res_off && total && total + (spans + 4) / 4 <= (1u << 24) && res_Create(total))
	{
		const u32 lists = u32(m_rows.size()) * (dm_obj_in_slot * 3);
		if (m_res_list.size() != lists)
			m_res_list.assign(lists, ResList{ 0, 0, 0 });
		m_res_span.resize(spans);

		enum { res_none, res_reuse, res_rows, res_walk };
		const bool sector = RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector);
		const float fade = fade_distance;
		const Fvector lpos = light_position;
		const u32 epoch = m_rows_epoch;
		const u32 build = m_res_build;
		const u32 next_build = build + 1 ? build + 1 : 1;
		const bool res_ex = m_inst_ex;

		xr_parallel_for(0u, spans, [&](u32 s)
		{
			InstSpan& span = m_inst_spans[s];
			ResSpan& rs = m_res_span[s];
			rs.src = 0;
			rs.kind = res_none;
			if (!sector)
				return;
			const SlotItemVec& items = *span.items;
			const u32 n = u32(items.size());
			if (span.rows_id != u32(-1))
			{
				const u32 j = span.rows_id % (dm_obj_in_slot * 3);
				const SlotRows& R = m_rows[span.rows_id / (dm_obj_in_slot * 3)];
				ResList& L = m_res_list[span.rows_id];
				if ((R.ready >> j & 1) && R.epoch == epoch && R.first[j + 1] - R.first[j] == n && (!res_ex || R.ex.size() * 4 == R.rows.size()))
				{
					const bool reuse = R.pack && L.build == build && L.pack == R.pack;
					rs.kind = reuse ? res_reuse : res_rows;
					rs.src = reuse ? L.first : 0;
					span.count = n;
					return;
				}
				L.build = 0;
			}
			span.count = n;
			rs.kind = res_walk;
		});

		u32 slots = 0, changed = 0, walked = 0;
		for (u32 s = 0; s < spans; s++)
		{
			const InstSpan& span = m_inst_spans[s];
			const ResSpan& rs = m_res_span[s];
			if (!span.count)
				continue;
			slots++;
			changed += rs.kind != res_reuse ? span.count : 0;
			walked += rs.kind == res_walk;
		}
		if (!res_Upload((slots + 4) / 4 + changed + (res_ex ? (changed + 3) / 4 : 0)))
			goto instanced;

		if (walked)
		{
			xr_parallel_for(0u, spans, [&](u32 s)
			{
				InstSpan& span = m_inst_spans[s];
				if (m_res_span[s].kind != res_walk)
					return;
				const SlotItemVec& items = *span.items;
				const u32 n = span.count;
				float scale = 1.f;
				if (fade <= -1)
					scale *= 1.0f - span.P.distance_to_xz_sqr(lpos) * 0.005f;
				else if (span.distance > fade)
					scale *= 1.0f - abs(span.distance - fade) * 0.005f;

				bool step = true;
				u32 i = 0;
				for (; i < n; i++)
				{
					SlotItem& Instance = *items[i];
					if (step)
						Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);
					if (Instance.alpha <= 0)
						break;
					if (scale <= 0)
						step = false;
				}
				span.count = i;
			});
		}

		u32 jobs = 0;
		bool dirty = false;
		for (u32 s = 0; s < spans; s++)
		{
			const InstSpan& span = m_inst_spans[s];
			const ResSpan& rs = m_res_span[s];
			if (!span.count)
				continue;
			jobs++;
			dirty = dirty || rs.kind != res_reuse || rs.src != span.first;
		}

		if (dirty)
		{
			D3D11_MAPPED_SUBRESOURCE sub;
			if (FAILED(HW.pContext->Map(m_res_up, 0, D3D11_MAP_WRITE_DISCARD, 0, &sub)))
				return;
			const u32 base = (jobs + 4) / 4;
			const u32 gx = _min(jobs, 65535u);
			u32* job = (u32*)sub.pData;
			job[0] = jobs;
			job[1] = gx;
			job[2] = base;
			job[3] = 0;
			job += 4;
			u32 up = base;
			for (u32 s = 0; s < spans; s++)
			{
				const InstSpan& span = m_inst_spans[s];
				ResSpan& rs = m_res_span[s];
				if (!span.count)
					continue;
				const bool copy = rs.kind == res_reuse;
				if (!copy)
				{
					rs.src = up;
					up += span.count;
				}
				job[0] = rs.src;
				job[1] = span.first;
				job[2] = span.count;
				job[3] = copy ? 0 : 1;
				job += 4;
			}

			// Ex entries follow the uploaded rows, one per uploaded instance
			Fvector4* upload = (Fvector4*)sub.pData;
			Fvector4* upload_ex = res_ex ? upload + up * 4 - base : nullptr;
			if (res_ex)
				((u32*)sub.pData)[3] = up * 4 - base;
			xr_parallel_for(0u, spans, [&](u32 s)
			{
				const InstSpan& span = m_inst_spans[s];
				const ResSpan& rs = m_res_span[s];
				if (!span.count)
					return;
				if (rs.kind == res_walk)
				{
					Fvector4* dst = upload + rs.src * 4;
					Fvector4* dst_ex = upload_ex ? upload_ex + rs.src : nullptr;
					for (u32 i = 0; i < span.count; i++)
					{
						const SlotItem& Instance = *(*span.items)[i];
						const Fmatrix& M = Instance.mRotY_calculated;
						dst[0].set(M._11, M._21, M._31, M._41);
						dst[1].set(M._12, M._22, M._32, M._42);
						dst[2].set(M._13, M._23, M._33, M._43);
						dst[3].set(Instance.c_sun, Instance.c_sun, Instance.c_sun, Instance.c_hemi);
						dst += 4;
						if (dst_ex)
							dst_ex[i].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);
					}
					return;
				}
				const u32 j = span.rows_id % (dm_obj_in_slot * 3);
				const SlotRows& R = m_rows[span.rows_id / (dm_obj_in_slot * 3)];
				if (rs.kind == res_rows)
				{
					CopyMemory(upload + rs.src * 4, &R.rows[R.first[j] * 4], span.count * sizeof(Fvector4) * 4);
					if (upload_ex)
						CopyMemory(upload_ex + rs.src, &R.ex[R.first[j]], span.count * sizeof(Fvector4));
				}
				m_res_list[span.rows_id] = { span.first, R.pack, next_build };
			});
			HW.pContext->Unmap(m_res_up, 0);

			const u32 next = m_res_cur ^ 1;
			RCache.set_CS(m_res_cs);
			SRVSManager.SetCSResource(0, m_res_srv[m_res_cur]);
			SRVSManager.SetCSResource(1, m_res_up_srv);
			if (res_ex)
				SRVSManager.SetCSResource(2, m_res_ex_srv[m_res_cur]);
			ID3D11UnorderedAccessView* uav[2] = { m_res_uav[next], m_res_ex_uav[next] };
			HW.pContext->CSSetUnorderedAccessViews(0, res_ex ? 2 : 1, uav, nullptr);
			RCache.Compute(gx, (jobs + gx - 1) / gx, 1);
			ID3D11UnorderedAccessView* none[2] = {};
			HW.pContext->CSSetUnorderedAccessViews(0, res_ex ? 2 : 1, none, nullptr);
			SRVSManager.SetCSResource(0, nullptr);
			SRVSManager.SetCSResource(1, nullptr);
			if (res_ex)
				SRVSManager.SetCSResource(2, nullptr);
			SRVSManager.Apply();
			m_res_cur = next;
			m_res_build = next_build;
		}

		m_inst_total = total;
		m_inst_frame = frame;
		m_res_frame = frame;
		return;
	}

	// A failed resident upload lands here before any alpha step
instanced:
	if (!total || total > (1u << 24) || !inst_Grow(total))
		return;

	D3D11_MAPPED_SUBRESOURCE sub, sub_ex = {};
	if (FAILED(HW.pContext->Map(m_inst_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &sub)))
		return;
	if (m_inst_ex && FAILED(HW.pContext->Map(m_inst_ex_buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &sub_ex)))
	{
		HW.pContext->Unmap(m_inst_buf, 0);
		return;
	}
	Fvector4* rows = (Fvector4*)sub.pData;
	Fvector4* ex = m_inst_ex ? (Fvector4*)sub_ex.pData : nullptr;

	const bool sector = RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector);
	const float fade = fade_distance;
	const Fvector lpos = light_position;
	const u32 epoch = m_rows_epoch;
	xr_parallel_for(0u, u32(m_inst_spans.size()), [&](u32 s)
	{
		InstSpan& span = m_inst_spans[s];
		if (!sector)
			return;
		const SlotItemVec& items = *span.items;
		const u32 n = u32(items.size());
		Fvector4* dst = rows + span.first * 4;
		Fvector4* dst_ex = ex ? ex + span.first : nullptr;

		if (span.rows_id != u32(-1))
		{
			const u32 j = span.rows_id % (dm_obj_in_slot * 3);
			const SlotRows& R = m_rows[span.rows_id / (dm_obj_in_slot * 3)];
			if ((R.ready >> j & 1) && R.epoch == epoch && R.first[j + 1] - R.first[j] == n && (!ex || R.ex.size() * 4 == R.rows.size()))
			{
				CopyMemory(dst, &R.rows[R.first[j] * 4], n * sizeof(Fvector4) * 4);
				if (ex)
					CopyMemory(dst_ex, &R.ex[R.first[j]], n * sizeof(Fvector4));
				span.count = n;
				return;
			}
		}

		float scale = 1.f;
		if (fade <= -1)
			scale *= 1.0f - span.P.distance_to_xz_sqr(lpos) * 0.005f;
		else if (span.distance > fade)
			scale *= 1.0f - abs(span.distance - fade) * 0.005f;

		bool step = true;
		u32 i = 0;
		for (; i < n; i++)
		{
			SlotItem& Instance = *items[i];
			if (step)
				Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);
			if (Instance.alpha <= 0)
				break;
			if (scale <= 0)
				step = false;
			const Fmatrix& M = Instance.mRotY_calculated;
			dst[0].set(M._11, M._21, M._31, M._41);
			dst[1].set(M._12, M._22, M._32, M._42);
			dst[2].set(M._13, M._23, M._33, M._43);
			dst[3].set(Instance.c_sun, Instance.c_sun, Instance.c_sun, Instance.c_hemi);
			dst += 4;
			if (dst_ex)
				dst_ex[i].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);
		}
		span.count = i;
	});

	HW.pContext->Unmap(m_inst_buf, 0);
	if (ex)
		HW.pContext->Unmap(m_inst_ex_buf, 0);
	m_inst_total = total;
	m_inst_frame = frame;
}

void CDetailManager::inst_Draw(CDetail& Object, u32 O, u32 var_id, const InstTwin& twin, const MergeTwin* merge, const OccRec* occ, light* L, bool cull, float cull_grow, bool cull_frustum, u32 vOffset, u32 iOffset)
{
	static shared_str strDraw("dt_draw");
	if (m_inst_bound[0] != s8(twin.rows))
	{
		HW.pContext->VSSetShaderResources(twin.rows, 1, m_res_frame == m_inst_frame ? &m_res_srv[m_res_cur] : &m_inst_srv);
		m_inst_bound[0] = s8(twin.rows);
	}
	if (twin.ex != 0xff && m_inst_bound[1] != s8(twin.ex))
	{
		HW.pContext->VSSetShaderResources(twin.ex, 1, m_res_frame == m_inst_frame ? &m_res_ex_srv[m_res_cur] : &m_inst_ex_srv);
		m_inst_bound[1] = s8(twin.ex);
	}
	if (merge && m_inst_bound[2] != s8(merge->verts))
	{
		HW.pContext->VSSetShaderResources(merge->verts, 1, &m_merge_srv);
		m_inst_bound[2] = s8(merge->verts);
	}
	if (merge)
		RCache.set_Indices(m_merge_ib);

	const bool thin_on = m_thin_on && !m_svp_pass;
	if (merge && thin_on && (occ ? merge->occ_thin._get() : merge->thin._get()))
	{
		static shared_str strThin("dt_thin");
		RCache.set_ca(strThin, 0, m_thin_c[0]);
		RCache.set_ca(strThin, 1, m_thin_c[1]);
	}

	const bool smap = RImplementation.phase == CRender::PHASE_SMAP;

	if (occ)
	{
		if (m_inst_bound[3] != s8(merge->idx))
		{
			HW.pContext->VSSetShaderResources(merge->idx, 1, &m_occ_idx_srv);
			m_inst_bound[3] = s8(merge->idx);
		}
		RCache.set_c(strDraw, float(occ->out), 1.f, float(m_merge_base[O]), float(Object.number_vertices));
		RCache.RenderInstancedIndirect(D3DPT_TRIANGLELIST, m_occ_args_buf, (var_id * u32(objects.size()) + O) * 20);

		if (m_occ_first_group)
		{
			RCache.set_Indices(hw_IB);
			return;
		}
		Device.Statistic->RenderDUMP_DT_Count += occ->inst;
		RCache.stat.r.s_details.add(occ->inst * Object.number_vertices);
		RCache.set_Indices(hw_IB);
		return;
	}
	const u32 s0 = m_inst_first[var_id][O], s1 = m_inst_first[var_id][O + 1];
	const bool cull_obj = cull && m_vis_bounds[var_id][O].size() == s1 - s0;

	u32 run_first = 0, run_n = 0;
	float run_scale = 1.f;
	// pip stats, only the culled passes count their runs
	auto count_run = [&](u32 draws)
	{
		if (cull_obj)
		{
			m_cull_stats.runs++;
			m_cull_stats.run_max = _max(m_cull_stats.run_max, run_n);
			m_cull_stats.draws += draws;
		}
		run_n = 0;
	};
	auto flush = [&]()
	{
		if (merge)
		{
			const u32 full = run_n / merge_K, rem = run_n % merge_K;
			const float base = float(m_merge_base[O]), count = float(Object.number_vertices);
			if (full)
			{
				RCache.set_c(strDraw, float(run_first), run_scale, base, count);
				RCache.RenderInstanced(D3DPT_TRIANGLELIST, 0, merge_K * Object.number_vertices, m_merge_first[O], merge_K * Object.number_indices / 3, full);
			}
			if (rem)
			{
				RCache.set_c(strDraw, float(run_first + full * merge_K), run_scale, base, count);
				RCache.RenderInstanced(D3DPT_TRIANGLELIST, 0, rem * Object.number_vertices, m_merge_first[O], rem * Object.number_indices / 3, 1);
			}
			Device.Statistic->RenderDUMP_DT_Count += run_n;
			RCache.stat.r.s_details.add(run_n * Object.number_vertices);
			count_run(u32(full != 0) + u32(rem != 0));
			return;
		}
		RCache.set_c(strDraw, float(run_first), run_scale, 0.f, 0.f);
		RCache.RenderInstanced(D3DPT_TRIANGLELIST, vOffset, Object.number_vertices, iOffset, Object.number_indices / 3, run_n);
		Device.Statistic->RenderDUMP_DT_Count += run_n;
		RCache.stat.r.s_details.add(run_n * Object.number_vertices);
		count_run(1);
	};

	for (u32 s = s0; s < s1; s++)
	{
		const InstSpan& span = m_inst_spans[s];
		if (cull_obj && cull_Span(m_vis_bounds[var_id][O][s - s0], L, cull_grow, cull_frustum, span.count))
			continue;

		float scale = 1.f;
		if (fade_distance <= -1)
			scale *= 1.0f - span.P.distance_to_xz_sqr(light_position) * 0.005f;
		else if (span.distance > fade_distance)
			scale *= 1.0f - abs(span.distance - fade_distance) * 0.005f;
		const bool drawn = span.count && scale > 0 && !(smap && L && L->position.distance_to_sqr(span.P) >= _sqr(L->range));
		if (!drawn)
			continue;

		if (run_n && (span.first != run_first + run_n || scale != run_scale))
			flush();
		if (!run_n)
		{
			run_first = span.first;
			run_scale = scale;
		}
		run_n += span.count;
	}
	if (run_n)
		flush();
	if (merge)
		RCache.set_Indices(hw_IB);
}
#endif

void CDetailManager::hw_Load_Shaders()
{
	// Create shader to access constant storage
	ref_shader S;
	S.create("details\\set");
	R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
	R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
	hwc_consts = T0.get("consts");
	hwc_wave = T0.get("wave");
	hwc_wind = T0.get("dir2D");
	hwc_array = T0.get("array");
	hwc_s_consts = T1.get("consts");
	hwc_s_xform = T1.get("xform");
	hwc_s_array = T1.get("array");
}

// pip adds one pass worth of cull counts to the stats overlay counters
static void cull_Publish(const CDetailManager::CullStats& c, u32& tested, u32& kept, u32& runs, u32& dropped,
                         u32& draws, u32& run_max)
{
	tested += c.tested;
	kept += c.kept;
	runs += c.runs;
	dropped += c.dropped;
	draws += c.draws;
	run_max = _max(run_max, c.run_max);
}

void CDetailManager::hw_Render(light* L)
{
	PROF_EVENT("CDetailManager::hw_Render");
	// Render-prepare
	//	Update timer
	//	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
	float fDelta = Device.fTimeGlobal - m_global_time_old;
	if ((fDelta < 0) || (fDelta > 1)) fDelta = 0.03;
	m_global_time_old = Device.fTimeGlobal;

	m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
	m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
	m_time_pos += fDelta * swing_current.speed;

	//float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
	//float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
	float tm_rot1 = m_time_rot_1;
	float tm_rot2 = m_time_rot_2;

#ifdef USE_DX11
	if (m_res_cap && m_res_frame != Device.dwFrame && !res_On())
		res_Release();

	if (m_occ_cap && !ps_r__detail_occ)
		occ_Release();

	// pip only the main view builds, a scope pass after a failed build must not step the alpha again
	const bool main_view = RImplementation.phase == CRender::PHASE_NORMAL && !m_svp_pass;

	if (ps_r__detail_inst && main_view && m_inst_frame != Device.dwFrame && !m_inst_twins.empty())
	{
		inst_Build();

		const float start = ps_r__detail_thin_start, keep = ps_r__detail_thin_keep;
		m_thin_on = ps_r__detail_thin && keep < 1 && !m_merge.empty();
		if (m_thin_on)
		{
			const Fvector& cam = Device.vCameraPosition;
			m_thin_c[0].set(cam.x, cam.y, cam.z, start);
			m_thin_c[1].set(keep, ps_r__detail_thin_band, (dm_fade - start) / (1.f - keep), 0.f);
		}
	}

	if (ps_r__detail_occ && !m_occ_off && main_view)
	{
		LPCSTR reason = nullptr;
		if (occ_On(reason))
		{
			if (m_occ_frame != Device.dwFrame && occ_Create(m_inst_total, u32(m_inst_spans.size())))
				occ_Build();
		}
		else if (xr_strcmp(reason, "frame"))
		{
			if (!m_occ_logged)
				Msg("[DT-OCC] off reason=%s", reason);
			m_occ_logged = true;
			occ_Release();
		}
	}
#endif

	Fvector4 dir1, dir2;
	dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(swing_current.amp1);
	dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(swing_current.amp2);

	// Setup geometry and DMA
	RCache.set_CullMode(CULL_NONE);
	RCache.set_xform_world(Fidentity);
	RCache.set_Geometry(hw_Geom);
	m_cull_stats = {};
	float scale = 1.f / float(quant);
	Fvector4 wave, prev_wave;
	Fvector4 consts;

#ifdef USE_DX11
	m_occ_first = m_occ_frame == Device.dwFrame && m_occ_first_on && m_occ_var[2][1] && main_view;
	if (m_occ_first)
	{
		occ_Dispatch(0, m_occ_var[2][1], 0, u32(-1));

		// Same constants as the main draw below, the still wave divided twice
		Fvector4 wave_consts, still_consts, wave1, prev_wave1, wave2, prev_wave2, wave0, prev_wave0;
		wave_consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
		still_consts.set(scale, scale, scale, 1.f);
		wave1.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
		prev_wave1.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
		wave2.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
		prev_wave2.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
		wave2.div(PI_MUL_2);
		prev_wave2.div(PI_MUL_2);
		wave0.set(wave2);
		prev_wave0.set(prev_wave2);

		m_occ_first_group = true;
		hw_Render_dump(wave_consts, wave1.div(PI_MUL_2), dir1, prev_wave1.div(PI_MUL_2), prev_dir1, 1, 0, L);
		hw_Render_dump(wave_consts, wave2, dir2, prev_wave2, prev_dir2, 2, 0, L);
		hw_Render_dump(still_consts, wave0.div(PI_MUL_2), dir2, prev_wave0.div(PI_MUL_2), prev_dir2, 0, 1, L);
		m_occ_first_group = false;
	}
#endif

	// Wave0
	consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
	//wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
	prev_wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, prev_time);
	//RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);				// consts
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir1);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir1, prev_wave.div(PI_MUL_2), prev_dir1, 1, 0, L);

	// Wave1
	//wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
	wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
	prev_wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, prev_time);
	//RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
	//RCache.set_c			(&*hwc_wind,	dir2);																					// wind-dir
	//hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 2, 0, L);

	// Still
	consts.set(scale, scale, scale, 1.f);
	//RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
	//RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
	//hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
	hw_Render_dump(consts, wave.div(PI_MUL_2), dir2, prev_wave.div(PI_MUL_2), prev_dir2, 0, 1, L);

#ifdef USE_DX11
	if (m_inst_frame == Device.dwFrame)
	{
		for (u32 k = 0; k < 4; k++)
		{
			if (m_inst_bound[k] < 0)
				continue;
			ID3D11ShaderResourceView* none = nullptr;
			HW.pContext->VSSetShaderResources(m_inst_bound[k], 1, &none);
			m_inst_bound[k] = -1;
		}
	}
#endif

	if (prev_frame != Device.dwFrame) 
	{
		prev_frame = Device.dwFrame;
		
		// Prev Frame swing time
		prev_time = m_time_pos;

		// Prev frame dir
		prev_dir1.set(dir1);
		prev_dir2.set(dir2);
	}

	// pip stats, the sun cascades fill the main grass counters and the scope pass its own
	if (ps_r__svp_stats && m_sun_cull)
		cull_Publish(m_cull_stats, svp_stats_grass_slots, svp_stats_grass_keep, svp_stats_grass_runs,
		             svp_stats_grass_drop, svp_stats_grass_draws, svp_stats_grass_run_max);
	else if (ps_r__svp_stats && m_svp_cull)
		cull_Publish(m_cull_stats, svp_stats_grass_svp_slots, svp_stats_grass_svp_keep, svp_stats_grass_svp_runs,
		             svp_stats_grass_svp_drop, svp_stats_grass_svp_draws, svp_stats_grass_svp_run_max);
	if (m_svp_cull && m_cull_stats.dropped)
		svp_ledger_grass_cull = 1;

	RCache.set_CullMode(CULL_CCW);
}

void CDetailManager::svp_Begin(Fmatrix& full_xform)
{
	m_svp_pass = true;
	if (!ps_r__svp_grass_cull)
		return;
	// lateral planes and a far plane, no near plane, same build the scope world cull uses
	m_svp_cull_frustum.CreateFromMatrix(full_xform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);
	m_svp_cull = &m_svp_cull_frustum;
}

void CDetailManager::svp_End()
{
	m_svp_pass = false;
	m_svp_cull = nullptr;
}

// True when a slot sphere falls outside the light, sun cascade or scope view
bool CDetailManager::cull_Span(Fsphere b, light* L, float grow, bool frustum, u32 count)
{
	bool culled;
	if (L)
	{
		culled = L->position.distance_to_sqr(b.P) >= _sqr(L->range);
		if (!culled && fade_distance <= -1)
			culled = 1.0f - b.P.distance_to_xz_sqr(light_position) * 0.005f <= 0;
		if (!culled && frustum)
			culled = !L->X.S.frustum.testSphere_dirty(b.P, b.R * grow);
	}
	else
		culled = !(m_sun_cull ? m_sun_cull : m_svp_cull)->testSphere_dirty(b.P, b.R * grow);
	m_cull_stats.tested++;
	if (culled)
		m_cull_stats.dropped += count;
	else
		m_cull_stats.kept++;
	return culled;
}

void CDetailManager::hw_Render_dump(const Fvector4& consts, const Fvector4& wave, const Fvector4& wind, 
									const Fvector4& prev_wave, const Fvector4& prev_wind, u32 var_id, u32 lod_id, light* L)
{
	if (RImplementation.phase == CRender::PHASE_SMAP && var_id == 0)
		return;

	bool inst_on = false;
#ifdef USE_DX11
	inst_on = m_inst_frame == Device.dwFrame;
#endif

	static shared_str strConsts("consts");
	static shared_str strWave("wave");
	static shared_str strDir2D("dir2D");
	static shared_str strArray("array");
	static shared_str strXForm("xform");

	// Vanilla grass/trees wind
	static shared_str strWavePrev("wave_prev");
	static shared_str strDir2DPrev("dir2D_prev");

	// Grass Benders
	static shared_str strPrevPos("benders_prevpos");
	static shared_str strPos("benders_pos");
	static shared_str strGrassSetup("benders_setup");

	static shared_str strExData("exdata");
	static shared_str strGrassAlign("grass_align");

	// Grass benders data
	IGame_Persistent::grass_data& GData = g_pGamePersistent->grass_shader_data;
	Fvector4 player_pos = { 0, 0, 0, 0 };
	int BendersQty = _min(16, ps_ssfx_grass_interactive.y + 1);

	// Add Player?
	if (ps_ssfx_grass_interactive.x > 0)
		player_pos.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, -1);

	Device.Statistic->RenderDUMP_DT_Count = 0;

	// Matrices and offsets
	u32 vOffset = 0;
	u32 iOffset = 0;

	vis_list& list = m_visibles[var_id];

	CEnvDescriptor& desc = *g_pGamePersistent->Environment().CurrentEnv;
	Fvector c_sun, c_ambient, c_hemi;
	c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
	c_sun.mul(.5f);
	c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
	c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

	// pip the main-pass drain keeps the visible set when the SVP pass draws the scope grass second
	extern bool g_svp_defer_detail_clear;

	const bool rows_on = ps_r__detail_rows != 0;
	if (rows_on && !(Device.fTimeDelta >= 0) && !m_occ_first_group)
		m_rows_epoch++;

	const bool use_rows = rows_on && m_vis_rows_frame == Device.dwFrame && m_vis_rows[var_id].size() == list.size() && Device.fTimeDelta >= 0;

	bool sector_visible = RImplementation.GMBase.is_sector_visible(RImplementation.pOutdoorSector);
	if (sector_visible && RImplementation.phase == CRender::PHASE_SMAP && L)
		sector_visible = L->GMLight.is_sector_visible(RImplementation.pOutdoorSector);

	// Prefetch spans SlotItem from the matrix to alpha_target
	const int prefetch_ahead = 8;
	const size_t prefetch_first = offsetof(SlotItem, mRotY_calculated);
	const size_t prefetch_last = offsetof(SlotItem, alpha_target) + sizeof(float) - 1;

	// pip the scope pass draws after the main one in the same frame, the alpha already stepped
	const bool alpha_step = RImplementation.phase == CRender::PHASE_NORMAL && !m_svp_pass;

	const bool cull = (RImplementation.phase == CRender::PHASE_SMAP ?
		(L ? ps_r__detail_shadow_cull != 0 : m_sun_cull != nullptr) : m_svp_cull != nullptr) &&
		m_vis_bounds_frame == Device.dwFrame && m_vis_bounds[var_id].size() == list.size();
	float cull_grow = 1.f;
	bool cull_frustum = false;
	if (cull)
	{
		cull_grow = 1.f + 4.f * _sqrt(wind.x * wind.x + wind.z * wind.z);
		cull_frustum = L && (L->flags.type == IRender_Light::SPOT || L->flags.type == IRender_Light::OMNIPART);
	}

#ifdef USE_DX11
	const bool cb_direct_on = rows_on;

	// pip the occlusion results come from the main camera's depth, the scope pass draws without them
	const bool occ_on = inst_on && m_occ_frame == Device.dwFrame && RImplementation.phase == CRender::PHASE_NORMAL && !m_svp_pass;
	if (occ_on && sector_visible && !m_occ_first_group)
	{
		occ_Test(var_id);
		occ_Dispatch(m_occ_var[var_id][0], m_occ_var[var_id][1], m_occ_first ? 2 : 0, m_occ_stamp);
	}
#endif

	// Iterate
	for (u32 O = 0; O < objects.size(); O++)
	{
		CDetail& Object = *objects[O];
		xr_vector<SlotItemVec*>& vis = list[O];
		if (!vis.empty() || m_occ_first_group)
		{
			ShaderElement* element = Object.shader->E[lod_id]._get();
#ifdef USE_DX11
			const InstTwin* twin = inst_on && m_inst_twins[O * 2 + lod_id].E ? &m_inst_twins[O * 2 + lod_id] : nullptr;
			if (twin)
				element = twin->E._get();

			const MergeTwin* merge = nullptr;
			if (twin && ps_r__detail_merge && !m_merge.empty() && m_merge_first[O] != u32(-1))
			{
				auto it = m_merge.find(element);
				if (it != m_merge.end())
				{
					merge = &it->second;
					element = m_thin_on && !m_svp_pass && it->second.thin ? it->second.thin._get() : it->second.E._get();
				}
			}

			const OccRec* occ = occ_on && merge && m_occ_rec[var_id * objects.size() + O].on ? &m_occ_rec[var_id * objects.size() + O] : nullptr;
			if (occ)
				element = m_thin_on && merge->occ_thin ? merge->occ_thin._get() : merge->occ._get();

			// The first group draws occ records only, their lists may already be cleared
			if (m_occ_first_group && !occ)
				continue;

			if (m_occ_first_group && !occ->forced)
				continue;
#endif
			for (u32 iPass = 0; iPass < element->passes.size(); ++iPass)
			{
				// Setup matrices + colors (and flush it as necessary)
				//RCache.set_Element				(Object.shader->E[lod_id]);
				RCache.set_Element(element, iPass);
				RImplementation.apply_lmaterial();

				//	This could be cached in the corresponding consatant buffer
				//	as it is done for DX9
				RCache.set_c(strConsts, consts);
				RCache.set_c(strWave, wave);
				RCache.set_c(strDir2D, wind);
				RCache.set_c(strXForm, Device.mFullTransform);
				RCache.set_c(strGrassAlign, ps_ssfx_terrain_grass_align);

				RCache.set_c(strWavePrev, prev_wave);
				RCache.set_c(strDir2DPrev, prev_wind);

				if (ps_ssfx_grass_interactive.y > 0)
				{
					RCache.set_c(strGrassSetup, ps_ssfx_int_grass_params_1);

					Fvector4* c_grass;
					{
						void* GrassData;
						RCache.get_ConstantDirect(strPos, BendersQty * sizeof(Fvector4) * 2, &GrassData, 0, 0);
						c_grass = (Fvector4*)GrassData;
					}
					VERIFY(c_grass);

					if (c_grass)
					{
						c_grass[0].set(player_pos);
						c_grass[16].set(0.0f, -99.0f, 0.0f, 1.0f);

						for (int Bend = 1; Bend < BendersQty; Bend++)
						{
							c_grass[Bend].set(GData.pos[Bend].x, GData.pos[Bend].y, GData.pos[Bend].z, GData.radius_curr[Bend]);
							c_grass[Bend + 16].set(GData.dir[Bend].x, GData.dir[Bend].y, GData.dir[Bend].z, GData.str[Bend]);
						}
					}

					Fvector4* c_prev_grass;
					{
						void* prev_GrassData;
						RCache.get_ConstantDirect(strPrevPos, BendersQty * sizeof(Fvector4) * 2, &prev_GrassData, 0, 0);
						c_prev_grass = (Fvector4*)prev_GrassData;
					}
					VERIFY(c_prev_grass);

					if (c_prev_grass)
					{
						for (int Bend = 0; Bend < BendersQty; Bend++)
						{
							c_prev_grass[Bend].set(GData.prev_pos[Bend]);
							c_prev_grass[Bend + 16].set(GData.prev_dir[Bend]);
						}
					}
				}

#ifdef USE_DX11
				if (twin)
				{
					if (sector_visible)
						inst_Draw(Object, O, var_id, *twin, merge, occ, L, cull, cull_grow, cull_frustum, vOffset, iOffset);
					continue;
				}
#endif

				Fvector4* c_ExData = 0;
				{
					void* pExtraData;
					RCache.get_ConstantDirect(strExData, hw_BatchSize * sizeof(Fvector4), &pExtraData, 0, 0);
					c_ExData = (Fvector4*)pExtraData;
				}
				VERIFY(c_ExData);

				if (rows_on && c_ExData && !m_rows_ex)
				{
					m_rows_ex = true;
					m_rows_epoch++;
				}

				//ref_constant constArray = RCache.get_c(strArray);
				//VERIFY(constArray);

				//u32			c_base				= x_array->vs.index;
				//Fvector4*	c_storage			= RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
				Fvector4* c_storage = 0;
				u32 dwBatch = 0;

#ifdef USE_DX11
				dx10ConstantBuffer* const direct = cb_direct_on ? cb_direct_begin(strArray, strExData, c_ExData) : nullptr;
				bool mapped = false;

				auto map_direct = [&]()
				{
					u8* image = cb_direct_map();
					c_storage = (Fvector4*)(image + m_cb_direct_array_off);
					if (m_cb_direct_ex_off != u32(-1))
						c_ExData = (Fvector4*)(image + m_cb_direct_ex_off);
					mapped = true;
				};
#endif

				auto map_array = [&]()
				{
					void* pVData;
					RCache.get_ConstantDirect(strArray,
					                          hw_BatchSize * sizeof(Fvector4) * 4,
					                          &pVData, 0, 0);
					c_storage = (Fvector4*)pVData;
				};

				auto submit = [&]()
				{
					Device.Statistic->RenderDUMP_DT_Count += dwBatch;
					u32 dwCNT_verts = dwBatch * Object.number_vertices;
					u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
					//RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
					//RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
#ifdef USE_DX11
					if (mapped)
					{
						cb_direct_submit(dwBatch);
						mapped = false;
					}
#endif
					RCache.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
					RCache.stat.r.s_details.add(dwCNT_verts);
					// pip stats, a culled pass counts each batch as one run
					if (cull)
					{
						m_cull_stats.runs++;
						m_cull_stats.run_max = _max(m_cull_stats.run_max, dwBatch);
						m_cull_stats.draws++;
					}
				};

				//	Map constants to memory directly
				map_array();
				VERIFY(c_storage);

				xr_vector<SlotItemVec*>::iterator _vI = vis.begin();
				xr_vector<SlotItemVec*>::iterator _vE = vis.end();
				if (!sector_visible)
					_vI = _vE;
				const bool cull_obj = cull && m_vis_bounds[var_id][O].size() == vis.size();
				const bool rows_obj = use_rows && m_vis_rows[var_id][O].size() == vis.size();
				for (; _vI != _vE; _vI++)
				{
					SlotItemVec* items = *_vI;

					if (cull_obj && cull_Span(m_vis_bounds[var_id][O][_vI - vis.begin()], L, cull_grow, cull_frustum, u32(items->size())))
						continue;

					if (rows_obj)
					{
						const u32 id = m_vis_rows[var_id][O][_vI - vis.begin()];
						const u32 j = id % (dm_obj_in_slot * 3);
						SlotRows& R = m_rows[id / (dm_obj_in_slot * 3)];
						if ((R.ready >> j & 1) && R.epoch == m_rows_epoch)
						{
							if (RImplementation.phase == CRender::PHASE_SMAP && L && L->position.distance_to_sqr(R.P) >= _sqr(L->range))
								continue;

							float scale = 1.f;
							if (fade_distance <= -1)
								scale *= 1.0f - R.P.distance_to_xz_sqr(light_position) * 0.005f;
							else if (R.distance > fade_distance)
								scale *= 1.0f - abs(R.distance - fade_distance) * 0.005f;
							if (scale <= 0)
								continue;

							for (u32 i = R.first[j], end = R.first[j + 1]; i < end;)
							{
#ifdef USE_DX11
								if (direct && !mapped)
									map_direct();
#endif
								const u32 take = _min(end - i, hw_BatchSize - dwBatch);
								const Fvector4* src = &R.rows[i * 4];
								Fvector4* dst = c_storage + dwBatch * 4;

								// w keeps the unscaled source value
								const __m128 s4 = _mm_set1_ps(scale);
								const __m128 w_mask = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
								for (u32 t = 0; t < take; t++, src += 4, dst += 4)
								{
									for (u32 r = 0; r < 3; r++)
									{
										const __m128 v = _mm_loadu_ps(&src[r].x);
										_mm_storeu_ps(&dst[r].x, _mm_or_ps(_mm_andnot_ps(w_mask, _mm_mul_ps(v, s4)), _mm_and_ps(w_mask, v)));
									}
									_mm_storeu_ps(&dst[3].x, _mm_loadu_ps(&src[3].x));
								}
								if (c_ExData)
									CopyMemory(c_ExData + dwBatch, &R.ex[i], take * sizeof(Fvector4));
								dwBatch += take;
								i += take;
								if (dwBatch == hw_BatchSize)
								{
									submit();
									dwBatch = 0;
									map_array();
									VERIFY(c_storage);
								}
							}
							continue;
						}
					}

					SlotItemVecIt _iI = items->begin();
					SlotItemVecIt _iE = items->end();
					for (; _iI != _iE; _iI++)
					{
						if (_iE - _iI > prefetch_ahead)
						{
							const char* P = (const char*)*(_iI + prefetch_ahead);
							_mm_prefetch(P + prefetch_first, _MM_HINT_T0);
							_mm_prefetch(P + prefetch_first + 64, _MM_HINT_T0);
							_mm_prefetch(P + prefetch_last, _MM_HINT_T0);
						}

						SlotItem& Instance = **_iI;

						if (RImplementation.phase == CRender::PHASE_SMAP && L)
						{
							if (L->position.distance_to_sqr(Instance.position) >= _sqr(L->range))
								continue;
						}

						u32 base = dwBatch * 4;

						if (alpha_step)
							Instance.alpha += GoToValue(Instance.alpha, Instance.alpha_target);

						float scale = 1.f;

						// Sort of fade using the scale
						// fade_distance == -1 use light_position to define "fade", anything else uses fade_distance
						if (fade_distance <= -1)
							scale *= 1.0f - Instance.position.distance_to_xz_sqr(light_position) * 0.005f;
						else if (Instance.distance > fade_distance)
							scale *= 1.0f - abs(Instance.distance - fade_distance) * 0.005f;

						if (scale <= 0 || Instance.alpha <= 0)
							break;

#ifdef USE_DX11
						if (direct && !mapped)
							map_direct();
#endif
						// Build matrix ( 3x4 matrix, last row - color )
						Fmatrix& M = Instance.mRotY_calculated;
						c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
						c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
						c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
						//RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,	M._31*scale,	M._41	);
						//RCache.set_ca(&*constArray, base+1, M._12*scale,	M._22*scale,	M._32*scale,	M._42	);
						//RCache.set_ca(&*constArray, base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

						// Build color
						// R2 only needs hemisphere
						float h = Instance.c_hemi;
						float s = Instance.c_sun;
						c_storage[base + 3].set(s, s, s, h);

						if (c_ExData)
							c_ExData[dwBatch].set(Instance.normal.x, Instance.normal.y, Instance.normal.z, Instance.alpha);

						//RCache.set_ca(&*constArray, base+3, s,				s,				s,				h		);
						dwBatch ++;
						if (dwBatch == hw_BatchSize)
						{
							// flush
							submit();

							// restart
							dwBatch = 0;

							//	Remap constants to memory directly (just in case anything goes wrong)
							map_array();
							VERIFY(c_storage);
						}
					}
				}
				// flush if nessecary
				if (dwBatch)
					submit();
			}
			// Clean up
			// KD: we must not clear vis on r2 since we want details shadows
			// pip the deferred clear keeps the set alive for the SVP drain, its own pass clears after
			if (ps_ssfx_grass_shadows.x <= 0 && !m_occ_first_group && !g_svp_defer_detail_clear)
			{
				if (!psDeviceFlags2.test(rsGrassShadow) || RImplementation.PHASE_NORMAL == RImplementation.phase) // phase normal without shadows
					vis.clear_not_free();
			}
		}
		vOffset += hw_BatchSize * Object.number_vertices;
		iOffset += hw_BatchSize * Object.number_indices;
	}
}
