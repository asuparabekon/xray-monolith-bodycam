-- FPSController: movement states and weapon handling for the bodycam port.
--
-- Pure logic (no Roblox API, testable anywhere):
--   MovementStateMachine  gaits, poses, slide, jump rules, eased speed, camera drop
--   Weapon                fire rate, fire modes, bursts, magazine, timed actions, scopes
--   RecoilPattern         per-shot camera recoil with build-up and recovery
-- Roblox adapters:
--   MovementController    applies the state machine to the local Humanoid
--   WeaponController      fires, recoils and aims through BodycamController and FPSys
--
-- Written for this project. The feature set follows the Kinemation FPS Animation
-- Framework demos; their code is unlicensed and depends on the paid framework, so
-- none of it is used here.

local FPSController = {
	MovementStateMachine = require(script.MovementStateMachine),
	Weapon = require(script.Weapon),
	RecoilPattern = require(script.RecoilPattern),
}

function FPSController.movement()
	return require(script.MovementController)
end

function FPSController.weapons()
	return require(script.WeaponController)
end

return FPSController
