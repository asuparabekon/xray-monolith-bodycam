-- Unit tests for the parts the golden traces do not cover: the camera rig's deadzone
-- lag and composition, movement inertia, the settings registry, gait speed and the
-- Roblox coordinate conversions (against a CFrame mock).
return function(T)
	local Bodycam = T.require(T.Bodycam)
	local CameraRig = Bodycam.CameraRig
	local Simulation = Bodycam.Simulation
	local Settings = Bodycam.Settings
	local MovementInertia = Bodycam.MovementInertia
	local GaitSpeed = Bodycam.GaitSpeed
	local Vec3 = Bodycam.Vec3
	local MoveFlags = Bodycam.MoveFlags

	local function rigSettings(config)
		return { camera = config.camera, head = config.head, footstep = config.footstep, sprint_sway = config.sim.sprint.enable }
	end
	local idleSway = { offset = Vec3.new(), pitch = 0, reach = 0 }
	local idleSignals = { move_intent = Vec3.new(), mouse_speed = Vec3.new() }

	T.test("deadzone lag holds inside the deadzone and is bounded outside", function()
		local config = Settings.defaultConfig()
		config.camera.follow_lag = false
		config.footstep.enable = false
		local rig = CameraRig.new()
		rig:reset(0, 0)
		local input = CameraRig.newInput()
		input.dt = 1 / 60
		-- 1 degree is inside the 1.5 degree hip deadzone: the camera does not follow.
		input.aim_yaw = math.rad(1)
		local c = rig:compose(rigSettings(config), input, idleSignals, idleSway, nil)
		T.near(c.lag.yaw, 0, 1e-9, "held in deadzone")
		-- A 40 degree snap is limited to the 19 degree maximum lag.
		input.aim_yaw = math.rad(40)
		c = rig:compose(rigSettings(config), input, idleSignals, idleSway, nil)
		T.truthy(math.rad(40) - c.lag.yaw <= math.rad(19) + 1e-9, "lag bounded")
		-- Holding still, it creeps toward the deadzone edge: the soft zone's ramp
		-- falls to zero there, so the edge is approached but not reached.
		local previous = math.huge
		for _ = 1, 600 do
			c = rig:compose(rigSettings(config), input, idleSignals, idleSway, nil)
			local lagDegrees = math.deg(input.aim_yaw - c.lag.yaw)
			T.truthy(lagDegrees <= previous + 1e-12, "lag only shrinks")
			previous = lagDegrees
		end
		T.truthy(previous > 1.5 and previous < 2, "inside the soft zone, outside the deadzone")
		T.truthy(c.composes, "composes")
	end)

	T.test("rig composes nothing with every camera feature off", function()
		local config = Settings.defaultConfig()
		Settings.applyPreset(config, Settings.Preset.Off)
		config.sim.sprint.enable = false
		local rig = CameraRig.new()
		local input = CameraRig.newInput()
		input.dt = 1 / 60
		local c = rig:compose(rigSettings(config), input, idleSignals, idleSway, nil)
		T.equal(c.composes, false, "composes")
	end)

	T.test("footstep offset is clamped before a wall", function()
		local config = Settings.defaultConfig()
		config.camera.enable = false
		local rig = CameraRig.new()
		local input = CameraRig.newInput()
		local calls = 0
		local host = {
			horizontalClearance = function(right, forward, maxDistance)
				calls += 1
				T.near(right * right + forward * forward, 1, 1e-9, "unit direction")
				return 0.1 -- the wall is right at the clearance distance
			end,
		}
		local phase = 0
		local c
		for _ = 1, 240 do
			input.dt = 1 / 60
			phase = (phase + input.dt) % 1
			input.foot_phase = phase
			input.sprinting = true
			input.reference_speed = 3.25
			input.velocity = Vec3.new(0, 0, 3.25)
			c = rig:compose(rigSettings(config), input, idleSignals, idleSway, host)
		end
		T.truthy(calls > 0, "wall check called")
		local f = c.footstep_offset
		T.near(math.sqrt(f.x * f.x + f.z * f.z), 0, 1e-9, "no horizontal travel into the wall")
		T.truthy(math.abs(f.y) > 1e-4, "vertical bob remains")
	end)

	T.test("movement inertia eases in and never overshoots", function()
		local settings = MovementInertia.defaultSettings()
		local state = MovementInertia.newState()
		local input = { dt = 1 / 60, move_flags = 0, ads = false, target_velocity = { x = 0, y = 0, z = 0 } }
		MovementInertia.update(settings, state, input)
		input.move_flags = MoveFlags.Forward
		input.target_velocity = { x = 0, y = 0, z = 4 }
		local out = MovementInertia.update(settings, state, input)
		T.truthy(out.filtered_speed < 1, "starts slowly")
		for _ = 1, 300 do
			out = MovementInertia.update(settings, state, input)
			T.truthy(out.filtered_speed <= 4 + 1e-9, "no overshoot")
		end
		T.near(out.speed_fraction, 1, 1e-3, "reaches the target")
		input.move_flags = MoveFlags.Forward + MoveFlags.Crouch
		out = MovementInertia.update(settings, state, input)
		T.equal(out.speed_fraction, 1, "crouch bypasses")
	end)

	T.test("settings clamp, presets switch features", function()
		local config = Settings.defaultConfig()
		T.truthy(Settings.set(config, "hip_camera_max_yaw_offset", 500), "known float")
		T.equal(config.camera.hip.max_yaw, 90, "clamped to range")
		T.equal(Settings.set(config, "no_such_setting", 1), false, "unknown name")
		T.equal(Settings.set(config, "hip_camera_max_yaw_offset", 0 / 0), false, "NaN rejected")
		Settings.applyPreset(config, Settings.Preset.Off)
		T.equal(config.camera.enable, false, "off preset disables")
		T.equal(config.camera.hip.follow_speed, 12, "off preset float")
		Settings.applyPreset(config, Settings.Preset.Balanced)
		T.equal(config.sim.fire.enable, true, "balanced enables")
		T.equal(config.camera.hip.max_yaw, 9, "balanced float")
		Settings.applyPreset(config, Settings.Preset.Default)
		T.equal(config.camera.hip.max_yaw, 19, "default restored")
		T.truthy(#Settings.list() > 90, "registry lists every setting")
	end)

	T.test("gait speed anchors map onto Reference speeds", function()
		T.near(GaitSpeed.referenceEquivalentSpeed(16, 16, 25.6), 1.25, 1e-9, "walk")
		T.near(GaitSpeed.referenceEquivalentSpeed(25.6, 16, 25.6), 3.25, 1e-9, "sprint")
		T.near(GaitSpeed.referenceEquivalentSpeed(8, 16, 25.6), 0.625, 1e-9, "half walk")
		local clock = GaitSpeed.StrideClock.new()
		T.equal(clock:update(1 / 60, 0, true), nil, "standing has no phase")
		T.truthy(clock:update(1 / 60, 1.25, true) > 0, "walking advances")
	end)

	T.test("shot kicks the viewmodel and lowering waits", function()
		local core = Bodycam.BodycamCore.new()
		core:reset(0, 0, 0)
		local frame = { camera = CameraRig.newInput(), move_flags = MoveFlags.Forward, accelerated = true }
		frame.camera.dt = 1 / 60
		core:setMovementSpeed(1.25, 1.25, 1)
		core:notifyShot(1, false)
		core:update(frame, nil)
		local pos, rot = core:getHudOffset(1)
		T.truthy(pos and pos.z < 0, "kick pulls the gun back")
		T.truthy(rot.y > 0, "kick pitches the muzzle up")
	end)

	-- Roblox conversions against the mock.
	local mock = T.require(T.Tests.mocks.RobloxMath)
	_G.Vector3 = mock.Vector3
	_G.CFrame = mock.CFrame
	local CoordinateSpace = T.require(T.Bodycam.Roblox.CoordinateSpace)

	local function closeVector(a, b, tol, message)
		T.near((a - b).Magnitude, 0, tol or 1e-9, message)
	end

	T.test("aim yaw/pitch round-trips through a Roblox look vector", function()
		for _, case in { { 0.3, 0.2 }, { -2.5, -0.6 }, { 3.0, 0.0 }, { 0, 1.2 } } do
			local yaw, pitch = case[1], case[2]
			local look = CoordinateSpace.aimRotation(yaw, pitch).LookVector
			-- X-Ray's view direction, mirrored in Z.
			closeVector(look, Vector3.new(-math.cos(pitch) * math.sin(yaw), math.sin(pitch), -math.cos(pitch) * math.cos(yaw)))
			local y2, p2 = CoordinateSpace.aimFromLook(look)
			T.near(y2, yaw, 1e-9, "yaw")
			T.near(p2, pitch, 1e-9, "pitch")
		end
	end)

	T.test("setHPB with no bank equals the aim rotation", function()
		local a = CoordinateSpace.hpb(0.7, -0.3, 0)
		local b = CoordinateSpace.aimRotation(0.7, -0.3)
		closeVector(a.RightVector, b.RightVector)
		closeVector(a.UpVector, b.UpVector)
		closeVector(a.LookVector, b.LookVector)
	end)

	T.test("yaw frame matches the X-Ray yaw basis", function()
		local right, forward = CoordinateSpace.yawFrame(0.9)
		local aim = CoordinateSpace.aimRotation(0.9, 0)
		closeVector(right, aim.RightVector)
		closeVector(forward, aim.LookVector)
	end)

	T.test("zero composition leaves the aim camera unchanged", function()
		local aim = CFrame.new(10, 5, -3) * CoordinateSpace.aimRotation(0.4, 0.1)
		local composition = {
			lag = { yaw = 0.4, pitch = 0.1, pos = Vec3.new(), sprint_pitch = 0 },
			head_rotation = Bodycam.Reference.CameraDelta.IDENTITY,
			footstep_offset = Vec3.new(),
		}
		local view = CoordinateSpace.composeView(aim, 0.4, 0.1, composition, 1 / 0.28)
		closeVector(view.Position, aim.Position)
		closeVector(view.LookVector, aim.LookVector)
	end)

	T.test("lagging yaw turns the view toward the old aim", function()
		local aim = CoordinateSpace.aimRotation(0.5, 0)
		local composition = {
			lag = { yaw = 0.4, pitch = 0, pos = Vec3.new(0.01, 0, 0), sprint_pitch = 0 },
			head_rotation = Bodycam.Reference.CameraDelta.IDENTITY,
			footstep_offset = Vec3.new(0, 0, 0.02),
		}
		local view = CoordinateSpace.composeView(aim, 0.5, 0, composition, 1 / 0.28)
		closeVector(view.LookVector, CoordinateSpace.aimRotation(0.4, 0).LookVector, 1e-9, "view at lag yaw")
		-- The 1 cm shift is along the view's right, the 2 cm footstep along the aim's forward.
		local expected = view.RightVector * (0.01 / 0.28) + aim.LookVector * (0.02 / 0.28)
		closeVector(view.Position, expected, 1e-9, "shift and footstep")
	end)

	T.test("camera delta matrices are proper rotations in Roblox", function()
		local m = CoordinateSpace.deltaToCFrame(Bodycam.Reference.CameraDelta.fromRotation(3, -7, 12))
		T.near(m.RightVector:Cross(m.UpVector):Dot(-m.LookVector), 1, 1e-9, "right-handed")
	end)
end
