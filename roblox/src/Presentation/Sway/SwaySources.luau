-- Port of SPT-SWAY src/Runtime/SwaySources.cs: the physical sources of weapon sway.
-- SPT-SWAY: Copyright (c) 2026 Savant, MIT License (see roblox/THIRD-PARTY-NOTICES.md).
--
-- Each source produces degrees of weapon rotation as (pitch, yaw, roll) in the
-- source's convention (Unity: pitch positive = muzzle down, yaw positive = right,
-- roll about the barrel) and knows nothing about the others. SwayDirector mixes them;
-- WeaponPresenter converts the result to Roblox.
--
-- The shooter state `s` the sources read:
--   exertion 0..1, oxygen 0..1, holdingBreath, isAiming, weightKg, moveSpeed (m/s),
--   armFractured, armBlacked, neuroTremor
-- `time` is the running clock in seconds (Unity Time.time in the source).

local SwayNoise = require(script.Parent.SwayNoise)
local SwayConfig = require(script.Parent.SwayConfig)

local Cal = SwayConfig.Calibration
local signed, fractal = SwayNoise.signed, SwayNoise.fractal

local function clamp01(x: number): number
	return if x < 0 then 0 elseif x > 1 then 1 else x
end

-- Mathf.Lerp: t clamped to [0, 1].
local function lerp(a: number, b: number, t: number): number
	return a + (b - a) * clamp01(t)
end

local function frac(v: number): number
	return v - math.floor(v)
end

-- Mathf.DeltaAngle: shortest signed difference in degrees.
local function deltaAngle(current: number, target: number): number
	local d = (target - current) % 360
	if d > 180 then
		d -= 360
	end
	return d
end

local SwaySources = {}
SwaySources.deltaAngle = deltaAngle

---------------------------------------------------------------------------
-- DcBlocker: removes the DC offset from a signal so a source can never drag the
-- point of aim off centre over time.
---------------------------------------------------------------------------

local DcBlocker = {}
DcBlocker.__index = DcBlocker
SwaySources.DcBlocker = DcBlocker

function DcBlocker.new()
	return setmetatable({ mean = 0, primed = false }, DcBlocker)
end

function DcBlocker:track(value: number, dt: number, timeConstant: number): number
	if not self.primed then
		self.mean, self.primed = value, true
	end
	local k = 1 - math.exp(-dt / math.max(0.01, timeConstant))
	self.mean += (value - self.mean) * k
	return self.mean
end

function DcBlocker:apply(value: number, dt: number, timeConstant: number): number
	return value - self:track(value, dt, timeConstant)
end

function DcBlocker:reset()
	self.mean, self.primed = 0, false
end

---------------------------------------------------------------------------
-- Respiration: the slowest and largest source. A real breath rises quickly, falls
-- more slowly, then sits still for a moment at the bottom: the window you fire in.
---------------------------------------------------------------------------

local Respiration = {}
Respiration.__index = Respiration
SwaySources.Respiration = Respiration

function Respiration.new(config)
	return setmetatable({
		config = config,
		phase = 0,
		cycleJitter = 1,
		holdBlend = 0,
		rebound = 0,
		wasHolding = false,
		dc = DcBlocker.new(),
		rateBpm = 0,
	}, Respiration)
end

function Respiration:waveform(p: number): number
	local c = self.config
	local ti = math.max(0.01, c.RespInhaleFraction)
	local te = math.max(0.01, c.RespExhaleFraction)
	local tp = math.max(0, c.RespPauseFraction)
	local sum = ti + te + tp
	ti /= sum
	te /= sum
	local shaped
	if p < ti then
		-- Inhale: brisk, muscular, accelerating out of the pause.
		local u = p / ti
		shaped = u * u * (3 - 2 * u)
	elseif p < ti + te then
		-- Exhale: passive elastic recoil, starts fast and trails off.
		local u = (p - ti) / te
		shaped = 1 - (1 - math.exp(-3.2 * u)) / (1 - math.exp(-3.2))
	else
		shaped = 0
	end
	local sine = 0.5 - 0.5 * math.cos(p * 2 * math.pi)
	local k = c.RespWaveformSharpness
	if k <= 1 then
		return lerp(sine, shaped, k)
	end
	-- Past 1 the shape gets pushed further from the sine, exaggerating the pause.
	local extra = clamp01(k - 1)
	return lerp(shaped, shaped ^ (1 + extra * 1.5), extra)
end

function Respiration:evaluate(s, stress, dt: number, time: number): (number, number, number)
	local c = self.config
	if not c.RespEnabled then
		return 0, 0, 0
	end
	local oxyDebt = clamp01((1 - s.oxygen) * c.RespOxygenInfluence)
	local drive = clamp01(math.max(s.exertion, oxyDebt))
	-- Exertion sets the baseline; the stress model multiplies it.
	self.rateBpm = lerp(c.RespRateRest, c.RespRateExhausted, drive) * self.cycleJitter * stress:breathRateScale()

	local prev = self.phase
	self.phase += (self.rateBpm / 60) * dt * c.GlobalTimeScale
	if self.phase >= 1 then
		self.phase -= math.floor(self.phase)
		-- Re-roll the jitter once per breath, so breaths differ from each other.
		local irr = c.RespIrregularity * stress:breathIrregularityScale()
		self.cycleJitter = 1 + signed(prev * 13.37 + time * 0.1, 4801) * irr * 0.35
	end

	-- Yaw and roll lag pitch by a fraction of a breath: the motion is an ellipse.
	local wPitch = self:waveform(self.phase)
	local wYaw = self:waveform(frac(self.phase + 0.09))
	local wRoll = self:waveform(frac(self.phase + 0.17))
	-- One shared mean keeps the axes in step while each is centred.
	local mean = self.dc:track(wPitch, dt, 6)
	wPitch -= mean
	wYaw -= mean
	wRoll -= mean

	-- Breath hold: very quiet, then pays for it when you let go.
	local holdQuality = stress:breathHoldQuality()
	local holding = s.holdingBreath
	local holdTarget = if holding then lerp(1, c.RespHoldSuppression, holdQuality) else 1
	self.holdBlend = lerp(self.holdBlend, holdTarget, clamp01(dt * 8))
	if self.wasHolding and not holding then
		-- The worse the hold was, the harder the catch-up breath after it.
		self.rebound = (c.RespHoldReboundGain - 1) * (2 - holdQuality)
	end
	self.wasHolding = holding
	self.rebound = math.max(0, self.rebound - dt * c.RespHoldReboundDecay)

	local cal = Cal.Resp
	local amp = c.RespAmplitude
		* cal.Base
		* self.holdBlend
		* (1 + self.rebound)
		* (1 + drive * c.RespExertionGain * cal.ExertionGain)
		* 0.55 -- degrees at base 1
	amp *= if s.isAiming then c.RespAds * cal.AdsScale else c.RespHip
	return wPitch * c.RespPitch * amp, wYaw * c.RespYaw * amp, wRoll * c.RespRoll * amp
end

function Respiration:reset()
	self.phase, self.holdBlend, self.rebound, self.wasHolding = 0, 1, 0, false
	self.dc:reset()
end

---------------------------------------------------------------------------
-- Cardiac: the heartbeat through the chest and support arm. A sharp systolic spike
-- followed by a smaller dicrotic bump.
---------------------------------------------------------------------------

local Cardiac = {}
Cardiac.__index = Cardiac
SwaySources.Cardiac = Cardiac

function Cardiac.new(config)
	return setmetatable({ config = config, phase = 0, bpm = 65, shotKick = 0, dc = DcBlocker.new() }, Cardiac)
end

function Cardiac:addShotKick(bpm: number)
	self.shotKick = math.min(self.shotKick + bpm, 60)
end

function Cardiac:beat(p: number): number
	local c = self.config
	local systoleWidth = 0.30
	local k = c.HeartSystolicSharpness
	local systole = 0
	if p < systoleWidth then
		systole = math.sin(math.pi * p / systoleWidth) ^ k
	end
	local d = c.HeartDicroticDelay
	local dw = systoleWidth * 0.8
	local dicrotic = 0
	if p >= d and p < d + dw then
		dicrotic = math.sin(math.pi * (p - d) / dw) ^ (k * 1.4) * c.HeartDicroticStrength
	end
	return systole + dicrotic
end

function Cardiac:evaluate(s, stress, dt: number): (number, number, number)
	local c = self.config
	if not c.HeartEnabled then
		return 0, 0, 0
	end
	local target = lerp(c.HeartRateRest, c.HeartRateMax, s.exertion)
		+ self.shotKick
		-- Fear raises the pulse without the body having moved.
		+ stress:heartRateBonus()
	-- The heart climbs faster than it comes down.
	local speed = if target > self.bpm then c.HeartRateRise else c.HeartRateFall
	self.bpm = lerp(self.bpm, target, clamp01(dt * speed * 2))
	self.shotKick = math.max(0, self.shotKick - dt * 6)

	self.phase += (self.bpm / 60) * dt * c.GlobalTimeScale
	self.phase -= math.floor(self.phase)
	local beat = self.dc:apply(self:beat(self.phase), dt, 3)

	local cal = Cal.Heart
	local exertionGain = 1 + s.exertion * c.HeartExertionGain * cal.ExertionGain
	local amp = c.HeartAmplitude * cal.Base * exertionGain * 0.085
	amp *= if s.isAiming then c.HeartAds * cal.AdsScale else c.HeartHip
	-- One push, not three wobbles: all axes share the waveform.
	return beat * c.HeartPitch * amp, beat * c.HeartYaw * amp, beat * c.HeartRoll * amp
end

function Cardiac:reset()
	self.phase, self.bpm, self.shotKick = 0, self.config.HeartRateRest, 0
	self.dc:reset()
end

---------------------------------------------------------------------------
-- Tremor: the 8-12 Hz physiological shimmer of every human hand.
---------------------------------------------------------------------------

local Tremor = {}
Tremor.__index = Tremor
SwaySources.Tremor = Tremor

function Tremor.new(config)
	return setmetatable({ config = config, t = 0 }, Tremor)
end

function Tremor:evaluate(s, stress, dt: number): (number, number, number)
	local c = self.config
	if not c.TremorEnabled then
		return 0, 0, 0
	end
	self.t += dt * c.GlobalTimeScale
	local f, oct, spread = c.TremorFrequency, c.TremorOctaves, c.TremorFrequencySpread
	local x = fractal(self.t * f, 101, oct, spread)
	local y = fractal(self.t * f, 227, oct, spread)
	local z = fractal(self.t * f, 353, oct, spread)

	local cal = Cal.Tremor
	local fatigue = 1 + s.exertion * c.TremorFatigueGain * cal.ExertionGain
	local refW = math.max(0.1, c.WeightReference)
	local load = 1 + math.max(0, (s.weightKg - refW) / refW) * c.TremorLoadGain
	local injury = 1
	if c.InjuryEnabled then
		local g = c.TremorInjuryGain
		if s.armFractured then
			injury += g * 0.6
		end
		if s.armBlacked then
			injury += g * 0.4
		end
		if s.neuroTremor then
			injury += g * 0.5
		end
	end
	local amp = c.TremorAmplitude * cal.Base * fatigue * load * injury * 0.06
	amp *= stress:tremorScale() -- adrenaline shake, fast and fine
	amp *= if s.isAiming then c.TremorAds * cal.AdsScale else c.TremorHip
	return x * c.TremorPitch * amp, y * c.TremorYaw * amp, z * c.TremorRoll * amp
end

function Tremor:reset()
	self.t = 0
end

---------------------------------------------------------------------------
-- Postural drift: the slow wander of a standing body balancing, well under 1 Hz.
---------------------------------------------------------------------------

local PosturalDrift = {}
PosturalDrift.__index = PosturalDrift
SwaySources.PosturalDrift = PosturalDrift

function PosturalDrift.new(config)
	return setmetatable({ config = config, t = 0 }, PosturalDrift)
end

function PosturalDrift:evaluate(s, stress, dt: number): (number, number, number)
	local c = self.config
	if not c.DriftEnabled then
		return 0, 0, 0
	end
	self.t += dt * c.GlobalTimeScale
	local f = c.DriftFrequency
	local x = fractal(self.t * f, 1901, 2, 0.4)
	local y = fractal(self.t * f, 2903, 2, 0.4)
	local z = fractal(self.t * f, 3907, 2, 0.4)
	-- Walking is a balance problem, so it feeds the same source.
	local move = 1 + clamp01(s.moveSpeed / 4) * c.DriftMoveGain
	local cal = Cal.Drift
	local fatigue = 1 + s.exertion * c.DriftFatigueGain * cal.ExertionGain
	local amp = c.DriftAmplitude * cal.Base * move * fatigue * 0.22
	amp *= if s.isAiming then c.DriftAds * cal.AdsScale else c.DriftHip
	return x * c.DriftPitch * amp, y * c.DriftYaw * amp, z * c.DriftRoll * amp
end

function PosturalDrift:reset()
	self.t = 0
end

---------------------------------------------------------------------------
-- Weapon inertia: a mass on two compliant arms, a damped harmonic oscillator
-- driven by the turn rate, so it trails a turn going in and overshoots coming out.
---------------------------------------------------------------------------

local WeaponInertia = {}
WeaponInertia.__index = WeaponInertia
SwaySources.WeaponInertia = WeaponInertia

function WeaponInertia.new(config)
	local self = setmetatable({ config = config }, WeaponInertia)
	self:reset()
	return self
end

-- yaw, pitch: the view angles in degrees (Unity convention: yaw right, pitch down).
-- vx, vy, vz: the player's velocity in m/s (x right, y up, z forward).
function WeaponInertia:drive(yaw: number, pitch: number, vx: number, vy: number, vz: number, s, dt: number)
	local c = self.config
	if not self.primed then
		self.lastYaw, self.lastPitch = yaw, pitch
		self.lastVx, self.lastVy, self.lastVz = vx, vy, vz
		self.primed = true
		return
	end
	local clamp = c.InertiaTurnClamp
	local safeDt = math.max(dt, 0.0001)
	local dYaw = deltaAngle(self.lastYaw, yaw) / safeDt
	local dPitch = deltaAngle(self.lastPitch, pitch) / safeDt
	self.lastYaw, self.lastPitch = yaw, pitch
	dYaw = math.clamp(dYaw, -clamp, clamp)
	dPitch = math.clamp(dPitch, -clamp, clamp)

	-- The turn rate inherits frame jitter; filter it so a still weapon stays still.
	local inSmooth = c.InertiaInputSmoothing
	dYaw = self.yawRate:filter(dYaw, dt, inSmooth)
	dPitch = self.pitchRate:filter(dPitch, dt, inSmooth)

	local ax, ay, az = (vx - self.lastVx) / safeDt, (vy - self.lastVy) / safeDt, (vz - self.lastVz) / safeDt
	self.lastVx, self.lastVy, self.lastVz = vx, vy, vz
	local accel = math.sqrt(ax * ax + ay * ay + az * az)
	if accel > 40 then
		ax, ay, az = ax * 40 / accel, ay * 40 / accel, az * 40 / accel
	end

	-- Heavier weapons take a larger impulse from the same turn.
	local refW = math.max(0.1, c.WeightReference)
	local mass = 1 + math.max(-0.6, (s.weightKg - refW) / refW) * c.InertiaMassCoupling
	local gain = c.InertiaStrength * mass * 0.0016
	local moveGain = c.InertiaMoveGain * 0.004
	self.tx = -dPitch * gain + ay * moveGain
	self.ty = -dYaw * gain
	self.tz = -dYaw * gain * c.InertiaRollCoupling - ax * moveGain
end

function WeaponInertia:evaluate(s, stress, dt: number): (number, number, number)
	local c = self.config
	if not c.InertiaEnabled then
		self.x, self.y, self.z, self.vx, self.vy, self.vz = 0, 0, 0, 0, 0, 0
		return 0, 0, 0
	end
	-- A heavy weapon on the same arms lowers the natural frequency.
	local refW = math.max(0.1, c.WeightReference)
	local massRatio = math.clamp(s.weightKg / refW, 0.35, 3.5)
	local f = c.InertiaFrequency / math.sqrt(massRatio)
	local omega = 2 * math.pi * math.max(0.05, f)
	local zeta = c.InertiaDamping
	-- Semi-implicit integration with a substep cap so a frame spike cannot explode it.
	local steps = math.clamp(math.ceil(dt * omega / 0.4), 1, 8)
	local h = dt / steps
	local k, d = omega * omega, 2 * zeta * omega
	for _ = 1, steps do
		self.vx += ((self.tx - self.x) * k - self.vx * d) * h
		self.vy += ((self.ty - self.y) * k - self.vy * d) * h
		self.vz += ((self.tz - self.z) * k - self.vz * d) * h
		self.x += self.vx * h
		self.y += self.vy * h
		self.z += self.vz * h
	end
	local limit = c.InertiaMaxDeflection
	local m = math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
	if m > limit then
		self.x, self.y, self.z = self.x * limit / m, self.y * limit / m, self.z * limit / m
	end
	local ads = if s.isAiming then c.InertiaAds else c.InertiaHip
	return self.x * ads, self.y * ads, self.z * ads
end

function WeaponInertia:reset()
	self.x, self.y, self.z, self.vx, self.vy, self.vz = 0, 0, 0, 0, 0, 0
	self.tx, self.ty, self.tz = 0, 0, 0
	self.primed = false
	self.yawRate = SwayNoise.OnePole.new()
	self.pitchRate = SwayNoise.OnePole.new()
end

return SwaySources
