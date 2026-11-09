-- Pure FPSController logic: movement states, weapon firing, recoil.
return function(T)
	local FPSController = T.require(T.Src.FPSController)
	local Machine = FPSController.MovementStateMachine
	local Weapon = FPSController.Weapon
	local RecoilPattern = FPSController.RecoilPattern
	local Gait, Pose = Machine.Gait, Machine.Pose

	local function run(machine, frames, input)
		local speed
		for _ = 1, frames do
			input.dt = input.dt or 1 / 60
			speed = machine:update(input)
		end
		return speed
	end

	T.test("walk eases up to walk speed, sprint needs forward input", function()
		local events = {}
		local m = Machine.new(nil, function(name)
			table.insert(events, name)
		end)
		local input = { moveX = 0, moveY = 1, grounded = true, canStand = true }
		local first = run(m, 1, input)
		T.truthy(first > 0 and first < 16, "eases in")
		T.near(run(m, 30, input), 16, 1e-9, "walk speed")
		T.equal(m.gait, Gait.Walking, "walking")
		m:setSprint(true)
		T.near(run(m, 30, input), 25.6, 1e-9, "sprint speed")
		T.truthy(m:isSprinting(), "sprinting")
		input.moveX, input.moveY = 1, 0
		run(m, 1, input)
		T.equal(m.gait, Gait.Walking, "strafing ends the sprint")
		T.truthy(table.find(events, "SprintStarted") and table.find(events, "SprintEnded"), "sprint events")
	end)

	T.test("crouch blocks sprint; standing needs room", function()
		local m = Machine.new()
		local input = { moveX = 0, moveY = 1, grounded = true, canStand = true }
		run(m, 10, input)
		T.truthy(m:toggleCrouch(true), "crouch")
		m:setSprint(true)
		T.near(run(m, 30, input), 8, 1e-9, "crouch speed, no sprint")
		T.equal(m:toggleCrouch(false), false, "no room to stand")
		T.equal(m.pose, Pose.Crouching, "still crouched")
		T.equal(m:requestJump(true), false, "jump from crouch stands up instead")
		T.equal(m.pose, Pose.Standing, "stood up")
	end)

	T.test("slide runs from sprint and ends crouched", function()
		local m = Machine.new()
		local input = { moveX = 0, moveY = 1, grounded = true, canStand = true }
		T.equal(m:requestSlide(), false, "no slide from a walk")
		m:setSprint(true)
		run(m, 30, input)
		T.truthy(m:requestSlide(), "slide from sprint")
		local start = run(m, 1, input)
		T.truthy(start > 30, "fast at the start")
		local speed = run(m, 60, input)
		T.equal(m.gait, Gait.Walking, "slide over")
		T.equal(m.pose, Pose.Crouching, "ends crouched")
		T.truthy(speed <= 34, "slowed down")
	end)

	T.test("prone locks input briefly; air and landing events", function()
		local events = {}
		local m = Machine.new(nil, function(name)
			table.insert(events, name)
		end)
		local input = { moveX = 0, moveY = 1, grounded = true, canStand = true }
		T.truthy(m:toggleProne(true), "prone")
		run(m, 1, input)
		T.equal(m.gait, Gait.Idle, "input locked during the transition")
		T.near(run(m, 60, input), 3.5, 1e-6, "prone crawl speed")
		T.truthy(m.drop > 2, "camera dropped")
		m:toggleProne(true)
		run(m, 60, input)
		T.truthy(m:requestJump(true), "jump when standing")
		input.grounded = false
		run(m, 5, input)
		T.equal(m.gait, Gait.InAir, "in air")
		input.grounded = true
		run(m, 1, input)
		T.truthy(table.find(events, "Jumped") and table.find(events, "Landed"), "jump and land events")
	end)

	T.test("auto fires at the rate, burst fires its length, semi once per pull", function()
		local w = Weapon.new()
		local now = 0
		local total = 0
		w:pullTrigger()
		for _ = 1, 60 do
			now += 1 / 60
			total += (w:update(now))
		end
		T.equal(total, 10, "600 rpm over a second")
		w:releaseTrigger()
		local _, ended = w:update(now + 0.01)
		T.truthy(ended, "spray ended on release")

		w:cycleFireMode() -- Semi
		total = 0
		w:pullTrigger()
		for _ = 1, 60 do
			now += 1 / 60
			total += (w:update(now))
		end
		T.equal(total, 1, "semi: one shot per pull")
		w:releaseTrigger()

		w:cycleFireMode() -- Burst
		total = 0
		w:pullTrigger()
		w:releaseTrigger()
		for _ = 1, 60 do
			now += 1 / 60
			total += (w:update(now))
		end
		T.equal(total, 3, "burst finishes after release")
	end)

	T.test("magazine empties, reload refills and blocks firing", function()
		local config = Weapon.defaultConfig()
		config.magazine = 5
		local w = Weapon.new(config)
		local now, shots, event = 0, 0, nil
		w:pullTrigger()
		for _ = 1, 120 do
			now += 1 / 60
			local n, _, e = w:update(now)
			shots += n
			event = event or e
		end
		T.equal(shots, 5, "five rounds")
		T.equal(event, "Empty", "empty event")
		T.truthy(w:reload(now), "reload starts")
		w:pullTrigger()
		local n = w:update(now + 1)
		T.equal(n, 0, "no fire while reloading")
		local _, _, done = w:update(now + 2.3)
		T.equal(done, "Reloaded", "reloaded")
		T.equal(w.ammo, 5, "full")
	end)

	T.test("scope sets FOV and sensitivity", function()
		local config = Weapon.defaultConfig()
		config.scopes = { { name = "Red dot", zoom = 0.9, sensitivity = 0.9 }, { name = "4x", zoom = 0.25, sensitivity = 0.3 } }
		local w = Weapon.new(config)
		T.near(w:fov(false), 70, 1e-9, "hip fov")
		T.near(w:fov(true), 63, 1e-9, "red dot")
		w:cycleScope()
		T.near(w:fov(true), 17.5, 1e-9, "4x")
		T.near(w:sensitivity(true), 0.3, 1e-9, "4x sensitivity")
	end)

	T.test("recoil climbs while firing and partly recovers after", function()
		local values = { 0.5, 0.2, 0.8, 0.4 }
		local i = 0
		local r = RecoilPattern.new(nil, function()
			i = i % #values + 1
			return values[i]
		end)
		local climbed = 0
		for shot = 1, 10 do
			r:onShot(1, false)
			for _ = 1, 6 do
				local _, pitch = r:update(1 / 60)
				climbed += pitch
			end
			T.truthy(climbed > 0, "climbs, shot " .. shot)
		end
		r:onFireEnd()
		local back = 0
		for _ = 1, 240 do
			local _, pitch = r:update(1 / 60)
			back += pitch
		end
		T.truthy(back < 0, "recovers downward")
		local share = -back / climbed
		T.truthy(share > 0.45 and share < 0.6, "about the recovery share, got " .. share)
		local _, pitch = r:update(1 / 60)
		T.near(pitch, 0, 1e-9, "settled")
	end)
end
