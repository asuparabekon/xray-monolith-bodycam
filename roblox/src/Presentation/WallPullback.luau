-- Weapon pull-back near walls: when the barrel would go into geometry, the weapon
-- draws back and tilts up toward a high-ready hold. Written for this project (no
-- commercially usable open-source implementation was found).
--
-- Each frame a sphere is cast from the eye along the aim for the weapon's length; the
-- blocked share (1 - hit distance / length) drives a critically damped spring.

local WallPullback = {}
WallPullback.__index = WallPullback

function WallPullback.defaultSettings()
	return {
		Enabled = true,
		Length = 2.6, -- studs from the eye to the muzzle
		Radius = 0.18, -- studs
		Retract = 0.55, -- share of the blocked length the weapon draws back
		Raise = math.rad(28), -- tilt at fully blocked
		Response = 14, -- 1/s
		BlockFireAt = 0.85, -- blocked share above which the weapon cannot fire
	}
end

function WallPullback.new(settings)
	return setmetatable({ settings = settings or WallPullback.defaultSettings(), amount = 0, velocity = 0, blocked = 0 }, WallPullback)
end

-- The blocked share 0..1 for a cast result (hit distance or nil).
function WallPullback.blockedShare(hitDistance: number?, length: number): number
	if not hitDistance then
		return 0
	end
	return math.clamp(1 - hitDistance / length, 0, 1)
end

-- Critically damped step toward the blocked share.
function WallPullback:step(blocked: number, dt: number): number
	local s = self.settings
	self.blocked = blocked
	local omega = s.Response
	local x, v = self.amount, self.velocity
	local steps = math.max(1, math.ceil(dt * 240))
	local h = dt / steps
	for _ = 1, steps do
		v += ((blocked - x) * omega * omega - 2 * omega * v) * h
		x += v * h
	end
	self.amount, self.velocity = math.clamp(x, 0, 1), v
	return self.amount
end

-- Casts from origin along direction (unit) and steps the spring. params: RaycastParams.
function WallPullback:update(dt: number, origin: Vector3, direction: Vector3, params): number
	local s = self.settings
	if not s.Enabled then
		return self:step(0, dt)
	end
	local hit = workspace:Spherecast(origin, s.Radius, direction * s.Length, params)
	local distance = hit and (hit.Position - origin).Magnitude or nil
	return self:step(WallPullback.blockedShare(distance, s.Length), dt)
end

function WallPullback:blocksFire(): boolean
	return self.settings.Enabled and self.amount >= self.settings.BlockFireAt
end

-- The weapon-local transform for the current amount (apply about the grip).
function WallPullback:transform(): CFrame
	local s = self.settings
	local a = self.amount
	return CFrame.new(0, 0, a * s.Length * s.Retract) * CFrame.Angles(a * s.Raise, 0, 0)
end

return WallPullback
