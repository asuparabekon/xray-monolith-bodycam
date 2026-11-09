-- Weapon handling for the local player, wired to the bodycam and FPSys:
--   * fire rate, fire modes, bursts, magazine and timed reload/equip (Weapon.luau)
--   * camera recoil turned into bodycam aim kicks (RecoilPattern.luau)
--   * the bodycam fire impulse, the FPSys camera shake, ADS blend, FOV zoom and look
--     sensitivity per scope, weapon switching with equip/unequip times, lean
--   * sprinting blocks aiming and firing; starting a sprint releases both
--
-- Hit detection is the game's: onFire(fireCFrame, weapon) gets the aim (plus lean),
-- which is where shots go. The view lags and shakes; shots do not.

local RunService = game:GetService("RunService")
local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

local Weapon = require(script.Parent.Weapon)
local RecoilPattern = require(script.Parent.RecoilPattern)

local WeaponController = {}
WeaponController.__index = WeaponController

-- options:
--   controller  BodycamController (required)
--   fpsys       FPSys client (optional: shake, lean)
--   presenter   Presentation WeaponPresenter (optional: shots follow the weapon's free
--               aim and sway, a wall in front of the barrel blocks firing, stress)
--   movement    MovementController (optional: sprint rules, stance scale)
--   weapons     list of Weapon configs (default: one Weapon.defaultConfig())
--   onFire      function(fireCFrame: CFrame, weapon) per shot
--   onEvent     function(name, weapon): Equipped, Unequipped, Reloaded, ReloadStarted,
--               Empty, FireModeChanged, ScopeChanged
--   fovSpeed    1/s FOV easing (default 12)
--   fovModifier function(target, weapon, aiming) -> number: changes the FOV target
--               (e.g. Headcam:fovFor for the wide helmet lens)
--   random      function() -> [0, 1) for recoil (default math.random)
function WeaponController.new(options)
	assert(options and options.controller, "WeaponController needs a BodycamController")
	local self = setmetatable({}, WeaponController)
	self.options = options
	self.controller = options.controller
	self.weapons = {}
	for _, config in options.weapons or { Weapon.defaultConfig() } do
		table.insert(self.weapons, Weapon.new(config))
	end
	self.index = 1
	self.pendingIndex = nil
	self.recoil = RecoilPattern.new(self.weapons[1].config.recoil, options.random)
	self.aimHeld = false
	self.aiming = false
	self.lean = 0
	self.clock = options.clock or os.clock
	self.baseSensitivity = nil
	return self
end

function WeaponController:current()
	return self.weapons[self.index]
end

function WeaponController:_emit(name: string)
	if self.options.onEvent then
		task.spawn(self.options.onEvent, name, self:current())
	end
end

function WeaponController:_sprinting(): boolean
	local movement = self.options.movement
	return movement ~= nil and movement:isSprinting()
end

function WeaponController:_applyProfile()
	local profile = self:current().config.profile
	if profile then
		self.controller:setWeaponProfile(profile.pos, profile.rot)
	else
		self.controller:clearWeaponProfile()
	end
end

function WeaponController:start()
	self.baseSensitivity = UserInputService.MouseDeltaSensitivity
	self:current():equip(self.clock())
	self:_applyProfile()
	local movement = self.options.movement
	if movement then
		movement:onEvent(function(name)
			if name == "SprintStarted" then
				self:releaseTrigger()
				self:_setAiming(false)
			elseif name == "SprintEnded" and self.aimHeld then
				self:_setAiming(true)
			end
		end)
		-- Reloading or switching blocks sprint, prone and slide starts.
		movement.machine.canAct = function()
			return not self:current():isBusy()
		end
	end
	self.bindName = "FPSWeapons" .. tostring(self)
	RunService:BindToRenderStep(self.bindName, Enum.RenderPriority.Input.Value + 2, function(dt)
		self:_step(dt)
	end)
end

function WeaponController:stop()
	if self.bindName then
		RunService:UnbindFromRenderStep(self.bindName)
		self.bindName = nil
	end
	if self.baseSensitivity then
		UserInputService.MouseDeltaSensitivity = self.baseSensitivity
	end
	self.controller:setAiming(false)
end

---------------------------------------------------------------------------
-- Input-facing API.
---------------------------------------------------------------------------

function WeaponController:pullTrigger()
	local weapon = self:current()
	local presenter = self.options.presenter
	if self:_sprinting() or weapon:isBusy() or (presenter and presenter:blocksFire()) then
		return
	end
	weapon:pullTrigger()
end

function WeaponController:releaseTrigger()
	self:current():releaseTrigger()
	self.recoil:onFireEnd()
end

function WeaponController:_setAiming(active: boolean)
	if active and (self:_sprinting() or self:current():isBusy()) then
		active = false
	end
	if self.aiming == active then
		return
	end
	self.aiming = active
	self.controller:setAiming(active)
end

-- Aim key held or released. Aiming resumes after a sprint or reload while held.
function WeaponController:setAim(held: boolean)
	self.aimHeld = held
	self:_setAiming(held)
end

function WeaponController:reload(): boolean
	if self:_sprinting() then
		return false
	end
	if self:current():reload(self.clock()) then
		self.recoil:onFireEnd()
		self:_setAiming(false)
		self:_emit("ReloadStarted")
		return true
	end
	return false
end

function WeaponController:cycleFireMode(): string
	local mode = self:current():cycleFireMode()
	self:_emit("FireModeChanged")
	return mode
end

function WeaponController:cycleScope()
	if not self.aiming then
		return nil
	end
	local scope = self:current():cycleScope()
	self:_emit("ScopeChanged")
	return scope
end

-- Switches weapon: the current one unequips, then the new one equips.
function WeaponController:select(index: number): boolean
	if index == self.index or not self.weapons[index] or self.pendingIndex then
		return false
	end
	if self:current():isBusy() and self:current().action ~= Weapon.Action.Equip then
		return false
	end
	self:releaseTrigger()
	self:_setAiming(false)
	self:current():unequip(self.clock())
	self.pendingIndex = index
	return true
end

function WeaponController:selectNext(): boolean
	return self:select(self.index % #self.weapons + 1)
end

-- Lean input -1 (left) .. 1 (right). FPSys reads it through getLean().
function WeaponController:setLean(value: number)
	self.lean = math.clamp(value, -1, 1)
end

function WeaponController:getLean(): number
	return self.lean
end

-- Where shots go: the aim, moved sideways and rolled by the lean, then turned by the
-- weapon's free aim and hand sway (so shots leave along the barrel, not screen centre).
function WeaponController:getFireCFrame(): CFrame?
	local aim = self.controller:getAimCFrame()
	if not aim then
		return nil
	end
	local fpsys = self.options.fpsys
	if fpsys then
		local offset, roll = fpsys:getLean()
		aim = aim * CFrame.new(offset, 0, 0) * CFrame.Angles(0, 0, roll)
	end
	-- The crouch drop moves the eye too.
	local movement = self.options.movement
	if movement and movement.machine.drop > 1e-3 then
		aim = aim * CFrame.new(0, -movement.machine.drop, 0)
	end
	local presenter = self.options.presenter
	if presenter then
		aim = aim * presenter:getFireRotation()
	end
	return aim
end

---------------------------------------------------------------------------
-- Frame.
---------------------------------------------------------------------------

function WeaponController:_stanceScale(): number
	local movement = self.options.movement
	if not movement then
		return 1
	end
	if movement.machine:isProne() then
		return 0.5
	elseif movement.machine:isCrouching() then
		return 0.75
	end
	return 1
end

function WeaponController:_step(dt: number)
	local now = self.clock()
	local weapon = self:current()
	local presenter = self.options.presenter
	-- A barrel pressed into a wall cannot fire.
	if presenter and presenter:blocksFire() and weapon.trigger then
		weapon:releaseTrigger()
	end
	local shots, sprayEnded, event = weapon:update(now)

	if event == "Unequipped" and self.pendingIndex then
		self.index = self.pendingIndex
		self.pendingIndex = nil
		self:_emit("Unequipped")
		weapon = self:current()
		self.recoil:setSettings(weapon.config.recoil)
		self:_applyProfile()
		weapon:equip(now)
	elseif event then
		self:_emit(event)
		if (event == "Reloaded" or event == "Equipped") and self.aimHeld then
			self:_setAiming(true)
		end
	end

	if shots > 0 then
		local stance = self:_stanceScale()
		local config = weapon.config
		for _ = 1, shots do
			self.controller:notifyShot(config.power * stance)
			if presenter then
				presenter:onShot()
			end
			self.recoil:onShot(stance, self.aiming)
			if self.options.fpsys then
				self.options.fpsys:shake(config.shake * stance * (if self.aiming then 0.5 else 1))
			end
			if self.options.onFire then
				local fire = self:getFireCFrame()
				if fire then
					task.spawn(self.options.onFire, fire, weapon)
				end
			end
		end
	end
	if sprayEnded then
		self.recoil:onFireEnd()
	end

	local yaw, pitch = self.recoil:update(dt)
	if yaw ~= 0 or pitch ~= 0 then
		self.controller:kickAim(yaw, pitch)
	end

	-- FOV and look sensitivity follow the scope while aiming.
	local camera = Workspace.CurrentCamera
	if camera then
		local target = weapon:fov(self.aiming)
		if self.options.fovModifier then
			target = self.options.fovModifier(target, weapon, self.aiming) or target
		end
		local rate = self.options.fovSpeed or 12
		camera.FieldOfView += (target - camera.FieldOfView) * (1 - math.exp(-rate * dt))
	end
	if self.baseSensitivity then
		UserInputService.MouseDeltaSensitivity = self.baseSensitivity * weapon:sensitivity(self.aiming)
	end
end

return WeaponController
