-- One weapon's firing state: fire rate, fire modes (semi, burst, auto), magazine,
-- reload / equip timing and scopes (FOV zoom and look sensitivity). Pure logic with
-- the time passed in, so it can be tested and driven by any loop.
--
-- Written for this project after the weapon feature set of the Kinemation FPS
-- Animation Framework demo (fire rate, fire modes, bursts, scope cycling, timed
-- actions); no Kinemation code is used.

local RecoilPattern = require(script.Parent.RecoilPattern)

local Weapon = {}
Weapon.__index = Weapon

Weapon.Action = table.freeze({ None = "None", Reload = "Reload", Equip = "Equip", Unequip = "Unequip" })
local Action = Weapon.Action

function Weapon.defaultConfig()
	return {
		name = "Rifle",
		rpm = 600,
		modes = { "Auto", "Semi", "Burst" },
		burstLength = 3,
		magazine = 30,
		reloadTime = 2.2,
		equipTime = 0.45,
		unequipTime = 0.3,
		fov = 70, -- hip field of view
		scopes = {
			{ name = "Iron sights", zoom = 0.85, sensitivity = 0.85 },
		},
		power = 1, -- bodycam shot power (fire impulse)
		weightKg = 3.6, -- loaded weight: heavier weapons sway and trail more (SPT-SWAY)
		shake = 1, -- FPSys camera shake scale
		recoil = RecoilPattern.defaultSettings(),
		-- Bodycam weapon profile: hip offset in studs (camera space: x right, y up, z back) and degrees.
		profile = nil,
		-- Name of a viewmodel rig (e.g. under ReplicatedStorage.Viewmodels) for this weapon.
		viewmodel = nil,
	}
end

function Weapon.new(config)
	local self = setmetatable({}, Weapon)
	self.config = config or Weapon.defaultConfig()
	self.modeIndex = 1
	self.scopeIndex = 1
	self.ammo = self.config.magazine
	self.trigger = false
	self.nextShot = -math.huge
	self.burstLeft = 0
	self.semiArmed = false
	self.action = Action.None
	self.actionEnds = 0
	return self
end

function Weapon:mode(): string
	return self.config.modes[self.modeIndex]
end

function Weapon:scope()
	return self.config.scopes[self.scopeIndex]
end

function Weapon:interval(): number
	return 60 / self.config.rpm
end

function Weapon:isBusy(): boolean
	return self.action ~= Action.None
end

function Weapon:cycleFireMode(): string
	self.modeIndex = self.modeIndex % #self.config.modes + 1
	self.burstLeft = 0
	return self:mode()
end

function Weapon:cycleScope()
	if #self.config.scopes > 0 then
		self.scopeIndex = self.scopeIndex % #self.config.scopes + 1
	end
	return self:scope()
end

-- Field of view and look sensitivity multiplier for the current scope.
function Weapon:fov(aiming: boolean): number
	local scope = self:scope()
	return if aiming and scope then self.config.fov * scope.zoom else self.config.fov
end

function Weapon:sensitivity(aiming: boolean): number
	local scope = self:scope()
	return if aiming and scope then scope.sensitivity else 1
end

function Weapon:_startAction(action: string, now: number, duration: number)
	self.action = action
	self.actionEnds = now + duration
	self.trigger = false
	self.burstLeft = 0
end

function Weapon:equip(now: number)
	self:_startAction(Action.Equip, now, self.config.equipTime)
end

function Weapon:unequip(now: number)
	self:_startAction(Action.Unequip, now, self.config.unequipTime)
end

function Weapon:reload(now: number): boolean
	if self:isBusy() or self.ammo >= self.config.magazine then
		return false
	end
	self:_startAction(Action.Reload, now, self.config.reloadTime)
	return true
end

-- Ignored while reloading or switching: the trigger must be pulled again afterwards.
function Weapon:pullTrigger()
	if self:isBusy() then
		return
	end
	self.trigger = true
	self.semiArmed = true
	if self:mode() == "Burst" and self.burstLeft == 0 then
		self.burstLeft = self.config.burstLength
	end
end

function Weapon:releaseTrigger()
	self.trigger = false
end

-- Advances to `now`. Returns the number of shots fired since the last call and
-- whether a spray ended this call (trigger released, burst done, or empty).
-- Events: "Reloaded", "Equipped", "Unequipped", "Empty" are returned as a third value.
function Weapon:update(now: number): (number, boolean, string?)
	local event = nil
	if self.action ~= Action.None and now >= self.actionEnds then
		if self.action == Action.Reload then
			self.ammo = self.config.magazine
			event = "Reloaded"
		elseif self.action == Action.Equip then
			event = "Equipped"
		else
			event = "Unequipped"
		end
		self.action = Action.None
	end
	if self:isBusy() then
		return 0, false, event
	end

	local interval = self:interval()
	-- A trigger pulled after a pause fires now, not at the stale next-shot time.
	if self.nextShot < now - interval then
		self.nextShot = now
	end
	local shots = 0
	local mode = self:mode()
	local wasFiring = self.firing
	while now >= self.nextShot do
		local wants = (mode == "Auto" and self.trigger)
			or (mode == "Semi" and self.semiArmed)
			or (mode == "Burst" and self.burstLeft > 0)
		if not wants then
			break
		end
		if self.ammo <= 0 then
			self.trigger, self.semiArmed, self.burstLeft = false, false, 0
			event = event or "Empty"
			break
		end
		self.ammo -= 1
		shots += 1
		self.nextShot += interval
		self.semiArmed = false
		if mode == "Burst" then
			self.burstLeft -= 1
		end
	end
	self.firing = (mode == "Auto" and self.trigger) or (mode == "Burst" and self.burstLeft > 0)
	local sprayEnded = (wasFiring or shots > 0) and not self.firing
	return shots, sprayEnded, event
end

return Weapon
