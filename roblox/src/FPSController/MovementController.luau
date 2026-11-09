-- Applies MovementStateMachine to the local Humanoid: WalkSpeed per gait, slides,
-- crouch/prone camera drop (a bodycam view layer), jump rules, and the sprint and
-- crouch flags the bodycam reads.
--
-- Crouch and prone change speed, jumping and the camera height. Playing crouch and
-- prone animations is left to the game: listen with onEvent.

local Players = game:GetService("Players")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local MovementStateMachine = require(script.Parent.MovementStateMachine)
local CoordinateSpace = require(script.Parent.Parent.Bodycam.Roblox.CoordinateSpace)

local MovementController = {}
MovementController.__index = MovementController

-- options:
--   settings    MovementStateMachine.defaultSettings() shape
--   controller  BodycamController (for the camera drop and the aim yaw)
--   onEvent     function(name) for state events (animations, sounds)
function MovementController.new(options)
	options = options or {}
	local self = setmetatable({}, MovementController)
	self.options = options
	self.listeners = {}
	self.machine = MovementStateMachine.new(options.settings, function(name)
		self:_event(name)
	end)
	self.slideDirection = nil
	self.connections = {}
	self.removeView = nil
	if options.onEvent then
		table.insert(self.listeners, options.onEvent)
	end
	return self
end

function MovementController:onEvent(fn)
	table.insert(self.listeners, fn)
end

function MovementController:_event(name: string)
	if name == "SlideStarted" then
		local root = self:_root()
		local humanoid = self:_humanoid()
		local direction = humanoid and humanoid.MoveDirection
		if not direction or direction.Magnitude < 0.1 then
			direction = root and root.CFrame.LookVector or Vector3.new(0, 0, -1)
		end
		self.slideDirection = Vector3.new(direction.X, 0, direction.Z).Unit
	elseif name == "SlideEnded" then
		self.slideDirection = nil
	end
	for _, listener in self.listeners do
		task.spawn(listener, name)
	end
end

function MovementController:_humanoid(): Humanoid?
	local character = Players.LocalPlayer.Character
	return character and character:FindFirstChildOfClass("Humanoid")
end

function MovementController:_root(): BasePart?
	local humanoid = self:_humanoid()
	return humanoid and humanoid.RootPart
end

-- Room to stand up: nothing solid within standing height above the root.
function MovementController:canStand(): boolean
	local root = self:_root()
	local humanoid = self:_humanoid()
	if not (root and humanoid) then
		return true
	end
	local params = RaycastParams.new()
	params.FilterType = Enum.RaycastFilterType.Exclude
	params.FilterDescendantsInstances = { humanoid.Parent }
	local height = root.Size.Y / 2 + (self.options.headroom or 2.2)
	return Workspace:Raycast(root.Position, Vector3.new(0, height, 0), params) == nil
end

function MovementController:setSprint(held: boolean)
	self.machine:setSprint(held)
end

function MovementController:toggleCrouch(): boolean
	return self.machine:toggleCrouch(self:canStand())
end

function MovementController:toggleProne(): boolean
	return self.machine:toggleProne(self:canStand())
end

function MovementController:jump(): boolean
	local humanoid = self:_humanoid()
	if self.machine:requestJump(self:canStand()) and humanoid then
		humanoid.Jump = true
		return true
	end
	return false
end

function MovementController:slide(): boolean
	return self.machine:requestSlide()
end

function MovementController:isSprinting(): boolean
	return self.machine:isSprinting()
end

function MovementController:isCrouching(): boolean
	return self.machine:isCrouching() or self.machine:isProne()
end

function MovementController:start()
	local controller = self.options.controller
	if controller then
		self.removeView = controller:addViewModifier(function(view)
			local drop = self.machine.drop
			return if drop > 1e-3 then view * CFrame.new(0, -drop, 0) else view
		end)
	end
	-- After the ControlModule (Input) so the slide's Move wins; before the camera.
	self.bindName = "FPSMovement" .. tostring(self)
	RunService:BindToRenderStep(self.bindName, Enum.RenderPriority.Input.Value + 1, function(dt)
		self:_step(dt)
	end)
end

function MovementController:stop()
	if self.bindName then
		RunService:UnbindFromRenderStep(self.bindName)
		self.bindName = nil
	end
	if self.removeView then
		self.removeView()
		self.removeView = nil
	end
end

function MovementController:_step(dt: number)
	local humanoid = self:_humanoid()
	local root = humanoid and humanoid.RootPart
	if not (humanoid and root) or humanoid.Health <= 0 then
		return
	end
	-- Move input relative to the aim's yaw: x right, y forward.
	local camera = Workspace.CurrentCamera
	local controller = self.options.controller
	local aim = (controller and controller:getAimCFrame()) or (camera and camera.CFrame)
	local yaw = if aim then CoordinateSpace.aimFromLook(aim.LookVector) else 0
	local right, forward = CoordinateSpace.yawFrame(yaw)
	local move = humanoid.MoveDirection
	local grounded = humanoid.FloorMaterial ~= Enum.Material.Air

	local speed = self.machine:update({
		dt = dt,
		moveX = move:Dot(right),
		moveY = move:Dot(forward),
		grounded = grounded,
		canStand = self:canStand(),
	})

	-- No jumping from crouch or prone: the jump key stands up instead (see jump()).
	local standing = self.machine.pose == MovementStateMachine.Pose.Standing
	humanoid:SetStateEnabled(Enum.HumanoidStateType.Jumping, standing)

	if self.machine:isSliding() and self.slideDirection then
		humanoid.WalkSpeed = speed
		humanoid:Move(self.slideDirection, false)
	elseif self.machine.gait ~= MovementStateMachine.Gait.InAir then
		humanoid.WalkSpeed = speed > 0 and speed or self.machine.settings.walkSpeed
	end
end

return MovementController
