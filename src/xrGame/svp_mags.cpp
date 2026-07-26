#include "stdafx.h"
#include "svp_mags.h"
#include "../xrEngine/svp_gameplay_cvars.h"

// pip parse a 3dss magnifications string, single N fixed, dash M-N dynamic, comma M,N stepped toggle
// returns svp_mag_none on empty or malformed so the caller keeps the legacy factors
static svp_mag_mode parse_svp_magnifications(LPCSTR raw, float& min_mag, float& max_mag)
{
	if (!raw || !raw[0]) return svp_mag_none;
	string256 buf;
	u32 j = 0;
	for (LPCSTR p = raw; *p && j + 1 < sizeof(buf); ++p)
		if (*p != ' ' && *p != '\t') buf[j++] = *p;
	buf[j] = 0;
	if (!buf[0]) return svp_mag_none;

	float a = 0.f, b = 0.f; char extra = 0;
	if (strchr(buf, '-'))
	{
		if (sscanf(buf, "%f-%f%c", &a, &b, &extra) != 2) return svp_mag_none;
		min_mag = a; max_mag = b; return svp_mag_dynamic;
	}
	if (strchr(buf, ','))
	{
		if (sscanf(buf, "%f,%f%c", &a, &b, &extra) != 2) return svp_mag_none;
		min_mag = a; max_mag = b; return svp_mag_stepped;
	}
	if (sscanf(buf, "%f%c", &a, &extra) != 1) return svp_mag_none;
	min_mag = max_mag = a; return svp_mag_fixed;
}

// pip three tier resolution with the [SVP-MAGS] proof line, mode none keeps the legacy factors
// true when the section authors either tier, malformed stays loud and terminal
static bool resolve_from_section(LPCSTR sect, float zoom_multiple, svp_mags_data& out)
{
	float min_mag = 0.f, max_mag = 0.f;
	bool authored = false, dyn = false, stepped = false;

	LPCSTR source_key = nullptr;
	LPCSTR raw = READ_IF_EXISTS(pSettings, r_string, sect, "svp_magnifications", NULL);
	if (raw && raw[0])
		source_key = "svp_magnifications";
	else
	{
		raw = READ_IF_EXISTS(pSettings, r_string, sect, "magnifications", NULL);
		if (raw && raw[0])
			source_key = "magnifications";
	}
	svp_mag_mode mode = parse_svp_magnifications(raw, min_mag, max_mag);
	if (raw && raw[0] && mode == svp_mag_none)
	{
		LPCSTR source = pSettings->DLTX_getFilenameOfLine(sect, source_key);
		PipMsg("[SVP-MAGS] %s key=%s value='%s' src=%s malformed, legacy retained",
			sect, source_key, raw, source ? source : "?");
		return true;
	}
	if (mode != svp_mag_none)
	{
		authored = true;
		dyn = (mode != svp_mag_fixed);
		stepped = (mode == svp_mag_stepped);
	}
	else if (pSettings->line_exist(sect, "magnification"))
	{
		raw = pSettings->r_string(sect, "magnification");
		source_key = "magnification";
		mode = parse_svp_magnifications(raw, min_mag, max_mag);
		if (mode == svp_mag_none)
		{
			LPCSTR source = pSettings->DLTX_getFilenameOfLine(sect, source_key);
			PipMsg("[SVP-MAGS] %s key=%s value='%s' src=%s malformed, legacy retained",
				sect, source_key, raw ? raw : "", source ? source : "?");
			return true;
		}
		authored = true;
		dyn = (mode != svp_mag_fixed);
		stepped = (mode == svp_mag_stepped);
		if (mode == svp_mag_fixed)
		{
			dyn = READ_IF_EXISTS(pSettings, r_bool, sect, "scope_dynamic_zoom", false);
			if (dyn)
				min_mag = READ_IF_EXISTS(pSettings, r_float, sect, "min_magnification", 1.f);
		}
	}

	if (!authored)
		return false;

	const float svp_mag_base = SVP_ZOOM_BASE_FOV / 0.75f;
	const float min_supported_mag = svp_mag_base / 200.f;
	const float max_supported_mag = svp_mag_base;
	if (!_valid(min_mag) || !_valid(max_mag) || min_mag < min_supported_mag
		|| max_mag > max_supported_mag || min_mag > max_mag
		|| !_valid(zoom_multiple) || zoom_multiple <= EPS)
	{
		LPCSTR source = pSettings->DLTX_getFilenameOfLine(sect, source_key);
		PipMsg("[SVP-MAGS] %s key=%s value='%s' src=%s malformed, legacy retained",
			sect, source_key, raw ? raw : "", source ? source : "?");
		return true;
	}

	// 0.5x mirrors the 200 default min factor and 100x keeps the top factor positive
	const float f_top = svp_mag_base / max_mag / zoom_multiple;
	const float f_floor = svp_mag_base / min_mag;
	// dynamic detents need an ordered range and the top factor under the base fov (NewGetZoomData VERIFY)
	if (!_valid(f_top) || !_valid(f_floor) || (dyn && f_top >= SVP_ZOOM_BASE_FOV))
	{
		LPCSTR source = pSettings->DLTX_getFilenameOfLine(sect, source_key);
		PipMsg("[SVP-MAGS] %s key=%s value='%s' src=%s malformed, legacy retained",
			sect, source_key, raw ? raw : "", source ? source : "?");
		return true;
	}

	out.mode = dyn ? (stepped ? svp_mag_stepped : svp_mag_dynamic) : svp_mag_fixed;
	out.f_top = f_top;
	out.f_floor = f_floor;
	LPCSTR source = pSettings->DLTX_getFilenameOfLine(sect, source_key);
	PipMsg("[SVP-MAGS] %s key=%s value='%s' src=%s mag=[%.2f..%.2f] f=[%.1f..%.1f]",
		sect, source_key, raw ? raw : "", source ? source : "?", min_mag, max_mag, f_floor, f_top);
	return true;
}

// pip tries the passed section then the parent stripped scope section
// an unauthored wpn_x_1p59 with parent_section wpn_x resolves from 1p59
svp_mags_data svp_mags_resolve(LPCSTR sect, float zoom_multiple)
{
	svp_mags_data out;
	if (resolve_from_section(sect, zoom_multiple, out))
		return out;

	LPCSTR parent = READ_IF_EXISTS(pSettings, r_string, sect, "parent_section", NULL);
	if (parent && parent[0])
	{
		const u32 plen = xr_strlen(parent);
		if (xr_strlen(sect) > plen + 1 && 0 == strncmp(sect, parent, plen) && sect[plen] == '_')
		{
			LPCSTR suffix = sect + plen + 1;
			if (pSettings->section_exist(suffix))
				resolve_from_section(suffix, zoom_multiple, out);
		}
	}
	return out;
}
