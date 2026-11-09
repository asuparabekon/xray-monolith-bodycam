-- Default values of SPT-SWAY src/SwayConfig.cs (the entries the sway model reads).
-- SPT-SWAY: Copyright (c) 2026 Savant, MIT License (see roblox/THIRD-PARTY-NOTICES.md).
-- Names keep the C# config entries so the original documentation applies.

local SwayConfig = {}

function SwayConfig.default()
	return {
		-- General
		MasterIntensity = 1,
		AdsMaster = 1,
		HipMaster = 1,
		GlobalTimeScale = 1,
		OutputSmoothing = 12, -- Hz
		-- Respiration
		RespEnabled = true,
		RespAmplitude = 1,
		RespRateRest = 14, -- breaths per minute
		RespRateExhausted = 32,
		RespInhaleFraction = 0.35,
		RespExhaleFraction = 0.45,
		RespPauseFraction = 0.20,
		RespWaveformSharpness = 1,
		RespPitch = 1,
		RespYaw = 0.38,
		RespRoll = 0.22,
		RespAds = 1,
		RespHip = 1,
		RespHoldSuppression = 0.12,
		RespHoldReboundGain = 1.8,
		RespHoldReboundDecay = 0.45,
		RespOxygenInfluence = 1,
		RespExertionGain = 1,
		RespIrregularity = 0.15,
		-- Cardiac
		HeartEnabled = true,
		HeartAmplitude = 1,
		HeartRateRest = 65,
		HeartRateMax = 165,
		HeartRateRise = 0.9,
		HeartRateFall = 0.25,
		HeartSystolicSharpness = 4.5,
		HeartDicroticStrength = 0.35,
		HeartDicroticDelay = 0.34,
		HeartPitch = 1,
		HeartYaw = 0.32,
		HeartRoll = 0.14,
		HeartAds = 1,
		HeartHip = 1,
		HeartExertionGain = 1,
		-- Tremor
		TremorEnabled = true,
		TremorAmplitude = 1,
		TremorFrequency = 9.5,
		TremorFrequencySpread = 0.35,
		TremorOctaves = 2,
		TremorFatigueGain = 1,
		TremorLoadGain = 1.2,
		TremorPitch = 1,
		TremorYaw = 0.95,
		TremorRoll = 0.45,
		TremorAds = 1,
		TremorHip = 1,
		-- Postural drift
		DriftEnabled = true,
		DriftAmplitude = 1,
		DriftFrequency = 0.28,
		DriftPitch = 0.7,
		DriftYaw = 1,
		DriftRoll = 0.3,
		DriftAds = 1,
		DriftHip = 1,
		DriftFatigueGain = 1,
		DriftMoveGain = 1.5,
		-- Inertia
		InertiaEnabled = true,
		InertiaStrength = 1,
		InertiaFrequency = 2.6, -- Hz
		InertiaDamping = 0.62,
		InertiaMassCoupling = 1,
		InertiaRollCoupling = 0.45,
		InertiaMaxDeflection = 4.5, -- degrees
		InertiaAds = 0.5,
		InertiaHip = 1.2,
		InertiaMoveGain = 1,
		InertiaTurnClamp = 900, -- degrees per second
		InertiaInputSmoothing = 9, -- Hz
		-- Weapon
		WeightReference = 3.6, -- kg
		-- Injury
		InjuryEnabled = true,
		TremorInjuryGain = 2.2,
		-- Fatigue (global multiplier)
		FatigueEnabled = true,
		ExhaustedMultiplier = 1.9,
		HoldFatigueRate = 0.09, -- per second while aiming
		HoldFatigueDecay = 0.35,
		HoldFatigueMax = 0.8,
		ArmFractureMultiplier = 2.4,
		ArmBlackedMultiplier = 1.7,
		-- Shot
		ShotEnabled = true,
		ShotGain = 0.12,
		ShotDecay = 1.1,
		ShotMaxAccumulation = 0.9,
		ShotHeartRateKick = 1.5, -- BPM per shot
		-- Camera coupling
		CameraCoupling = 0.10,
		CameraRespShare = 0.75,
		CameraHeartShare = 0.45,
		CameraTremorShare = 0.04,
		CameraDriftShare = 0.45,
		CameraInertiaShare = 0.12,
		-- Stress and panic
		StressEnabled = true,
		StressHitGain = 0.55,
		StressNearMissGain = 0.35,
		StressNearMissRadius = 6, -- metres
		StressBlastGain = 0.70,
		StressKillGain = 0.30,
		StressOwnShotGain = 0.015,
		StressDecay = 0.06,
		StressAdrenalineDecay = 0.35,
		StressHoldSeconds = 8,
		StressWoundedFloor = 0.5,
		StressHeartRate = 55,
		StressBreathRate = 0.80,
		StressBreathRagged = 3,
		StressTremor = 1.5,
		StressHoldPenalty = 0.70,
		PanicThreshold = 0.60,
	}
end

-- Calibration identities (src/Runtime/Calibration.cs): base, ADS scale and exertion
-- gain each source starts from before the in-game solve. The solve fits EFT's
-- weapon springs and has no Roblox counterpart, so the identities are used.
SwayConfig.Calibration = table.freeze({
	Resp = table.freeze({ Base = 1.15, AdsScale = 0.7 / 1.15, ExertionGain = 0.9 }),
	Heart = table.freeze({ Base = 0.4, AdsScale = 1.25 / 0.5, ExertionGain = 1.6 }),
	Tremor = table.freeze({ Base = 0.3375, AdsScale = 1.05 / 0.75, ExertionGain = 2.2 }),
	Drift = table.freeze({ Base = 0.7, AdsScale = 0.85, ExertionGain = 0.8 }),
})

return SwayConfig
