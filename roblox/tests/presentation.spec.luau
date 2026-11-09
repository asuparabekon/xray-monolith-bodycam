-- Presentation: SPT-SWAY sway port, deadzone free aim, handheld shake, screen effects.
return function(T)
	local Presentation = T.Src.Presentation
	local SwayNoise = T.require(Presentation.Sway.SwayNoise)
	local SwayDirector = T.require(Presentation.Sway.SwayDirector)
	local SwayConfig = T.require(Presentation.Sway.SwayConfig)

	T.test("SwayNoise hash and fractal match the C# arithmetic", function()
		local golden = T.require(T.Golden.sway_noise)
		for _, row in golden.hash do
			T.near(SwayNoise.hash(row[1], row[2]), row[3], 1e-7, string.format("hash(%d, %d)", row[1], row[2]))
		end
		for _, row in golden.fractal do
			-- The C# multiplies x * freq * detune in single precision; Luau in double.
			T.near(SwayNoise.signed(row[1], 4801), row[2], 1e-5, "signed " .. row[1])
			T.near(SwayNoise.fractal(row[1], 101, 2, 0.35), row[3], 1e-5, "fractal " .. row[1])
			T.near(SwayNoise.fractal(row[1], 1901, 2, 0.4), row[4], 1e-5, "fractal drift " .. row[1])
		end
	end)

	local function run(director, seconds, dt, fn)
		local frames = math.floor(seconds / dt)
		for i = 1, frames do
			if fn then
				fn(i, director)
			end
			director:update(dt, 0, 0, 0, 0, 0)
		end
	end

	T.test("sway stays centred over time (no aim drift)", function()
		local d = SwayDirector.new()
		local sum = { 0, 0, 0 }
		local peak = 0
		local n = 0
		run(d, 60, 1 / 60, function()
			n += 1
			for a = 1, 3 do
				sum[a] += d.hands[a]
			end
			peak = math.max(peak, math.abs(d.hands[1]))
		end)
		T.truthy(peak > 0.05, "breathing moves the weapon: " .. peak)
		for a = 1, 3 do
			T.truthy(math.abs(sum[a] / n) < 0.1 * peak, "axis " .. a .. " mean near zero")
		end
	end)

	T.test("exertion raises the pulse; stress raises it further", function()
		local d = SwayDirector.new()
		run(d, 10, 1 / 60)
		local rest = d.heart.bpm
		d.state.exertion = 1
		run(d, 20, 1 / 60)
		local spent = d.heart.bpm
		T.truthy(spent > rest + 40, "exertion pulse " .. rest .. " -> " .. spent)
		d.stress:onDamaged(0.3)
		run(d, 5, 1 / 60)
		T.truthy(d.heart.bpm > spent, "fear adds to it")
		T.truthy(d.stress:level() > 0.2, "stress level up")
	end)

	T.test("holding breath quiets the sway, releasing it overshoots", function()
		local d = SwayDirector.new()
		d.config.HeartEnabled, d.config.TremorEnabled, d.config.DriftEnabled = false, false, false
		local function amplitude(seconds)
			local peak = 0
			run(d, seconds, 1 / 60, function()
				peak = math.max(peak, math.abs(d.hands[1]))
			end)
			return peak
		end
		local normal = amplitude(10)
		d.state.holdingBreath = true
		amplitude(2)
		local held = amplitude(4)
		T.truthy(held < normal * 0.4, "held " .. held .. " vs " .. normal)
		d.state.holdingBreath = false
		T.truthy(d.breath.rebound == 0, "no rebound while holding")
		amplitude(1 / 30)
		T.truthy(d.breath.rebound > 0, "rebound after release")
	end)

	T.test("inertia trails a turn and settles", function()
		local d = SwayDirector.new()
		local c = d.config
		c.RespEnabled, c.HeartEnabled, c.TremorEnabled, c.DriftEnabled = false, false, false, false
		local yaw = 0
		for _ = 1, 30 do
			yaw += 3 -- 180 deg/s to the right
			d:update(1 / 60, yaw, 0, 0, 0, 0)
		end
		T.truthy(d.hands[2] < -0.1, "weapon trails the turn to the left: " .. d.hands[2])
		T.truthy(d.hands[3] < 0, "turn cants the weapon")
		for _ = 1, 300 do
			d:update(1 / 60, yaw, 0, 0, 0, 0)
		end
		T.near(d.hands[2], 0, 1e-3, "settled")
	end)

	-- Value noise draws a new random knot TremorFrequency times a second, so its sign
	-- changes less often than that; drift draws 0.28 knots a second.
	T.test("tremor is fast and drift is slow", function()
		local d = SwayDirector.new()
		local c = d.config
		c.RespEnabled, c.HeartEnabled, c.DriftEnabled, c.InertiaEnabled = false, false, false, false
		c.OutputSmoothing = 0
		local previous, crossings = 0, 0
		run(d, 10, 1 / 240, function()
			local v = d.hands[1]
			if (v > 0) ~= (previous > 0) then
				crossings += 1
			end
			previous = v
		end)
		local hz = crossings / 2 / 10
		T.truthy(hz > 2 and hz < 12, "tremor cycles per second: " .. hz)
		local drift = SwayDirector.new()
		local dc = drift.config
		dc.RespEnabled, dc.HeartEnabled, dc.TremorEnabled, dc.InertiaEnabled = false, false, false, false
		previous, crossings = 0, 0
		run(drift, 60, 1 / 60, function()
			local v = drift.hands[1]
			if (v > 0) ~= (previous > 0) then
				crossings += 1
			end
			previous = v
		end)
		T.truthy(crossings / 2 / 60 < 0.5, "drift under 0.5 Hz: " .. crossings / 120)
	end)

	T.test("config holds the SPT-SWAY defaults", function()
		local c = SwayConfig.default()
		T.equal(c.RespRateRest, 14, "breaths per minute")
		T.equal(c.TremorFrequency, 9.5, "tremor Hz")
		T.equal(c.InertiaFrequency, 2.6, "inertia Hz")
		T.equal(c.CameraCoupling, 0.10, "camera coupling")
	end)

	local UnityQuaternion = T.require(Presentation.UnityQuaternion)
	local Deadzone = T.require(Presentation.Deadzone)
	local HandheldShake = T.require(Presentation.HandheldShake)

	T.test("Unity Euler and eulerAngles round-trip in ZXY order", function()
		for _, e in { { 10, 20, 30 }, { -45, 170, 5 }, { 80, -100, -60 }, { 0, 0, 0 } } do
			local q = UnityQuaternion.euler(e[1], e[2], e[3])
			local x, y, z = UnityQuaternion.eulerAngles(q)
			local q2 = UnityQuaternion.euler(x, y, z)
			local dot = math.abs(q[1] * q2[1] + q[2] * q2[2] + q[3] * q2[3] + q[4] * q2[4])
			T.near(dot, 1, 1e-9, "same rotation")
			T.truthy(x >= 0 and x < 360 and y >= 0 and y < 360, "0..360")
		end
		-- Order check: Euler(90, 90, 0) = qy(90) * qx(90).
		local qx, qy = UnityQuaternion.euler(90, 0, 0), UnityQuaternion.euler(0, 90, 0)
		local composed = UnityQuaternion.multiply(qy, qx)
		local direct = UnityQuaternion.euler(90, 90, 0)
		for i = 1, 4 do
			T.near(composed[i], direct[i], 1e-12, "q = qy * qx * qz")
		end
	end)

	T.test("deadzone: the weapon leads a turn by the sensitivity share, up to the max angle", function()
		local dz = Deadzone.new()
		dz:update(1 / 60, 0, 0, false)
		local yawOff = dz:update(1 / 60, math.rad(2), 0, false) -- turned 2 degrees left
		T.near(math.deg(yawOff), 0.5, 1e-3, "a quarter of the turn")
		for k = 1, 60 do
			dz:update(1 / 60, math.rad(2 + k), 0, false)
		end
		local y, p = dz:update(1 / 60, math.rad(62), 0, false)
		T.near(math.deg(math.sqrt(y * y + p * p)), 5, 1e-6, "clamped to 5 degrees")
		T.truthy(y > 0, "offset to the left, the way the view turned")
		-- No return to centre without turning back.
		for _ = 1, 120 do
			y = dz:update(1 / 60, math.rad(62), 0, false)
		end
		T.near(math.deg(y), 5, 1e-6, "stays off centre")
		for k = 1, 20 do
			y = dz:update(1 / 60, math.rad(62 - k), 0, false)
		end
		T.truthy(math.deg(y) < 0.5, "turning back brings it home")
	end)

	T.test("deadzone: aiming fades the offset to the aim multiplier", function()
		local dz = Deadzone.new()
		dz:update(1 / 60, 0, 0, false)
		for k = 1, 40 do
			dz:update(1 / 60, 0, math.rad(k), false)
		end
		local _, hip = dz:update(1 / 60, 0, math.rad(40), false)
		T.truthy(hip > math.rad(4.9), "pitch offset at hip")
		local ads
		for _ = 1, 120 do
			_, ads = dz:update(1 / 60, 0, math.rad(40), true)
		end
		T.truthy(math.abs(ads) < math.rad(0.01), "centred while aiming")
	end)

	T.test("simplex noise: range, continuity and repeatable seeds", function()
		local function lcg(seed)
			local state = seed
			return function()
				state = (state * 1103515245 + 12345) % 2147483648
				return state / 2147483648
			end
		end
		local a = HandheldShake.SimplexNoise.new(lcg(7))
		local b = HandheldShake.SimplexNoise.new(lcg(7))
		local previous = a:noise(-20, 1)
		local maxAbs, maxStep = 0, 0
		for k = 1, 4000 do
			local x = k * 0.01 - 20
			local v = a:noise(x, 1)
			T.equal(v, b:noise(x, 1), "same seed, same noise")
			maxAbs = math.max(maxAbs, math.abs(v))
			maxStep = math.max(maxStep, math.abs(v - previous))
			previous = v
		end
		T.truthy(maxAbs <= 1 and maxAbs > 0.3, "within [-1, 1]: " .. maxAbs)
		T.truthy(maxStep < 0.2, "continuous: largest step " .. maxStep)
	end)

	T.test("handheld shake scales with intensity squared and decays", function()
		local settings = HandheldShake.defaultSettings()
		local full = HandheldShake.new(settings, function()
			return 0.42
		end)
		local half = HandheldShake.new(table.clone(settings), function()
			return 0.42
		end)
		half:setIntensity(0.5)
		for _ = 1, 200 do
			local y1 = full:update(1 / 60)
			local y2 = half:update(1 / 60)
			T.near(y2, y1 * 0.25, 1e-12, "intensity squared")
			T.truthy(math.abs(y1) <= settings.maxYaw + 1e-12, "bounded by maxYaw")
		end
		settings = HandheldShake.defaultSettings()
		settings.decay = true
		local decaying = HandheldShake.new(settings)
		for _ = 1, 120 do
			decaying:update(1 / 60)
		end
		T.near(decaying:getIntensity(), 0, 1e-9, "decays to rest")
	end)
end
