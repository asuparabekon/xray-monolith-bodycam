-- Port of src/xrGame/bodycam_mouse_input.h/.cpp.
-- The aim as the sum of the mouse deltas handed to the camera. Mouse speed is
-- the frame-to-frame difference of the aim, and the aim read back from the
-- camera direction is too coarse for that at small movements. The camera stays
-- authoritative: a difference beyond rounding snaps the aim to it.

local MathUtil = require(script.Parent.MathUtil)

local normalizeAngle = MathUtil.normalizeAngle
local angleDifference = MathUtil.angleDifference

-- Radians. A larger difference from the camera's aim means something other than the mouse moved it.
local RECONCILE_TOLERANCE = 1.0e-5

local MouseAim = {}

function MouseAim.newState()
	return { yaw = 0, pitch = 0, pending_yaw = 0, pending_pitch = 0, initialized = false }
end

function MouseAim.reset(state, yaw: number, pitch: number)
	state.yaw = normalizeAngle(yaw)
	state.pitch = normalizeAngle(pitch)
	state.pending_yaw = 0
	state.pending_pitch = 0
	state.initialized = true
end

function MouseAim.addDelta(state, yawDelta: number, pitchDelta: number)
	state.pending_yaw += yawDelta
	state.pending_pitch += pitchDelta
end

function MouseAim.rebase(state, yawDelta: number, pitchDelta: number)
	if not state.initialized then
		return
	end
	state.yaw = normalizeAngle(state.yaw + yawDelta)
	state.pitch = normalizeAngle(state.pitch + pitchDelta)
end

-- Returns yaw, pitch.
function MouseAim.resolve(state, targetYaw: number, targetPitch: number): (number, number)
	if not state.initialized then
		MouseAim.reset(state, targetYaw, targetPitch)
	end
	state.yaw = normalizeAngle(state.yaw + state.pending_yaw)
	state.pitch = normalizeAngle(state.pitch + state.pending_pitch)
	state.pending_yaw = 0
	state.pending_pitch = 0

	-- Recoil, script cameras and pitch limits move the camera without the mouse.
	if math.abs(angleDifference(targetYaw, state.yaw)) > RECONCILE_TOLERANCE then
		state.yaw = normalizeAngle(targetYaw)
	end
	if math.abs(angleDifference(targetPitch, state.pitch)) > RECONCILE_TOLERANCE then
		state.pitch = normalizeAngle(targetPitch)
	end
	return state.yaw, state.pitch
end

return MouseAim
