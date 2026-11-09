-- Bodycam for Roblox: a Luau port of the X-Ray Monolith Bodycam Classic layer
-- (camera decoupling, footstep camera, head roll and shake, sprint sway,
-- viewmodel lowering, fire impulse, STALKER 2 style arm IK, movement inertia).
--
-- Core/ and Reference/ are engine-free and run in any Luau VM.
-- Roblox/ holds the adapter that drives them from a Humanoid and the CurrentCamera.

local Core = script.Core
local Reference = script.Reference

local Bodycam = {
	MathUtil = require(Core.MathUtil),
	Vec3 = require(Core.Vec3),
	Simulation = require(Core.Simulation),
	CameraRig = require(Core.CameraRig),
	MouseAim = require(Core.MouseAim),
	MovementInertia = require(Core.MovementInertia),
	ViewmodelProfile = require(Core.ViewmodelProfile),
	Settings = require(Core.Settings),
	BodycamCore = require(Core.BodycamCore),
	GaitSpeed = require(Core.GaitSpeed),
	Reference = {
		ScalarMath = require(Reference.ScalarMath),
		CameraMotion = require(Reference.CameraMotion),
		ClassicFollow = require(Reference.ClassicFollow),
		FootstepCamera = require(Reference.FootstepCamera),
		CameraShake = require(Reference.CameraShake),
		ReferenceHead = require(Reference.ReferenceHead),
		CameraDelta = require(Reference.CameraDelta),
	},
}

Bodycam.MoveFlags = Bodycam.Simulation.MoveFlags

-- The Roblox adapter is loaded on demand so the core stays usable outside Roblox.
function Bodycam.controller()
	return require(script.Roblox.BodycamController)
end

function Bodycam.coordinateSpace()
	return require(script.Roblox.CoordinateSpace)
end

return Bodycam
