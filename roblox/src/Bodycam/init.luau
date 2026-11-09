-- Bodycam: a first-person body-worn camera feel for Roblox.
--
--   The weapon points where the player aims; the view trails slightly behind it (look
--   lag). The head moves with the body (stride bob, strafe/turn roll, sprint lean,
--   landing dip). The weapon moves with the hands (walk lowering, holster, fire kick,
--   wrist roll, per-weapon profile). Starting and stopping take a moment (movement
--   inertia).
--
-- Pure modules (no Roblox API, testable anywhere):
--   Math, Settings, LookLag, HeadMotion, WeaponMotion, MovementInertia, StrideClock,
--   BodycamCore
-- Roblox:
--   Aim         aim angles <-> CFrames (CFrame/Vector3 only)
--   Controller  the adapter: camera, humanoid, viewmodel, hooks
--
-- Usage (LocalScript):
--   local Bodycam = require(ReplicatedStorage.Bodycam)
--   local controller = Bodycam.controller().new({ walkSpeed = 16, sprintSpeed = 25.6 })
--   controller:start()
--   -- shots: controller:getAimCFrame(), never the view

local Bodycam = {
	Math = require(script.Math),
	Settings = require(script.Settings),
	LookLag = require(script.LookLag),
	HeadMotion = require(script.HeadMotion),
	WeaponMotion = require(script.WeaponMotion),
	MovementInertia = require(script.MovementInertia),
	StrideClock = require(script.StrideClock),
	BodycamCore = require(script.BodycamCore),
	Aim = require(script.Aim),
}

-- The Roblox adapter, loaded on demand (it reads services).
function Bodycam.controller()
	return require(script.Controller)
end

-- The aim helper module.
function Bodycam.aim()
	return Bodycam.Aim
end

return Bodycam
