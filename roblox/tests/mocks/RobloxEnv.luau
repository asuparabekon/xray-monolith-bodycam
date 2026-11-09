-- A small scripted Roblox client world for adapter tests: services, Enum, Instance,
-- Random, task, a camera and one local character. install() puts the Roblox globals
-- in _G; step() runs one frame of bound render steps in priority order.

local RobloxMath = require(script.Parent.RobloxMath)
local Vector3, CFrame = RobloxMath.Vector3, RobloxMath.CFrame

local Env = {}

local function signal()
	local s = { handlers = {} }
	function s:Connect(fn)
		table.insert(self.handlers, fn)
		local handlers = self.handlers
		local function disconnect()
			local i = table.find(handlers, fn)
			if i then
				table.remove(handlers, i)
			end
		end
		return { __connection = true, Disconnect = disconnect, Destroy = disconnect }
	end
	function s:Fire(...)
		for _, fn in table.clone(self.handlers) do
			fn(...)
		end
	end
	return s
end
Env.signal = signal

function Env.new()
	local env = {}
	local enumCache = {}
	env.Enum = setmetatable({
		RenderPriority = { Camera = { Value = 200 }, Input = { Value = 100 } },
	}, {
		__index = function(_, category)
			local items = enumCache[category]
			if not items then
				items = setmetatable({}, {
					__index = function(t, name)
						local item = { Name = name, EnumType = category }
						rawset(t, name, item)
						return item
					end,
				})
				enumCache[category] = items
			end
			return items
		end,
	})

	-- Instances: plain tables with a class, children and Destroy.
	local function newInstance(className)
		local inst = { ClassName = className, Name = className, children = {} }
		function inst:IsA(c)
			return self.ClassName == c
		end
		function inst:Destroy()
			self.Parent = nil
			self.destroyed = true
		end
		function inst:Play()
			self.plays = (self.plays or 0) + 1
		end
		function inst:FindFirstChild(name)
			return self.children[name]
		end
		return inst
	end
	env.created = {}
	env.Instance = {
		new = function(className)
			local inst = newInstance(className)
			table.insert(env.created, inst)
			return inst
		end,
	}

	-- Random with a fixed sequence.
	env.Random = {
		new = function()
			local state = 12345
			return {
				NextNumber = function(_, lo, hi)
					state = (state * 1103515245 + 12345) % 2147483648
					local u = state / 2147483648
					return (lo or 0) + ((hi or 1) - (lo or 0)) * u
				end,
			}
		end,
	}
	env.task = {
		spawn = function(fn, ...)
			fn(...)
		end,
	}

	-- Render steps.
	env.bound = {}
	env.heartbeat = signal()
	env.RunService = {
		Heartbeat = env.heartbeat,
		BindToRenderStep = function(_, name, priority, fn)
			table.insert(env.bound, { name = name, priority = priority, fn = fn })
			table.sort(env.bound, function(a, b)
				return a.priority < b.priority
			end)
		end,
		UnbindFromRenderStep = function(_, name)
			for i, b in env.bound do
				if b.name == name then
					table.remove(env.bound, i)
					return
				end
			end
		end,
	}

	-- Character.
	env.camera = {
		CFrame = CFrame.new(0, 5, 0),
		Focus = CFrame.new(0, 5, 0),
		FieldOfView = 70,
		CameraType = env.Enum.CameraType.Custom,
	}
	env.root = {
		Name = "HumanoidRootPart",
		AssemblyLinearVelocity = Vector3.new(0, 0, 0),
		CFrame = CFrame.new(0, 3, 0),
		Position = Vector3.new(0, 3, 0),
		Size = Vector3.new(2, 2, 1),
		children = {},
	}
	function env.root:FindFirstChild(name)
		return self.children[name]
	end
	env.root.ChildAdded = signal()
	local humanoid = {
		ClassName = "Humanoid",
		Health = 100,
		WalkSpeed = 16,
		HipHeight = 2,
		MoveDirection = Vector3.new(0, 0, 0),
		FloorMaterial = env.Enum.Material.Plastic,
		RootPart = env.root,
		Jump = false,
		moves = {},
		stateEnabled = {},
		state = env.Enum.HumanoidStateType.Running,
		AncestryChanged = signal(),
	}
	humanoid.RigType = env.Enum.HumanoidRigType.R15
	function humanoid:GetState()
		return self.state
	end
	function humanoid:Move(direction)
		table.insert(self.moves, direction)
	end
	function humanoid:SetStateEnabled(state, enabled)
		self.stateEnabled[state] = enabled
	end
	function humanoid:IsA(c)
		return c == "Humanoid"
	end
	local floorChanged = signal()
	function humanoid:GetPropertyChangedSignal()
		return floorChanged
	end
	env.humanoid = humanoid
	local character = {
		Name = "Player1",
		FindFirstChildOfClass = function(_, className)
			return if className == "Humanoid" then humanoid else nil
		end,
		FindFirstChild = function(_, name)
			return if name == "HumanoidRootPart" then env.root else nil
		end,
		WaitForChild = function(_, name)
			return if name == "HumanoidRootPart" then env.root else nil
		end,
		GetDescendants = function()
			return {}
		end,
		DescendantAdded = signal(),
		Parent = true,
	}
	humanoid.Parent = character
	env.character = character
	env.player = { Character = character, CharacterAdded = signal(), Name = "Player1" }
	env.Players = {
		LocalPlayer = env.player,
		GetPlayerFromCharacter = function(_, c)
			return if c == character then env.player else nil
		end,
	}
	env.walls = {} -- functions(origin, direction) -> hit?
	env.raycasts = 0
	env.Workspace = {
		CurrentCamera = env.camera,
		Raycast = function(_, origin, direction)
			env.raycasts += 1
			for _, wall in env.walls do
				local hit = wall(origin, direction)
				if hit then
					return hit
				end
			end
			return nil
		end,
	}
	env.UserInputService = { MouseDeltaSensitivity = 1, mouseDelta = { X = 0, Y = 0 } }
	function env.UserInputService:GetMouseDelta()
		return self.mouseDelta
	end
	env.Lighting = {}
	env.CollectionService = {
		tagged = {},
		added = signal(),
		GetTagged = function(self)
			return self.tagged
		end,
		GetInstanceAddedSignal = function(self)
			return self.added
		end,
	}
	local services = {
		Players = env.Players,
		RunService = env.RunService,
		Workspace = env.Workspace,
		UserInputService = env.UserInputService,
		Lighting = env.Lighting,
		CollectionService = env.CollectionService,
		ReplicatedStorage = {},
	}
	env.game = {
		GetService = function(_, name)
			return services[name] or error("no mock service " .. name)
		end,
	}

	function env.install()
		_G.Vector3, _G.CFrame = Vector3, CFrame
		_G.Enum, _G.game, _G.Instance, _G.Random, _G.task = env.Enum, env.game, env.Instance, env.Random, env.task
		_G.workspace = env.Workspace
		_G.RaycastParams = {
			new = function()
				return {}
			end,
		}
		_G.typeof = function(v)
			if type(v) == "table" and v.EnumType then
				return "EnumItem"
			elseif type(v) == "table" and v.__connection then
				return "RBXScriptConnection"
			end
			return type(v)
		end
		_G.warn = function(...)
			print("warn:", ...)
		end
	end

	-- One frame: every bound render step in priority order, then Heartbeat.
	-- beforeCamera(dt) plays the camera scripts and input between the bodycam's steps.
	function env.step(dt, beforeCamera)
		for _, b in table.clone(env.bound) do
			-- The camera scripts run at RenderPriority.Camera (200).
			if b.priority >= 200 and beforeCamera then
				beforeCamera(dt)
				beforeCamera = nil
			end
			b.fn(dt)
		end
		if beforeCamera then
			beforeCamera(dt)
		end
		env.heartbeat:Fire(dt)
	end

	return env
end

return Env
