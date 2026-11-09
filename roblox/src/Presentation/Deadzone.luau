-- Port of lualeet/sptarkov-deadzone src/DeadzonePatch.cs (and its maintained fork
-- danauraborealis/manimal-deadzone): Insurgency-style free aim. Released into the
-- public domain under the Unlicense (see roblox/THIRD-PARTY-NOTICES.md).
--
-- Each frame a share (Sensitivity) of the view turn is added to a weapon offset,
-- wrapped, and clamped to MaxAngle. The weapon therefore leads the view in the
-- direction of a turn and stays off centre until the view turns back. Aiming fades the
-- offset to AimMultiplier (0 = centred while aiming).
--
-- Angles in and out are the bodycam convention: radians, yaw positive = left, pitch
-- positive = up. Internally the source's Unity convention is kept (yaw right, pitch
-- down, Euler(yaw, pitch, 0) exactly as the C# builds it).

local UnityQuaternion = require(script.Parent.UnityQuaternion)

local Deadzone = {}
Deadzone.__index = Deadzone

local DEG = 180 / math.pi
local euler, multiply, inverse, slerp, eulerAngles =
	UnityQuaternion.euler, UnityQuaternion.multiply, UnityQuaternion.inverse, UnityQuaternion.slerpUnclamped, UnityQuaternion.eulerAngles

-- WeaponSettings defaults (PluginSettings.cs) with the plugin's Position override.
function Deadzone.defaultSettings()
	return {
		Enabled = true,
		Position = 0.1, -- metres the pivot sits behind the weapon
		Sensitivity = 0.25, -- share of the view turn the weapon takes
		MaxAngle = 5.0, -- degrees
		AimMultiplier = 0.0, -- share of the offset kept at full ADS
	}
end

function Deadzone.new(settings)
	local self = setmetatable({ settings = settings or Deadzone.defaultSettings() }, Deadzone)
	self:reset()
	return self
end

function Deadzone:reset()
	self.initialized = false
	self.cumulativeYaw, self.cumulativePitch, self.aimSmoothed = 0, 0, 0
	self.lastYaw, self.lastPitch = 0, 0
end

-- (-180, 180]
local function wrapped(yaw: number, pitch: number): (number, number)
	if yaw < 0 then
		yaw += 360
	end
	if pitch < 0 then
		pitch += 360
	end
	pitch %= 360
	yaw %= 360
	if yaw > 180 then
		yaw -= 360
	end
	if pitch > 180 then
		pitch -= 360
	end
	return yaw, pitch
end

-- Vector2.ClampMagnitude
local function clamped(yaw: number, pitch: number, maxAngle: number): (number, number)
	local m = math.sqrt(yaw * yaw + pitch * pitch)
	if m > maxAngle and m > 0 then
		return yaw * maxAngle / m, pitch * maxAngle / m
	end
	return yaw, pitch
end

function Deadzone:_updateRotation(yaw: number, pitch: number)
	local s = self.settings
	local lastRotation = euler(self.lastYaw, self.lastPitch, 0)
	local currentRotation = euler(yaw, pitch, 0)
	self.lastYaw, self.lastPitch = yaw, pitch
	-- "all euler angles should go to hell": the share of the turn, as a rotation.
	lastRotation = slerp(currentRotation, lastRotation, s.Sensitivity)
	local dx, dy = eulerAngles(multiply(currentRotation, inverse(lastRotation)))
	self.cumulativeYaw += dx
	self.cumulativePitch += dy
	self.cumulativeYaw, self.cumulativePitch = wrapped(self.cumulativeYaw, self.cumulativePitch)
	self.cumulativeYaw, self.cumulativePitch = clamped(self.cumulativeYaw, self.cumulativePitch, s.MaxAngle)
end

-- One frame. aimYaw/aimPitch: the view aim in radians (bodycam convention).
-- Returns the weapon offset yaw, pitch in radians (bodycam convention).
function Deadzone:update(dt: number, aimYaw: number, aimPitch: number, aiming: boolean): (number, number)
	local s = self.settings
	if not s.Enabled then
		self:reset()
		return 0, 0
	end
	-- Unity convention: yaw right, pitch down, degrees.
	local yaw, pitch = -aimYaw * DEG, -aimPitch * DEG
	if not self.initialized then
		-- Seed the baseline so the spawn heading is not read as input.
		self.lastYaw, self.lastPitch = yaw, pitch
		self.aimSmoothed = if aiming then 1 else 0
		self.initialized = true
	end
	self:_updateRotation(yaw, pitch)
	local blend = 1 - math.exp(-6 * math.max(0, dt))
	self.aimSmoothed += ((if aiming then 1 else 0) - self.aimSmoothed) * blend
	local aimMultiplier = 1 - ((1 - s.AimMultiplier) * self.aimSmoothed)
	-- Back to the bodycam convention.
	return -self.cumulativeYaw * aimMultiplier / DEG, -self.cumulativePitch * aimMultiplier / DEG
end

-- The pivot the offset turns the weapon about, metres behind the weapon.
function Deadzone:pivotMetres(): number
	return self.settings.Position
end

return Deadzone
