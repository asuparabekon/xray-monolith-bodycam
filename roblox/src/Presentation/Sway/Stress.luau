-- Port of SPT-SWAY src/Runtime/Stress.cs: fear and adrenaline from combat events,
-- feeding pulse, breathing and tremor.
-- SPT-SWAY: Copyright (c) 2026 Savant, MIT License (see roblox/THIRD-PARTY-NOTICES.md).

local Stress = {}
Stress.__index = Stress

local function clamp01(x: number): number
	return if x < 0 then 0 elseif x > 1 then 1 else x
end

function Stress.new(config)
	local self = setmetatable({ config = config }, Stress)
	self:reset()
	return self
end

-- A stress model pinned at a given level, never ticked (for measurements).
function Stress.synthetic(config, level: number)
	local s = Stress.new(config)
	s.arousal = clamp01(level)
	return s
end

function Stress:reset()
	self.arousal = 0 -- sustained fear, 0 calm .. 1 panicked
	self.adrenaline = 0 -- sharp per-event spike on top of arousal
	self.suppression = 0 -- recent incoming fire, decays faster than arousal
	self.sinceThreat = 999
end

-- The combined drive the physiological sources consume, 0..1.
function Stress:level(): number
	return clamp01(self.arousal + self.adrenaline * 0.6)
end

function Stress:panicking(): boolean
	return self:level() >= self.config.PanicThreshold
end

function Stress:_spike(amount: number)
	self.adrenaline = clamp01(self.adrenaline + amount)
	self.arousal = clamp01(self.arousal + amount * 0.55)
	self.sinceThreat = 0
end

-- You were hit. fraction is damage as a share of max health.
function Stress:onDamaged(fraction: number)
	if not self.config.StressEnabled then
		return
	end
	self:_spike(self.config.StressHitGain * clamp01(0.35 + fraction * 2.5))
end

-- A round passed within `metres` of your head.
function Stress:onNearMiss(metres: number)
	local c = self.config
	if not c.StressEnabled then
		return
	end
	local radius = math.max(0.5, c.StressNearMissRadius)
	local close = 1 - clamp01(metres / radius) -- 1 = grazed you
	if close <= 0 then
		return
	end
	self.suppression = clamp01(self.suppression + close * 0.5)
	self:_spike(c.StressNearMissGain * close * close)
end

-- An explosion close enough to rattle you.
function Stress:onConcussed()
	if self.config.StressEnabled then
		self:_spike(self.config.StressBlastGain)
	end
end

-- A kill: a spike, then relief. The threat is gone, so decay may start.
function Stress:onKill()
	if self.config.StressEnabled then
		self.adrenaline = clamp01(self.adrenaline + self.config.StressKillGain)
	end
end

-- Firing your own weapon keys you up a little.
function Stress:onShotFired()
	if self.config.StressEnabled then
		self.adrenaline = clamp01(self.adrenaline + self.config.StressOwnShotGain)
	end
end

-- s.healthFraction: current health over max health.
function Stress:tick(s, dt: number)
	local c = self.config
	if not c.StressEnabled then
		if self.arousal > 0 or self.adrenaline > 0 then
			self:reset()
		end
		return
	end
	self.sinceThreat += dt
	-- Adrenaline always drains, fast.
	self.adrenaline = math.max(0, self.adrenaline - dt * c.StressAdrenalineDecay)
	self.suppression = math.max(0, self.suppression - dt * 0.35)
	-- Arousal holds while the threat is recent, then unwinds slowly.
	if self.sinceThreat > c.StressHoldSeconds then
		self.arousal = math.max(0, self.arousal - dt * c.StressDecay)
	end
	-- Being hurt is itself frightening, and it does not fade while you are hurt.
	local wounded = 1 - clamp01(s.healthFraction or 1)
	if wounded > 0.01 then
		local floor = wounded * c.StressWoundedFloor
		if self.arousal < floor then
			self.arousal = math.min(floor, 1)
		end
	end
end

-- Outputs the physiological sources consume.
function Stress:heartRateBonus(): number
	return self:level() * self.config.StressHeartRate
end

function Stress:breathRateScale(): number
	return 1 + self:level() * self.config.StressBreathRate
end

function Stress:breathIrregularityScale(): number
	return 1 + self:level() * self.config.StressBreathRagged
end

function Stress:tremorScale(): number
	return 1 + self:level() * self.config.StressTremor
end

function Stress:breathHoldQuality(): number
	return clamp01(1 - self:level() * self.config.StressHoldPenalty)
end

return Stress
