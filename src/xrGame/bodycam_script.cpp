#include "pch_script.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "level.h"
#include "ai_space.h"
#include "script_engine.h"

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
		def("dump", &bodycam_dump),
		def("get_state", &bodycam_get_state),
		def("get_bindings", &bodycam_get_bindings)
	];
}
