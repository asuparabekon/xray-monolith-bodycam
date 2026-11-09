-- Connects the ported presentation layers to the bodycam:
--   * SwayDirector (SPT-SWAY): breathing, pulse, tremor, drift and inertia. The hands
--     share turns the viewmodel and the shots; the camera share turns the view.
--   * Deadzone (sptarkov-deadzone): free aim. The weapon leads the view; shots follow
--     the weapon, not the screen centre.
--   * HandheldShake (drei): the small handheld wander of a body-worn camera.
--   * WallPullback: the weapon draws back near walls and cannot fire into them.
-- The exertion model (sprinting tires you, rest recovers) is written for this project;
-- SPT-SWAY reads exertion from EFT's stamina.

local Players = game:GetService("Players")

local SwayDirector = require(script.Parent.Sway.SwayDirector)
local SwayConfig = require(script.Parent.Sway.SwayConfig)
local Deadzone = require(script.Parent.Deadzone)
local HandheldShake = require(script.Parent.HandheldShake)
local WallPullback = require(script.Parent.WallPullback)
local Aim = require(script.Parent.Parent.Bodycam.Aim)

local WeaponPresenter = {}
WeaponPresenter.__index = WeaponPresenter

local RAD = math.pi / 180

-- SPT-SWAY degrees (pitch down, yaw right, roll) as a Roblox rotation.
function WeaponPresenter.swayRotation(pitch: number, yaw: number, roll: number): CFrame
	return CFrame.fromEulerAnglesYXZ(-pitch * RAD, -yaw * RAD, roll * RAD)
end

function WeaponPresenter.defaultExertion()
	return {
		SprintRise = 1 / 9, -- per second to full exertion while sprinting
		WalkTarget = 0.2,
		WalkRate = 1 / 20,
		RestFall = 1 / 14, -- per second back to rested
		ExhaustedAt = 0.98,
	}
end

-- options:
--   controller   BodycamController (required)
--   sway         SwayConfig table (default SwayConfig.default())
--   deadzone     Deadzone settings
--   shake        HandheldShake settings (default bodycamSettings)
--   pullback     WallPullback settings
--   exertion     WeaponPresenter.defaultExertion() shape
--   getWeightKg  function(): number  current weapon mass (default 3.6)
--   isHoldingBreath function(): boolean
--   random       function() -> [0, 1) for the shake noise
function WeaponPresenter.new(options)
	assert(options and options.controller, "WeaponPresenter needs a BodycamController")
	local self = setmetatable({}, WeaponPresenter)
	self.options = options
	self.controller = options.controller
	self.director = SwayDirector.new(options.sway or SwayConfig.default())
	self.deadzone = Deadzone.new(options.deadzone)
	self.shake = HandheldShake.new(options.shake or HandheldShake.bodycamSettings(), options.random)
	self.pullback = WallPullback.new(options.pullback)
	self.exertionSettings = options.exertion or WeaponPresenter.defaultExertion()
	self.handsRotation = CFrame.identity
	self.cameraRotation = CFrame.identity
	self.aimOffset = CFrame.identity
	self.removers = {}
	return self
end

function WeaponPresenter:start()
	table.insert(self.removers, self.controller:addViewModifier(function(view, info)
		self:_update(info)
		return view * self.cameraRotation
	end))
	table.insert(self.removers, self.controller:addViewmodelModifier(function(localCFrame)
		return self:_weaponTransform(localCFrame)
	end))
end

function WeaponPresenter:stop()
	for _, remove in self.removers do
		remove()
	end
	self.removers = {}
end

-- Combat events for the stress model.
function WeaponPresenter:onShot()
	self.director:onShot()
end

function WeaponPresenter:onDamaged(fraction: number)
	self.director.stress:onDamaged(fraction)
end

function WeaponPresenter:onNearMiss(studs: number)
	self.director.stress:onNearMiss(studs / self.controller.studsPerMetre)
end

function WeaponPresenter:onExplosion()
	self.director.stress:onConcussed()
end

function WeaponPresenter:onKill()
	self.director.stress:onKill()
end

function WeaponPresenter:blocksFire(): boolean
	return self.pullback:blocksFire()
end

-- The weapon's turn away from the aim (free aim plus hand sway): where shots go.
function WeaponPresenter:getFireRotation(): CFrame
	return self.aimOffset * self.handsRotation
end

function WeaponPresenter:_updateExertion(info, dt: number)
	local s = self.exertionSettings
	local state = self.director.state
	local e = state.exertion
	if info.sprinting then
		e += s.SprintRise * dt
	elseif info.planarSpeed > 1 then
		e += math.clamp(s.WalkTarget - e, -s.RestFall * dt, s.WalkRate * dt)
	else
		e -= s.RestFall * dt
	end
	state.exertion = math.clamp(e, 0, 1)
	state.exhausted = state.exertion >= s.ExhaustedAt
	state.oxygen = 1 - 0.5 * state.exertion
end

function WeaponPresenter:_update(info)
	local dt = info.dt
	if dt <= 0 then
		return
	end
	local spm = info.studsPerMetre
	local state = self.director.state
	self:_updateExertion(info, dt)
	state.isAiming = info.aiming
	state.moveSpeed = info.planarSpeed / spm
	state.weightKg = if self.options.getWeightKg then self.options.getWeightKg() else 3.6
	state.holdingBreath = if self.options.isHoldingBreath then self.options.isHoldingBreath() else false
	local humanoid = info.humanoid
	state.healthFraction = if humanoid.MaxHealth > 0 then humanoid.Health / humanoid.MaxHealth else 1

	-- Velocity relative to the aim's yaw frame, metres per second.
	local right, forward = Aim.yawFrame(info.yaw)
	local v = info.rootPart.AssemblyLinearVelocity / spm
	local hands, camera = self.director:update(
		dt,
		-info.yaw / RAD,
		-info.pitch / RAD,
		v:Dot(right),
		v.Y,
		v:Dot(forward)
	)
	self.handsRotation = WeaponPresenter.swayRotation(hands[1], hands[2], hands[3])

	local yawOffset, pitchOffset = self.deadzone:update(dt, info.yaw, info.pitch, info.aiming)
	self.aimOffset = CFrame.fromEulerAnglesYXZ(pitchOffset, yawOffset, 0)

	local sy, sp, sr = self.shake:update(dt)
	self.cameraRotation = WeaponPresenter.swayRotation(camera[1], camera[2], camera[3]) * CFrame.fromEulerAnglesYXZ(sp, sy, sr)

	-- Wall check from the eye along where the weapon points.
	local params = RaycastParams.new()
	params.FilterType = Enum.RaycastFilterType.Exclude
	local player = Players.LocalPlayer
	params.FilterDescendantsInstances = { player and player.Character, self.controller.options.viewmodel }
	local direction = (info.aim * self:getFireRotation()).LookVector
	self.pullback:update(dt, info.aim.Position, direction, params)
end

-- Turns the viewmodel (camera-local CFrame) by free aim, hand sway and pull-back,
-- about a pivot the deadzone's distance behind the weapon.
function WeaponPresenter:_weaponTransform(localCFrame: CFrame): CFrame
	local spm = self.controller.studsPerMetre
	local pivot = localCFrame * CFrame.new(0, 0, self.deadzone:pivotMetres() * spm)
	local turned = pivot * self.aimOffset * self.handsRotation * pivot:Inverse() * localCFrame
	return turned * self.pullback:transform()
end

return WeaponPresenter
