#pragma once

namespace luabind
{
	class object;
}

int svp_optic_api_version();
bool svp_optic_api_connect(int version);
bool svp_optic_api_has_capability(LPCSTR capability);
u32 svp_optic_route_epoch();
u32 svp_begin_optic_context(LPCSTR context, LPCSTR weapon, int weapon_id,
	LPCSTR scope, int zoom_type,
	LPCSTR identity_source, LPCSTR diagnostic_scope);
bool svp_apply_optic_profile(u32 context_token, const luabind::object& table);
bool svp_clear_optic_profile(u32 context_token);
luabind::object svp_current_optic_profile();
