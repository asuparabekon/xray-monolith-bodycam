-- Example client: bodycam + FPSys + FPSController on the local character.
--
-- Keys: WASD move, Left Shift sprint, C crouch, Z prone, Left Ctrl slide (while
-- sprinting), Space jump (stands up from crouch/prone), right mouse aim, left mouse
-- fire, R reload, V fire mode, T next scope (while aiming), 1/2 weapons, Q/E lean.
-- Replace onFire with your game's hit detection (validate hits on the server).

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

local Bodycam = require(ReplicatedStorage:WaitForChild("Bodycam"))
local FPSys = require(ReplicatedStorage:WaitForChild("FPSys"))
local FPSController = require(ReplicatedStorage:WaitForChild("FPSController"))

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

local rayParams = RaycastParams.new()
rayParams.FilterType = Enum.RaycastFilterType.Exclude

weapons = FPSController.weapons().new({
	controller = controller,
	fpsys = fpsys,
	movement = movement,
	weapons = { rifle, pistol },
	onFire = function(fireCFrame)
		rayParams.FilterDescendantsInstances = { player.Character }
		local hit = Workspace:Raycast(fireCFrame.Position, fireCFrame.LookVector * 1000, rayParams)
		if hit then
			-- Send the hit to the server here.
		end
	end,
})

controller:start()
fpsys:start()
movement:start()
weapons:start()

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
	end
end)
