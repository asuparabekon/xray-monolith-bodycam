-- The Unity Quaternion operations the ported C# code relies on, with Unity's
-- conventions: Euler(x, y, z) in degrees applies Z, then X, then Y (q = qy * qx * qz),
-- and eulerAngles returns degrees in [0, 360). Quaternions are { x, y, z, w }.
-- Written for this project from the Unity documentation's definitions.

local UnityQuaternion = {}

local RAD = math.pi / 180
local DEG = 180 / math.pi

function UnityQuaternion.euler(xDeg: number, yDeg: number, zDeg: number)
	local hx, hy, hz = xDeg * RAD / 2, yDeg * RAD / 2, zDeg * RAD / 2
	local sx, cx = math.sin(hx), math.cos(hx)
	local sy, cy = math.sin(hy), math.cos(hy)
	local sz, cz = math.sin(hz), math.cos(hz)
	return {
		cz * cy * sx + cx * sy * sz,
		cz * cx * sy - cy * sx * sz,
		cx * cy * sz - sx * sy * cz,
		cx * cy * cz + sx * sy * sz,
	}
end

function UnityQuaternion.multiply(a, b)
	local ax, ay, az, aw = a[1], a[2], a[3], a[4]
	local bx, by, bz, bw = b[1], b[2], b[3], b[4]
	return {
		aw * bx + ax * bw + ay * bz - az * by,
		aw * by + ay * bw + az * bx - ax * bz,
		aw * bz + az * bw + ax * by - ay * bx,
		aw * bw - ax * bx - ay * by - az * bz,
	}
end

function UnityQuaternion.inverse(q)
	local n = q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4]
	if n == 0 then
		return { 0, 0, 0, 1 }
	end
	return { -q[1] / n, -q[2] / n, -q[3] / n, q[4] / n }
end

local function normalize(q)
	local n = math.sqrt(q[1] * q[1] + q[2] * q[2] + q[3] * q[3] + q[4] * q[4])
	if n < 1e-12 then
		return { 0, 0, 0, 1 }
	end
	return { q[1] / n, q[2] / n, q[3] / n, q[4] / n }
end

-- Spherical interpolation along the shortest arc; t is not clamped.
function UnityQuaternion.slerpUnclamped(a, b, t: number)
	local dot = a[1] * b[1] + a[2] * b[2] + a[3] * b[3] + a[4] * b[4]
	local bx, by, bz, bw = b[1], b[2], b[3], b[4]
	if dot < 0 then
		dot, bx, by, bz, bw = -dot, -bx, -by, -bz, -bw
	end
	local wa, wb
	if dot > 0.9995 then
		wa, wb = 1 - t, t
	else
		local theta = math.acos(math.clamp(dot, -1, 1))
		local s = math.sin(theta)
		wa, wb = math.sin((1 - t) * theta) / s, math.sin(t * theta) / s
	end
	return normalize({ a[1] * wa + bx * wb, a[2] * wa + by * wb, a[3] * wa + bz * wb, a[4] * wa + bw * wb })
end

local function wrap360(v: number): number
	v %= 360
	return if v < 0 then v + 360 else v
end

-- Inverse of euler(): degrees in [0, 360).
function UnityQuaternion.eulerAngles(q): (number, number, number)
	local x, y, z, w = q[1], q[2], q[3], q[4]
	local sinX = math.clamp(2 * (w * x - y * z), -1, 1)
	local ex = math.asin(sinX)
	local ey, ez
	if math.abs(sinX) < 0.99999 then
		ey = math.atan2(2 * (w * y + x * z), 1 - 2 * (x * x + y * y))
		ez = math.atan2(2 * (w * z + x * y), 1 - 2 * (x * x + z * z))
	else
		-- Gimbal lock: put the whole turn in y.
		ey = math.atan2(-2 * (x * z - w * y), 1 - 2 * (y * y + z * z))
		ez = 0
	end
	return wrap360(ex * DEG), wrap360(ey * DEG), wrap360(ez * DEG)
end

return UnityQuaternion
