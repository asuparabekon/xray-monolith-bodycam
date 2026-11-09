-- This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
-- If a copy of the MPL was not distributed with this file, You can obtain one at
-- https://mozilla.org/MPL/2.0/. (A copy is in this folder: LICENSE.)
--
-- Port of Character-Realism RealismClient/Config.lua by CloneTrooper1019 (MaximumADHD),
-- https://github.com/MaximumADHD/Character-Realism. Unchanged values.

return {
	-- Multiplier values (in radians) for each joint, based on the pitch/yaw look angles.
	-- In the source, Pitch is the horizontal look and Yaw the vertical look.
	RotationFactors = {
		-- Shared
		Head = { Pitch = 0.8, Yaw = 0.75 },
		-- R15
		UpperTorso = { Pitch = 0.5, Yaw = 0.5 },
		LeftUpperArm = { Pitch = 0.0, Yaw = -0.5 },
		RightUpperArm = { Pitch = 0.0, Yaw = -0.5 },
		-- R6
		Torso = { Pitch = 0.4, Yaw = 0.2 },
		["Left Arm"] = { Pitch = 0.0, Yaw = -0.5 },
		["Right Arm"] = { Pitch = 0.0, Yaw = -0.5 },
		["Left Leg"] = { Pitch = 0.0, Yaw = -0.2 },
		["Right Leg"] = { Pitch = 0.0, Yaw = -0.2 },
	},
	BindTag = "RealismHook",
	RemoteName = "SetLookAngles",
	-- Seconds between look-angle sends, and the rounding step (the source: 0.05, 0.01).
	SendInterval = 0.05,
	SendStep = 0.01,
	-- Characters farther than this from the camera are not animated (studs).
	MaxDistance = 30,
	-- Smoothing time constant of the look angles (seconds).
	Smoothing = 0.1,
}
