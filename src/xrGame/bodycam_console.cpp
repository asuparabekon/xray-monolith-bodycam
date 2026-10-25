#include "stdafx.h"
#include "bodycam_camera.h"
#include "bodycam_settings.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"

#include <algorithm>
#include <cmath>

namespace Bodycam
{
class CCC_BodycamPreset : public IConsole_Command
{
public:
	explicit CCC_BodycamPreset(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
	void Execute(LPCSTR args) override { ApplyPreset(args && xr_strlen(args) ? atoi(args) : kDefaultPreset); }
	void Info(TInfo& I) override { xr_sprintf(I, "0=off, 1=balanced, 2=default"); }
};

class CCC_BodycamDump : public IConsole_Command
{
public:
	explicit CCC_BodycamDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
	void Execute(LPCSTR args) override { DumpState(); }
	void Info(TInfo& I) override { xr_sprintf(I, "prints current bodycam camera/viewmodel state"); }
};

// 0 Classic, 1 Reference: requests the mode, which the actor takes at its next frame boundary.
class CCC_BodycamMotionMode : public CCC_Integer
{
public:
	explicit CCC_BodycamMotionMode(LPCSTR N) : CCC_Integer(N, MotionModeConsoleValue(), 0, 1) {}
	void Execute(LPCSTR args) override
	{
		if (!RequestMotionMode(atoi(args)))
			InvalidSyntax();
	}
	void Info(TInfo& I) override { xr_sprintf(I, "0=Classic, 1=Reference"); }
};

// A 0/1 console command over a bool setting. It is an integer command, so
// get_console():get_bool() and get_integer() read it, and it writes the bool.
class CCC_BodycamBool : public CCC_Integer
{
public:
	CCC_BodycamBool(BoolBinding& binding) : CCC_Integer(binding.console_name, &binding.console_value, 0, 1), m_target(binding.value) {}
	void Execute(LPCSTR args) override
	{
		CCC_Integer::Execute(args);
		*m_target = *value != 0;
	}

private:
	bool* m_target;
};

// A float setting: rejects non-finite values and clamps like SetFloat.
class CCC_BodycamFloat : public CCC_Float
{
public:
	CCC_BodycamFloat(const FloatBinding& binding) : CCC_Float(binding.console_name, binding.value, binding.min_value, binding.max_value) {}
	void Execute(LPCSTR args) override
	{
		const float v = float(atof(args));
		if (!std::isfinite(v) || v < min - EPS || v > max + EPS)
			InvalidSyntax();
		else
			*value = std::clamp(v, min, max);
	}
};

// r__reticle_inspect: the GAMMA patch's Reticle Editor draws the HUD this many times closer so the reticle can be
// seen while it is adjusted. Never saved, so a restart is never zoomed.
float g_reticle_inspect_zoom = 1.f;

class CCC_ReticleInspect : public CCC_Float
{
public:
	explicit CCC_ReticleInspect(LPCSTR N) : CCC_Float(N, &g_reticle_inspect_zoom, 1.f, 8.f) {}
	void Save(IWriter*) override {}
	void Info(TInfo& I) override { xr_sprintf(I, "Reticle Editor inspect zoom, 1 = off (not saved)"); }
};

float ReticleInspectZoom()
{
	return g_reticle_inspect_zoom;
}

void RegisterConsoleCommands()
{
	CMD1(CCC_BodycamPreset, "bodycam_preset");
	CMD1(CCC_ReticleInspect, "r__reticle_inspect");
	CMD1(CCC_BodycamDump, "bodycam_dump");
	CMD1(CCC_BodycamMotionMode, "bodycam_motion_mode");

	u32 count = 0;
	const FloatBinding* float_bindings = GetFloatBindings(count);
	for (u32 i = 0; i < count; ++i)
		Console->AddCommand(xr_new<CCC_BodycamFloat>(float_bindings[i]));

	BoolBinding* bool_bindings = GetBoolBindings(count);
	for (u32 i = 0; i < count; ++i)
		Console->AddCommand(xr_new<CCC_BodycamBool>(bool_bindings[i]));
}
} // namespace Bodycam
