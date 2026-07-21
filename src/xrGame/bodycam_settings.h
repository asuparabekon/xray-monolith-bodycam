#pragma once

#include "bodycam_simulation.h"

namespace Bodycam
{
struct RuntimeFeatureSettings
{
	BOOL camera_enable = TRUE;
	BOOL vm_enable = FALSE;
	BOOL lower_enable = TRUE;
	BOOL bodycam_arm_enable = FALSE;
	BOOL stalker2_arm_enable = TRUE;
	BOOL impulse_debug = FALSE;
	BOOL lower_disable_in_combat = TRUE;
	float layer_vm_weight = 1.f;
	float layer_lower_weight = 1.f;
	float layer_arm_weight = 1.f;
};

struct MovementResponseSettings
{
	BOOL enable = TRUE;
	BOOL ads_disable = TRUE;
	float accel_time = 0.28f;
	float decel_time = 0.38f;
	float turn_response = 0.55f;
	float stop_response = 0.45f;
	float sprint_mult = 1.f;
};

struct RuntimeConfig
{
	RuntimeFeatureSettings features;
	MovementResponseSettings movement;
	SimulationCameraSettings camera;
	SimulationViewmodelSettings viewmodel;
	SimulationImpulseSettings impulse;
	SimulationSprintSettings sprint;
	SimulationLoweringSettings lowering;
	SimulationArmSettings bodycam_arm;
	SimulationStalker2ArmSettings stalker2_arm;
};

struct FloatBinding
{
	LPCSTR name;
	LPCSTR console_name;
	float* value;
	float min_value;
	float max_value;
};

struct BoolBinding
{
	LPCSTR name;
	LPCSTR console_name;
	BOOL* value;
};

RuntimeConfig& GetConfig();
const FloatBinding* GetFloatBindings(u32& count);
const BoolBinding* GetBoolBindings(u32& count);
void DumpConfigBindings();
bool CameraEnabled();
bool HudEffectsEnabled();
bool AnyEffectEnabled();
void ApplyPreset(int preset);
bool GetFloat(LPCSTR name, float& value);
bool SetFloat(LPCSTR name, float value);
bool GetBool(LPCSTR name, bool& value);
bool SetBool(LPCSTR name, bool value);
void SetLayerWeight(LPCSTR layer, float weight);
float GetLayerWeight(LPCSTR layer);
SimulationSettings GetSimulationSettings();
} // namespace Bodycam
