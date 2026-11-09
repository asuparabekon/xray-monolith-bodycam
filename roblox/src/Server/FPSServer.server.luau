-- This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
-- If a copy of the MPL was not distributed with this file, You can obtain one at
-- https://mozilla.org/MPL/2.0/. (A copy is in roblox/src/Realism/LICENSE.)
--
-- Derived from Character-Realism by CloneTrooper1019 (MaximumADHD),
-- https://github.com/MaximumADHD/Character-Realism, by way of the FPSys model.
--
-- FPSys server: tags humanoids for the client components and relays look angles.
--
-- From the FPSys model (Character-Realism's Realism.server.lua), with two changes: each
-- player may send at most MIN_SEND_INTERVAL apart (the source relayed every event to
-- every client), and non-finite numbers (inf as well as NaN) are rejected.

local CollectionService = game:GetService("CollectionService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

-- Only the config: the rest of FPSys is client code.
local Config = require(ReplicatedStorage:WaitForChild("FPSys"):WaitForChild("Config"))

-- The Realism client sends at most every 0.05 s; anything faster is dropped.
local MIN_SEND_INTERVAL = 0.04

local remote = ReplicatedStorage:FindFirstChild(Config.RemoteName)
if not remote then
	remote = Instance.new("RemoteEvent")
	remote.Name = Config.RemoteName
	remote.Archivable = false
	remote.Parent = ReplicatedStorage
end

local lastSend = {}

local function finite(value): boolean
	return typeof(value) == "number" and value == value and math.abs(value) ~= math.huge
end

remote.OnServerEvent:Connect(function(player, turn, tilt)
	if not (finite(turn) and finite(tilt)) then
		return
	end
	local now = os.clock()
	if now - (lastSend[player] or -math.huge) < MIN_SEND_INTERVAL then
		return
	end
	lastSend[player] = now
	remote:FireAllClients(player, math.clamp(turn, -1, 1), math.clamp(tilt, -1, 1))
end)

local function onCharacterAdded(character)
	local humanoid = character:WaitForChild("Humanoid", 10)
	if humanoid and humanoid:IsA("Humanoid") then
		CollectionService:AddTag(humanoid, Config.BindTag)
	end
end

local function onPlayerAdded(player)
	if player.Character then
		task.spawn(onCharacterAdded, player.Character)
	end
	player.CharacterAdded:Connect(onCharacterAdded)
end

for _, player in Players:GetPlayers() do
	onPlayerAdded(player)
end
Players.PlayerAdded:Connect(onPlayerAdded)
Players.PlayerRemoving:Connect(function(player)
	lastSend[player] = nil
end)
