-- Presentation: the bodycam look ported from permissively licensed open source.
--
--   Sway/            SPT-SWAY physiological weapon sway (MIT)
--   Deadzone         sptarkov-deadzone free aim (Unlicense)
--   HandheldShake    drei CameraShake on three-stdlib simplex noise (MIT)
--   ScreenEffects/   three.js (MIT) and pmndrs/postprocessing (Zlib) shader math, baked
--                    into overlays, plus a bodycam HUD, colour grade and glass lens
--   WallPullback     weapon pull-back near walls (written for this project)
--   WeaponPresenter  connects them to the BodycamController
--   Viewmodel        first-person arms rig: synthranger/Viewmodel (MIT) animation and
--                    culling, plus aim-down-sights, the scale trick, IK grips
--   Headcam          bodycam / helmet-camera view modes and head-camera spectating
--                    (written for this project)
--   Smoothing        frame-rate independent smoothing from InuFirstPerson (MIT)
-- Notices: roblox/THIRD-PARTY-NOTICES.md.

local Presentation = {
	SwayDirector = require(script.Sway.SwayDirector),
	SwayConfig = require(script.Sway.SwayConfig),
	SwayNoise = require(script.Sway.SwayNoise),
	Stress = require(script.Sway.Stress),
	Deadzone = require(script.Deadzone),
	HandheldShake = require(script.HandheldShake),
	UnityQuaternion = require(script.UnityQuaternion),
	Smoothing = require(script.Smoothing),
}

function Presentation.weaponPresenter()
	return require(script.WeaponPresenter)
end

function Presentation.wallPullback()
	return require(script.WallPullback)
end

function Presentation.viewmodel()
	return require(script.Viewmodel)
end

function Presentation.headcam()
	return require(script.Headcam)
end

function Presentation.screenEffects()
	return require(script.ScreenEffects)
end

return Presentation
