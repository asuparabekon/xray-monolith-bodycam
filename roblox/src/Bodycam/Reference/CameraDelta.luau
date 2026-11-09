-- Port of ClassicRegripCameraDelta (src/xrGame/bodycam_reference/classic_regrip.h) and the
-- quaternion helpers it uses (recoil_pose.cpp, camera_presentation.cpp, transform.cpp).
--
-- Turns a camera-relative rotation { pitch, yaw, roll } in degrees into a 3x3 rotation in the
-- engine's camera-local basis (X right, Y up, Z forward, left-handed). The result is
-- { i = {x,y,z}, j = {x,y,z}, k = {x,y,z} }: the turned right, up and forward axes.
-- CoordinateSpace.luau converts it to a Roblox CFrame.

local CameraDelta = {}

local RAD_HALF = 0.017453292519943295 / 2

local function trunc(x: number): number
	return if x >= 0 then math.floor(x) else math.ceil(x)
end

local function halfAngle(a: number): number
	return (a - trunc(a / 360) * 360) * RAD_HALF
end

-- RecoilQuaternion: { x, y, z, w }.
local function recoilQuaternion(pitch: number, yaw: number, roll: number)
	local p, y, l = halfAngle(pitch), halfAngle(yaw), halfAngle(roll)
	local sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(l), math.cos(l)
	return {
		((sy * sp) * cr) + -((cy * cp) * sr),
		-((cy * sp) * cr) + -((sy * cp) * sr),
		-((cy * sp) * sr) + ((sy * cp) * cr),
		((sy * sp) * sr) + ((cy * cp) * cr),
	}
end

local function product(a, b)
	local x, y, z, w = a[1], a[2], a[3], a[4]
	local bx, by, bz, bw = b[1], b[2], b[3], b[4]
	return {
		x * bw + w * bx + y * bz - z * by,
		y * bw + w * by + z * bx - x * bz,
		z * bw + w * bz + x * by - y * bx,
		w * bw - x * bx - y * by - z * bz,
	}
end

local function normalize(q)
	local length = math.sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4])
	if length == 0 then
		return { 0, 0, 0, 1 }
	end
	local inverse = 1 / length
	return { q[1] * inverse, q[2] * inverse, q[3] * inverse, q[4] * inverse }
end

-- WorldCameraQuaternion(parent = {}, relative).
local function worldCameraQuaternion(pitch: number, yaw: number, roll: number)
	local q = product(recoilQuaternion(0, 0, 0), recoilQuaternion(pitch, yaw, roll))
	return normalize(product({ q[2], -q[3], -q[1], q[4] }, { 0, 1, 0, 0 }))
end

-- Rotation part of Compose(): the three columns (rotated X, Y, Z axes).
local function columns(q)
	local x, y, z, w = q[1], q[2], q[3], q[4]
	local x2, y2, z2 = x + x, y + y, z + z
	local xx, xy, xz, yy, yz, zz = x * x2, x * y2, x * z2, y * y2, y * z2, z * z2
	local wx, wy, wz = w * x2, w * y2, w * z2
	return {
		{ 1 - (yy + zz), xy + wz, xz - wy },
		{ xy - wz, 1 - (xx + zz), yz + wx },
		{ xz + wy, yz - wx, 1 - (xx + yy) },
	}
end

local NEUTRAL = columns(worldCameraQuaternion(0, 0, 0))

local function dot(a, b): number
	return a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
end

function CameraDelta.fromRotation(pitch: number, yaw: number, roll: number)
	local perturbed = columns(worldCameraQuaternion(pitch, yaw, roll))
	-- relative = inverse(neutral) * perturbed; neutral is a rotation, so its inverse is its transpose.
	local rel = {}
	for c = 1, 3 do
		local col = perturbed[c]
		rel[c] = { dot(NEUTRAL[1], col), dot(NEUTRAL[2], col), dot(NEUTRAL[3], col) }
	end
	-- cameraBasis * relative * cameraBasis, cameraBasis = diag(1, 1, -1): flips the Z row and Z column.
	return {
		i = { rel[1][1], rel[1][2], -rel[1][3] },
		j = { rel[2][1], rel[2][2], -rel[2][3] },
		k = { -rel[3][1], -rel[3][2], rel[3][3] },
	}
end

CameraDelta.IDENTITY = table.freeze({
	i = table.freeze({ 1, 0, 0 }),
	j = table.freeze({ 0, 1, 0 }),
	k = table.freeze({ 0, 0, 1 }),
})

return CameraDelta
