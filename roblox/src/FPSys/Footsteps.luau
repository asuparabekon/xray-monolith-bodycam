-- FPSys material footsteps.
--
-- Other characters: the default "Running" sound gets the floor material's sound, and
-- its volume and pitch follow the speed and the character's size, as in the model.
-- The local character with a bodycam controller: the Running loop is muted and a step
-- sound plays on each foot contact of the bodycam stride, so steps match the camera.
--
-- Fixes against the model: the speed loop ran a new `while` loop on every state change
-- (stacking loops) and divided by zero when standing; one Heartbeat update per
-- character replaces it. Connections are cleaned up when the character goes away.

local RunService = game:GetService("RunService")

local DefaultConfig = require(script.Parent.Config)

local Footsteps = {}
Footsteps.__index = Footsteps

local XZ = Vector3.new(1, 0, 1)

-- The sound id for a floor material (Enum.Material or its name).
function Footsteps.soundFor(config, material): number
	local name = if typeof(material) == "EnumItem" then material.Name else tostring(material)
	local sounds = config.Sounds
	local group = if sounds[name] then name else config.MaterialMap[name]
	return sounds[group or ""] or sounds.Concrete
end

-- Running-loop volume and playback speed for a planar speed (studs/s), as in the model.
function Footsteps.runningMix(speed: number, hipHeight: number): (number, number)
	local scale = hipHeight / 3
	local volume = math.clamp(((speed - 4) / 12) * scale, 0, 1)
	local playbackSpeed = if speed > 1e-3 then speed / (scale * 15) else 1
	return volume, playbackSpeed
end

function Footsteps.new(options)
	options = options or {}
	local self = setmetatable({}, Footsteps)
	self.config = options.config or DefaultConfig
	self.random = options.random or Random.new()
	self.mounted = {}
	return self
end

local function hipHeightOf(humanoid: Humanoid): number
	return if humanoid.RigType == Enum.HumanoidRigType.R6 then 2.8 else humanoid.HipHeight
end

-- Starts footsteps for a tagged humanoid. `controller` is the BodycamController for
-- the local character, nil for everyone else.
function Footsteps:mount(humanoid: Humanoid, controller)
	local character = humanoid.Parent
	if not character or self.mounted[humanoid] then
		return
	end
	local root = character:WaitForChild("HumanoidRootPart", 10)
	if not root then
		return
	end
	local entry = { connections = {} }
	self.mounted[humanoid] = entry
	local config = self.config

	if controller then
		-- Two voices so a quick step does not cut the previous one off.
		local voices = {}
		for _, side in { "Left", "Right" } do
			local sound = Instance.new("Sound")
			sound.Name = "FPSysStep" .. side
			sound.RollOffMinDistance = 1
			sound.RollOffMaxDistance = 50
			sound.Parent = root
			voices[side] = sound
		end
		entry.voices = voices
		entry.disconnectStep = controller:onFootstep(function(side, info)
			local sound = voices[side]
			if not sound or not sound.Parent then
				return
			end
			sound.SoundId = `rbxassetid://{Footsteps.soundFor(config, humanoid.FloorMaterial)}`
			local volume = Footsteps.runningMix(info.planarSpeed, hipHeightOf(humanoid))
			sound.Volume = math.max(volume, if info.crouching then 0.15 else 0.35)
			sound.PlaybackSpeed = self.random:NextNumber(0.92, 1.08)
			sound:Play()
		end)
	end

	local function withRunning(fn)
		local running = root:FindFirstChild("Running")
		if running and running:IsA("Sound") then
			fn(running)
		end
	end
	local function updateSoundId()
		withRunning(function(running)
			running.SoundId = `rbxassetid://{Footsteps.soundFor(config, humanoid.FloorMaterial)}`
			running.RollOffMinDistance = 1
			running.RollOffMaxDistance = 50
		end)
	end
	updateSoundId()
	table.insert(entry.connections, humanoid:GetPropertyChangedSignal("FloorMaterial"):Connect(updateSoundId))
	table.insert(entry.connections, root.ChildAdded:Connect(function(child)
		if child.Name == "Running" then
			updateSoundId()
		end
	end))
	table.insert(
		entry.connections,
		RunService.Heartbeat:Connect(function()
			withRunning(function(running)
				if controller then
					running.Volume = 0 -- the stride steps replace the loop
					return
				end
				local speed = (root.AssemblyLinearVelocity * XZ).Magnitude
				local volume, playbackSpeed = Footsteps.runningMix(speed, hipHeightOf(humanoid))
				running.Volume = volume
				running.PlaybackSpeed = playbackSpeed
			end)
		end)
	)
	table.insert(entry.connections, humanoid.AncestryChanged:Connect(function(_, parent)
		if parent == nil then
			self:unmount(humanoid)
		end
	end))
end

function Footsteps:unmount(humanoid: Humanoid)
	local entry = self.mounted[humanoid]
	if not entry then
		return
	end
	for _, connection in entry.connections do
		connection:Disconnect()
	end
	if entry.disconnectStep then
		entry.disconnectStep()
	end
	if entry.voices then
		for _, sound in entry.voices do
			sound:Destroy()
		end
	end
	self.mounted[humanoid] = nil
end

function Footsteps:destroy()
	for humanoid in self.mounted do
		self:unmount(humanoid)
	end
end

return Footsteps
