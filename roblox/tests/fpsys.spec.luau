-- FPSys: the Realism look angles, footstep sound choice, and the camera effects layer.
return function(T)
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local env = Env.new()
	env.install()
	local require = T.isolatedRequire()
	local FPSys = require(T.Src.FPSys)
	local Footsteps, CameraEffects, Realism = FPSys.Footsteps, FPSys.CameraEffects, FPSys.Realism
	local Config = FPSys.Config

	T.test("Realism look angles: left of the root is positive pitch, up is positive yaw", function()
		local root = CFrame.new(0, 0, 0)
		local pitch, yaw = Realism.computeLookAngle(root, Vector3.new(-1, 0, 0), true)
		T.near(pitch, 1, 1e-9, "pitch")
		T.near(yaw, 0, 1e-9, "yaw")
		_, yaw = Realism.computeLookAngle(root, Vector3.new(0, 0.6, -0.8), true)
		T.near(yaw, 0.6, 1e-9, "yaw up")
		-- Third person, looking behind: both flip.
		pitch = Realism.computeLookAngle(root, Vector3.new(-0.6, 0, 0.8), false)
		T.near(pitch, -0.6, 1e-9, "flipped behind")
		local state = { Goal = 1, Current = 0 }
		T.near(Realism.expSmooth(state, 0.1, 0.1), 1 - math.exp(-1), 1e-12, "exponential smoothing")
	end)

	T.test("Realism joint rotation turns the head up and left", function()
		local factors = Realism.Config.RotationFactors.Head
		T.truthy(Realism.jointRotation(0, factors.Yaw).LookVector.Y > 0, "looks up")
		T.truthy(Realism.jointRotation(factors.Pitch, 0).LookVector.X < 0, "turns left")
	end)

	-- A character whose Head joint is a Motor6D or (Avatar Joint Upgrade) an
	-- AnimationConstraint with a read-only C0.
	local function joint(className, tracks)
		local head = { Name = "Head" }
		local motor = { Active = true, Enabled = true, Transform = CFrame.identity, Parent = true }
		if className == "AnimationConstraint" then
			motor.Attachment1 = { Name = "NeckRigAttachment", Parent = head }
			function motor.Attachment1:IsA(c)
				return c == "Attachment"
			end
		else
			motor.Part1 = head
			function head:IsA()
				return false
			end
		end
		function motor:IsA(c)
			return c == className
		end
		if className == "AnimationConstraint" then
			-- C0 and the other Motor6D aliases are read-only; only Transform may be written.
			local transform = motor.Transform
			motor.Transform = nil
			setmetatable(motor, {
				__index = function(_, key)
					return if key == "Transform" then transform else nil
				end,
				__newindex = function(_, key, value)
					if key ~= "Transform" then
						error(key .. " is read-only", 2)
					end
					transform = value
				end,
			})
		end
		local animator = { GetPlayingAnimationTracks = function()
			return tracks
		end }
		local root = { Position = Vector3.new(3, 3, 0), CFrame = CFrame.new(3, 3, 0) }
		root.AssemblyRootPart = root
		local humanoid = { RootPart = root }
		function humanoid:FindFirstChildOfClass(c)
			return if c == "Animator" then animator else nil
		end
		local character = { Parent = true, DescendantAdded = Env.signal() }
		function character:IsA(c)
			return c == "Model"
		end
		function character:GetDescendants()
			return { motor }
		end
		function character:FindFirstChildOfClass(c)
			return if c == "Humanoid" then humanoid elseif c == "Tool" then nil else nil
		end
		humanoid.Parent = character
		return motor, humanoid, character
	end

	for _, case in { { "Motor6D", {} }, { "AnimationConstraint", { "walk" } } } do
		T.test("Realism drives Transform on a " .. case[1] .. " and replicates to the sender", function()
			local motor, humanoid, character = joint(case[1], case[2])
			local other = { Name = "Other", Character = character }
			function other:DistanceFromCharacter()
				return 5
			end
			local players = env.Players
			local previous = players.GetPlayerFromCharacter
			players.GetPlayerFromCharacter = function(_, c)
				return if c == character then other else previous(players, c)
			end
			local now = 0
			local realism = Realism.new({ clock = function()
				return now
			end })
			realism:MountLookAngle(humanoid)
			-- The server relays (player, pitch, yaw): it must land on that player's character.
			realism:OnLookReceive(other, 0, 0.5)
			for _ = 1, 60 do
				now += 1 / 60
				if #case[2] > 0 then
					motor.Transform = CFrame.identity -- the Animator rewrites it each frame
				end
				realism:UpdateLookAngles(1 / 60)
			end
			local expected = Realism.jointRotation(0, 0.5 * Realism.Config.RotationFactors.Head.Yaw)
			T.near((motor.Transform.LookVector - expected.LookVector).Magnitude, 0, 1e-3, "head turned")
			players.GetPlayerFromCharacter = previous
		end)
	end

	T.test("first person: the body stays visible, the head and hats hide", function()
		local FirstPerson = Realism.FirstPerson
		local camera = env.camera
		local humanoid = { ClassName = "Humanoid" }
		function humanoid:IsA(c)
			return c == "Humanoid"
		end
		camera.CameraSubject = humanoid
		local model = {}
		function model:FindFirstChildOfClass(c)
			return if c == "Humanoid" then humanoid else nil
		end
		local function part(name, parent)
			local p = { Name = name, Parent = parent }
			function p:IsA(c)
				return c == "BasePart"
			end
			function p:FindFirstAncestorOfClass()
				return nil
			end
			function p:FindFirstAncestorWhichIsA()
				return self.accessory
			end
			function p:GetChildren()
				return self.children or {}
			end
			return p
		end
		T.equal(FirstPerson.IsValidPartToModify(nil, part("Head", model)), true, "own head hides")
		T.equal(FirstPerson.IsValidPartToModify(nil, part("UpperTorso", model)), false, "torso stays visible")
		local hat = part("Handle", model)
		hat.accessory = {}
		local attachment = { Name = "HatAttachment" }
		function attachment:IsA(c)
			return c == "Attachment"
		end
		hat.children = { attachment }
		T.equal(FirstPerson.IsValidPartToModify(nil, hat), true, "hat hides")

		-- The overloads on the PlayerModule's tables.
		local baseCamera = { GetSubjectPosition = function()
			return Vector3.new(1, 2, 3)
		end }
		FirstPerson._mountBaseCamera(baseCamera)
		T.equal(baseCamera.GetSubjectPosition, FirstPerson.GetSubjectPosition, "subject position overloaded")
		local transparency = {
			Update = function() end,
			IsValidPartToModify = function() end,
			SetupTransparency = function() end,
		}
		FirstPerson._mountTransparency(transparency)
		T.equal(transparency.IsValidPartToModify, FirstPerson.IsValidPartToModify, "part filter overloaded")
		T.truthy(transparency.ForceRefresh, "refresh requested")

		-- First person follows the aim, not the view the bodycam layers move.
		camera.Focus = CFrame.new(0, 5, 0)
		camera.CFrame = CFrame.new(1.5, 5, 0) -- leaned view
		FirstPerson.getAimCFrame = function()
			return CFrame.new(0, 5, 0)
		end
		T.equal(FirstPerson.IsInFirstPerson(), true, "aim at the focus")
		FirstPerson.getAimCFrame = nil
		T.equal(FirstPerson.IsInFirstPerson(), false, "the source's view check would fail")
		camera.CFrame = CFrame.new(0, 5, 0)
	end)

	T.test("footstep sounds follow the material map", function()
		T.equal(Footsteps.soundFor(Config, "Grass"), Config.Sounds.Grass, "direct")
		T.equal(Footsteps.soundFor(Config, "Mud"), Config.Sounds.Dirt, "mapped")
		T.equal(Footsteps.soundFor(Config, "Neon"), Config.Sounds.Concrete, "fallback")
		local volume, speed = Footsteps.runningMix(0, 2)
		T.equal(volume, 0, "silent standing")
		T.equal(speed, 1, "no division by zero")
	end)

	local function frameInfo(overrides)
		local info = {
			dt = 1 / 60,
			humanoid = env.humanoid,
			rootPart = env.root,
			character = env.character,
			state = env.Enum.HumanoidStateType.Running,
			ads = 0,
		}
		for k, v in overrides or {} do
			info[k] = v
		end
		return info
	end

	T.test("camera effects: breathing at rest, landing kick, overlap rule", function()
		local bodycam = { featureEnabled = function(_, name)
			return name == "HeadBob" or name == "HeadRoll"
		end }
		local effects = CameraEffects.new({ config = Config, bodycam = bodycam, blurParent = false })
		T.equal(effects:enabled("WalkBob"), false, "bodycam head bob owns the bob")
		T.equal(effects:enabled("StrafeTilt"), false, "bodycam head roll owns the tilt")
		local view = CFrame.new(0, 5, 0)
		local moved = false
		for _ = 1, 60 do
			local out = effects:apply(view, frameInfo())
			if (out.Position - view.Position).Magnitude > 1e-4 then
				moved = true
			end
		end
		T.truthy(moved, "breathing moves the view at rest")
		effects:apply(view, frameInfo({ state = env.Enum.HumanoidStateType.Landed }))
		local minPitch = 0
		for _ = 1, 30 do
			local out = effects:apply(view, frameInfo())
			minPitch = math.min(minPitch, out.LookVector.Y)
		end
		T.truthy(minPitch < -0.01, "landing pitches the view down")
	end)

	T.test("lean shifts sideways and stops short of a wall", function()
		local lean = 1
		local effects = CameraEffects.new({ config = Config, blurParent = false, getLean = function()
			return lean
		end })
		local view = CFrame.new(0, 5, 0)
		for _ = 1, 120 do
			effects:bodyOffset(frameInfo({ aim = view }))
		end
		local offset, roll = effects:getLean()
		T.near(offset, Config.Lean.Offset, 1e-3, "full lean offset")
		T.truthy(roll < 0, "leaning right rolls the view right")
		-- A wall 0.6 studs to the right.
		table.insert(env.walls, function(origin, direction)
			if direction.X > 0 then
				return { Position = origin + Vector3.new(0.6, 0, 0) }
			end
			return nil
		end)
		effects:bodyOffset(frameInfo({ aim = view }))
		offset = effects:getLean()
		T.near(offset, 0.6 - Config.Lean.WallClearance, 1e-6, "stopped before the wall")
		table.clear(env.walls)
	end)

	T.test("shot shake kicks the view and settles", function()
		local effects = CameraEffects.new({ config = Config, blurParent = false })
		local view = CFrame.new(0, 5, 0)
		effects:shake(1)
		local peak = 0
		for _ = 1, 10 do
			peak = math.max(peak, effects:apply(view, frameInfo()).LookVector.Y)
		end
		T.truthy(peak > 0.005, "kick up")
		for _ = 1, 240 do
			effects:apply(view, frameInfo())
		end
		local out = effects:apply(view, frameInfo())
		T.truthy(math.abs(out.LookVector.Y) < 0.005, "settled")
	end)
end
