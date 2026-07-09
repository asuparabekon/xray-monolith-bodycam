#include "stdafx.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "Actor.h"
#include "level.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"

namespace Bodycam
{
class CCC_BodycamPreset : public IConsole_Command
{
public:
	CCC_BodycamPreset(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
	virtual void Execute(LPCSTR args)
	{
		int preset = 2;
		if (args && xr_strlen(args))
			preset = atoi(args);
		ApplyPreset(preset);
	}
	virtual void Info(TInfo& I) { xr_sprintf(I, "0=off, 1=balanced, 2=stronger V3, 3=cinematic"); }
};

class CCC_BodycamDump : public IConsole_Command
{
public:
	CCC_BodycamDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
	virtual void Execute(LPCSTR args)
	{
		CActor* actor = smart_cast<CActor*>(Level().CurrentEntity());
		if (!actor)
		{
			Msg("! bodycam_dump: no active actor");
			return;
		}
		actor->cam_BodycamDumpState();
	}
	virtual void Info(TInfo& I) { xr_sprintf(I, "prints current bodycam camera/viewmodel state"); }
};

void RegisterConsoleCommands()
{
#ifndef XR_BODYCAM_DECOUPLE_DISABLED
	static bool registered = false;
	if (registered)
		return;
	registered = true;

	CMD1(CCC_BodycamPreset, "bodycam_preset");
	CMD1(CCC_BodycamDump, "bodycam_dump");

	static xr_vector<IConsole_Command*> commands;
	u32 count = 0;
	const FloatBinding* float_bindings = GetFloatBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const FloatBinding& binding = float_bindings[i];
		IConsole_Command* command = xr_new<CCC_Float>(binding.console_name, binding.value, binding.min_value, binding.max_value);
		commands.push_back(command);
		Console->AddCommand(command);
	}

	const BoolBinding* bool_bindings = GetBoolBindings(count);
	for (u32 i = 0; i < count; ++i)
	{
		const BoolBinding& binding = bool_bindings[i];
		IConsole_Command* command = xr_new<CCC_Integer>(binding.console_name, binding.value, 0, 1);
		commands.push_back(command);
		Console->AddCommand(command);
	}
#endif
}
} // namespace Bodycam
