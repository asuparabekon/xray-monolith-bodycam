-- FPSys look angles: characters turn their head, torso and arms toward where their
-- player looks, replicated through a RemoteEvent.
--
-- Fixes against the FPSys model:
--  * The per-frame Motor6D update was a stub ("Process rotator updates..."), so no
--    character ever turned. update() now steps each rotator and writes the motors.
--  * The server sends (player, turn, tilt) but the client handler read (turn, tilt),
--    taking the player as the angle, and applied every update to the local character.
--    Updates now go to the sending player's character.
--  * First person is judged from the aim camera, not the view, which the bodycam
--    view layers move.

local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")

local DefaultConfig = require(script.Parent.Config)

local LookAngles = {}
LookAngles.__index = LookAngles

-- The look angles of a character: turn = how far the look points to the root's left
-- (-1..1), tilt = how far it points up (-1..1). Outside first person, looking behind
-- the character flips both, as in the model.
function LookAngles.compute(rootCFrame: CFrame, look: Vector3, firstPerson: boolean): (number, number)
	local turn = -rootCFrame.RightVector:Dot(look)
	local tilt = look.Y
	local dir = 1
	if not firstPerson then
		dir = math.clamp(rootCFrame.LookVector:Dot(look) * 10, -1, 1)
	end
	return turn * dir, tilt * dir
end

-- Moves current toward goal at most `rate` share per second, never overshooting.
function LookAngles.step(current: number, goal: number, rate: number, dt: number): number
	local maxStep = math.min(1, rate * dt)
	local diff = goal - current
	if math.abs(diff) <= maxStep then
		return goal
	end
	return current + maxStep * (if diff > 0 then 1 else -1)
end

-- The motor's offset for a look: tilt about X, turn about Y, in the motor's C0 frame.
function LookAngles.motorRotation(factors, turn: number, tilt: number): CFrame
	return CFrame.Angles(tilt * factors.Tilt, turn * factors.Turn, 0)
end

local function round(value: number, step: number): number
	return math.floor(value / step + 0.5) * step
end

-- options:
--   config       FPSys Config (default FPSys.Config)
--   remote       the RemoteEvent (default ReplicatedStorage[Config.RemoteName], waited for)
--   getAimCFrame function(): CFrame?  the aim camera (default Camera.CFrame)
function LookAngles.new(options)
	options = options or {}
	local self = setmetatable({}, LookAngles)
	self.options = options
	self.config = options.config or DefaultConfig
	self.rotators = {}
	self.remote = options.remote
	self.lastSend = -math.huge
	self.sentTurn, self.sentTilt = 0, 0
	self.clock = options.clock or os.clock
	return self
end

function LookAngles:isFirstPerson(): boolean
	local camera = Workspace.CurrentCamera
	if not camera or camera.CameraType == Enum.CameraType.Scriptable then
		return false
	end
	local aim = (self.options.getAimCFrame and self.options.getAimCFrame()) or camera.CFrame
	return (camera.Focus.Position - aim.Position).Magnitude <= 1
end

local function rigAttachment(motor: Motor6D): Attachment?
	local part0 = motor.Part0
	local attachment = part0 and part0:FindFirstChild(motor.Name .. "RigAttachment")
	return if attachment and attachment:IsA("Attachment") then attachment else nil
end

function LookAngles:_addMotor(rotator, motor: Motor6D)
	local part1 = motor.Part1
	if not part1 then
		return
	end
	local factors = self.config.RotationFactors[part1.Name]
	if not factors then
		return
	end
	-- R15 motors follow their rig attachment (body scaling moves it); R6 keeps its C0.
	rotator.motors[motor] = { factors = factors, c0 = motor.C0, origin = rigAttachment(motor) }
end

-- Starts animating a character's look (called for every tagged humanoid).
function LookAngles:mount(humanoid: Humanoid)
	local character = humanoid.Parent
	if not character or self.rotators[character] then
		return self.rotators[character]
	end
	local rotator = {
		motors = {},
		turn = { goal = 0, current = 0 },
		tilt = { goal = 0, current = 0 },
		isLocal = Players:GetPlayerFromCharacter(character) == Players.LocalPlayer,
	}
	self.rotators[character] = rotator
	for _, desc in character:GetDescendants() do
		if desc:IsA("Motor6D") then
			self:_addMotor(rotator, desc)
		end
	end
	rotator.listener = character.DescendantAdded:Connect(function(desc)
		if desc:IsA("Motor6D") then
			self:_addMotor(rotator, desc)
		end
	end)
	return rotator
end

function LookAngles:unmount(character)
	local rotator = self.rotators[character]
	if not rotator then
		return
	end
	if rotator.listener then
		rotator.listener:Disconnect()
	end
	for motor, data in rotator.motors do
		if motor.Parent then
			motor.C0 = if data.origin then data.origin.CFrame else data.c0
		end
	end
	self.rotators[character] = nil
end

-- A replicated look from another player.
function LookAngles:receive(player: Player, turn: number, tilt: number)
	if player == Players.LocalPlayer or type(turn) ~= "number" or type(tilt) ~= "number" then
		return
	end
	local rotator = player.Character and self.rotators[player.Character]
	if rotator then
		rotator.turn.goal = math.clamp(turn, -1, 1)
		rotator.tilt.goal = math.clamp(tilt, -1, 1)
	end
end

function LookAngles:_updateLocal()
	local player = Players.LocalPlayer
	local character = player and player.Character
	local rotator = character and self.rotators[character]
	local root = character and character:FindFirstChild("HumanoidRootPart")
	local camera = Workspace.CurrentCamera
	if not (rotator and root and camera) then
		return
	end
	local aim = (self.options.getAimCFrame and self.options.getAimCFrame()) or camera.CFrame
	local firstPerson = self:isFirstPerson()
	local turn, tilt = LookAngles.compute(root.CFrame, aim.LookVector, firstPerson)
	rotator.turn.goal, rotator.tilt.goal = turn, tilt
	rotator.snap = firstPerson

	-- Send when the rounded angles changed, at most once per SendInterval.
	local cfg = self.config.LookAngles
	local now = self.clock()
	if self.remote and now - self.lastSend > cfg.SendInterval then
		local sendTurn, sendTilt = round(turn, cfg.SendStep), round(tilt, cfg.SendStep)
		if sendTurn ~= self.sentTurn or sendTilt ~= self.sentTilt then
			self.sentTurn, self.sentTilt = sendTurn, sendTilt
			self.lastSend = now
			self.remote:FireServer(sendTurn, sendTilt)
		end
	end
end

-- Steps every rotator and writes the motors. Call every Heartbeat.
function LookAngles:update(dt: number)
	self:_updateLocal()
	local cfg = self.config.LookAngles
	local camera = Workspace.CurrentCamera
	local cameraPosition = camera and camera.CFrame.Position
	for character, rotator in self.rotators do
		if not character.Parent then
			self:unmount(character)
			continue
		end
		if not rotator.isLocal and cameraPosition then
			local root = character:FindFirstChild("HumanoidRootPart")
			if root and (root.Position - cameraPosition).Magnitude > cfg.MaxDistance then
				continue
			end
		end
		local turn, tilt = rotator.turn, rotator.tilt
		if rotator.snap then
			turn.current, tilt.current = turn.goal, tilt.goal
		else
			turn.current = LookAngles.step(turn.current, turn.goal, cfg.TurnRate, dt)
			tilt.current = LookAngles.step(tilt.current, tilt.goal, cfg.TurnRate, dt)
		end
		for motor, data in rotator.motors do
			if not motor.Parent then
				rotator.motors[motor] = nil
				continue
			end
			local base = if data.origin and data.origin.Parent then data.origin.CFrame else data.c0
			motor.C0 = base * LookAngles.motorRotation(data.factors, turn.current, tilt.current)
		end
	end
end

function LookAngles:destroy()
	for character in self.rotators do
		self:unmount(character)
	end
end

return LookAngles
