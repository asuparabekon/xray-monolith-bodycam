-- FPSys: look angle math, footstep sound choice, and the camera effects layer.
return function(T)
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local env = Env.new()
	env.install()
	local require = T.isolatedRequire()
	local FPSys = require(T.Src.FPSys)
	local LookAngles, Footsteps, CameraEffects = FPSys.LookAngles, FPSys.Footsteps, FPSys.CameraEffects
	local Config = FPSys.Config

	T.test("look angles: turn left is positive, up is positive", function()
		local root = CFrame.new(0, 0, 0)
		local left = Vector3.new(-1, 0, 0)
		local turn, tilt = LookAngles.compute(root, left, true)
		T.near(turn, 1, 1e-9, "turn")
		T.near(tilt, 0, 1e-9, "tilt")
		_, tilt = LookAngles.compute(root, Vector3.new(0, 0.6, -0.8), true)
		T.near(tilt, 0.6, 1e-9, "tilt up")
		-- Third person, looking behind: both flip.
		turn = LookAngles.compute(root, Vector3.new(-0.6, 0, 0.8), false)
		T.near(turn, -0.6, 1e-9, "flipped behind")
		T.near(LookAngles.step(0, 1, 6, 1 / 60), 0.1, 1e-9, "rate-limited step")
		T.near(LookAngles.step(0.95, 1, 6, 1 / 60), 1, 1e-9, "no overshoot")
	end)

	T.test("motor rotation tilts the head up for a positive tilt", function()
		local rot = LookAngles.motorRotation(Config.RotationFactors.Head, 0, 1)
		T.truthy(rot.LookVector.Y > 0, "head looks up")
		rot = LookAngles.motorRotation(Config.RotationFactors.Head, 1, 0)
		T.truthy(rot.LookVector.X < 0, "head turns left")
	end)

	T.test("look angles drive motors and replicate to the sender's character", function()
		local motor = { Parent = true, C0 = CFrame.new(0, 1, 0), Name = "Neck" }
		motor.Part1 = { Name = "Head" }
		motor.Part0 = { FindFirstChild = function() return nil end }
		function motor:IsA(c)
			return c == "Motor6D"
		end
		local other = { Name = "Other", Character = nil }
		local otherRoot = { Position = Vector3.new(3, 3, 0) }
		local otherCharacter = {
			Parent = true,
			GetDescendants = function()
				return { motor }
			end,
			DescendantAdded = Env.signal(),
			FindFirstChild = function(_, name)
				return if name == "HumanoidRootPart" then otherRoot else nil
			end,
		}
		other.Character = otherCharacter
		local lookAngles = LookAngles.new({ config = Config })
		lookAngles:mount({ Parent = otherCharacter })
		-- The server relays (player, turn, tilt): it must land on that player's character.
		lookAngles:receive(other, 0, 0.5)
		for _ = 1, 30 do
			lookAngles:update(1 / 60)
		end
		local expected = CFrame.new(0, 1, 0) * LookAngles.motorRotation(Config.RotationFactors.Head, 0, 0.5)
		T.near((motor.C0.LookVector - expected.LookVector).Magnitude, 0, 1e-9, "neck turned")
		lookAngles:unmount(otherCharacter)
		T.near(motor.C0.LookVector.Y, 0, 1e-9, "restored on unmount")
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
		local bodycamConfig = { footstep = { enable = true }, camera = { enable = true } }
		local effects = CameraEffects.new({ config = Config, bodycamConfig = bodycamConfig, blurParent = false })
		T.equal(effects:enabled("WalkBob"), false, "bodycam footstep camera owns the bob")
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
			effects:apply(view, frameInfo())
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
		effects:apply(view, frameInfo())
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
