-- This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
-- If a copy of the MPL was not distributed with this file, You can obtain one at
-- https://mozilla.org/MPL/2.0/. (A copy is in roblox/src/Realism/LICENSE.)
--
-- Derived from Character-Realism by CloneTrooper1019 (MaximumADHD),
-- https://github.com/MaximumADHD/Character-Realism, by way of the FPSys model.
--
-- FPSys configuration. The values come from the FPSys model (Configuration,
-- RealismConfiguration, CameraConfiguration); the sounds and material map are
-- Character-Realism's. The Lean, Shake and Overlap sections are new and belong to the
-- bodycam integration. Look-angle settings moved to Realism/Config.luau.

local Config = {}

Config.BindTag = "RealismHook"
Config.RemoteName = "SetLookAngles"

-- Footstep sound ids per sound group, and the Roblox materials each group covers.
Config.Sounds = {
	Dirt = 178054124,
	Wood = 177940988,
	Concrete = 277067660,
	Grass = 4776173570,
	Metal = 4790537991,
	Sand = 4777003964,
	Fabric = 4776951843,
	Gravel = 4776998555,
	Marble = 4776962643,
}

Config.MaterialMap = {
	Mud = "Dirt",
	Pebble = "Dirt",
	Ground = "Dirt",
	Sand = "Sand",
	Snow = "Sand",
	Sandstone = "Sand",
	Rock = "Gravel",
	Basalt = "Gravel",
	Asphalt = "Gravel",
	Glacier = "Gravel",
	Slate = "Gravel",
	WoodPlanks = "Wood",
	LeafyGrass = "Grass",
	Ice = "Marble",
	Salt = "Marble",
	Marble = "Marble",
	Pavement = "Marble",
	Limestone = "Marble",
	Foil = "Metal",
	DiamondPlate = "Metal",
	CorrodedMetal = "Metal",
}

Config.Camera = {
	MaxBlur = 25,
	BlurMult = 5,
	BaseMult = 12,
	DriftMin = -3,
	DriftMax = 3,
	BreathingScale = { X = 0, Y = 2 },
	BreathingSpeed = 3,
	TiltAngle = 3,
	TiltSpeed = 8,
	TiltDamper = 0.8,
	LookSwayMaxAngle = { X = 8, Y = 2 },
	LookSwayMouseDamper = { X = 1, Y = 12 },
	LookSwaySpeed = 10,
	LookSwayDamper = 0.65,
	LandEffectVelocity = 12.5,
	LandEffectSpeed = 8,
	LandEffectDamper = 0.4,
	-- Share of every FPSys camera effect kept at full ADS.
	AdsScale = 0.35,
}

-- Lean (new): camera roll and sideways shift, stopped short of walls.
Config.Lean = {
	Angle = 12, -- degrees of camera roll at full lean
	Offset = 1.1, -- studs sideways at full lean
	Speed = 10,
	Damper = 1,
	WallClearance = 0.4, -- studs kept from a wall
}

-- Per-shot camera shake (new): random kick picked per shot, eased by springs (degrees).
Config.Shake = {
	Pitch = { 0.6, 1.2 },
	Yaw = { -0.4, 0.4 },
	Roll = { -1.0, 1.0 },
	Speed = 18,
	Damper = 0.55,
}

-- Which FPSys effects run next to the bodycam. "Auto" turns off the FPSys walk bob
-- when the bodycam head bob is on, and the strafe tilt when the bodycam head roll is
-- on, so the same motion is not applied twice.
Config.Overlap = "Auto" -- "Auto" | "All" (keep every FPSys effect)

-- Effects that can be switched off one by one.
Config.Enabled = {
	MotionBlur = true,
	Breathing = true,
	WalkBob = true,
	StrafeTilt = true,
	LookSway = true,
	LandEffect = true,
	Lean = true,
	Shake = true,
	LookAngles = true,
	Footsteps = true,
}

return Config
