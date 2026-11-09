-- Example bootstrap: first-person bodycam on the local character.
-- Right mouse aims, left mouse fires, Left Shift sprints, C crouches.
-- Replace the input handling with your weapon system's events.

local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local UserInputService = game:GetService("UserInputService")

local Bodycam = require(ReplicatedStorage:WaitForChild("Bodycam"))

local player = Players.LocalPlayer
player.CameraMode = Enum.CameraMode.LockFirstPerson

local WALK_SPEED = 16
local SPRINT_SPEED = 25.6

local sprinting = false
local crouching = false

local controller = Bodycam.controller().new({
	walkSpeed = WALK_SPEED,
	sprintSpeed = SPRINT_SPEED,
	isSprinting = function()
		return sprinting
	end,
	isCrouching = function()
		return crouching
	end,
	-- viewmodel = ReplicatedStorage.Viewmodel:Clone(), -- a Model with a PrimaryPart, parented to the camera
	-- viewmodelOffset = CFrame.new(0.6, -0.6, -1.2),
})
controller:start()

local function setSprint(on: boolean)
	sprinting = on
	local humanoid = player.Character and player.Character:FindFirstChildOfClass("Humanoid")
	if humanoid then
		humanoid.WalkSpeed = if on then SPRINT_SPEED else WALK_SPEED
	end
end

UserInputService.InputBegan:Connect(function(input, processed)
	if processed then
		return
	end
	if input.UserInputType == Enum.UserInputType.MouseButton2 then
		setSprint(false)
		controller:setAiming(true)
	elseif input.UserInputType == Enum.UserInputType.MouseButton1 then
		controller:notifyShot(1)
	elseif input.KeyCode == Enum.KeyCode.LeftShift then
		setSprint(true)
	elseif input.KeyCode == Enum.KeyCode.C then
		crouching = not crouching
	end
end)

UserInputService.InputEnded:Connect(function(input)
	if input.UserInputType == Enum.UserInputType.MouseButton2 then
		controller:setAiming(false)
	elseif input.KeyCode == Enum.KeyCode.LeftShift then
		setSprint(false)
	end
end)
