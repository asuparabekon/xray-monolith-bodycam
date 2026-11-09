-- Bodycam package: pure modules, then the Controller on the mocked client.
return function(T)
	local Bodycam = T.require(T.Src.Bodycam)
	local BMath = Bodycam.Math
	local Settings = Bodycam.Settings
	local LookLag = Bodycam.LookLag
	local HeadMotion = Bodycam.HeadMotion
	local WeaponMotion = Bodycam.WeaponMotion
	local MovementInertia = Bodycam.MovementInertia
	local StrideClock = Bodycam.StrideClock
	local BodycamCore = Bodycam.BodycamCore

	local RAD = math.pi / 180
	local DT = 1 / 60

	---------------------------------------------------------------------------
	-- Math.
	---------------------------------------------------------------------------

	T.test("math: wrap, soft limit and springs", function()
		T.near(BMath.wrapAngle(3 * math.pi), math.pi, 1e-9, "wrap 3pi")
		T.near(BMath.wrapAngle(-0.5), -0.5, 1e-12, "wrap small")
		T.near(BMath.softLimit(0.2, 4), 0.2, 1e-12, "linear while small")
		T.near(BMath.softLimit(4, 4), 4, 1e-12, "full at the limit")
		T.near(BMath.softLimit(-9, 4), -4, 1e-12, "held beyond")
		local last = 0
		for i = 1, 100 do
			local v = BMath.softSaturate(i / 80)
			T.truthy(v >= last - 1e-12 and v <= 1, "monotone and bounded")
			last = v
		end
		-- A critically damped spring settles without overshoot.
		local x, v = 0, 0
		local omega = BMath.omega(5)
		for _ = 1, 120 do
			x, v = BMath.criticalSpring(x, v, 1, omega, DT)
			T.truthy(x <= 1 + 1e-9, "no overshoot")
		end
		T.near(x, 1, 1e-6, "settled")
		-- An impulse sized by impulseFor peaks at the wanted value.
		for _, zeta in { 0.6, 0.75, 1, 1.5 } do
			x, v = 0, BMath.impulseFor(0.1, BMath.omega(4), zeta)
			local peak = 0
			for _ = 1, 2000 do
				x, v = BMath.spring(x, v, 0, BMath.omega(4), zeta, 1 / 2000)
				peak = math.max(peak, x)
			end
			T.near(peak, 0.1, 1e-4, "impulse peak at zeta " .. zeta)
		end
	end)

	---------------------------------------------------------------------------
	-- Look lag.
	---------------------------------------------------------------------------

	local function newLag(ads)
		local config = Settings.default()
		local lag = LookLag.new(config.lookLag)
		lag:reset(0, 0)
		return lag, config
	end
	local function lagStep(lag, yaw, pitch, ads, rate)
		return lag:update({ dt = DT, yaw = yaw, pitch = pitch or 0, ads = ads or 0, turnRate = rate or 0 })
	end
	local function gapOf(lag)
		return math.sqrt(lag.gapYaw ^ 2 + lag.gapPitch ^ 2)
	end

	T.test("look lag holds within the dead band", function()
		local lag = newLag()
		for _ = 1, 60 do
			lagStep(lag, 0.3 * RAD, 0.1 * RAD)
		end
		T.equal(lag.yaw, 0, "yaw held")
		T.equal(lag.pitch, 0, "pitch held")
		lagStep(lag, 0.5 * RAD, 0)
		T.truthy(lag.yaw > 0, "outside the band the view follows")
	end)

	T.test("look lag is bounded by the cone after a snap and settles to the aim", function()
		local lag = newLag()
		lagStep(lag, 30 * RAD, -10 * RAD)
		T.truthy(gapOf(lag) <= 7 * RAD + 1e-9, "inside the hip cone: " .. gapOf(lag) / RAD)
		T.truthy(gapOf(lag) > 6.9 * RAD, "dragged at the edge")
		local maxGap = 0
		for _ = 1, 120 do
			lagStep(lag, 30 * RAD, -10 * RAD)
			maxGap = math.max(maxGap, gapOf(lag))
		end
		T.truthy(maxGap <= 7 * RAD + 1e-9, "stays in the cone")
		T.near(lag.yaw, 30 * RAD, 1e-9, "settled yaw")
		T.near(lag.pitch, -10 * RAD, 1e-9, "settled pitch")
		T.equal(lag.engaged, false, "spring at rest")
	end)

	T.test("look lag: ADS tightens it", function()
		local hip, ads = newLag(), newLag()
		lagStep(hip, 20 * RAD, 0, 0)
		lagStep(ads, 20 * RAD, 0, 1)
		T.truthy(gapOf(ads) <= 1.5 * RAD + 1e-9, "ADS cone: " .. gapOf(ads) / RAD)
		-- A steady turn: the ADS view trails less.
		local yaw = 0
		for _ = 1, 60 do
			yaw += 1 * DT
			lagStep(hip, 20 * RAD + yaw, 0, 0, 1)
			lagStep(ads, 20 * RAD + yaw, 0, 1, 1)
		end
		T.truthy(gapOf(ads) < gapOf(hip) * 0.6, "ADS trails less: " .. gapOf(ads) / RAD .. " < " .. gapOf(hip) / RAD)
		T.truthy(gapOf(hip) > 1 * RAD, "hip view trails a steady turn")
	end)

	T.test("look lag: fast turns stiffen the spring", function()
		local slow, fast = newLag(), newLag()
		lagStep(slow, 5 * RAD, 0, 0, 0)
		lagStep(fast, 5 * RAD, 0, 0, 6)
		for _ = 1, 3 do
			lagStep(slow, 5 * RAD, 0, 0, 0)
			lagStep(fast, 5 * RAD, 0, 0, 6)
		end
		T.truthy(gapOf(fast) < gapOf(slow), "stiffer at a high turn rate")
	end)

	T.test("look lag: a kick moves the view by the kick at once", function()
		local lag = newLag()
		lag:kick(0.05, 0.02)
		lagStep(lag, 0.05, 0.02)
		T.near(lag.yaw, 0.05, 1e-12, "yaw kicked")
		T.near(lag.pitch, 0.02, 1e-12, "pitch kicked")
		T.near(gapOf(lag), 0, 1e-12, "no lag from the kick")
		-- While lagging: the kick adds to the view exactly.
		local a, b = newLag(), newLag()
		for i = 1, 10 do
			lagStep(a, i * 0.02, 0, 0, 1.2)
			lagStep(b, i * 0.02, 0, 0, 1.2)
		end
		b:kick(0.03, 0.01)
		lagStep(a, 0.22, 0, 0, 1.2)
		lagStep(b, 0.25, 0.01, 0, 1.2)
		T.near(b.yaw - a.yaw, 0.03, 1e-9, "same lag, turned by the kick")
		T.near(b.pitch - a.pitch, 0.01, 1e-9, "pitch too")
	end)

	T.test("look lag: the view slides toward the side it lags on", function()
		local lag = newLag()
		local yaw = 0
		for _ = 1, 30 do
			yaw -= 2 * DT -- turning right: the view lags on the left
			lagStep(lag, yaw, 0, 0, 2)
		end
		T.truthy(lag.gapYaw > 0, "view left of the aim")
		T.truthy(lag.shiftX < 0, "slides left: " .. lag.shiftX)
		T.truthy(math.abs(lag.shiftX) <= 0.06 + 1e-9, "at most lagShift")
		-- Moving right slides the view left.
		local moving = newLag()
		for _ = 1, 120 do
			moving:update({ dt = DT, yaw = 0, pitch = 0, velRight = 16, velForward = 16, walkSpeed = 16 })
		end
		T.near(moving.shiftX, -0.05, 1e-3, "against a move right")
		T.near(moving.shiftZ, 0.03, 1e-3, "back when moving forward")
	end)

	---------------------------------------------------------------------------
	-- Head motion.
	---------------------------------------------------------------------------

	local function newHead()
		local config = Settings.default()
		return HeadMotion.new(config.head), config
	end
	local function walkInput(phase, speed, extra)
		local input = {
			dt = DT,
			phase = phase,
			speed = speed,
			walkSpeed = 16,
			sprintSpeed = 25.6,
			grounded = true,
			sprinting = false,
			ads = 0,
			velRight = 0,
			yawRate = 0,
		}
		for k, v in extra or {} do
			input[k] = v
		end
		return input
	end
	-- Runs strides at a constant speed; returns samples of the last stride.
	local function stride(head, speed, strides, extra)
		local perStride = 120
		local samples = {}
		for s = 1, strides do
			for i = 0, perStride - 1 do
				local phase = i / perStride
				head:update(walkInput(phase, speed, extra))
				if s == strides then
					table.insert(samples, { phase = phase, x = head.x, y = head.y, pitch = head.pitch })
				end
			end
		end
		return samples
	end

	T.test("head bob: two vertical dips and one sway per stride", function()
		local head = newHead()
		local samples = stride(head, 16, 4)
		local dips = 0
		local n = #samples
		for i = 1, n do
			local prev = samples[(i - 2) % n + 1].y
			local nxt = samples[i % n + 1].y
			if samples[i].y < prev and samples[i].y <= nxt then
				dips += 1
			end
		end
		T.equal(dips, 2, "two dips per phase unit")
		T.near(samples[1].y, -0.07, 1e-3, "down at left contact")
		T.near(samples[61].y, -0.07, 1e-3, "down at right contact")
		T.near(samples[31].y, 0, 1e-3, "up between")
		T.truthy(samples[1].x < -0.04, "toward the left foot")
		T.truthy(samples[61].x > 0.04, "toward the right foot")
		T.truthy(samples[1].pitch < 0, "nods down at contact")
	end)

	local function amplitude(samples)
		local lo, hi = math.huge, -math.huge
		for _, s in samples do
			lo, hi = math.min(lo, s.y), math.max(hi, s.y)
		end
		return hi - lo
	end

	T.test("head bob scales with speed and ADS", function()
		local slow = amplitude(stride(newHead(), 8, 4))
		local walk = amplitude(stride(newHead(), 16, 4))
		local sprint = amplitude(stride(newHead(), 25.6, 4))
		local ads = amplitude(stride(newHead(), 16, 4, { ads = 1 }))
		T.near(slow / walk, 0.5, 0.02, "half speed, half bob")
		T.near(sprint / walk, 2.2, 0.05, "sprint scale")
		T.near(ads / walk, 0.4, 0.02, "ADS scale")
	end)

	T.test("head bob eases to rest when the phase stops", function()
		local head = newHead()
		stride(head, 16, 3)
		head:update(walkInput(0, 16))
		local moving = head.y
		head:update(walkInput(nil, 0))
		T.truthy(math.abs(head.y - moving) < 0.02, "no snap: " .. moving .. " -> " .. head.y)
		for _ = 1, 15 do
			head:update(walkInput(nil, 0))
		end
		T.truthy(math.abs(head.y) < 0.07 * 0.1, "mostly at rest after 0.25 s")
		for _ = 1, 30 do
			head:update(walkInput(nil, 0))
		end
		T.truthy(math.abs(head.y) < 1e-3 and math.abs(head.x) < 1e-3, "at rest")
		-- Airborne fades it too.
		stride(head, 16, 2)
		for _ = 1, 30 do
			head:update(walkInput(0.25, 16, { grounded = false }))
		end
		T.truthy(head.bobAmp < 0.01, "airborne fades the bob")
	end)

	T.test("head bob keeps clear of walls beside the head", function()
		local head = newHead()
		local samples = stride(head, 16, 3, { clearRight = 0.27, clearLeft = 10 })
		local maxX, minX = -math.huge, math.huge
		for _, s in samples do
			maxX, minX = math.max(maxX, s.x), math.min(minX, s.x)
		end
		T.truthy(maxX <= 0.02 + 1e-9, "right sway stops 0.25 short of the wall: " .. maxX)
		T.truthy(minX < -0.04, "left sway free")
	end)

	T.test("head roll signs: strafe and turn", function()
		local function settle(extra)
			local head = newHead()
			for _ = 1, 120 do
				head:update(walkInput(nil, 0, extra))
			end
			return head.rollOut
		end
		T.truthy(settle({ velRight = 16 }) < 0, "strafing right rolls right")
		T.near(settle({ velRight = 16 }), -1.8 * RAD, 1e-4, "strafe roll size")
		T.truthy(settle({ velRight = -16 }) > 0, "strafing left rolls left")
		T.truthy(settle({ yawRate = -2 }) < 0, "turning right rolls right")
		T.truthy(settle({ yawRate = 2 }) > 0, "turning left rolls left")
		T.near(settle({ yawRate = 10 }), 2.5 * RAD, 1e-4, "turn roll capped")
		T.near(settle({ velRight = 16, leaning = true }), 0, 1e-6, "lean owns the roll")
	end)

	T.test("sprint lean eases in, with a one-shot lunge", function()
		local head = newHead()
		head:update(walkInput(nil, 0, { sprinting = true }))
		T.truthy(head.pitch > -2 * RAD * 0.3, "not at once")
		local lunge = 0
		for i = 2, 60 do
			head:update(walkInput(nil, 0, { sprinting = true }))
			if i == 9 then
				lunge = head.z
			end
		end
		T.truthy(lunge < -0.15, "forward dip near 0.15 s: " .. lunge)
		T.near(head.pitch, -2 * RAD, 0.05 * RAD, "leaned forward")
		T.near(head.y, -0.12, 0.005, "lowered")
		T.near(head.z, 0, 1e-9, "lunge gone")
		for _ = 1, 60 do
			head:update(walkInput(nil, 0))
		end
		T.truthy(math.abs(head.pitch) < 0.01 * RAD, "eased out")
	end)

	T.test("landing dip drops and returns", function()
		local head = newHead()
		head:update(walkInput(nil, 0, { landingSpeed = 50 }))
		local low = 0
		for _ = 1, 30 do
			head:update(walkInput(nil, 0))
			low = math.min(low, head.y)
		end
		T.near(low, -0.12, 0.01, "drop for a 50 studs/s fall")
		for _ = 1, 120 do
			head:update(walkInput(nil, 0))
		end
		T.truthy(math.abs(head.y) < 1e-3, "back up")
		local hard = newHead()
		hard:update(walkInput(nil, 0, { landingSpeed = 500 }))
		low = 0
		for _ = 1, 30 do
			hard:update(walkInput(nil, 0))
			low = math.min(low, hard.y)
		end
		T.near(low, -0.24, 0.02, "capped at twice the drop")
	end)

	---------------------------------------------------------------------------
	-- Weapon motion.
	---------------------------------------------------------------------------

	local function newWeapon()
		local config = Settings.default()
		return WeaponMotion.new(config.weapon, function()
			return 0.7
		end), config
	end
	local function weaponInput(extra)
		local input = {
			dt = DT,
			ads = 0,
			aiming = false,
			sprinting = false,
			grounded = true,
			speed = 0,
			velRight = 0,
			velForward = 0,
			walkSpeed = 16,
			yawRate = 0,
			pitchRate = 0,
		}
		for k, v in extra or {} do
			input[k] = v
		end
		return input
	end
	local function run(w, seconds, extra)
		for _ = 1, math.floor(seconds / DT + 0.5) do
			w:update(weaponInput(extra))
		end
	end
	local FORWARD = { speed = 16, velForward = 16 }

	T.test("weapon lowers only when walking forward", function()
		local w = newWeapon()
		run(w, 3, FORWARD)
		T.near(w.y, -0.12, 1e-3, "dropped")
		T.near(w.z, 0.08, 1e-3, "pulled back")
		T.near(w.pitch, -7 * RAD, 0.02 * RAD, "muzzle down")
		local back = newWeapon()
		run(back, 3, { speed = 16, velForward = -16 })
		T.near(back.y, 0, 1e-9, "not walking backward")
		local strafe = newWeapon()
		run(strafe, 3, { speed = 16, velRight = 16 })
		T.near(strafe.y, -0.06, 1e-3, "half when strafing")
		local sprint = newWeapon()
		run(sprint, 3, { speed = 25, velForward = 25, sprinting = true })
		T.near(sprint.y, 0, 1e-9, "not while sprinting")
		-- Eases in over about 0.6 s.
		local easing = newWeapon()
		run(easing, 0.2, FORWARD)
		T.truthy(easing.lower > 0.3 and easing.lower < 0.8, "easing in: " .. easing.lower)
	end)

	T.test("weapon lowering is suppressed after a shot and while aiming", function()
		local w = newWeapon()
		run(w, 3, FORWARD)
		w:notifyShot(1, 0)
		run(w, 0.6, FORWARD)
		T.truthy(w.lower < 0.05, "raised after the shot: " .. w.lower)
		run(w, 0.15, FORWARD)
		T.truthy(w.lower < 0.05, "held for 0.8 s")
		run(w, 1.2, FORWARD)
		T.truthy(w.lower > 0.9, "lowers again")
		run(w, 0.5, { speed = 16, velForward = 16, aiming = true, ads = 1 })
		T.truthy(w.lower < 0.01, "not while aiming")
		run(w, 0.15, FORWARD)
		T.truthy(w.lower < 0.01, "held 0.2 s after aiming")
		run(w, 1.5, FORWARD)
		T.truthy(w.lower > 0.9, "lowers after the hold")
	end)

	T.test("holster pose", function()
		local w = newWeapon()
		run(w, 2, { lowered = true })
		T.near(w.y, -0.25, 1e-3, "holster drop")
		T.near(w.pitch, -25 * RAD, 0.05 * RAD, "holster pitch")
	end)

	T.test("fire kick: back, up, muzzle up, then settles", function()
		local w = newWeapon()
		w:notifyShot(1, 0)
		local peakZ, peakPitch = 0, 0
		for _ = 1, 30 do
			w:update(weaponInput())
			peakZ = math.max(peakZ, w.z)
			peakPitch = math.max(peakPitch, w.pitch)
			T.truthy(w.z >= -0.01, "kicks back, not forward")
		end
		T.near(peakZ, 0.05, 0.005, "kick back")
		T.near(peakPitch, 2.2 * RAD, 0.2 * RAD, "muzzle up")
		run(w, 2)
		T.truthy(math.abs(w.z) < 1e-4 and math.abs(w.pitch) < 1e-4 and math.abs(w.roll) < 1e-4, "settled")
		-- ADS and power scale it.
		local aimed = newWeapon()
		aimed:notifyShot(1, 1)
		local half = newWeapon()
		half:notifyShot(0.5, 0)
		local pa, ph = 0, 0
		for _ = 1, 30 do
			aimed:update(weaponInput({ ads = 1, aiming = true }))
			half:update(weaponInput())
			pa, ph = math.max(pa, aimed.z), math.max(ph, half.z)
		end
		T.near(pa / peakZ, 0.35, 0.03, "ADS kick")
		T.near(ph / peakZ, 0.5, 0.03, "power scales the kick")
	end)

	T.test("fire kick caps hold", function()
		local w = newWeapon()
		for _ = 1, 60 do
			w:notifyShot(3, 0)
		end
		for _ = 1, 30 do
			w:update(weaponInput())
			T.truthy(w.z <= 0.25 + 1e-9, "back capped: " .. w.z)
			T.truthy(w.pitch <= 10 * RAD + 1e-9, "pitch capped")
		end
	end)

	T.test("wrist roll goes against the turn; pitch with it", function()
		local left = newWeapon()
		run(left, 1, { yawRate = 3 })
		T.truthy(left.wristRoll < 0, "turning left rolls right")
		local right = newWeapon()
		run(right, 1, { yawRate = -3 })
		T.truthy(right.wristRoll > 0, "turning right rolls left")
		local up = newWeapon()
		run(up, 1, { pitchRate = 3 })
		T.truthy(up.wristPitch > 0, "turning up pitches up")
		local flick = newWeapon()
		run(flick, 1, { yawRate = 40, pitchRate = -40 })
		T.near(flick.wristRoll, -9 * RAD, 1e-4, "roll capped")
		T.near(flick.wristPitch, -5 * RAD, 1e-4, "pitch capped")
		local aimed = newWeapon()
		run(aimed, 1, { yawRate = 40, ads = 1, aiming = true })
		T.near(aimed.wristRoll, -9 * RAD * 0.3, 1e-4, "ADS scale")
		run(left, 1)
		T.truthy(math.abs(left.wristRoll) < 1e-4, "returns when the turn stops")
	end)

	T.test("weapon profile blends in and fades with ADS", function()
		local w = newWeapon()
		w:setProfile({ x = 0.1, y = -0.05, z = 0.02 }, { x = 0, y = 2, z = 0 })
		run(w, 0.1)
		T.truthy(w.x > 0.02 and w.x < 0.09, "blending: " .. w.x)
		run(w, 2)
		T.near(w.x, 0.1, 1e-4, "profile x")
		T.near(w.yaw, 2 * RAD, 1e-4, "profile yaw")
		run(w, 0.1, { ads = 1, aiming = true })
		T.near(w.x, 0, 1e-9, "faded by ADS")
		w:clearProfile()
		run(w, 2)
		T.near(w.x, 0, 1e-4, "cleared")
	end)

	---------------------------------------------------------------------------
	-- Movement inertia and stride clock.
	---------------------------------------------------------------------------

	T.test("inertia eases in and never overshoots", function()
		local config = Settings.default()
		local inertia = MovementInertia.new(config.inertia)
		local last = 0
		local state = { grounded = true }
		for i = 1, 60 do
			local x, z, fraction = inertia:update(DT, 0, -16, state)
			local speed = math.sqrt(x * x + z * z)
			T.truthy(speed <= 16 + 1e-9, "never above the target")
			T.truthy(speed >= last - 1e-9, "monotone")
			last = speed
			if i == math.floor(0.22 / DT + 0.5) then
				T.truthy(fraction > 0.9 and fraction < 0.99, "about 95 % at accelTime: " .. fraction)
			end
		end
		-- Stop.
		for i = 1, 30 do
			local x, z = inertia:update(DT, 0, 0, state)
			local speed = math.sqrt(x * x + z * z)
			T.truthy(speed <= last + 1e-9, "never speeds up while stopping")
			last = speed
			if i == math.floor(0.12 / DT + 0.5) then
				T.truthy(speed < 16 * 0.1, "stopped quickly: " .. speed)
			end
		end
		-- Reversing is slower than starting.
		local a = MovementInertia.new(config.inertia)
		local b = MovementInertia.new(config.inertia)
		for _ = 1, 60 do
			a:update(DT, 0, 16, state)
		end
		for _ = 1, 10 do
			a:update(DT, 0, -16, state)
			b:update(DT, 0, -16, state)
		end
		T.truthy(a.z > -16 * b.speedFraction, "reversal penalty")
	end)

	T.test("inertia bypass rules", function()
		local config = Settings.default()
		local inertia = MovementInertia.new(config.inertia)
		for _, state in { { crouching = true }, { climbing = true }, { grounded = false } } do
			inertia:reset()
			local x = inertia:update(DT, 12, 0, state)
			T.equal(x, 12, "bypassed")
		end
		inertia:reset()
		local x = inertia:update(DT, 12, 0, { aiming = true })
		T.truthy(x < 12, "aiming eases by default")
		config.inertia.bypassAiming = true
		inertia:reset()
		x = inertia:update(DT, 12, 0, { aiming = true })
		T.equal(x, 12, "aiming bypass when set")
		config.inertia.enabled = false
		inertia:reset()
		T.equal((inertia:update(DT, 5, 0, {})), 5, "off")
	end)

	T.test("stride clock: stride length by gait, nil when stopped or airborne", function()
		local config = Settings.default()
		local clock = StrideClock.new(config.stride)
		T.equal(clock:update(DT, 0.3, true, 16, 25.6), nil, "too slow")
		T.equal(clock:update(DT, 16, false, 16, 25.6), nil, "airborne")
		clock:reset(0)
		local phase
		for _ = 1, 60 do
			phase = clock:update(4.6 / 16 / 60, 16, true, 16, 25.6)
		end
		T.near(phase, 0, 1e-9, "one stride per 4.6 studs at walk")
		for _ = 1, 30 do
			phase = clock:update(6.8 / 25.6 / 60, 25.6, true, 16, 25.6)
		end
		T.near(phase, 0.5, 1e-9, "6.8 studs at sprint")
	end)

	T.test("core: footstep contacts alternate and landing is detected", function()
		T.equal(BodycamCore.footContact(0.45, 0.55), "Right", "right contact")
		T.equal(BodycamCore.footContact(0.95, 0.05), "Left", "left contact")
		T.equal(BodycamCore.footContact(0.1, 0.2), nil, "between")
		T.equal(BodycamCore.footContact(nil, 0.2), nil, "starting")
		local core = BodycamCore.new(Settings.default())
		local steps = {}
		local function frame(extra)
			local input = { dt = DT, yaw = 0, pitch = 0, velZ = -16, moveZ = -16, walkSpeed = 16, sprintSpeed = 25.6 }
			for k, v in extra or {} do
				input[k] = v
			end
			local out = core:update(input)
			if out.footstep then
				table.insert(steps, out.footstep)
			end
			return out
		end
		for _ = 1, 180 do
			frame()
		end
		T.truthy(#steps >= 10, "steps while walking: " .. #steps)
		for i = 2, #steps do
			T.truthy(steps[i] ~= steps[i - 1], "alternating feet")
		end
		for _ = 1, 20 do
			frame({ grounded = false, velY = -40 })
		end
		local out = frame({ grounded = true, velY = 0 })
		T.near(out.landingSpeed, 40, 1e-9, "landing speed")
	end)

	---------------------------------------------------------------------------
	-- Settings.
	---------------------------------------------------------------------------

	T.test("settings: defaults, clamping, unknown names, types", function()
		local config = Settings.default()
		for _, group in { "lookLag", "head", "weapon", "inertia", "stride" } do
			T.equal(config[group].enabled, true, group .. " on by default")
		end
		T.equal(config.lookLag.followHz, 5, "followHz")
		T.equal(config.head.bobDown, 0.07, "bobDown")
		T.equal(config.weapon.kickHz, 7, "kickHz")
		T.equal(config.inertia.accelTime, 0.22, "accelTime")
		T.equal(config.stride.strideLength, 4.6, "strideLength")
		T.truthy(Settings.set(config, "lookLag.maxLagDeg", 100), "set ok")
		T.equal(config.lookLag.maxLagDeg, 10, "clamped high")
		T.truthy(Settings.set(config, "head.bobDown", -1), "set ok")
		T.equal(config.head.bobDown, 0, "clamped low")
		T.equal((Settings.set(config, "head.nope", 1)), false, "unknown name")
		T.equal((Settings.set(config, "nope.enabled", true)), false, "unknown group")
		T.equal((Settings.set(config, "head.bobDown", "big")), false, "wrong type")
		T.equal((Settings.set(config, "head.enabled", 1)), false, "number for a switch")
		T.equal((Settings.set(config, "head.bobDown", 0 / 0)), false, "NaN")
		T.truthy(Settings.set(config, "head.enabled", false), "switch")
		T.equal(config.head.enabled, false, "switched off")
		local rows = Settings.list()
		T.truthy(#rows > 50, "registry rows")
		for _, row in rows do
			T.truthy(type(row.path) == "string" and row.kind and row.default ~= nil, "row fields")
			if row.kind == "number" then
				T.truthy(row.min <= row.default and row.default <= row.max, row.path .. " default in range")
			end
		end
	end)

	T.test("settings presets", function()
		local config = Settings.default()
		T.truthy(Settings.applyPreset(config, "Off"), "Off")
		for _, group in { "lookLag", "head", "weapon", "inertia", "stride" } do
			T.equal(config[group].enabled, false, group .. " off")
		end
		T.equal(config.head.bobEnabled, false, "sub-switches off")
		T.truthy(Settings.applyPreset(config, "Subtle"), "Subtle")
		T.equal(config.head.enabled, true, "on again")
		T.near(config.head.bobDown, 0.035, 1e-12, "half amplitude")
		T.near(config.lookLag.maxLagDeg, 3.5, 1e-12, "half cone")
		T.near(config.lookLag.followHz, 5, 1e-12, "frequencies unchanged")
		T.truthy(Settings.applyPreset(config, "Strong"), "Strong")
		T.near(config.weapon.kickBack, 0.075, 1e-12, "1.5x amplitude")
		T.equal(config.lookLag.maxLagDeg, 10, "cone clamped to its range")
		T.truthy(Settings.applyPreset(config, "Default"), "Default")
		T.equal(config.head.bobDown, 0.07, "defaults back")
		T.equal(config.lookLag.maxLagDeg, 7, "cone back")
		T.equal((Settings.applyPreset(config, "Loud")), false, "unknown preset")
	end)

	---------------------------------------------------------------------------
	-- Controller on the mocked client.
	---------------------------------------------------------------------------

	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local function client(options)
		local env = Env.new()
		env.install()
		local require = T.isolatedRequire()
		local B = require(T.Src.Bodycam)
		local Aim = B.aim()
		local viewmodel = { PrimaryPart = {}, placed = nil }
		function viewmodel:PivotTo(cframe)
			self.placed = cframe
		end
		local opts = { viewmodel = viewmodel, walkSpeed = 16, sprintSpeed = 25.6 }
		for k, v in options or {} do
			opts[k] = v
		end
		local controller = B.controller().new(opts)
		local world = { env = env, Aim = Aim, controller = controller, viewmodel = viewmodel }
		world.mouseYaw, world.mousePitch = 0, 0
		world.position = Vector3.new(0, 5, 0)
		world.seen = {} -- what the camera scripts read each frame
		function world.cameraScripts()
			local cf = env.camera.CFrame
			table.insert(world.seen, cf)
			local yaw, pitch = Aim.aimFromLook(cf.LookVector)
			env.camera.CFrame = CFrame.new(world.position) * Aim.aimRotation(yaw + world.mouseYaw, pitch + world.mousePitch)
		end
		function world.frames(n)
			for _ = 1, n do
				env.step(DT, world.cameraScripts)
			end
		end
		function world.walk(speed)
			local yaw = Aim.aimFromLook(controller:getAimCFrame().LookVector)
			local _, forward = Aim.yawFrame(yaw)
			env.humanoid.MoveDirection = if speed > 0 then forward else Vector3.new(0, 0, 0)
			env.root.AssemblyLinearVelocity = forward * speed
		end
		controller:start()
		return world
	end
	local function angle(a, b)
		return math.acos(math.clamp(a.LookVector:Dot(b.LookVector), -1, 1))
	end

	T.test("controller: the aim is restored before the camera scripts", function()
		local w = client()
		w.mouseYaw = -0.02
		w.frames(30)
		local c = w.controller
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) > 0.5 * RAD, "view lags while turning")
		-- The camera holds the view between frames, the aim when the scripts run.
		T.truthy(w.env.camera.CFrame == c:getViewCFrame(), "camera shows the view")
		local aimBefore = c:getAimCFrame()
		w.frames(1)
		local seen = w.seen[#w.seen]
		T.truthy(angle(seen, aimBefore) < 1e-9, "camera scripts read the aim")
		T.truthy((seen.Position - aimBefore.Position).Magnitude < 1e-9, "aim position too")
	end)

	T.test("controller: the view differs from the aim while turning, the HUD does not", function()
		local w = client()
		w.mouseYaw = 0.02
		w.frames(20)
		local c = w.controller
		local view, aim, hud = c:getViewCFrame(), c:getAimCFrame(), c:getHudCFrame()
		local lag = angle(view, aim)
		T.truthy(lag > 1 * RAD and lag <= 7 * RAD + 1e-6, "view trails the turn: " .. lag / RAD)
		T.truthy(view.LookVector:Dot(aim.RightVector) > 0, "trails to the right of a left turn")
		T.truthy(angle(hud, aim) < 1e-6, "HUD looks along the aim")
		-- Stopping lets the view settle.
		w.mouseYaw = 0
		w.frames(90)
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) < 1e-4, "view settled")
	end)

	T.test("controller: the viewmodel hangs from the HUD camera", function()
		local offset = CFrame.new(0.4, -0.5, -1.2)
		local w = client({ viewmodelOffset = offset })
		w.frames(30)
		local c = w.controller
		local rel = c:getHudCFrame():Inverse() * w.viewmodel.placed
		T.truthy((rel.Position - offset.Position).Magnitude < 1e-6, "placed at the offset from the HUD")
		w.mouseYaw = 0.02
		w.frames(20)
		T.truthy(math.abs(w.viewmodel.placed.LookVector:Dot(c:getAimCFrame().LookVector) - 1) < 1e-6, "weapon on the aim")
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) > 1 * RAD, "while the view trails")
		-- Viewmodel modifiers change the local placement.
		local remove = c:addViewmodelModifier(function(localCFrame)
			return localCFrame * CFrame.new(0, 1, 0)
		end)
		w.mouseYaw = 0
		w.frames(1)
		local rel2 = c:getHudCFrame():Inverse() * w.viewmodel.placed
		T.truthy(rel2.Position.Y > offset.Position.Y + 0.5, "modifier applied")
		remove()
	end)

	T.test("controller: footstep events fire while walking", function()
		local w = client()
		local steps = {}
		local disconnect = w.controller:onFootstep(function(side, info)
			table.insert(steps, side)
			T.truthy(info.planarSpeed > 15, "info speed")
		end)
		w.walk(16)
		w.frames(180)
		T.truthy(#steps >= 10, "steps: " .. #steps)
		for i = 2, #steps do
			T.truthy(steps[i] ~= steps[i - 1], "alternating")
		end
		local info = w.controller.frameInfo
		for _, key in { "dt", "aim", "yaw", "pitch", "humanoid", "rootPart", "character", "planarSpeed", "ads", "studsPerMetre", "state" } do
			T.truthy(info[key] ~= nil, "info." .. key)
		end
		T.equal(info.grounded, true, "grounded")
		T.equal(info.aiming, false, "aiming")
		T.truthy(info.footPhase ~= nil, "foot phase")
		-- The bob moves the view down at contacts, not the aim.
		local lowest = 0
		for _ = 1, 60 do
			w.frames(1)
			local c = w.controller
			lowest = math.min(lowest, c:getViewCFrame().Position.Y - c:getAimCFrame().Position.Y)
		end
		T.truthy(lowest < -0.05, "head bob: " .. lowest)
		disconnect()
		local count = #steps
		w.frames(60)
		T.equal(#steps, count, "disconnected")
		w.walk(0)
	end)

	T.test("controller: kickAim reaches the camera scripts without counting as lag", function()
		local w = client()
		w.frames(30)
		local c = w.controller
		local _, before = w.Aim.aimFromLook(c:getAimCFrame().LookVector)
		c:kickAim(0.01, 0.03)
		w.frames(1)
		local _, seenPitch = w.Aim.aimFromLook(w.seen[#w.seen].LookVector)
		T.near(seenPitch, before + 0.03, 1e-9, "the camera scripts read the kicked aim")
		local _, after = w.Aim.aimFromLook(c:getAimCFrame().LookVector)
		T.near(after, before + 0.03, 1e-9, "aim kicked")
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) < 1e-6, "the view turned with it (no lag)")
		T.truthy(math.abs(c.core.yawRate) < 1e-9, "not counted as mouse speed")
	end)

	T.test("controller: body, view and viewmodel hooks; notifyShot; ADS", function()
		local w = client()
		local c = w.controller
		local removeBody = c:addBodyModifier(function()
			return CFrame.new(0, -1.5, 0)
		end)
		local removeView = c:addViewModifier(function(view)
			return view * CFrame.new(0.5, 0, 0)
		end)
		w.frames(10)
		local aim, view, hud = c:getAimCFrame(), c:getViewCFrame(), c:getHudCFrame()
		T.near(aim.Position.Y - view.Position.Y, 1.5, 0.01, "body lowers the view")
		T.near(aim.Position.Y - hud.Position.Y, 1.5, 0.01, "and the HUD")
		T.near((view.Position - aim.Position):Dot(aim.RightVector), 0.5, 0.01, "view modifier on the view")
		T.near((hud.Position - aim.Position):Dot(aim.RightVector), 0, 0.01, "not on the HUD")
		removeBody()
		removeView()
		w.frames(1)
		T.truthy((c:getViewCFrame().Position - c:getAimCFrame().Position).Magnitude < 0.01, "removed")
		-- A shot kicks the viewmodel back from the HUD camera.
		local rest = (c:getHudCFrame():Inverse() * w.viewmodel.placed).Position
		c:notifyShot(1)
		local back = 0
		for _ = 1, 4 do
			w.frames(1)
			back = math.max(back, (c:getHudCFrame():Inverse() * w.viewmodel.placed).Position.Z - rest.Z)
		end
		T.truthy(back > 0.03, "weapon kicked back: " .. back)
		-- ADS blend eases.
		c:setAiming(true)
		T.equal(c.ads.active, true, "active")
		w.frames(3)
		T.truthy(c.ads.blend > 0 and c.ads.blend < 1, "blending")
		w.frames(30)
		T.equal(c.ads.blend, 1, "aimed")
		c:setAdsBlend(0.25)
		w.frames(5)
		T.equal(c.ads.blend, 0.25, "manual blend holds")
		c:setAiming(false)
		w.frames(30)
		T.equal(c.ads.blend, 0, "released")
	end)

	T.test("controller: features, presets and resets", function()
		local w = client()
		local c = w.controller
		for _, name in { "LookLag", "HeadBob", "HeadRoll", "SprintLean", "LandingDip", "WeaponLower", "FireKick", "WristRoll", "MovementInertia" } do
			T.equal(c:featureEnabled(name), true, name)
		end
		T.equal(c:featureEnabled("Nope"), false, "unknown feature")
		c.config.head.bobEnabled = false
		T.equal(c:featureEnabled("HeadBob"), false, "sub-switch")
		T.equal(c:featureEnabled("HeadRoll"), true, "others stay")
		T.truthy(c:applyPreset("Off"), "Off preset")
		T.equal(c:featureEnabled("LookLag"), false, "off")
		w.mouseYaw = 0.02
		w.frames(20)
		T.truthy(c:getViewCFrame() == c:getAimCFrame(), "all off: the view is the aim")
		T.truthy(c:applyPreset("Default"), "Default preset")
		w.frames(20)
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) > 1 * RAD, "lagging again")
		-- A camera jump of more than 50 studs resets all motion.
		w.position = Vector3.new(200, 5, 0)
		w.frames(1)
		T.truthy(angle(c:getViewCFrame(), c:getAimCFrame()) < 1e-9, "reset on the jump")
		-- The camera scripts' own moves are not overwritten by the restore.
		w.mouseYaw = 0
		w.frames(30)
		local placed = CFrame.new(0, 50, 0)
		w.env.camera.CFrame = placed
		w.env.step(DT, function() end)
		T.truthy((c:getAimCFrame().Position - placed.Position).Magnitude < 1e-9, "a camera moved by a script becomes the aim")
	end)

	T.test("controller: movement inertia drives the humanoid when asked", function()
		local w = client({ applyMovementInertia = true })
		w.env.humanoid.MoveDirection = Vector3.new(0, 0, -1)
		w.frames(1)
		local moves = w.env.humanoid.moves
		T.truthy(#moves >= 1, "Move called")
		local first = moves[#moves]
		T.truthy(first.Magnitude > 0 and first.Magnitude < 0.5, "eased start: " .. first.Magnitude)
		w.frames(60)
		T.near(moves[#moves].Magnitude, 1, 1e-3, "full speed")
		T.near(w.controller.frameInfo.speedFraction, 1, 1e-3, "speed fraction")
	end)

	T.test("controller: stop unbinds and leaves the camera on the aim", function()
		local w = client()
		w.mouseYaw = 0.02
		w.frames(10)
		local c = w.controller
		local aim = c:getAimCFrame()
		c:stop()
		T.equal(#w.env.bound, 0, "no render steps")
		T.truthy(w.env.camera.CFrame == aim, "camera on the aim")
	end)
end
