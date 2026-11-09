-- This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
-- If a copy of the MPL was not distributed with this file, You can obtain one at
-- https://mozilla.org/MPL/2.0/. (A copy is in this folder: LICENSE.)
--
-- Port of Character-Realism RealismClient/FirstPerson.lua by CloneTrooper1019
-- (MaximumADHD), https://github.com/MaximumADHD/Character-Realism: shows the player's
-- body in first person by overloading Roblox's PlayerModule BaseCamera and
-- TransparencyController, and puts the camera at the head.
--
-- Changes for the bodycam integration (MPL: changes to this file stay MPL):
--  * IsInFirstPerson() reads the aim camera through FirstPerson.getAimCFrame when set:
--    the bodycam view layers (lean, crouch drop, head bob) move the view away
--    from the focus and would end first person in the source's check.
--  * The source's late "FirstPersonCamera" render step moves the final camera onto the
--    head every frame, which undoes the bodycam's view offsets. It runs only when
--    FirstPerson.snapCameraToHead is true (default false here). The camera still starts
--    at the head through the overloaded GetSubjectPosition. The airborne root rotation
--    smoothing of that step always runs, and turns toward the aim, not the view.
--  * Start() also applies the current rotation type once (the source waited for the
--    first change), so a game already in first person is smoothed from the start.

local Players = game:GetService("Players")
local RunService = game:GetService("RunService")

local XZ_VECTOR3 = Vector3.new(1, 0, 1)

local FirstPerson = {
	Started = false,
	-- Integration hooks.
	getAimCFrame = nil :: (() -> CFrame?)?,
	snapCameraToHead = false,
}

local HEAD_ATTACHMENTS = {
	FaceCenterAttachment = true,
	FaceFrontAttachment = true,
	HairAttachment = true,
	HatAttachment = true,
}

local INVALID_ROTATION_STATES = {
	Swimming = true,
	Climbing = true,
	Dead = true,
}

function FirstPerson.IsInFirstPerson(_self: any?): boolean
	local camera = workspace.CurrentCamera
	if camera then
		if camera.CameraType.Name == "Scriptable" then
			return false
		end
		local aim = FirstPerson.getAimCFrame and FirstPerson.getAimCFrame()
		local focus = camera.Focus.Position
		local origin = (aim or camera.CFrame).Position
		return (focus - origin).Magnitude <= 1
	end
	return false
end

function FirstPerson.GetSubjectPosition(self: any)
	if FirstPerson.IsInFirstPerson() then
		local camera = workspace.CurrentCamera
		local subject = camera.CameraSubject
		if subject and subject:IsA("Humanoid") and subject.Health > 0 then
			local character = subject.Parent
			local head = character and character:FindFirstChild("Head")
			if head and head:IsA("BasePart") then
				local cf = head.CFrame
				local offset = cf * CFrame.new(0, head.Size.Y / 3, 0)
				return offset.Position, cf.LookVector
			end
		end
	end
	local getBase: any = assert(self.GetBaseSubjectPosition)
	return getBase(self)
end

function FirstPerson.GetBaseSubjectPosition()
	-- STUB! Overwritten at run-time
	return Vector3.zero
end

function FirstPerson.IsValidPartToModify(_self: any, part: Instance?): boolean
	if part then
		if part:FindFirstAncestorOfClass("Tool") then
			return false
		elseif part:IsA("Decal") then
			part = part.Parent
		end
	end
	if part and part:IsA("BasePart") then
		local accessory = part:FindFirstAncestorWhichIsA("Accoutrement")
		if accessory then
			if part.Name ~= "Handle" then
				local handle = accessory:FindFirstChild("Handle", true)
				if handle and handle:IsA("BasePart") then
					part = handle
				end
			end
			for _, child in (part and part:GetChildren() or {}) do
				if child:IsA("Attachment") and HEAD_ATTACHMENTS[child.Name] then
					return true
				end
			end
		elseif part.Name == "Head" then
			local model = part.Parent
			local camera = assert(workspace.CurrentCamera)
			local humanoid = model and model:FindFirstChildOfClass("Humanoid")
			if humanoid and camera.CameraSubject == humanoid then
				return true
			end
		end
	end
	return false
end

-- Overloads BaseCamera:GetSubjectPosition() with FirstPerson.GetSubjectPosition.
local function mountBaseCamera(unsafe_BaseCamera: any)
	local base = if type(unsafe_BaseCamera) == "table" then rawget(unsafe_BaseCamera, "GetSubjectPosition") else nil
	if type(base) == "function" then
		FirstPerson.GetBaseSubjectPosition = base
		unsafe_BaseCamera.GetBaseSubjectPosition = base
		unsafe_BaseCamera.GetSubjectPosition = FirstPerson.GetSubjectPosition
	else
		warn("Could not find BaseCamera:GetSubjectPosition()!")
	end
end

-- The overloaded TransparencyController:Update(...).
local function updateTransparency(self: any, ...)
	self:BaseUpdate(...)
	if self.ForceRefresh then
		self.ForceRefresh = false
		if type(self.SetSubject) == "function" then
			local camera = workspace.CurrentCamera
			self:SetSubject(camera.CameraSubject)
		end
	end
end

-- The overloaded TransparencyController:SetupTransparency(character, ...).
local function setupTransparency(self: any, character: Model, ...)
	self:BaseSetupTransparency(character, ...)
	if self.AttachmentListener then
		self.AttachmentListener:Disconnect()
	end
	self.AttachmentListener = character.DescendantAdded:Connect(function(obj)
		if obj:IsA("Attachment") and HEAD_ATTACHMENTS[obj.Name] then
			if type(self.cachedParts) == "table" then
				self.cachedParts[obj.Parent] = true
			end
			if self.transparencyDirty ~= nil then
				self.transparencyDirty = true
			end
		end
	end)
end

local function mountTransparency(Transparency: any)
	local baseUpdate = Transparency.Update
	if type(baseUpdate) == "function" then
		Transparency.BaseUpdate = baseUpdate
		Transparency.Update = updateTransparency
	else
		warn("MountTransparency - Could not find Transparency:Update()!")
	end
	if type(Transparency.IsValidPartToModify) == "function" then
		Transparency.IsValidPartToModify = FirstPerson.IsValidPartToModify
		Transparency.ForceRefresh = true
	else
		warn("MountTransparency - Could not find Transparency:IsValidPartToModify(part)!")
	end
	if type(Transparency.SetupTransparency) == "function" then
		Transparency.BaseSetupTransparency = Transparency.SetupTransparency
		Transparency.SetupTransparency = setupTransparency
	else
		warn("MountTransparency - Could not find Transparency:SetupTransparency(character)!")
	end
end

-- Called when the user's rotation type changes: a strong sign of first person, whose
-- movement needs smoothing.
local function onRotationTypeChanged()
	local UserGameSettings = UserSettings().GameSettings
	local camera = workspace.CurrentCamera
	local subject = camera and camera.CameraSubject
	if subject and subject:IsA("Humanoid") then
		if UserGameSettings.RotationType ~= Enum.RotationType.CameraRelative then
			subject.AutoRotate = true
			return
		end
		subject.AutoRotate = false
		RunService:BindToRenderStep("FirstPersonCamera", 1000, function(dt: number)
			local cancel = subject.AutoRotate
				or not subject:IsDescendantOf(game)
				or (subject.SeatPart and subject.SeatPart:IsA("VehicleSeat"))
			if cancel then
				RunService:UnbindFromRenderStep("FirstPersonCamera")
				return
			end
			-- Keep on standby, but ignore if we're in scriptable mode.
			if camera.CameraType.Name == "Scriptable" then
				return
			end
			local rootPart = subject.RootPart
			local isGrounded = rootPart and rootPart:IsGrounded()
			if rootPart and not isGrounded then
				local state = subject:GetState()
				local canRotate = not INVALID_ROTATION_STATES[state.Name]
				if subject.Sit and subject.SeatPart then
					if rootPart.AssemblyRootPart ~= rootPart then
						canRotate = false
					end
				end
				if canRotate then
					local pos = rootPart.Position
					local step = math.min(0.2, (dt * 40) / 3)
					local aim = FirstPerson.getAimCFrame and FirstPerson.getAimCFrame() or camera.CFrame
					local look = (aim.LookVector * XZ_VECTOR3).Unit
					local cf = CFrame.new(pos, pos + look)
					rootPart.CFrame = rootPart.CFrame:Lerp(cf, step)
				end
			end
			if FirstPerson.snapCameraToHead and FirstPerson.IsInFirstPerson() then
				local cf = camera.CFrame
				local headPos = FirstPerson:GetSubjectPosition()
				if headPos then
					local offset = (headPos - cf.Position)
					cf += offset
					camera.CFrame = cf
					camera.Focus += offset
				end
			end
		end)
	end
end

-- Exposed for tests: the overloads applied to the PlayerModule's module tables.
FirstPerson._mountBaseCamera = mountBaseCamera
FirstPerson._mountTransparency = mountTransparency

-- Called once to start the FirstPerson logic. Binds and overloads everything necessary.
function FirstPerson.Start()
	if FirstPerson.Started then
		return
	end
	local player = assert(Players.LocalPlayer)
	FirstPerson.Started = true
	task.spawn(function()
		local StarterPlayer = game:GetService("StarterPlayer")
		local requireUnsafe = require :: any
		local playerScripts = player:WaitForChild("PlayerScripts")
		local playerModule = StarterPlayer:WaitForChild("PlayerModule", 1)
		if not playerModule then
			playerModule = playerScripts:WaitForChild("PlayerModule")
		end
		if not playerModule then
			warn("Start - Could not find PlayerModule!")
			return
		end
		local baseCamera = playerModule:FindFirstChild("BaseCamera", true)
		local transparency = playerModule:FindFirstChild("TransparencyController", true)
		if baseCamera and baseCamera:IsA("ModuleScript") then
			task.spawn(mountBaseCamera, requireUnsafe(baseCamera))
		else
			warn("Start - Could not find BaseCamera module!")
		end
		if transparency and transparency:IsA("ModuleScript") then
			task.spawn(mountTransparency, requireUnsafe(transparency))
		else
			warn("Start - Cound not find TransparencyController module!")
		end
	end)
	local UserGameSettings = UserSettings().GameSettings
	UserGameSettings:GetPropertyChangedSignal("RotationType"):Connect(onRotationTypeChanged)
	onRotationTypeChanged()
end

return FirstPerson
