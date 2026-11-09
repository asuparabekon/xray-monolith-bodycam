-- Port of the mixing in SPT-SWAY src/Runtime/SwayDirector.cs: runs every sway
-- source, applies the global multiplier (intensity, ADS, aim-hold fatigue,
-- exhaustion, injury, shot charge), splits the sum into a weapon (hands) share and a
-- camera share, and low-passes both.
-- SPT-SWAY: Copyright (c) 2026 Savant, MIT License (see roblox/THIRD-PARTY-NOTICES.md).
--
-- Not ported: the EFT spring drive (Harmony patches into ProceduralWeaponAnimation),
-- the in-game calibration solve, heartbeat audio and the amplitude report.

local SwayConfig = require(script.Parent.SwayConfig)
local SwaySources = require(script.Parent.SwaySources)
local SwayNoise = require(script.Parent.SwayNoise)
local Stress = require(script.Parent.Stress)

local SwayDirector = {}
SwayDirector.__index = SwayDirector

-- The shooter state the sources read (ShooterState.cs), with neutral defaults.
function SwayDirector.newShooterState()
	return {
		exertion = 0, -- 0 rested .. 1 spent
		oxygen = 1,
		exhausted = false,
		holdingBreath = false,
		isAiming = false,
		weightKg = 3.6,
		moveSpeed = 0, -- m/s
		healthFraction = 1,
		armFractured = false,
		armBlacked = false,
		neuroTremor = false,
		handlingFactor = 1, -- grip quality: scales breathing, pulse, tremor
		supportFactor = 1, -- braced or rested weapon < 1
	}
end

function SwayDirector.new(config)
	local self = setmetatable({}, SwayDirector)
	self.config = config or SwayConfig.default()
	self.state = SwayDirector.newShooterState()
	self.stress = Stress.new(self.config)
	self.breath = SwaySources.Respiration.new(self.config)
	self.heart = SwaySources.Cardiac.new(self.config)
	self.shake = SwaySources.Tremor.new(self.config)
	self.drift = SwaySources.PosturalDrift.new(self.config)
	self.inertia = SwaySources.WeaponInertia.new(self.config)
	self.handsFilter = SwayNoise.OnePole.new()
	self.cameraFilter = SwayNoise.OnePole.new()
	self.holdFatigue = 0
	self.shotCharge = 0
	self.time = 0
	-- Last outputs, degrees (pitch, yaw, roll).
	self.hands = { 0, 0, 0 }
	self.camera = { 0, 0, 0 }
	return self
end

function SwayDirector:reset()
	self.stress:reset()
	self.breath:reset()
	self.heart:reset()
	self.shake:reset()
	self.drift:reset()
	self.inertia:reset()
	self.handsFilter:reset()
	self.cameraFilter:reset()
	self.holdFatigue, self.shotCharge = 0, 0
end

-- A shot from the player's weapon.
function SwayDirector:onShot()
	local c = self.config
	self.stress:onShotFired()
	if not c.ShotEnabled then
		return
	end
	self.shotCharge = math.min(self.shotCharge + c.ShotGain, c.ShotMaxAccumulation)
	self.heart:addShotKick(c.ShotHeartRateKick)
end

local function moveTowards(current: number, target: number, maxDelta: number): number
	if math.abs(target - current) <= maxDelta then
		return target
	end
	return current + math.sign(target - current) * maxDelta
end

function SwayDirector:_globalMultiplier(dt: number): number
	local c, s = self.config, self.state
	local m = c.MasterIntensity
	m *= if s.isAiming then c.AdsMaster else c.HipMaster
	m *= s.supportFactor
	if c.FatigueEnabled then
		-- Holding an aim is work: it builds while the weapon is up.
		local target = if s.isAiming then c.HoldFatigueMax else 0
		local rate = if s.isAiming then c.HoldFatigueRate else c.HoldFatigueDecay
		self.holdFatigue = moveTowards(self.holdFatigue, target, rate * dt)
		m *= 1 + self.holdFatigue
		if s.exhausted then
			m *= c.ExhaustedMultiplier
		end
	end
	if c.InjuryEnabled then
		if s.armFractured then
			m *= c.ArmFractureMultiplier
		end
		if s.armBlacked then
			m *= c.ArmBlackedMultiplier
		end
	end
	self.shotCharge = math.max(0, self.shotCharge - dt * c.ShotDecay)
	m *= 1 + self.shotCharge
	return m
end

-- One frame. view: { yaw, pitch } degrees in the source convention (yaw right,
-- pitch down); velocity: { x right, y up, z forward } m/s relative to the view.
-- Returns hands (degrees pitch, yaw, roll) and camera (degrees) tables.
function SwayDirector:update(dt: number, viewYaw: number, viewPitch: number, vx: number, vy: number, vz: number)
	if dt <= 0 then
		return self.hands, self.camera
	end
	local c, s = self.config, self.state
	self.time += dt
	self.stress:tick(s, dt)
	self.inertia:drive(viewYaw, viewPitch, vx, vy, vz, s, dt)

	local stress = self.stress
	local rx, ry, rz = self.breath:evaluate(s, stress, dt, self.time)
	local hx, hy, hz = self.heart:evaluate(s, stress, dt)
	local tx, ty, tz = self.shake:evaluate(s, stress, dt)
	local dx, dy, dz = self.drift:evaluate(s, stress, dt)
	local ix, iy, iz = self.inertia:evaluate(s, stress, dt)

	local global = self:_globalMultiplier(dt)
	-- Handling scales the sources the grip governs; inertia already carries mass.
	local handling = s.handlingFactor
	local px = ((rx + hx + tx) * handling + dx + ix) * global
	local py = ((ry + hy + ty) * handling + dy + iy) * global
	local pz = ((rz + hz + tz) * handling + dz + iz) * global

	-- The camera takes a configurable share of each source.
	local cx, cy, cz = 0, 0, 0
	local cc = c.CameraCoupling
	if cc > 0 then
		local k = cc * global
		local function share(r, h, t, d, i)
			return (r * c.CameraRespShare * handling
				+ h * c.CameraHeartShare * handling
				+ t * c.CameraTremorShare * handling
				+ d * c.CameraDriftShare
				+ i * c.CameraInertiaShare) * k
		end
		cx = share(rx, hx, tx, dx, ix)
		cy = share(ry, hy, ty, dy, iy)
		cz = share(rz, hz, tz, dz, iz)
	end

	-- One low-pass across the summed output keeps the sources in phase.
	local cutoff = c.OutputSmoothing
	local hands, camera = self.hands, self.camera
	hands[1], hands[2], hands[3] = self.handsFilter:filterVector(px, py, pz, dt, cutoff)
	camera[1], camera[2], camera[3] = self.cameraFilter:filterVector(cx, cy, cz, dt, cutoff)
	return hands, camera
end

return SwayDirector
