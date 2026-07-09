#pragma once

class CActor;
class CCameraBase;
struct lua_State;

#include "bodycam_simulation.h"

namespace Bodycam
{
struct UpdateInput
{
	float target_yaw = 0.f;
	float target_pitch = 0.f;
	float dt = 0.f;
	u32 mstate = 0;
	bool ads = false;
	float ads_blend = 0.f;
	bool weapon_lowered = false;
	bool combat = false;
	bool firearm_equipped = true;
	float actor_speed_fraction = 0.f;
};

struct VisualOutput
{
	float yaw = 0.f;
	float pitch = 0.f;
	float roll = 0.f;
	Fvector pos = { 0.f, 0.f, 0.f };
	float fov_offset = 0.f;
};

struct DebugSnapshot
{
	bool active = false;
	bool camera_enabled = false;
	bool vm_enabled = false;
	bool lower_enabled = false;
	bool ads = false;
	u32 mstate = 0;
	float camera_yaw = 0.f;
	float camera_pitch = 0.f;
	float camera_roll = 0.f;
	Fvector camera_pos = { 0.f, 0.f, 0.f };
	Fvector vm_pos = { 0.f, 0.f, 0.f };
	Fvector vm_rot = { 0.f, 0.f, 0.f };
	float movement_target_speed = 0.f;
	float movement_actual_speed = 0.f;
	float movement_speed_fraction = 0.f;
	float lower_target = 0.f;
	float lower_amount = 0.f;
	float lower_holster = 0.f;
};

class CBodycam
{
public:
	bool CameraEnabled() const;
	bool HudEnabled() const;
	void Reset(const CCameraBase* camera, u32 mstate, float ads_blend);
	void Update(const UpdateInput& input, VisualOutput& output);
	void AddFireImpulse(float power, bool ads);
	void AddImpulse(LPCSTR kind, float power, bool ads);
	void Dump(bool ads, u32 mstate) const;
	bool GetHudOffset(Fvector& pos, Fvector& rot) const;
	void SetMovementDebug(float target_speed, float actual_speed, float speed_fraction);
	void CaptureDebugSnapshot(bool ads, float ads_blend, u32 mstate) const;

private:
	SimulationState m_state;
	BOOL m_viewmodel_active = FALSE;
	Fvector m_viewmodel_pos = { 0.f, 0.f, 0.f };
	Fvector m_viewmodel_rot = { 0.f, 0.f, 0.f };
	float m_movement_target_speed = 0.f;
	float m_movement_actual_speed = 0.f;
	float m_movement_speed_fraction = 0.f;

	void ResetHudOutput();
};

bool CameraEnabled();
bool HudSpringEnabled();
void ResetHudOutput();
bool GetDebugSnapshot(DebugSnapshot& snapshot);
void BuildBasis(float yaw, float pitch, float roll, Fvector& dir, Fvector& up, Fvector& right);
void ApplyPreset(int preset);
bool GetFloat(LPCSTR name, float& value);
bool SetFloat(LPCSTR name, float value);
bool GetBool(LPCSTR name, bool& value);
bool SetBool(LPCSTR name, bool value);
void SetLayerWeight(LPCSTR layer, float weight);
float GetLayerWeight(LPCSTR layer);
void RegisterConsoleCommands();
void script_register(lua_State* L);
} // namespace Bodycam
