-- Minimal Vector3 and CFrame stand-ins with Roblox semantics, for testing
-- the Roblox adapters outside Roblox. Only what they use.

local Vector3 = {}
local V = {}
V.__index = function(v, key)
	if key == "Magnitude" then
		return math.sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z)
	elseif key == "Unit" then
		local m = math.sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z)
		return Vector3.new(v.X / m, v.Y / m, v.Z / m)
	end
	return V[key]
end

function Vector3.new(x, y, z)
	return setmetatable({ X = x or 0, Y = y or 0, Z = z or 0 }, V)
end
function V.Dot(a, b)
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z
end
function V.Cross(a, b)
	return Vector3.new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X)
end
V.__add = function(a, b)
	return Vector3.new(a.X + b.X, a.Y + b.Y, a.Z + b.Z)
end
V.__sub = function(a, b)
	return Vector3.new(a.X - b.X, a.Y - b.Y, a.Z - b.Z)
end
V.__unm = function(a)
	return Vector3.new(-a.X, -a.Y, -a.Z)
end
V.__mul = function(a, b)
	if type(a) == "number" then
		a, b = b, a
	end
	if type(b) == "table" then
		return Vector3.new(a.X * b.X, a.Y * b.Y, a.Z * b.Z)
	end
	return Vector3.new(a.X * b, a.Y * b, a.Z * b)
end
V.__div = function(a, b)
	return Vector3.new(a.X / b, a.Y / b, a.Z / b)
end
Vector3.zero = Vector3.new(0, 0, 0)
Vector3.yAxis = Vector3.new(0, 1, 0)

-- CFrame: columns r (X), u (Y), b (Z = back), position p.
local CFrame = {}
local C = {}

local function make(p, r, u, b)
	return setmetatable({ p = p, r = r, u = u, b = b }, C)
end

C.__index = function(cf, key)
	if key == "Position" then
		return cf.p
	elseif key == "LookVector" then
		return -cf.b
	elseif key == "RightVector" then
		return cf.r
	elseif key == "UpVector" then
		return cf.u
	elseif key == "Rotation" then
		return make(Vector3.zero, cf.r, cf.u, cf.b)
	end
	return C[key]
end

function CFrame.new(x, y, z)
	if type(x) == "table" then
		return make(x, Vector3.new(1, 0, 0), Vector3.new(0, 1, 0), Vector3.new(0, 0, 1))
	end
	return make(Vector3.new(x or 0, y or 0, z or 0), Vector3.new(1, 0, 0), Vector3.new(0, 1, 0), Vector3.new(0, 0, 1))
end
CFrame.identity = CFrame.new()

function CFrame.fromMatrix(p, vX, vY, vZ)
	return make(p, vX, vY, vZ or vX:Cross(vY))
end

local function rotX(a)
	local c, s = math.cos(a), math.sin(a)
	return make(Vector3.zero, Vector3.new(1, 0, 0), Vector3.new(0, c, s), Vector3.new(0, -s, c))
end
local function rotY(a)
	local c, s = math.cos(a), math.sin(a)
	return make(Vector3.zero, Vector3.new(c, 0, -s), Vector3.new(0, 1, 0), Vector3.new(s, 0, c))
end
local function rotZ(a)
	local c, s = math.cos(a), math.sin(a)
	return make(Vector3.zero, Vector3.new(c, s, 0), Vector3.new(-s, c, 0), Vector3.new(0, 0, 1))
end

-- Roblox: fromOrientation applies Z, then X, then Y (Ry * Rx * Rz); Angles is Rx * Ry * Rz.
function CFrame.fromOrientation(rx, ry, rz)
	return rotY(ry) * rotX(rx) * rotZ(rz)
end
CFrame.fromEulerAnglesYXZ = CFrame.fromOrientation

function CFrame.Angles(rx, ry, rz)
	return rotX(rx) * rotY(ry) * rotZ(rz)
end

local function rotate(cf, v)
	return cf.r * v.X + cf.u * v.Y + cf.b * v.Z
end

C.__mul = function(a, b)
	if getmetatable(b) == V then
		return a.p + rotate(a, b)
	end
	return make(a.p + rotate(a, b.p), rotate(a, b.r), rotate(a, b.u), rotate(a, b.b))
end
C.__add = function(cf, v)
	return make(cf.p + v, cf.r, cf.u, cf.b)
end

function C.VectorToWorldSpace(cf, v)
	return rotate(cf, v)
end

function C.VectorToObjectSpace(cf, v)
	return Vector3.new(cf.r:Dot(v), cf.u:Dot(v), cf.b:Dot(v))
end

function C.Inverse(cf)
	-- Transpose of the rotation.
	local r = Vector3.new(cf.r.X, cf.u.X, cf.b.X)
	local u = Vector3.new(cf.r.Y, cf.u.Y, cf.b.Y)
	local b = Vector3.new(cf.r.Z, cf.u.Z, cf.b.Z)
	local inv = make(Vector3.zero, r, u, b)
	return make(-rotate(inv, cf.p), r, u, b)
end

-- Rotation <-> quaternion (w, x, y, z), for Lerp.
local function toQuaternion(cf)
	local m00, m01, m02 = cf.r.X, cf.u.X, cf.b.X
	local m10, m11, m12 = cf.r.Y, cf.u.Y, cf.b.Y
	local m20, m21, m22 = cf.r.Z, cf.u.Z, cf.b.Z
	local trace = m00 + m11 + m22
	if trace > 0 then
		local s = math.sqrt(trace + 1) * 2
		return s / 4, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s
	elseif m00 > m11 and m00 > m22 then
		local s = math.sqrt(1 + m00 - m11 - m22) * 2
		return (m21 - m12) / s, s / 4, (m01 + m10) / s, (m02 + m20) / s
	elseif m11 > m22 then
		local s = math.sqrt(1 + m11 - m00 - m22) * 2
		return (m02 - m20) / s, (m01 + m10) / s, s / 4, (m12 + m21) / s
	end
	local s = math.sqrt(1 + m22 - m00 - m11) * 2
	return (m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, s / 4
end

local function fromQuaternion(p, w, x, y, z)
	return make(
		p,
		Vector3.new(1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)),
		Vector3.new(2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)),
		Vector3.new(2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y))
	)
end

-- Position linear, rotation spherical (shortest path), as Roblox.
function C.Lerp(a, b, t)
	local aw, ax, ay, az = toQuaternion(a)
	local bw, bx, by, bz = toQuaternion(b)
	local dot = aw * bw + ax * bx + ay * by + az * bz
	if dot < 0 then
		bw, bx, by, bz, dot = -bw, -bx, -by, -bz, -dot
	end
	local k0, k1
	if dot > 0.9995 then
		k0, k1 = 1 - t, t
	else
		local theta = math.acos(dot)
		local sinTheta = math.sin(theta)
		k0, k1 = math.sin((1 - t) * theta) / sinTheta, math.sin(t * theta) / sinTheta
	end
	local w, x, y, z = aw * k0 + bw * k1, ax * k0 + bx * k1, ay * k0 + by * k1, az * k0 + bz * k1
	local n = math.sqrt(w * w + x * x + y * y + z * z)
	return fromQuaternion(a.p + (b.p - a.p) * t, w / n, x / n, y / n, z / n)
end

function C.ToObjectSpace(a, b)
	return C.Inverse(a) * b
end

return { Vector3 = Vector3, CFrame = CFrame }
