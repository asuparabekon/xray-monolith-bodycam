#pragma once

class attachable_hud_item;
class player_hud;

namespace Bodycam
{
struct ArmPose;

class HudArms
{
public:
	explicit HudArms(player_hud& hud);
	~HudArms();

	HudArms(const HudArms&) = delete;
	HudArms& operator=(const HudArms&) = delete;

	void Reset();
	void UpdateAndApply(const ArmPose& pose, float dt);
	void OnWeaponChanged(const attachable_hud_item* item);
	void DumpAuthoredMotionProfile() const;

private:
	struct State;

	State* m_state;
};
} // namespace Bodycam
