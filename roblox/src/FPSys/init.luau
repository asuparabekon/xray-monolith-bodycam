-- FPSys client, integrated with the bodycam port.
--
-- Components (each can be switched off in Config.Enabled):
--   CameraEffects  walk bob, breathing, strafe tilt, look sway, landing kick, motion
--                  blur, lean, shot shake; a view layer on the BodycamController
--   LookAngles     replicated head/torso/arm look for every tagged character
--   Footsteps      material footstep sounds; stride-synced for the local character
--
-- Usage (LocalScript):
--   local fpsys = FPSys.new({ controller = bodycamController, getLean = function() return lean end })
--   fpsys:start()
--   fpsys:shake(1)        -- per-shot camera shake
--   fpsys:stop()

local CollectionService = game:GetService("CollectionService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local RunService = game:GetService("RunService")

local Config = require(script.Config)
local Maid = require(script.Maid)
local CameraEffects = require(script.CameraEffects)
local LookAngles = require(script.LookAngles)
local Footsteps = require(script.Footsteps)

local FPSys = {}
FPSys.__index = FPSys
FPSys.Config = Config
FPSys.Spring = require(script.Spring)
FPSys.Maid = Maid
FPSys.CameraEffects = CameraEffects
FPSys.LookAngles = LookAngles
FPSys.Footsteps = Footsteps

-- options:
--   controller  BodycamController (required for camera effects and stride footsteps)
--   config      FPSys Config (default FPSys.Config)
--   getLean     function(): number  -1..1
--   remote      RemoteEvent for look angles (default: waits for ReplicatedStorage[Config.RemoteName])
function FPSys.new(options)
	options = options or {}
	local self = setmetatable({}, FPSys)
	self.options = options
	self.config = options.config or Config
	self.controller = options.controller
	self.maid = Maid.new()
	self.started = false
	return self
end

function FPSys:_mount(humanoid: Instance)
	if not humanoid:IsA("Humanoid") then
		return
	end
	local enabled = self.config.Enabled
	if enabled.LookAngles and self.lookAngles then
		self.lookAngles:mount(humanoid)
	end
	if enabled.Footsteps and self.footsteps then
		local isLocal = Players:GetPlayerFromCharacter(humanoid.Parent) == Players.LocalPlayer
		task.spawn(function()
			self.footsteps:mount(humanoid, if isLocal then self.controller else nil)
		end)
	end
end

function FPSys:start()
	assert(not self.started, "FPSys already started")
	self.started = true
	local config = self.config
	local controller = self.controller

	if controller then
		self.cameraEffects = CameraEffects.new({
			config = config,
			bodycamConfig = controller.config,
			getLean = self.options.getLean,
		})
		self.maid:GiveTask(controller:addViewModifier(function(view, info)
			return self.cameraEffects:apply(view, info)
		end))
		self.maid:GiveTask(function()
			self.cameraEffects:destroy()
		end)
	end

	if config.Enabled.LookAngles then
		local remote = self.options.remote or ReplicatedStorage:WaitForChild(config.RemoteName, 10)
		self.lookAngles = LookAngles.new({
			config = config,
			remote = remote,
			getAimCFrame = if controller then function()
				return controller:getAimCFrame()
			end else nil,
		})
		if remote then
			self.maid:GiveTask(remote.OnClientEvent:Connect(function(player, turn, tilt)
				self.lookAngles:receive(player, turn, tilt)
			end))
		end
		self.maid:GiveTask(RunService.Heartbeat:Connect(function(dt)
			self.lookAngles:update(dt)
		end))
		self.maid:GiveTask(function()
			self.lookAngles:destroy()
		end)
	end

	if config.Enabled.Footsteps then
		self.footsteps = Footsteps.new({ config = config })
		self.maid:GiveTask(function()
			self.footsteps:destroy()
		end)
	end

	for _, humanoid in CollectionService:GetTagged(config.BindTag) do
		self:_mount(humanoid)
	end
	self.maid:GiveTask(CollectionService:GetInstanceAddedSignal(config.BindTag):Connect(function(humanoid)
		self:_mount(humanoid)
	end))
end

function FPSys:stop()
	if not self.started then
		return
	end
	self.started = false
	self.maid:DoCleaning()
	self.cameraEffects, self.lookAngles, self.footsteps = nil, nil, nil
end

-- Per-shot camera shake (scale 1 = the Config.Shake ranges).
function FPSys:shake(scale: number?)
	if self.cameraEffects then
		self.cameraEffects:shake(scale)
	end
end

-- The lean this frame: sideways offset (studs) and roll (radians), for aligning shots.
function FPSys:getLean(): (number, number)
	if self.cameraEffects then
		return self.cameraEffects:getLean()
	end
	return 0, 0
end

return FPSys
