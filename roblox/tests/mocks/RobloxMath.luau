-- Minimal Vector3 and CFrame stand-ins with Roblox semantics, for testing
-- Roblox/CoordinateSpace.luau outside Roblox. Only what the adapter uses.

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

return { Vector3 = Vector3, CFrame = CFrame }
