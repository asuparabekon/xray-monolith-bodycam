-- Port of Bodycam::SVec3 (bodycam_simulation.h): a mutable 3-vector as a plain table.
-- The simulation mutates its state every frame, so it does not use the immutable Roblox Vector3.

export type Vec3 = { x: number, y: number, z: number }

local Vec3 = {}

function Vec3.new(x: number?, y: number?, z: number?): Vec3
	return { x = x or 0, y = y or 0, z = z or 0 }
end

function Vec3.copy(v: Vec3): Vec3
	return { x = v.x, y = v.y, z = v.z }
end

function Vec3.set(v: Vec3, x: number, y: number, z: number)
	v.x = x
	v.y = y
	v.z = z
end

function Vec3.assign(v: Vec3, from: Vec3)
	v.x = from.x
	v.y = from.y
	v.z = from.z
end

function Vec3.add(v: Vec3, other: Vec3)
	v.x += other.x
	v.y += other.y
	v.z += other.z
end

function Vec3.addXYZ(v: Vec3, x: number, y: number, z: number)
	v.x += x
	v.y += y
	v.z += z
end

function Vec3.mul(v: Vec3, s: number)
	v.x *= s
	v.y *= s
	v.z *= s
end

function Vec3.magnitude(v: Vec3): number
	return math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
end

function Vec3.normalizeSafe(v: Vec3)
	local m = Vec3.magnitude(v)
	if m > 1.0e-6 then
		Vec3.mul(v, 1 / m)
	end
end

return Vec3
