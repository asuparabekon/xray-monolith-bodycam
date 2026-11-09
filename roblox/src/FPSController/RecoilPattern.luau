-- Camera recoil: each shot adds a random upward and sideways kick to the aim, applied
-- over a few frames, growing with consecutive shots; after the trigger is released the
-- aim drifts back by a share of the climb. Pure logic; feed update() into
-- BodycamController:kickAim().
--
-- Written for this project after the recoil-pattern feature of the Kinemation FPS
-- Animation Framework demo; no Kinemation code is used.

local RecoilPattern = {}
RecoilPattern.__index = RecoilPattern

function RecoilPattern.defaultSettings()
	return {
		pitch = { 0.55, 0.85 }, -- degrees up per shot
		yaw = { -0.35, 0.35 }, -- degrees sideways per shot (positive = left)
		yawBias = 0.15, -- degrees added to the side the last shot went
		buildUp = 0.06, -- extra share per consecutive shot
		maxBuildUp = 0.6,
		firstShotScale = 1.2,
		adsScale = 0.7,
		smoothing = 28, -- 1/s: how fast a kick reaches the aim
		recovery = 0.55, -- share of the climb taken back after firing
		recoveryDelay = 0.12, -- seconds after the last shot
		recoverySpeed = 6, -- 1/s
		chainGap = 0.35, -- seconds between shots that still count as one spray
	}
end

-- random: function() -> number in [0, 1) (default math.random).
function RecoilPattern.new(settings, random)
	local self = setmetatable({}, RecoilPattern)
	self.settings = settings or RecoilPattern.defaultSettings()
	self.random = random or math.random
	self:reset()
	return self
end

function RecoilPattern:reset()
	self.pendingYaw, self.pendingPitch = 0, 0
	self.climb = 0 -- degrees of pitch applied in this spray, for recovery
	self.recoverTo = nil -- the climb recovery stops at, set when recovery starts
	self.shots = 0
	self.sinceShot = math.huge
	self.firing = false
	self.lastSide = 1
end

function RecoilPattern:setSettings(settings)
	self.settings = settings
	self:reset()
end

local function range(random, r): number
	return r[1] + (r[2] - r[1]) * random()
end

-- A shot. scale multiplies the kick (stance, attachments); aiming applies adsScale.
function RecoilPattern:onShot(scale: number?, aiming: boolean?)
	local s = self.settings
	if self.sinceShot > s.chainGap then
		self.shots = 0
	end
	self.shots += 1
	self.firing = true
	self.sinceShot = 0
	local k = (scale or 1) * (if aiming then s.adsScale else 1)
	if self.shots == 1 then
		k *= s.firstShotScale
	else
		k *= 1 + math.min(s.buildUp * (self.shots - 1), s.maxBuildUp)
	end
	local yaw = range(self.random, s.yaw) + s.yawBias * self.lastSide
	self.lastSide = if yaw >= 0 then 1 else -1
	self.pendingPitch += range(self.random, s.pitch) * k
	self.pendingYaw += yaw * k
end

-- The trigger was released (or the burst ended).
function RecoilPattern:onFireEnd()
	self.firing = false
end

-- Returns the aim turn for this frame in radians: yaw (positive = left), pitch (positive = up).
function RecoilPattern:update(dt: number): (number, number)
	local s = self.settings
	self.sinceShot += dt
	local a = 1 - math.exp(-s.smoothing * dt)
	local yaw, pitch = self.pendingYaw * a, self.pendingPitch * a
	self.pendingYaw -= yaw
	self.pendingPitch -= pitch
	self.climb += pitch

	local recovering = (not self.firing or self.sinceShot > s.chainGap) and self.sinceShot > s.recoveryDelay
	if not recovering then
		self.recoverTo = nil
	elseif self.climb > 0 then
		-- The target is fixed when recovery starts; the rest of the climb stays.
		self.recoverTo = self.recoverTo or self.climb * (1 - s.recovery)
		local keep = self.recoverTo
		local back = (self.climb - keep) * (1 - math.exp(-s.recoverySpeed * dt))
		if self.climb - back <= keep + 1e-4 then
			back = self.climb - keep
			self.climb = 0
			self.recoverTo = nil
		else
			self.climb -= back
		end
		pitch -= back
	end
	return math.rad(yaw), math.rad(pitch)
end

return RecoilPattern
