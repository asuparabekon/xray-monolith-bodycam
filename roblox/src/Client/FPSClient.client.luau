-- Example client: bodycam + FPSys + FPSController on the local character.
--
-- Keys: WASD move, Left Shift sprint, C crouch, Z prone, Left Ctrl slide (while
-- sprinting), Space jump (stands up from crouch/prone), right mouse aim, left mouse
-- fire, R reload, V fire mode, T next scope (while aiming), 1/2 weapons, Q/E lean,
-- Left Alt hold breath (steadies the sway while aiming), H bodycam / helmet camera,
-- F next player to spectate (after death).
-- Viewmodels: put rigs in ReplicatedStorage.Viewmodels and name one in a weapon
-- config's `viewmodel`; Animation instances in the rig's "Animations" folder load by
-- name (Idle loops; Equip, Reload, Fire play on those events).
-- Replace onFire with your game's hit detection (validate hits on the server).

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

local Bodycam = require(ReplicatedStorage:WaitForChild("Bodycam"))
local FPSys = require(ReplicatedStorage:WaitForChild("FPSys"))
local FPSController = require(ReplicatedStorage:WaitForChild("FPSController"))
local Presentation = require(ReplicatedStorage:WaitForChild("Presentation"))

local player = Players.LocalPlayer
player.CameraMode = Enum.CameraMode.LockFirstPerson

local movement, weapons

local controller = Bodycam.controller().new({
	walkSpeed = 16,
	sprintSpeed = 25.6,
	isSprinting = function()
		return movement:isSprinting()
	end,
	isCrouching = function()
		return movement:isCrouching()
	end,
	isLeaning = function()
		return weapons:getLean() ~= 0
	end,
})

local fpsys = FPSys.new({
	controller = controller,
	getLean = function()
		return weapons:getLean()
	end,
})

movement = FPSController.movement().new({ controller = controller })

-- Breathing, pulse, tremor, drift and inertia on the weapon; free aim; wall pull-back.
local holdingBreath = false
local presenter = Presentation.weaponPresenter().new({
	controller = controller,
	getWeightKg = function()
		return weapons:current().config.weightKg
	end,
	isHoldingBreath = function()
		return holdingBreath
	end,
})

-- Bodycam (eye) and helmet camera (head mount, wide lens, own body in view).
local viewmodel -- the current weapon's Presentation Viewmodel, if it has one
local headcam = Presentation.headcam().new({
	controller = controller,
	onModeChanged = function(mode)
		-- In the helmet camera the character's own arms hold the weapon.
		if viewmodel then
			viewmodel:Cull(mode == "Headcam")
		end
	end,
})

-- Grain, vignette, eye adaptation, colour grade and the bodycam HUD.
local screen = Presentation.screenEffects().new({
	getViewCFrame = function()
		return controller:getViewCFrame()
	end,
})

local rifle = FPSController.Weapon.defaultConfig()
rifle.scopes = {
	{ name = "Red dot", zoom = 0.85, sensitivity = 0.85 },
	{ name = "4x", zoom = 0.3, sensitivity = 0.35 },
}

local pistol = FPSController.Weapon.defaultConfig()
pistol.name = "Pistol"
pistol.rpm = 450
pistol.modes = { "Semi" }
pistol.magazine = 15
pistol.reloadTime = 1.6
pistol.power = 0.6
pistol.shake = 0.6
pistol.recoil.pitch = { 0.9, 1.3 }
pistol.profile = { pos = { x = 0.02, y = -0.01, z = 0.03 }, rot = { x = 0, y = 1.5, z = 0 } }
pistol.weightKg = 1.0
rifle.viewmodel = "Rifle"
pistol.viewmodel = "Pistol"

-- One Viewmodel per rig name, made on first use.
local viewmodelFolder = ReplicatedStorage:FindFirstChild("Viewmodels")
local viewmodels = {}
local function viewmodelFor(name)
	if not (name and viewmodelFolder) then
		return nil
	end
	if viewmodels[name] == nil then
		local template = viewmodelFolder:FindFirstChild(name)
		viewmodels[name] = false
		if template and template:IsA("Model") then
			local rig = Presentation.viewmodel().new(template:Clone(), { Scale = 0.5 })
			local animations = rig.Instance:FindFirstChild("Animations")
			if animations then
				for _, animation in animations:GetChildren() do
					if animation:IsA("Animation") then
						rig:LoadAnimation(animation.Name, animation)
					end
				end
			end
			viewmodels[name] = rig
		end
	end
	return viewmodels[name] or nil
end

local function showViewmodel(config)
	local nextRig = viewmodelFor(config.viewmodel)
	if nextRig == viewmodel then
		return
	end
	if viewmodel then
		viewmodel:detach()
	end
	viewmodel = nextRig
	if viewmodel then
		viewmodel:attach(controller)
		viewmodel:Cull(headcam:getMode() == "Headcam")
		local idle = viewmodel:GetAnimation("Idle")
		if idle then
			idle.Looped = true
			idle:Play()
		end
	end
end

local rayParams = RaycastParams.new()
rayParams.FilterType = Enum.RaycastFilterType.Exclude

weapons = FPSController.weapons().new({
	controller = controller,
	fpsys = fpsys,
	movement = movement,
	presenter = presenter,
	weapons = { rifle, pistol },
	fovModifier = function(target, weapon)
		return headcam:fovFor(target, weapon.config.fov)
	end,
	onEvent = function(name, weapon)
		if name == "Unequipped" then
			-- Sent once the next weapon is current: switch rigs, then it equips.
			showViewmodel(weapon.config)
			if viewmodel then
				viewmodel:Play("Equip")
			end
		elseif name == "ReloadStarted" and viewmodel then
			viewmodel:Play("Reload")
		end
	end,
	onFire = function(fireCFrame)
		rayParams.FilterDescendantsInstances = { player.Character }
		local hit = Workspace:Raycast(fireCFrame.Position, fireCFrame.LookVector * 1000, rayParams)
		if hit then
			-- Send the hit to the server here.
		end
		if viewmodel then
			viewmodel:Play("Fire")
		end
	end,
})

controller:start()
fpsys:start()
movement:start()
presenter:start()
weapons:start()
headcam:start()
screen:start()
showViewmodel(weapons:current().config)

-- After death, watch the others through their helmet cameras until respawning.
local function spectateNext()
	local others = {}
	for _, other in Players:GetPlayers() do
		local humanoid = other ~= player and other.Character and other.Character:FindFirstChildOfClass("Humanoid")
		if humanoid and humanoid.Health > 0 then
			table.insert(others, other)
		end
	end
	if #others == 0 then
		headcam:spectate(nil)
		return
	end
	local index = (table.find(others, headcam:getSpectating()) or 0) % #others + 1
	headcam:spectate(others[index])
end

-- Getting hurt frightens you: pulse, breathing and tremor rise (SPT-SWAY stress model).
local function watchHealth(character)
	local humanoid = character:WaitForChild("Humanoid")
	headcam:spectate(nil)
	humanoid.Died:Connect(function()
		task.wait(2)
		if humanoid.Health <= 0 then
			spectateNext()
		end
	end)
	local last = humanoid.Health
	humanoid.HealthChanged:Connect(function(health)
		if health < last and humanoid.MaxHealth > 0 then
			presenter:onDamaged((last - health) / humanoid.MaxHealth)
		end
		last = health
	end)
end
if player.Character then
	task.spawn(watchHealth, player.Character)
end
player.CharacterAdded:Connect(watchHealth)

local leanLeft, leanRight = false, false
local function updateLean()
	weapons:setLean((if leanRight then 1 else 0) - (if leanLeft then 1 else 0))
end

UserInputService.InputBegan:Connect(function(input, processed)
	if processed then
		return
	end
	local key = input.KeyCode
	if input.UserInputType == Enum.UserInputType.MouseButton1 then
		weapons:pullTrigger()
	elseif input.UserInputType == Enum.UserInputType.MouseButton2 then
		weapons:setAim(true)
	elseif key == Enum.KeyCode.LeftShift then
		movement:setSprint(true)
	elseif key == Enum.KeyCode.C then
		movement:toggleCrouch()
	elseif key == Enum.KeyCode.Z then
		movement:toggleProne()
	elseif key == Enum.KeyCode.LeftControl then
		movement:slide()
	elseif key == Enum.KeyCode.Space then
		movement:jump()
	elseif key == Enum.KeyCode.R then
		weapons:reload()
	elseif key == Enum.KeyCode.V then
		weapons:cycleFireMode()
	elseif key == Enum.KeyCode.T then
		weapons:cycleScope()
	elseif key == Enum.KeyCode.One then
		weapons:select(1)
	elseif key == Enum.KeyCode.Two then
		weapons:select(2)
	elseif key == Enum.KeyCode.Q then
		leanLeft = true
		updateLean()
	elseif key == Enum.KeyCode.E then
		leanRight = true
		updateLean()
	elseif key == Enum.KeyCode.LeftAlt then
		holdingBreath = true
	elseif key == Enum.KeyCode.H then
		headcam:toggle()
	elseif key == Enum.KeyCode.F and headcam:getSpectating() then
		spectateNext()
	end
end)

UserInputService.InputEnded:Connect(function(input)
	local key = input.KeyCode
	if input.UserInputType == Enum.UserInputType.MouseButton1 then
		weapons:releaseTrigger()
	elseif input.UserInputType == Enum.UserInputType.MouseButton2 then
		weapons:setAim(false)
	elseif key == Enum.KeyCode.LeftShift then
		movement:setSprint(false)
	elseif key == Enum.KeyCode.Q then
		leanLeft = false
		updateLean()
	elseif key == Enum.KeyCode.E then
		leanRight = false
		updateLean()
	elseif key == Enum.KeyCode.LeftAlt then
		holdingBreath = false
	end
end)
