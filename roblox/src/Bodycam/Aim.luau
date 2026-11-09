-- Aim angles <-> Roblox CFrames. Uses only CFrame and Vector3.
--
-- yaw: rotation about world Y, positive turns left (as CFrame.fromOrientation).
-- pitch: positive looks up. Radians.

local Aim = {}

-- Yaw and pitch of a look direction (any length > 0).
function Aim.aimFromLook(look: Vector3): (number, number)
	local m = look.Magnitude
	if m < 1e-9 then
		return 0, 0
	end
	local yaw = math.atan2(-look.X, -look.Z)
	local pitch = math.asin(math.clamp(look.Y / m, -1, 1))
	return yaw, pitch
end

-- The rotation (no position) that looks along yaw, pitch with no roll.
function Aim.aimRotation(yaw: number, pitch: number): CFrame
	return CFrame.fromOrientation(pitch, yaw, 0)
end

-- Unit world vectors of the yaw-only frame: right and forward on the ground plane.
function Aim.yawFrame(yaw: number): (Vector3, Vector3)
	local c, s = math.cos(yaw), math.sin(yaw)
	return Vector3.new(c, 0, -s), Vector3.new(-s, 0, -c)
end

return Aim
