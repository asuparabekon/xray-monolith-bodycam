-- Bodycam + FPSys + FPSController together on the mock client world: a player walks,
-- aims, fires a full-auto spray, sprints and leans. Checks the parts reach each other.
return function(T)
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local env = Env.new()
	env.install()
	local require = T.isolatedRequire()
	local Bodycam = require(T.Src.Bodycam)
	local FPSys = require(T.Src.FPSys)
	local FPSController = require(T.Src.FPSController)
	local CoordinateSpace = Bodycam.coordinateSpace()

	local now = 0
	local remote = { sent = 0, OnClientEvent = Env.signal() }
	function remote:FireServer()
		self.sent += 1
	end

	local movement, weapons
	local controller = Bodycam.controller().new({
		walkSpeed = 16,
		sprintSpeed = 25.6,
		isSprinting = function()
			return movement:isSprinting()
		end,
		isCrouching = function()
			return movement:isCrouching()
		end,
		isLeaning = function()
			return weapons:getLean() ~= 0
		end,
	})
	local fpsys = FPSys.new({
		controller = controller,
		remote = remote,
		getLean = function()
			return weapons:getLean()
		end,
	})
	movement = FPSController.movement().new({ controller = controller })
	local fired = {}
	weapons = FPSController.weapons().new({
		controller = controller,
		fpsys = fpsys,
		movement = movement,
		clock = function()
			return now
		end,
		random = function()
			return 0.5
		end,
		onFire = function(cframe)
			table.insert(fired, cframe)
		end,
	})
	env.CollectionService.tagged = { env.humanoid }

	controller:start()
	fpsys:start()
	movement:start()
	weapons:start()

	-- The camera scripts: start from the restored aim and apply this frame's mouse turn.
	local mouseYaw = 0
	local function cameraScripts()
		local yaw, pitch = CoordinateSpace.aimFromLook(env.camera.CFrame.LookVector)
		env.camera.CFrame = CFrame.new(0, 5, 0) * CoordinateSpace.aimRotation(yaw + mouseYaw, pitch)
		env.camera.Focus = CFrame.new(0, 5, 0)
	end
	local function walk(forward)
		local yaw = CoordinateSpace.aimFromLook(env.camera.CFrame.LookVector)
		local _, fwd = CoordinateSpace.yawFrame(yaw)
		env.humanoid.MoveDirection = if forward then fwd else Vector3.new(0, 0, 0)
		env.root.AssemblyLinearVelocity = env.humanoid.MoveDirection * env.humanoid.WalkSpeed
	end
	local function frames(n)
		for _ = 1, n do
			now += 1 / 60
			env.step(1 / 60, cameraScripts)
		end
	end
	local function aimPitch()
		local _, pitch = CoordinateSpace.aimFromLook(controller:getAimCFrame().LookVector)
		return pitch
	end

	T.test("all layers start and compose a view", function()
		walk(true)
		mouseYaw = 0.01 -- turn left a little so the look angles change
		frames(60)
		mouseYaw = 0
		frames(10)
		T.truthy(controller:getViewCFrame() ~= nil, "view")
		T.truthy(#controller.viewModifiers >= 2, "FPSys and movement layers registered")
		T.truthy(remote.sent >= 1, "look angles sent")
	end)

	T.test("stride footsteps play while walking", function()
		local plays = 0
		for _, inst in env.created do
			if inst.ClassName == "Sound" then
				plays += inst.plays or 0
			end
		end
		T.truthy(plays >= 1, "step sounds played")
	end)

	T.test("aiming zooms; full auto climbs the aim and every shot reports", function()
		weapons:setAim(true)
		frames(30)
		T.truthy(env.camera.FieldOfView < 65, "ADS FOV")
		T.truthy(env.UserInputService.MouseDeltaSensitivity < 1, "ADS sensitivity")
		local before = aimPitch()
		weapons:pullTrigger()
		frames(60)
		weapons:releaseTrigger()
		-- 600 rpm over 1 s, counting the shot at both ends.
		T.truthy(#fired >= 10 and #fired <= 11, "600 rpm for a second: " .. #fired)
		local climbed = aimPitch() - before
		T.truthy(climbed > math.rad(3), "recoil climbed the aim: " .. math.deg(climbed))
		frames(120)
		local recovered = aimPitch() - before
		T.truthy(recovered < climbed and recovered > 0, "partly recovered")
		weapons:setAim(false)
		frames(30)
	end)

	T.test("sprinting blocks firing and aiming", function()
		movement:setSprint(true)
		frames(30)
		T.truthy(movement:isSprinting(), "sprinting")
		local count = #fired
		weapons:pullTrigger()
		weapons:setAim(true)
		frames(30)
		T.equal(#fired, count, "no shots while sprinting")
		T.equal(controller.ads.active, false, "no ADS while sprinting")
		movement:setSprint(false)
		weapons:releaseTrigger()
		frames(10)
		T.equal(controller.ads.active, true, "aim resumes when the sprint ends")
		weapons:setAim(false)
	end)

	T.test("lean moves the fire origin sideways", function()
		walk(false)
		weapons:setLean(1)
		frames(90)
		local fire = weapons:getFireCFrame()
		local aim = controller:getAimCFrame()
		local side = (fire.Position - aim.Position):Dot(aim.RightVector)
		T.truthy(side > 0.9, "fire origin leaned right: " .. side)
		weapons:setLean(0)
		frames(60)
	end)

	T.test("crouch drops the camera and slows the walk", function()
		walk(true)
		T.truthy(movement:toggleCrouch(), "crouched")
		frames(60)
		T.near(env.humanoid.WalkSpeed, 8, 1e-6, "crouch speed")
		local drop = controller:getAimCFrame().Position.Y - controller:getViewCFrame().Position.Y
		T.truthy(drop > 1, "view lowered: " .. drop)
		T.equal(env.humanoid.stateEnabled[env.Enum.HumanoidStateType.Jumping], false, "no jump from crouch")
	end)

	T.test("switching weapons respects unequip and equip time", function()
		local config = FPSController.Weapon.defaultConfig()
		config.name = "Pistol"
		table.insert(weapons.weapons, FPSController.Weapon.new(config))
		T.truthy(weapons:selectNext(), "switch starts")
		frames(10)
		T.equal(weapons.index, 1, "still unequipping")
		frames(30)
		T.equal(weapons:current().config.name, "Pistol", "switched")
		T.truthy(weapons:current():isBusy(), "equipping")
		frames(40)
		T.equal(weapons:current():isBusy(), false, "ready")
	end)

	T.test("everything stops cleanly", function()
		weapons:stop()
		movement:stop()
		fpsys:stop()
		controller:stop()
		T.equal(#env.bound, 0, "no render steps left")
		T.equal(#env.heartbeat.handlers, 0, "no heartbeat handlers left")
	end)
end
