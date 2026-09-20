#include "pch_script.h"
#include "console_registrator.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "../xrEngine/device.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../xrSound/Sound.h"
#include "player_hud.h"
#include "svp_optic_config_script.h"

using namespace luabind;

CConsole* console()
{
	return Console;
}

int get_console_integer(CConsole* c, LPCSTR cmd)
{
	int min = 0, max = 0;
	int val = c->GetInteger(cmd, min, max);
	return val;
}

float get_console_float(CConsole* c, LPCSTR cmd)
{
	float min = 0.0f, max = 0.0f;
	float val = c->GetFloat(cmd, min, max);
	return val;
}

bool get_console_bool(CConsole* c, LPCSTR cmd)
{
	return c->GetBool(cmd);
}

void execute_console_command_deferred(CConsole* c, LPCSTR string_to_execute)
{
	Engine.Event.Defer("KERNEL:console", size_t(xr_strdup(string_to_execute)));
}

::luabind::object get_console_bounds(CConsole* c, LPCSTR cmd)
{
	IConsole_Command* command = c->GetCommand(cmd);
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	if (command)
	{
		CCC_Float* float_command = smart_cast<CCC_Float*>(command);
		if (float_command)
		{
			float min, max;
			float_command->GetBounds(min, max);
			table["min"] = min;
			table["max"] = max;
			return table;
		}

		CCC_Integer* integer_command = smart_cast<CCC_Integer*>(command);
		if (integer_command) {
			int min, max;
			integer_command->GetBounds(min, max);
			table["min"] = min;
			table["max"] = max;
			return table;
		}
	}
	return table;
}

::luabind::object get_console_token_list(CConsole* c, LPCSTR cmd)
{
	::luabind::object table = ::luabind::newtable(ai().script_engine().lua());
	xr_token* tok = c->GetXRToken(cmd);
	if (tok)
	{
		int idx = 1;
		while (tok->name)
		{
			table[idx] = tok->name;
			++tok;
			++idx;
		}
	}
	return table;
}

// pip true if a real PiP scope is rendering via the second viewport (reflex/iron/non-PiP sights never set it)
bool is_svp_active()
{
	return Device.m_SecondViewport.IsSVPActive();
}

// pip measured lens optics fills missing authored geometry
extern int ps_r__svp_measured_optics;

// run the mesh detection on one hud model, true when a lens with an objective is found
static bool svp_detect_hud(attachable_hud_item* h, SLensDetection& d)
{
	return h && h->m_model && h->m_model->GetLensDetection(d) && d.ok && d.has_objective;
}

// the active scope hud, the attached scope model carries an addon lens, the weapon model an integral one
static bool svp_detect_active(SLensDetection& d)
{
	if (!ps_r__svp_measured_optics || !g_player_hud)
		return false;
	if (svp_detect_hud(g_player_hud->attached_item(SCOPE_ATTACH_IDX), d))
		return true;
	return svp_detect_hud(g_player_hud->attached_item(0), d);
}

// scope_objective_lens_offset x,y,z,w in eyepiece-radius units, "" when off or nothing fits
LPCSTR svp_detected_offset()
{
	static string128 s;
	SLensDetection d;
	if (!svp_detect_active(d))
		return "";
	xr_sprintf(s, "%f,%f,%f,%f", d.offset.x, d.offset.y, d.offset.z, d.offset.w);
	return s;
}

// s3ds_objective_mm 2000 x obj_radius, -1 when off or nothing fits
float svp_detected_obj_mm()
{
	SLensDetection d;
	if (!svp_detect_active(d))
		return -1.f;
	return d.mm > 0.f ? d.mm : -1.f;
}

// Current PiP scope magnification (1.0 = 1x, up to ~8x), -1 when not looking
// through a magnifying PiP scope. svp_mag is the engine's own "zoom scaled
// trigger" value (svp_state.h), set from g_pip_scope_magnification.
float svp_scope_magnification()
{
	// svp_mag is set while the SVP optics pass runs but is NOT reset on unscope, so it
	// holds the last scope's value at hipfire. Gate on the scope being ACTIVE so we
	// report -1 (not scoped) once the PiP viewport is down -- fixes the "stuck mag" bug.
	if (!Device.m_SecondViewport.IsSVPActive())
		return -1.f;
	float m = Device.m_SecondViewport.svp_mag;
	return m > 0.01f ? m : -1.f;
}

// Bodycam: in-scope identification markers. Lua builds a list of
// WORLD-space markers each frame; CRenderTarget::phase_svp_markers (xrRenderPC_R4)
// projects them through the SVP camera and draws them inside the scope. Same
// begin/add/commit staging as the bodycam redaction batch so the render thread only
// ever sees a complete list. Inert (no draw) when the list is empty. 12 floats
// per marker -- see g_bodycam_svp_markers in xr_ioc_cmd.cpp.
#define BODYCAM_SVP_MARKER_MAX 16

extern ENGINE_API float g_bodycam_svp_markers[BODYCAM_SVP_MARKER_MAX * 12];
extern ENGINE_API int   g_bodycam_svp_marker_count;
extern ENGINE_API int   g_bodycam_svp_marker_debug;

static int   s_svp_marker_pending = 0;
static float s_svp_marker_pending_buf[BODYCAM_SVP_MARKER_MAX * 12];

static void svp_ui_markers_begin()
{
	s_svp_marker_pending = 0;
}

// Args mirror the mod's existing call site (12): world x,y,z; fill r,g,b (0..1);
// alpha; radius (fraction of the scope viewport); ring r,g,b (0..1); scanning.
static void svp_ui_markers_add(float wx, float wy, float wz,
	float fr, float fg, float fb, float a, float radius,
	float rr, float rg, float rb, bool scanning)
{
	if (s_svp_marker_pending >= BODYCAM_SVP_MARKER_MAX) return;
	if (a <= 0.f || radius <= 0.f) return;   // nothing to draw
	float* m = &s_svp_marker_pending_buf[s_svp_marker_pending * 12];
	m[0] = wx; m[1] = wy; m[2] = wz;
	m[3] = fr; m[4] = fg; m[5] = fb; m[6] = a;
	m[7] = radius;
	m[8] = rr; m[9] = rg; m[10] = rb;
	m[11] = scanning ? 1.f : 0.f;
	++s_svp_marker_pending;
}

static void svp_ui_markers_commit()
{
	for (int i = 0; i < s_svp_marker_pending * 12; ++i)
		g_bodycam_svp_markers[i] = s_svp_marker_pending_buf[i];
	g_bodycam_svp_marker_count = s_svp_marker_pending;
	if (g_bodycam_svp_marker_debug && s_svp_marker_pending > 0)
		Msg("[bodycam] svp_ui_markers commit count=%d", s_svp_marker_pending);
}

#pragma optimize("s",on)
void console_registrator::script_register(lua_State* L)
{
	module(L)
	[
		def("get_console", &console),
		def("is_svp_active", &is_svp_active),
		def("svp_ui_markers_begin", &svp_ui_markers_begin),
		def("svp_ui_markers_add", &svp_ui_markers_add),
		def("svp_ui_markers_commit", &svp_ui_markers_commit),
		def("svp_detected_offset", &svp_detected_offset),
		def("svp_detected_obj_mm", &svp_detected_obj_mm),
		def("svp_scope_magnification", &svp_scope_magnification),
		def("svp_optic_api_version", &svp_optic_api_version),
		def("svp_optic_api_info", &svp_optic_api_info),
		def("svp_optic_api_has_capability", &svp_optic_api_has_capability),
		def("svp_optic_api_connect", &svp_optic_api_connect),
		def("svp_optic_api_describe", &svp_optic_api_describe),
		def("svp_validate_optic_fields", &svp_validate_optic_fields),
		def("svp_validate_optic_profile", &svp_validate_optic_profile),
		def("svp_optic_route_epoch", &svp_optic_route_epoch),
		def("svp_begin_optic_context", &svp_begin_optic_context),
		def("svp_apply_optic_profile", &svp_apply_optic_profile),
		def("svp_clear_optic_profile", &svp_clear_optic_profile),
		def("svp_current_optic_profile", &svp_current_optic_profile),

		class_<CConsole>("CConsole")
		.def("execute", &CConsole::Execute)
		.def("execute_script", &CConsole::ExecuteScript)
		.def("show", &CConsole::Show)
		.def("hide", &CConsole::Hide)

		.def("get_string", &CConsole::GetString)
		.def("get_integer", &get_console_integer)
		.def("get_variable_bounds", &get_console_bounds)
		.def("get_bool", &get_console_bool)
		.def("get_float", &get_console_float)
		.def("get_token", &CConsole::GetToken)
		.def("get_token_list", &get_console_token_list)
		.def("execute_deferred", &execute_console_command_deferred)
	];
}
