#include "pch_script.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "level.h"
#include "ai_space.h"
#include "script_engine.h"
#include "player_hud.h"
#include "HudItem.h"
#include "../xrEngine/CameraBase.h"

using namespace luabind;

static bool bodycam_set_float(LPCSTR name, float value)
{
	return Bodycam::SetFloat(name, value);
}

static float bodycam_get_float(LPCSTR name, float fallback)
{
	float value = fallback;
	Bodycam::GetFloat(name, value);
	return value;
}

static bool bodycam_set_bool(LPCSTR name, bool value)
{
	return Bodycam::SetBool(name, value);
}

static bool bodycam_get_bool(LPCSTR name, bool fallback)
{
	bool value = fallback;
	Bodycam::GetBool(name, value);
	return value;
}

static void bodycam_set_layer_weight(LPCSTR layer, float weight)
{
	Bodycam::SetLayerWeight(layer, weight);
}

static float bodycam_get_layer_weight(LPCSTR layer)
{
	return Bodycam::GetLayerWeight(layer);
}

static void bodycam_apply_preset(int preset)
{
	Bodycam::ApplyPreset(preset);
}

static void bodycam_dump()
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
	if (actor)
		actor->cam_BodycamDumpState();
	else
		Msg("! bodycam.dump: no active actor");
}

static void bodycam_add_impulse(LPCSTR kind, float power)
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
	if (actor)
		actor->cam_BodycamAddImpulse(kind, power);
}

static void bodycam_set_viewmodel_profile(float pos_x, float pos_y, float pos_z,
	float pitch, float yaw, float roll, float blend_speed)
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
	if (!actor)
		return;

	Fvector pos;
	pos.set(pos_x, pos_y, pos_z);
	Fvector rot;
	rot.set(yaw, pitch, roll);
	actor->cam_BodycamSetViewmodelProfile(pos, rot, blend_speed);
}

static void bodycam_clear_viewmodel_profile(float blend_speed)
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
	if (actor)
		actor->cam_BodycamClearViewmodelProfile(blend_speed);
}

static object bodycam_get_bindings()
{
	lua_State* L = ai().script_engine().lua();
	object bindings = newtable(L);
	int index = 1;

	u32 count = 0;
	const Bodycam::FloatBinding* floats = Bodycam::GetFloatBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const Bodycam::FloatBinding& binding = floats[i];
		object entry = newtable(L);
		entry["name"] = binding.name;
		entry["console_name"] = binding.console_name;
		entry["type"] = "float";
		entry["default"] = binding.default_value;
		entry["min"] = binding.min_value;
		entry["max"] = binding.max_value;
		bindings[index++] = entry;
	}

	const Bodycam::BoolBinding* bools = Bodycam::GetBoolBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const Bodycam::BoolBinding& binding = bools[i];
		object entry = newtable(L);
		entry["name"] = binding.name;
		entry["console_name"] = binding.console_name;
		entry["type"] = "bool";
		entry["default"] = !!binding.default_value;
		bindings[index++] = entry;
	}

	return bindings;
}

static object bodycam_get_state()
{
	lua_State* L = ai().script_engine().lua();
	object table = newtable(L);

	Bodycam::DebugSnapshot snapshot;
	if (!Bodycam::GetDebugSnapshot(snapshot))
	{
		table["active"] = false;
		return table;
	}

	table["active"] = snapshot.active;
	table["camera_enabled"] = snapshot.camera_enabled;
	table["vm_enabled"] = snapshot.vm_enabled;
	table["lower_enabled"] = snapshot.lower_enabled;
	table["ads"] = snapshot.ads;
	table["mstate"] = snapshot.mstate;
	table["camera_yaw"] = snapshot.camera_yaw;
	table["camera_pitch"] = snapshot.camera_pitch;
	table["camera_roll"] = snapshot.camera_roll;
	table["camera_pos_x"] = snapshot.camera_pos.x;
	table["camera_pos_y"] = snapshot.camera_pos.y;
	table["camera_pos_z"] = snapshot.camera_pos.z;
	table["vm_pos_x"] = snapshot.vm_pos.x;
	table["vm_pos_y"] = snapshot.vm_pos.y;
	table["vm_pos_z"] = snapshot.vm_pos.z;
	table["vm_rot_x"] = snapshot.vm_rot.x;
	table["vm_rot_y"] = snapshot.vm_rot.y;
	table["vm_rot_z"] = snapshot.vm_rot.z;
	table["lower_target"] = snapshot.lower_target;
	table["lower_amount"] = snapshot.lower_amount;
	table["lower_holster"] = snapshot.lower_holster;
	return table;
}

// Free-aim aim ray for scripts (e.g. the Immersive Identification mod's target
// acquisition). Returns two rays in world space:
//   { valid=bool,
//     pos_x/y/z, dir_x/y/z,           -- PRIMARY: gameplay aim (first-eye cam)
//     bar_pos_x/y/z, bar_dir_x/y/z }  -- diagnostic: cosmetic weapon barrel
// (all dirs unit length).
//
// The PRIMARY ray is the actor's real first-eye camera. Under bodycam the RENDER
// camera (device().cam_dir, used by world2ui and the screen-centre pick) is a
// swayed OVERRIDE of this, so only the first-eye camera reflects where the
// player is actually AIMING -- HudItem::Ray() itself remaps the pick through
// cam_FirstEye() for the same reason. The barrel ray (main-hand weapon's
// per-frame pick, PP.defs) is the swaying weapon-model direction, NOT the aim;
// it is exposed only for diagnostics. Pure reads, no side effects. valid=false
// when there is no controlled actor.
static object bodycam_get_fire_ray()
{
	lua_State* L = ai().script_engine().lua();
	object t = newtable(L);
	t["valid"] = false;

	CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
	if (!actor)
		return t;

	CCameraBase* eye = actor->cam_FirstEye();
	if (!eye)
		return t;

	Fvector eye_dir = eye->vDirection;
	eye_dir.normalize_safe();

	t["valid"] = true;
	t["pos_x"] = eye->vPosition.x;
	t["pos_y"] = eye->vPosition.y;
	t["pos_z"] = eye->vPosition.z;
	t["dir_x"] = eye_dir.x;
	t["dir_y"] = eye_dir.y;
	t["dir_z"] = eye_dir.z;

	// Diagnostic: cosmetic weapon barrel pick (swaying model direction).
	if (g_player_hud)
	{
		attachable_hud_item* item = g_player_hud->attached_item(0);
		if (!item)
			item = g_player_hud->attached_item(1);
		if (item && item->m_parent_hud_item)
		{
			const SPickParam& pp = item->m_parent_hud_item->GetPick();
			Fvector bar_dir = pp.defs.dir;
			bar_dir.normalize_safe();
			t["bar_pos_x"] = pp.defs.start.x;
			t["bar_pos_y"] = pp.defs.start.y;
			t["bar_pos_z"] = pp.defs.start.z;
			t["bar_dir_x"] = bar_dir.x;
			t["bar_dir_y"] = bar_dir.y;
			t["bar_dir_z"] = bar_dir.z;
		}
	}
	return t;
}

// Immersive Identification: drive the dead-body glitch post-process. Builds a
// LIST of head rects (one per visible corpse) into the shared xrEngine globals
// that CRenderTarget::phase_glitch() (xrRenderPC_R4) reads each frame. Rects are
// normalised [0,1] screen space (top-left origin). Usage from Lua each frame:
//   bodycam.glitch_begin()
//   bodycam.glitch_add(x0,y0,x1,y1)   -- once per visible corpse head box
//   bodycam.glitch_commit(intensity)  -- publishes the list (0 intensity clears)
// The begin/commit split makes the render thread see a complete list, never a
// half-filled one. Same guarded, inert-without-this-exe pattern as get_fire_ray.
#define II_GLITCH_MAX 16   // keep in sync with xr_ioc_cmd.cpp and ii_glitch.ps

extern ENGINE_API float g_ii_glitch_rects[II_GLITCH_MAX * 4];
extern ENGINE_API int   g_ii_glitch_count;
extern ENGINE_API float g_ii_glitch_intensity;
extern ENGINE_API int   g_ii_glitch_mode;
extern ENGINE_API int   g_ii_glitch_active;
extern ENGINE_API int   g_ii_glitch_debug;

// Effect variant applied inside every head box: 0 glitch, 1 pixelate, 2 black.
// Separate from commit so older mod builds that don't call it just leave mode 0
// (glitch) -- graceful degradation.
static void bodycam_glitch_set_mode(int mode)
{
	g_ii_glitch_mode = mode;
}

extern ENGINE_API float g_ii_glitch_depths[II_GLITCH_MAX];

static int   s_ii_glitch_pending = 0;
static float s_ii_glitch_pending_rects[II_GLITCH_MAX * 4];
static float s_ii_glitch_pending_depths[II_GLITCH_MAX];

static void bodycam_glitch_begin()
{
	s_ii_glitch_pending = 0;
}

// depth = the box's view-space depth in metres (distance along the view axis).
// The renderer skips any pixel whose scene depth is nearer than this, so the
// effect never paints over the viewmodel / foreground geometry. depth <= 0 means
// "no depth test" (the back-compat single-rect wrapper passes 0).
static void bodycam_glitch_add(float x0, float y0, float x1, float y1, float depth)
{
	if (s_ii_glitch_pending >= II_GLITCH_MAX) return;
	if (!(x1 > x0 && y1 > y0)) return;   // skip degenerate rects
	float* r = &s_ii_glitch_pending_rects[s_ii_glitch_pending * 4];
	r[0] = x0; r[1] = y0; r[2] = x1; r[3] = y1;
	s_ii_glitch_pending_depths[s_ii_glitch_pending] = depth;
	++s_ii_glitch_pending;
}

static void bodycam_glitch_commit(float intensity)
{
	const int was_active = g_ii_glitch_active;
	for (int i = 0; i < s_ii_glitch_pending * 4; ++i)
		g_ii_glitch_rects[i] = s_ii_glitch_pending_rects[i];
	for (int i = 0; i < s_ii_glitch_pending; ++i)
		g_ii_glitch_depths[i] = s_ii_glitch_pending_depths[i];
	g_ii_glitch_count = s_ii_glitch_pending;
	g_ii_glitch_intensity = intensity;
	g_ii_glitch_active = (s_ii_glitch_pending > 0 && intensity > 0.f) ? 1 : 0;
	if (g_ii_glitch_debug && g_ii_glitch_active && !was_active)
		Msg("[ii] bodycam.glitch ACTIVE count=%d intensity=%.2f", g_ii_glitch_count, intensity);
}

// Back-compat single-rect wrappers (older mod builds call these). depth 0 = no test.
static void bodycam_set_glitch_rect(float x0, float y0, float x1, float y1, float intensity)
{
	bodycam_glitch_begin();
	bodycam_glitch_add(x0, y0, x1, y1, 0.f);
	bodycam_glitch_commit(intensity);
}

static void bodycam_clear_glitch()
{
	bodycam_glitch_begin();
	bodycam_glitch_commit(0.f);
}

#pragma optimize("s", on)
void Bodycam::script_register(lua_State* L)
{
	module(L, "bodycam")
	[
		def("get_float", &bodycam_get_float),
		def("set_float", &bodycam_set_float),
		def("get_bool", &bodycam_get_bool),
		def("set_bool", &bodycam_set_bool),
		def("get_layer_weight", &bodycam_get_layer_weight),
		def("set_layer_weight", &bodycam_set_layer_weight),
		def("apply_preset", &bodycam_apply_preset),
		def("add_impulse", &bodycam_add_impulse),
		def("set_viewmodel_profile", &bodycam_set_viewmodel_profile),
		def("clear_viewmodel_profile", &bodycam_clear_viewmodel_profile),
		def("dump", &bodycam_dump),
		def("get_state", &bodycam_get_state),
		def("get_fire_ray", &bodycam_get_fire_ray),
		def("get_bindings", &bodycam_get_bindings),
		def("set_glitch_rect", &bodycam_set_glitch_rect),
		def("clear_glitch", &bodycam_clear_glitch),
		def("glitch_begin", &bodycam_glitch_begin),
		def("glitch_add", &bodycam_glitch_add),
		def("glitch_commit", &bodycam_glitch_commit),
		def("glitch_set_mode", &bodycam_glitch_set_mode)
	];
}
