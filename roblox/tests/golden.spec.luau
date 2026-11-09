-- Replays the C++ golden traces (tools/golden/golden.cpp) through the Luau port.
-- The C++ code works in single precision in many places and Luau in double, so
-- values are compared with a tolerance: |actual - expected| <= abs + rel * |expected|.
return function(T)
	local Bodycam = T.require(T.Bodycam)
	local Ref = Bodycam.Reference
	local Vec3 = Bodycam.Vec3

	local worst = {}
	local function close(name, actual, expected, abs, rel)
		local err = math.abs(actual - expected)
		local limit = abs + rel * math.abs(expected)
		worst[name] = math.max(worst[name] or 0, err)
		if not (err <= limit) then
			error(string.format("%s: expected %.10g, got %.10g (err %.3g > %.3g)", name, expected, actual, err, limit), 2)
		end
	end
	local function closeVec(name, actual, expected, abs, rel)
		close(name .. ".x", actual.x or actual[1], expected[1], abs, rel)
		close(name .. ".y", actual.y or actual[2], expected[2], abs, rel)
		close(name .. ".z", actual.z or actual[3], expected[3], abs, rel)
	end
	local function frameError(i, err)
		error(string.format("frame %d: %s", i, tostring(err)), 0)
	end

	T.test("simulation and mouse aim match C++", function()
		local frames = T.require(T.Golden.simulation)
		local Simulation = Bodycam.Simulation
		local MouseAim = Bodycam.MouseAim
		local settings = Simulation.defaultSettings()
		local state = Simulation.newState()
		Simulation.resetState(state, 3.0, -0.05, 0)
		local aim = MouseAim.newState()
		MouseAim.reset(aim, 3.0, -0.05)
		local input = Simulation.newInput()
		local output = Simulation.newOutput()
		for i, f in frames do
			local ok, err = pcall(function()
				MouseAim.addDelta(aim, f.dyaw, f.dpitch)
				local yaw, pitch = MouseAim.resolve(aim, f.cam_yaw, f.cam_pitch)
				close("aim.yaw", yaw, f.aim_yaw, 1e-5, 0)
				close("aim.pitch", pitch, f.aim_pitch, 1e-5, 0)
				input.dt = f.dt
				input.move_flags = f.flags
				input.ads = f.ads
				input.ads_blend = f.ads_blend
				input.weapon_lowered = f.lowered
				input.combat = f.combat
				input.accelerated = f.accel
				input.firearm_equipped = f.armed
				input.actor_speed_fraction = f.speed
				input.sprint_camera_owned_elsewhere = f.owned
				input.gun_first_follow = f.follow
				-- The C++ aim is fed in, so float rounding in the aim does not compound.
				input.mouse_aim_yaw = f.aim_yaw
				input.mouse_aim_pitch = f.aim_pitch
				if f.shot then
					Simulation.notifyShot(settings, state, 1, f.ads)
				end
				Simulation.update(settings, state, input, output)
				closeVec("mouse_speed", state.signals.mouse_speed, f.mouse_speed, 2e-3, 1e-3)
				closeVec("intent", state.signals.move_intent, f.intent, 1e-5, 1e-4)
				closeVec("sway", output.sprint_sway.offset, f.sway, 1e-7, 1e-3)
				close("sway_pitch", output.sprint_sway.pitch, f.sway_pitch, 1e-7, 1e-3)
				close("sway_reach", output.sprint_sway.reach, f.sway_reach, 1e-7, 1e-4)
				T.equal(output.viewmodel_active, f.vm, "viewmodel_active")
				closeVec("vm_pos", output.viewmodel_pos, f.vm_pos, 1e-6, 1e-3)
				closeVec("vm_rot", output.viewmodel_rot, f.vm_rot, 1e-4, 1e-3)
				close("lower", output.lower_amount, f.lower, 1e-5, 1e-3)
				T.equal(output.stalker2_arm_active, f.arm, "arm_active")
				closeVec("wrist", output.stalker2_wrist_rot, f.wrist, 1e-4, 1e-3)
				closeVec("follow_rot", output.stalker2_arm_follow_rot, f.follow_rot, 1e-4, 1e-3)
				close("walk", output.stalker2_movement_weight, f.walk, 1e-5, 1e-3)
			end)
			if not ok then
				frameError(i, err)
			end
		end
	end)

	T.test("gun-first follow and look translation match C++", function()
		local frames = T.require(T.Golden.follow)
		local follow = Ref.ClassicFollow.Follow.new()
		local look = Ref.ClassicFollow.LookTranslation.new()
		for i, f in frames do
			local ok, err = pcall(function()
				local out = follow:update({
					yaw = f.yaw,
					pitch = f.pitch,
					dt = f.dt,
					enabled = f.enabled,
					aiming = f.aiming,
					strafe = f.strafe,
					speedMetres = f.speed,
				})
				T.equal(out.ownsFollow, f.owns, "ownsFollow")
				close("cam_yaw", out.cameraYaw, f.cam_yaw, 1e-5, 0)
				close("cam_pitch", out.cameraPitch, f.cam_pitch, 1e-5, 0)
				close("arms_yaw", out.armsYawOffset, f.arms_yaw, 1e-5, 0)
				close("arms_pitch", out.armsPitchOffset, f.arms_pitch, 1e-5, 0)
				local x, y = 0, 0
				if out.ownsFollow then
					x, y = look:update(f.yaw, f.pitch, out.cameraYaw, out.cameraPitch, 19, 19, 0.025, f.dt, 6.4)
				else
					look:reset()
				end
				close("shift.x", x, f.shift[1], 1e-6, 1e-3)
				close("shift.y", y, f.shift[2], 1e-6, 1e-3)
			end)
			if not ok then
				frameError(i, err)
			end
		end
	end)

	T.test("footstep camera matches C++", function()
		local frames = T.require(T.Golden.footstep)
		local camera = Ref.FootstepCamera.new()
		for i, f in frames do
			local ok, err = pcall(function()
				local out = camera:update({
					dt = f.dt,
					footPhase = f.phase_in,
					speed = f.speed,
					forward = f.forward,
					right = f.right,
					sprinting = f.sprinting,
					grounded = f.grounded,
					bobScale = 1,
					postureScale = 2,
					lungeScale = 2,
				})
				closeVec("offset", out.offset, f.offset, 1e-6, 1e-4)
				close("phase", out.phase, f.phase, 1e-6, 0)
				T.equal(out.locked, f.locked, "locked")
				T.equal(out.brakeContacts, f.brake, "brakeContacts")
			end)
			if not ok then
				frameError(i, err)
			end
		end
	end)

	T.test("reference head roll and shake match C++", function()
		local frames = T.require(T.Golden.head)
		local head = Ref.ReferenceHead.new()
		for i, f in frames do
			local ok, err = pcall(function()
				local out = head:update({
					yaw = f.yaw,
					pitch = f.pitch,
					dt = f.dt,
					strafe = f.strafe,
					speedMetres = f.speed,
					aiming = f.aiming,
					leaning = f.leaning,
				})
				close("roll", out.roll, f.roll, 1e-4, 1e-3)
				close("shake.pitch", out.shake.pitch, f.shake[1], 1e-4, 1e-3)
				close("shake.yaw", out.shake.yaw, f.shake[2], 1e-4, 1e-3)
				close("shake.roll", out.shake.roll, f.shake[3], 1e-4, 1e-3)
			end)
			if not ok then
				frameError(i, err)
			end
		end
	end)

	T.test("camera delta matrix matches C++", function()
		for i, c in T.require(T.Golden.camera_delta) do
			local m = Ref.CameraDelta.fromRotation(c.rot[1], c.rot[2], c.rot[3])
			for _, axis in { "i", "j", "k" } do
				for n = 1, 3 do
					close(string.format("case %d %s[%d]", i, axis, n), m[axis][n], c[axis][n], 1e-9, 0)
				end
			end
		end
	end)

	T.test("viewmodel profile matches C++", function()
		local Profile = Bodycam.ViewmodelProfile
		local state = Profile.newState()
		for i, f in T.require(T.Golden.viewmodel_profile) do
			if i == 11 then
				Profile.set(state, Vec3.new(0.05, -0.02, 0.9), Vec3.new(3, -60, 2), 6)
			elseif i == 201 then
				Profile.clear(state, 4)
			end
			local out = Profile.update(state, f.dt, f.ads)
			closeVec("profile.pos", out.pos, f.pos, 1e-6, 1e-4)
			closeVec("profile.rot", out.rot, f.rot, 1e-5, 1e-4)
			T.equal(out.active, f.active, "profile.active")
		end
	end)

	T.test("report worst errors", function()
		local names = {}
		for name in worst do
			table.insert(names, name)
		end
		table.sort(names)
		local parts = {}
		for _, name in names do
			table.insert(parts, string.format("%s=%.2g", name, worst[name]))
		end
		print("golden worst errors: " .. table.concat(parts, " "))
	end)
end
