#include "pch_script.h"
#include "svp_optic_config_script.h"
#include "../xrEngine/device.h"
#include "../xrEngine/svp_gameplay_cvars.h"
#include "../Layers/xrRender/svp_console.h"
#include "ai_space.h"
#include "script_engine.h"

#include <cmath>

namespace
{
constexpr int SVP_OPTIC_API_VERSION =
	static_cast<int>(CSecondVPParams::optic_api_version);
constexpr int SVP_OPTIC_SCHEMA_VERSION = 1;

struct SParseError
{
	string64 field = {};
	string128 reason = {};
};

class SLuaStackGuard
{
public:
	explicit SLuaStackGuard(lua_State* state) : m_state(state), m_top(lua_gettop(state))
	{
	}

	~SLuaStackGuard()
	{
		lua_settop(m_state, m_top);
	}

private:
	lua_State* m_state;
	int m_top;
};

struct SFloatField
{
	LPCSTR name;
	float CSecondVPParams::OpticConfig::*member;
	float minimum;
	float maximum;
};

constexpr LPCSTR s_top_keys[] =
{
	"schema_version",
	"context_token",
	"context",
	"weapon",
	"weapon_id",
	"scope",
	"diagnostic_scope",
	"identity_source",
	"zoom_type",
	"reticle_type",
	"hybrid_reflex",
	"profile",
	"spec",
	"model",
	"binding",
	"binding_section",
	"has_objective_offset",
	"objective_x",
	"objective_y",
	"objective_z",
	"objective_w",
	"objective_mm",
	"middle_grey",
	"adapt_speed",
	"zero_m",
	"tunneling_parallax",
	"tunneling_min",
	"tunneling_max",
	"tracking_speed",
	"tracking_accel_mm_s2",
	"tracking_limit_mm",
	"eye_relief_low_mm",
	"eye_relief_high_mm",
	"exit_pupil_low_mm",
	"exit_pupil_high_mm",
	"pupil_parity",
	"pupil_field_low",
	"pupil_field_high",
	"transmission",
	"twilight_strength",
	"physical_min",
	"physical_max",
	"sources"
};

constexpr LPCSTR s_source_keys[] =
{
	"objective_offset",
	"objective_mm",
	"middle_grey",
	"adapt_speed",
	"zero_m",
	"tunneling_parallax",
	"tunneling_min",
	"tunneling_max",
	"tracking_speed",
	"tracking_accel_mm_s2",
	"tracking_limit_mm",
	"eye_relief_low_mm",
	"eye_relief_high_mm",
	"exit_pupil_low_mm",
	"exit_pupil_high_mm",
	"pupil_parity",
	"pupil_field_low",
	"pupil_field_high",
	"transmission",
	"twilight_strength",
	"physical_min",
	"physical_max"
};

constexpr SFloatField s_float_fields[] =
{
	{ "objective_mm", &CSecondVPParams::OpticConfig::objective_mm, 0.f, 200.f },
	{ "middle_grey", &CSecondVPParams::OpticConfig::middle_grey, 0.f, 2.f },
	{ "adapt_speed", &CSecondVPParams::OpticConfig::adapt_speed, 0.f, 20.f },
	{ "zero_m", &CSecondVPParams::OpticConfig::zero_m, 0.f, 1000.f },
	{ "tunneling_parallax", &CSecondVPParams::OpticConfig::tunneling_parallax, 0.f, 0.15f },
	{ "tunneling_min", &CSecondVPParams::OpticConfig::tunneling_min, 0.f, 1.f },
	{ "tunneling_max", &CSecondVPParams::OpticConfig::tunneling_max, 0.f, 1.f },
	{ "tracking_speed", &CSecondVPParams::OpticConfig::tracking_speed, 0.1f, 30.f },
	{ "tracking_accel_mm_s2", &CSecondVPParams::OpticConfig::tracking_accel_mm_s2, 1.f, 500.f },
	{ "tracking_limit_mm", &CSecondVPParams::OpticConfig::tracking_limit_mm, 0.f, 20.f },
	{ "eye_relief_low_mm", &CSecondVPParams::OpticConfig::eye_relief_low_mm, 20.f, 150.f },
	{ "eye_relief_high_mm", &CSecondVPParams::OpticConfig::eye_relief_high_mm, 20.f, 150.f },
	{ "exit_pupil_low_mm", &CSecondVPParams::OpticConfig::exit_pupil_low_mm, 0.f, 100.f },
	{ "exit_pupil_high_mm", &CSecondVPParams::OpticConfig::exit_pupil_high_mm, 0.f, 100.f },
	{ "pupil_parity", &CSecondVPParams::OpticConfig::pupil_parity, -1.f, 1.f },
	{ "pupil_field_low", &CSecondVPParams::OpticConfig::pupil_field_low, 0.f, 6.f },
	{ "pupil_field_high", &CSecondVPParams::OpticConfig::pupil_field_high, 0.f, 6.f },
	{ "transmission", &CSecondVPParams::OpticConfig::transmission, 0.f, 1.f },
	{ "twilight_strength", &CSecondVPParams::OpticConfig::twilight_strength, 0.f, 1.f },
	{ "physical_min", &CSecondVPParams::OpticConfig::physical_min, 0.f, 200.f },
	{ "physical_max", &CSecondVPParams::OpticConfig::physical_max, 0.f, 200.f }
};

static_assert(_countof(s_source_keys) == CSecondVPParams::optic_value_count);

void svp_parse_error(SParseError& error, LPCSTR field, LPCSTR reason)
{
	xr_strcpy(error.field, sizeof(error.field), field ? field : "table");
	xr_strcpy(error.reason, sizeof(error.reason), reason ? reason : "invalid");
}

bool svp_key_allowed(LPCSTR key, size_t length, const LPCSTR* allowed, size_t count)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (xr_strlen(allowed[i]) == length && !memcmp(key, allowed[i], length))
			return true;
	}
	return false;
}

bool svp_validate_table(lua_State* state, int table_index, const LPCSTR* allowed,
	size_t allowed_count, SParseError& error)
{
	if (lua_getmetatable(state, table_index))
	{
		lua_pop(state, 1);
		svp_parse_error(error, "table", "metatable");
		return false;
	}

	lua_pushnil(state);
	while (lua_next(state, table_index) != 0)
	{
		if (lua_type(state, -2) != LUA_TSTRING)
		{
			svp_parse_error(error, "table", "non_string_key");
			return false;
		}
		size_t length = 0;
		LPCSTR key = lua_tolstring(state, -2, &length);
		if (!svp_key_allowed(key, length, allowed, allowed_count))
		{
			string64 field = {};
			const size_t copy_length = _min(length, sizeof(field) - 1);
			memcpy(field, key, copy_length);
			svp_parse_error(error, field, "unknown_field");
			return false;
		}
		lua_pop(state, 1);
	}
	return true;
}

int svp_raw_field(lua_State* state, int table_index, LPCSTR key)
{
	lua_pushstring(state, key);
	lua_rawget(state, table_index);
	return lua_gettop(state);
}

bool svp_read_integer(lua_State* state, int table_index, LPCSTR key, u32 minimum,
	u32 maximum, u32& value, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, key);
	if (lua_type(state, value_index) != LUA_TNUMBER)
	{
		svp_parse_error(error, key, "expected_integer");
		return false;
	}
	const lua_Number number = lua_tonumber(state, value_index);
	if (!std::isfinite(static_cast<double>(number)) || std::floor(number) != number ||
		number < static_cast<lua_Number>(minimum) || number > static_cast<lua_Number>(maximum))
	{
		svp_parse_error(error, key, "integer_range");
		return false;
	}
	value = static_cast<u32>(number);
	lua_pop(state, 1);
	return true;
}

bool svp_read_float(lua_State* state, int table_index, LPCSTR key, float minimum,
	float maximum, float& value, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, key);
	if (lua_type(state, value_index) != LUA_TNUMBER)
	{
		svp_parse_error(error, key, "expected_number");
		return false;
	}
	const lua_Number number = lua_tonumber(state, value_index);
	if (!std::isfinite(static_cast<double>(number)) ||
		number < static_cast<lua_Number>(minimum) || number > static_cast<lua_Number>(maximum))
	{
		svp_parse_error(error, key, "number_range");
		return false;
	}
	value = static_cast<float>(number);
	if (value == 0.f)
		value = 0.f;
	lua_pop(state, 1);
	return true;
}

bool svp_read_bool(lua_State* state, int table_index, LPCSTR key, bool& value, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, key);
	if (lua_type(state, value_index) != LUA_TBOOLEAN)
	{
		svp_parse_error(error, key, "expected_boolean");
		return false;
	}
	value = lua_toboolean(state, value_index) != 0;
	lua_pop(state, 1);
	return true;
}

bool svp_read_optional_bool(lua_State* state, int table_index, LPCSTR key, bool& value,
	bool& present, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, key);
	if (lua_isnil(state, value_index))
	{
		present = false;
		lua_pop(state, 1);
		return true;
	}
	if (lua_type(state, value_index) != LUA_TBOOLEAN)
	{
		svp_parse_error(error, key, "expected_boolean");
		return false;
	}
	present = true;
	value = lua_toboolean(state, value_index) != 0;
	lua_pop(state, 1);
	return true;
}

bool svp_read_string(lua_State* state, int table_index, LPCSTR key, LPSTR destination,
	size_t capacity, SParseError& error)
{
	const int value_index = svp_raw_field(state, table_index, key);
	if (lua_type(state, value_index) != LUA_TSTRING)
	{
		svp_parse_error(error, key, "expected_string");
		return false;
	}
	size_t length = 0;
	LPCSTR value = lua_tolstring(state, value_index, &length);
	if (length >= capacity || memchr(value, 0, length))
	{
		svp_parse_error(error, key, "string_length");
		return false;
	}
	memcpy(destination, value, length);
	destination[length] = 0;
	lua_pop(state, 1);
	return true;
}

bool svp_parse_optic_config(const luabind::object& table, u32 context_token,
	CSecondVPParams::OpticConfig& config, SParseError& error)
{
	if (!table.is_valid() || table.type() != LUA_TTABLE)
	{
		svp_parse_error(error, "table", "expected_table");
		return false;
	}

	lua_State* state = table.lua_state();
	SLuaStackGuard stack_guard(state);
	table.pushvalue();
	const int table_index = lua_gettop(state);
	if (!svp_validate_table(state, table_index, s_top_keys, _countof(s_top_keys), error))
		return false;

	u32 integer = 0;
	if (!svp_read_integer(state, table_index, "schema_version", SVP_OPTIC_SCHEMA_VERSION,
		SVP_OPTIC_SCHEMA_VERSION, integer, error))
		return false;
	if (!svp_read_integer(state, table_index, "context_token", 1, u32(-1), integer, error))
		return false;
	if (integer != context_token)
	{
		svp_parse_error(error, "context_token", "token_mismatch");
		return false;
	}
	if (!svp_read_integer(state, table_index, "weapon_id", 0, u16(-1), integer, error))
		return false;
	config.weapon_id = integer;
	if (!svp_read_integer(state, table_index, "zoom_type", 0, u8(-1), integer, error))
		return false;
	config.zoom_type = static_cast<u8>(integer);
	if (!svp_read_integer(state, table_index, "reticle_type", 0, u8(-1), integer, error))
		return false;
	config.reticle_type = static_cast<u8>(integer);
	if (!svp_read_optional_bool(state, table_index, "hybrid_reflex",
		config.hybrid_reflex, config.has_hybrid_reflex, error))
		return false;

	if (!svp_read_string(state, table_index, "context", config.context, sizeof(config.context), error) ||
		!svp_read_string(state, table_index, "weapon", config.weapon, sizeof(config.weapon), error) ||
		!svp_read_string(state, table_index, "scope", config.scope, sizeof(config.scope), error) ||
		!svp_read_string(state, table_index, "diagnostic_scope", config.diagnostic_scope,
			sizeof(config.diagnostic_scope), error) ||
		!svp_read_string(state, table_index, "identity_source", config.identity_source,
			sizeof(config.identity_source), error) ||
		!svp_read_string(state, table_index, "profile", config.profile, sizeof(config.profile), error) ||
		!svp_read_string(state, table_index, "spec", config.spec, sizeof(config.spec), error) ||
		!svp_read_string(state, table_index, "model", config.model, sizeof(config.model), error) ||
		!svp_read_string(state, table_index, "binding", config.binding, sizeof(config.binding), error) ||
		!svp_read_string(state, table_index, "binding_section", config.binding_section,
			sizeof(config.binding_section), error))
		return false;

	if (!config.context[0] || !config.weapon[0] || !config.identity_source[0])
	{
		svp_parse_error(error, "identity", "empty_identity");
		return false;
	}

	if (!svp_read_bool(state, table_index, "has_objective_offset",
		config.has_objective_offset, error) ||
		!svp_read_float(state, table_index, "objective_x", -8.f, 64.f,
			config.objective_offset.x, error) ||
		!svp_read_float(state, table_index, "objective_y", -8.f, 64.f,
			config.objective_offset.y, error) ||
		!svp_read_float(state, table_index, "objective_z", -8.f, 64.f,
			config.objective_offset.z, error) ||
		!svp_read_float(state, table_index, "objective_w", -8.f, 64.f,
			config.objective_offset.w, error))
		return false;

	if (config.has_objective_offset &&
		(config.objective_offset.z <= 0.f || config.objective_offset.w <= 0.f))
	{
		svp_parse_error(error, "objective_offset", "invalid_geometry");
		return false;
	}

	for (const SFloatField& field : s_float_fields)
	{
		if (!svp_read_float(state, table_index, field.name, field.minimum, field.maximum,
			config.*(field.member), error))
			return false;
	}
	if (config.physical_min > 0.f && config.physical_max > 0.f &&
		config.physical_min > config.physical_max)
	{
		svp_parse_error(error, "physical_min", "endpoint_order");
		return false;
	}

	const int source_index = svp_raw_field(state, table_index, "sources");
	if (lua_type(state, source_index) != LUA_TTABLE)
	{
		svp_parse_error(error, "sources", "expected_table");
		return false;
	}
	if (!svp_validate_table(state, source_index, s_source_keys, _countof(s_source_keys), error))
		return false;
	for (u32 i = 0; i < CSecondVPParams::optic_value_count; ++i)
	{
		if (!svp_read_string(state, source_index, s_source_keys[i], config.source[i],
			sizeof(config.source[i]), error))
			return false;
		if (!config.source[i][0])
		{
			svp_parse_error(error, s_source_keys[i], "empty_source");
			return false;
		}
	}

	return true;
}

bool svp_bounded_text(LPCSTR text, size_t capacity)
{
	return text && xr_strlen(text) < capacity;
}

luabind::object svp_profile_table(const CSecondVPParams::OpticConfig& config,
	LPCSTR route)
{
	lua_State* state = ai().script_engine().lua();
	luabind::object table = luabind::newtable(state);
	table["valid"] = config.valid;
	table["route"] = route;
	table["context"] = config.context;
	table["weapon"] = config.weapon;
	table["weapon_id"] = static_cast<int>(config.weapon_id);
	table["scope"] = config.scope;
	table["diagnostic_scope"] = config.diagnostic_scope;
	table["identity_source"] = config.identity_source;
	table["zoom_type"] = static_cast<int>(config.zoom_type);
	table["reticle_type"] = static_cast<int>(config.reticle_type);
	table["profile"] = config.profile;
	table["spec"] = config.spec;
	table["model"] = config.model;
	table["binding"] = config.binding;
	table["binding_section"] = config.binding_section;
	table["has_objective_offset"] = config.has_objective_offset;
	table["objective_x"] = config.objective_offset.x;
	table["objective_y"] = config.objective_offset.y;
	table["objective_z"] = config.objective_offset.z;
	table["objective_w"] = config.objective_offset.w;
	table["objective_mm"] = config.objective_mm;
	table["middle_grey"] = config.middle_grey;
	table["adapt_speed"] = config.adapt_speed;
	table["zero_m"] = config.zero_m;
	table["tunneling_parallax"] = config.tunneling_parallax;
	table["tunneling_min"] = config.tunneling_min;
	table["tunneling_max"] = config.tunneling_max;
	table["tracking_speed"] = config.tracking_speed;
	table["tracking_accel_mm_s2"] = config.tracking_accel_mm_s2;
	table["tracking_limit_mm"] = config.tracking_limit_mm;
	table["eye_relief_low_mm"] = config.eye_relief_low_mm;
	table["eye_relief_high_mm"] = config.eye_relief_high_mm;
	table["exit_pupil_low_mm"] = config.exit_pupil_low_mm;
	table["exit_pupil_high_mm"] = config.exit_pupil_high_mm;
	table["pupil_parity"] = config.pupil_parity;
	table["pupil_field_low"] = config.pupil_field_low;
	table["pupil_field_high"] = config.pupil_field_high;
	table["transmission"] = config.transmission;
	table["twilight_strength"] = config.twilight_strength;
	table["physical_min"] = config.physical_min;
	table["physical_max"] = config.physical_max;
	luabind::object sources = luabind::newtable(state);
	for (u32 i = 0; i < CSecondVPParams::optic_value_count; ++i)
		sources[s_source_keys[i]] = config.source[i];
	table["sources"] = sources;
	return table;
}
}

int svp_optic_api_version()
{
	return SVP_OPTIC_API_VERSION;
}

bool svp_optic_api_connect(int version)
{
	return version >= 0 &&
		Device.m_SecondViewport.ConnectOpticApi(static_cast<u32>(version));
}

bool svp_optic_api_has_capability(LPCSTR capability)
{
	return capability && (
		0 == xr_strcmp(capability, "hybrid_reflex") ||
		0 == xr_strcmp(capability, "profile_inspector"));
}

u32 svp_optic_route_epoch()
{
	return Device.m_SecondViewport.GetOpticRouteEpoch();
}

u32 svp_begin_optic_context(LPCSTR context, LPCSTR weapon, int weapon_id,
	LPCSTR scope, int zoom_type,
	LPCSTR identity_source, LPCSTR diagnostic_scope)
{
	if (!svp_optic_api_active() || scope_svp_enabled <= 0 ||
		weapon_id < 0 || weapon_id > u16(-1) ||
		zoom_type < 0 || zoom_type > u8(-1) ||
		!svp_bounded_text(context, sizeof(string256)) ||
		!svp_bounded_text(weapon, sizeof(string128)) ||
		!svp_bounded_text(scope, sizeof(string128)) ||
		!svp_bounded_text(identity_source, sizeof(string64)) ||
		!svp_bounded_text(diagnostic_scope, sizeof(string128)))
		return 0;

	const u32 token = Device.m_SecondViewport.BeginOpticContext(context, weapon,
		static_cast<u32>(weapon_id), scope, static_cast<u8>(zoom_type),
		identity_source, diagnostic_scope);
	Msg("[SVP-CONFIG] begin token=%u context=%s weapon_id=%d scope=%s diagnostic_scope=%s",
		token, context, weapon_id, scope, diagnostic_scope);
	return token;
}

bool svp_apply_optic_profile(u32 context_token, const luabind::object& table)
{
	if (!svp_optic_api_active() || scope_svp_enabled <= 0 || !context_token)
		return false;

	SParseError error;
	CSecondVPParams::OpticConfig config;
	try
	{
		if (!svp_parse_optic_config(table, context_token, config, error))
		{
			Device.m_SecondViewport.RejectOpticConfig(context_token);
			Msg("![SVP-CONFIG] reject token=%u field=%s reason=%s",
				context_token, error.field, error.reason);
			return false;
		}

		CSecondVPParams::OpticConfig before;
		Device.m_SecondViewport.ReadOpticConfig(before);
		if (!Device.m_SecondViewport.PublishOpticConfig(context_token, config))
		{
			Device.m_SecondViewport.RejectOpticConfig(context_token);
			Msg("![SVP-CONFIG] reject token=%u field=identity reason=stale_or_mismatch",
				context_token);
			return false;
		}
		CSecondVPParams::OpticConfig accepted;
		Device.m_SecondViewport.ReadOpticConfig(accepted);
		if (accepted.generation != before.generation)
		{
			Msg("[SVP-CONFIG] publish token=%u gen=%u context=%s reticle=%u hybrid=%d/%d profile=%s spec=%s",
				accepted.context_token, accepted.generation, accepted.context,
				accepted.reticle_type, accepted.has_hybrid_reflex,
				accepted.hybrid_reflex,
				accepted.profile, accepted.spec);
		}
		return true;
	}
	catch (const std::exception& exception)
	{
		Device.m_SecondViewport.RejectOpticConfig(context_token);
		Msg("![SVP-CONFIG] reject token=%u field=table reason=exception_%s",
			context_token, exception.what());
		return false;
	}
	catch (...)
	{
		Device.m_SecondViewport.RejectOpticConfig(context_token);
		Msg("![SVP-CONFIG] reject token=%u field=table reason=exception",
			context_token);
		return false;
	}
}

bool svp_clear_optic_profile(u32 context_token)
{
	return Device.m_SecondViewport.ClearOpticConfig(context_token);
}

luabind::object svp_current_optic_profile()
{
	CSecondVPParams::OpticConfig config;
	if (Device.m_SecondViewport.ReadOpticConfig(config))
		return svp_profile_table(config, "typed");
	return svp_profile_table(config, "none");
}
