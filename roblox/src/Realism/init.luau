-- This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
-- If a copy of the MPL was not distributed with this file, You can obtain one at
-- https://mozilla.org/MPL/2.0/. (A copy is in this folder: LICENSE.)
--
-- Port of Character-Realism RealismClient/init.client.lua by CloneTrooper1019
-- (MaximumADHD), https://github.com/MaximumADHD/Character-Realism: characters turn
-- their head, torso and arms toward where their player looks, replicated through the
-- SetLookAngles RemoteEvent. Credit CloneTrooper1019 in your game (the source asks for
-- it in the description or credits).
--
-- The rotation is multiplied into each joint's Transform during Stepped, after the
-- Animator writes it and before physics, so it works on Motor6D rigs and on rigs that
-- use AnimationConstraint (Avatar Joint Upgrade), whose C0 is read-only.
--
-- Changes from the source (MPL: changes to this file stay MPL):
--  * A module with Start/Stop instead of a script that starts itself; the material
--    walking sounds live in FPSys/Footsteps.luau (stride-synced for the bodycam).
--  * The look angle comes from the bodycam aim (getAimCFrame) when given, not the
--    view, which the bodycam layers move.
--  * The tool arm rule used the local player's raw angles for every character; it
--    uses each character's own smoothed angles.
--  * The "dirty" check shared one value across all motors of a character and skipped
--    a motor whose value had not changed. With animations playing the Animator resets
--    Transform every frame, so a skipped motor snapped back. Motors are now written
--    every frame while animations play, and on change otherwise, per motor.
--  * Stop() disconnects everything and restores the joints.

local CollectionService = game:GetService("CollectionService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")

local FirstPerson = require(script.FirstPerson)
local DefaultConfig = require(script.Config)

local Realism = {}
Realism.__index = Realism
Realism.FirstPerson = FirstPerson
Realism.Config = DefaultConfig

local function round(n: number, factor: number?): number
	local mult = 10 ^ (factor or 0)
	return math.floor((n * mult) + 0.5) / mult
end

local function roundNearestInterval(n: number, factor: number): number
	return round(n / factor) * factor
end

-- Interpolates the current value of a rotator state (pitch/yaw) toward its goal.
local function expSmooth(state, tau: number, dt: number): number
	if tau <= 0 then
		state.Current = state.Goal
		return state.Current
	end
	local alpha = 1 - math.exp(-dt / tau)
	state.Current += (state.Goal - state.Current) * alpha
	return state.Current
end
Realism.expSmooth = expSmooth

-- The look angles for a root CFrame and a look vector. pitch is the horizontal look
-- (positive = left of the root), yaw the vertical look. Outside first person, looking
-- behind the character flips both (dir).
function Realism.computeLookAngle(rootCFrame: CFrame, lookVector: Vector3, inFirstPerson: boolean, useDir: number?): (number, number)
	local pitch = -rootCFrame.RightVector:Dot(lookVector)
	local yaw = lookVector.Y
	local dir = 1
	if not inFirstPerson then
		dir = math.clamp(rootCFrame.LookVector:Dot(lookVector) * 10, -1, 1)
	end
	if useDir then
		dir = useDir
	end
	return pitch * dir, yaw * dir
end

-- The joint offset for factored angles, as the source builds it.
function Realism.jointRotation(fPitch: number, fYaw: number): CFrame
	return CFrame.Angles(0, fPitch, 0) * CFrame.Angles(fYaw, 0, 0)
end

-- options:
--   config        Realism.Config shape
--   remote        the SetLookAngles RemoteEvent (default: waited for in ReplicatedStorage)
--   getAimCFrame  function(): CFrame?  the bodycam aim (default: the camera)
--   firstPerson   false to leave the PlayerModule alone (no body in first person)
--   clock         function(): number (default os.clock)
function Realism.new(options)
	options = options or {}
	local self = setmetatable({}, Realism)
	self.options = options
	self.config = options.config or DefaultConfig
	self.clock = options.clock or os.clock
	self.rotators = {}
	self.connections = {}
	self.lastUpdate = 0
	self.pitch, self.yaw = 0 / 0, 0 / 0
	self.started = false
	return self
end

-- Registers a newly added joint once it is active.
function Realism:_addMotor(rotator, motor)
	task.spawn(function()
		local timeOut = self.clock() + 10
		while not motor.Active do
			if self.clock() > timeOut then
				return
			end
			RunService.Heartbeat:Wait()
		end
		local part1 = if motor:IsA("AnimationConstraint") then motor.Attachment1 else motor.Part1
		if part1 and part1:IsA("Attachment") then
			part1 = part1.Parent or part1
		end
		if part1 then
			rotator.Motors[part1.Name] = { Motor = motor }
		end
	end)
end

-- Mounts a humanoid into the look-angle system, binding its current and future joints.
function Realism:MountLookAngle(humanoid: Humanoid)
	local character = humanoid.Parent
	assert(character and character:IsA("Model"), "Invalid parent for Humanoid!")
	if self.rotators[character] == nil then
		local rotator = {
			Motors = {},
			Pitch = { Goal = 0, Current = 0 },
			Yaw = { Goal = 0, Current = 0 },
		}
		self.rotators[character] = rotator
		local function onDescendantAdded(desc: Instance)
			if desc:IsA("Motor6D") or desc:IsA("AnimationConstraint") then
				self:_addMotor(rotator, desc)
			end
		end
		for _, desc in character:GetDescendants() do
			onDescendantAdded(desc)
		end
		rotator.Listener = character.DescendantAdded:Connect(onDescendantAdded)
	end
	return self.rotators[character]
end

-- A look-angle from the server for a player's character (or our own, every frame).
function Realism:OnLookReceive(player: Player, pitch: number, yaw: number)
	if type(pitch) ~= "number" or type(yaw) ~= "number" then
		return
	end
	local character = player.Character
	local rotator = character and self.rotators[character]
	if rotator then
		rotator.Pitch.Goal = pitch
		rotator.Yaw.Goal = yaw
	end
end

function Realism:_localLookAngle(): (number, number)
	local player = Players.LocalPlayer
	local character = player and player.Character
	local rootPart = character and character:FindFirstChild("HumanoidRootPart")
	local aim = self.options.getAimCFrame and self.options.getAimCFrame()
	if not aim then
		local camera = workspace.CurrentCamera
		aim = camera and camera.CFrame
	end
	if not (aim and rootPart and rootPart:IsA("BasePart")) then
		return 0, if aim then aim.LookVector.Y else 0
	end
	return Realism.computeLookAngle(rootPart.CFrame, aim.LookVector, FirstPerson.IsInFirstPerson())
end

-- Steps every rotator and writes the joints. Runs on Stepped.
function Realism:UpdateLookAngles(_dt: number)
	local config = self.config
	local player = Players.LocalPlayer
	-- Our own look-angles with no latency.
	local pitch, yaw = self:_localLookAngle()
	if player then
		self:OnLookReceive(player, pitch, yaw)
	end

	-- Submit our look-angles if they changed enough.
	local now = self.clock()
	if (now - self.lastUpdate) > config.SendInterval and self.remote then
		local dirty = false
		yaw = roundNearestInterval(yaw, config.SendStep)
		pitch = roundNearestInterval(pitch, config.SendStep)
		if pitch ~= self.pitch then
			self.pitch = pitch
			dirty = true
		end
		if yaw ~= self.yaw then
			self.yaw = yaw
			dirty = true
		end
		if dirty then
			self.lastUpdate = now
			self.remote:FireServer(pitch, yaw)
		end
	end

	local camera = workspace.CurrentCamera
	local camPos = camera and camera.CFrame.Position
	local drop = {}
	for character, rotator in self.rotators do
		if not character.Parent then
			table.insert(drop, character)
			continue
		end
		local owner = Players:GetPlayerFromCharacter(character)
		local dist = if owner and camPos then owner:DistanceFromCharacter(camPos) else 0
		if owner ~= player and dist > config.MaxDistance then
			continue
		end
		local stepDelta = now - (rotator.LastStep or now)
		rotator.LastStep = now
		local humanoid = character:FindFirstChildOfClass("Humanoid")
		local rootPart = humanoid and humanoid.RootPart
		if not rootPart then
			continue
		end
		local animator = humanoid:FindFirstChildOfClass("Animator")
		local numTracks = if animator then #animator:GetPlayingAnimationTracks() else 0
		local myPitch = expSmooth(rotator.Pitch, config.Smoothing, stepDelta)
		local myYaw = expSmooth(rotator.Yaw, config.Smoothing, stepDelta)

		for name, factors in config.RotationFactors do
			local data = rotator.Motors[name]
			local motor = data and data.Motor
			if not motor then
				continue
			end
			local fPitch = myPitch * factors.Pitch
			local fYaw = myYaw * factors.Yaw
			-- Make the arms rotate with a tool.
			if name:sub(-4) == " Arm" or name:sub(-8) == "UpperArm" then
				local tool = character:FindFirstChildOfClass("Tool")
				if tool and not tool:HasTag("NoArmRotation") then
					if name:sub(1, 5) == "Right" and rootPart.AssemblyRootPart ~= rootPart then
						fPitch = myPitch * 1.3
						fYaw = myYaw * 1.3
					else
						fYaw = myYaw * 0.8
					end
				end
			end
			if not motor.Enabled then
				continue
			end
			local changed = fPitch ~= data.Pitch or fYaw ~= data.Yaw
			if numTracks > 0 or changed then
				data.Pitch, data.Yaw = fPitch, fYaw
				local cf = Realism.jointRotation(fPitch, fYaw)
				if numTracks > 0 then
					motor.Transform *= cf
				else
					motor.Transform = cf
				end
			end
		end
	end
	for _, character in drop do
		self:_unmount(character)
	end
end

function Realism:_unmount(character)
	local rotator = self.rotators[character]
	if not rotator then
		return
	end
	if rotator.Listener then
		rotator.Listener:Disconnect()
	end
	for _, data in rotator.Motors do
		if data.Motor.Parent then
			data.Motor.Transform = CFrame.identity
		end
	end
	self.rotators[character] = nil
end

local function onHumanoidAdded(self, humanoid: Instance)
	if humanoid:IsA("Humanoid") then
		task.spawn(function()
			self:MountLookAngle(humanoid)
		end)
	end
end

function Realism:Start()
	if self.started then
		return
	end
	self.started = true
	local config = self.config
	self.remote = self.options.remote or ReplicatedStorage:WaitForChild(config.RemoteName, 10)
	FirstPerson.getAimCFrame = self.options.getAimCFrame
	if self.options.firstPerson ~= false then
		task.spawn(FirstPerson.Start)
	end
	for _, humanoid in CollectionService:GetTagged(config.BindTag) do
		onHumanoidAdded(self, humanoid)
	end
	table.insert(self.connections, CollectionService:GetInstanceAddedSignal(config.BindTag):Connect(function(humanoid)
		onHumanoidAdded(self, humanoid)
	end))
	table.insert(self.connections, RunService.Stepped:Connect(function(_, dt)
		self:UpdateLookAngles(dt)
	end))
	if self.remote then
		table.insert(self.connections, self.remote.OnClientEvent:Connect(function(player, pitch, yaw)
			self:OnLookReceive(player, pitch, yaw)
		end))
	end
end

function Realism:Stop()
	if not self.started then
		return
	end
	self.started = false
	for _, connection in self.connections do
		connection:Disconnect()
	end
	self.connections = {}
	for character in self.rotators do
		self:_unmount(character)
	end
end

return Realism
