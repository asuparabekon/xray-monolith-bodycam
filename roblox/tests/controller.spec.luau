-- Drives Roblox/BodycamController.luau against mocked Roblox services: a walking,
-- turning character with a first-person camera, shots and ADS. Checks that the
-- adapter runs, never feeds the visual lag back into the aim, and moves the view.
return function(T)
	local mock = T.require(T.Tests.mocks.RobloxMath)
	local Vector3, CFrame = mock.Vector3, mock.CFrame

	-- Enum: any Enum.X.Y is a unique value; RenderPriority values are numbers.
	local enumCache = {}
	local Enum = setmetatable({
		RenderPriority = { Camera = { Value = 200 } },
	}, {
		__index = function(_, category)
			local items = enumCache[category] or setmetatable({}, {
				__index = function(t, name)
					local item = { Name = name, EnumType = category }
					rawset(t, name, item)
					return item
				end,
			})
			enumCache[category] = items
			return items
		end,
	})

	local bound = {}
	local RunService = {
		BindToRenderStep = function(_, name, priority, fn)
			table.insert(bound, { name = name, priority = priority, fn = fn })
			table.sort(bound, function(a, b)
				return a.priority < b.priority
			end)
		end,
		UnbindFromRenderStep = function(_, name)
			for i, b in bound do
				if b.name == name then
					table.remove(bound, i)
					return
				end
			end
		end,
	}

	local camera = { CFrame = CFrame.new(0, 5, 0) }
	local root = { AssemblyLinearVelocity = Vector3.new(0, 0, 0) }
	local humanoid = {
		Health = 100,
		WalkSpeed = 16,
		MoveDirection = Vector3.new(0, 0, 0),
		FloorMaterial = Enum.Material.Plastic,
		RootPart = root,
		moves = 0,
	}
	function humanoid:GetState()
		return Enum.HumanoidStateType.Running
	end
	function humanoid:Move()
		self.moves += 1
	end
	local character = {
		FindFirstChildOfClass = function(_, className)
			return if className == "Humanoid" then humanoid else nil
		end,
	}
	local player = {
		Character = character,
		CharacterAdded = { Connect = function()
			return { Disconnect = function() end }
		end },
	}
	local raycasts = 0
	local Workspace = {
		CurrentCamera = camera,
		Raycast = function()
			raycasts += 1
			return nil
		end,
	}
	local services = {
		Players = { LocalPlayer = player },
		RunService = RunService,
		Workspace = Workspace,
	}
	local game = { GetService = function(_, name)
		return services[name]
	end }

	_G.Vector3, _G.CFrame, _G.Enum, _G.game = Vector3, CFrame, Enum, game
	_G.RaycastParams = { new = function()
		return {}
	end }

	local Controller = T.require(T.Bodycam.Roblox.BodycamController)
	local CoordinateSpace = T.require(T.Bodycam.Roblox.CoordinateSpace)

	T.test("controller runs frames without feeding lag back into the aim", function()
		local controller = Controller.new({ walkSpeed = 16, sprintSpeed = 25.6, applyMovementInertia = true })
		controller:start()
		T.equal(#bound, 2, "two render steps bound")
		local yaw, moved = 0, false
		for i = 1, 240 do
			-- The camera scripts: restore step, then the aim the player asked for.
			bound[1].fn(1 / 60)
			if i > 1 then
				-- The restore step must hand the camera scripts last frame's aim, not the view.
				local aimYaw = CoordinateSpace.aimFromLook(camera.CFrame.LookVector)
				T.near(aimYaw, yaw, 1e-9, "aim restored before the camera scripts")
			end
			yaw = 0.8 * math.sin(i / 30)
			camera.CFrame = CFrame.new(0, 5, 0) * CoordinateSpace.aimRotation(yaw, 0.1)
			humanoid.MoveDirection = if i > 30 then CoordinateSpace.aimRotation(yaw, 0).LookVector else Vector3.zero
			root.AssemblyLinearVelocity = humanoid.MoveDirection * 16
			if i % 40 == 0 then
				controller:notifyShot(1)
			end
			controller:setAiming(i > 180)
			bound[2].fn(1 / 60)
			local view = controller:getViewCFrame()
			T.truthy(view ~= nil, "view composed")
			if (view.LookVector - controller:getAimCFrame().LookVector).Magnitude > 1e-6 then
				moved = true
			end
		end
		T.truthy(moved, "the bodycam view differs from the aim")
		T.truthy(humanoid.moves > 0, "movement inertia drove the humanoid")
		T.truthy(raycasts > 0, "footstep wall check raycast")
		controller:stop()
		T.equal(#bound, 0, "render steps unbound")
	end)
end
