-- Screen effects: the ported shader math, and the overlay built on a mocked client.
return function(T)
	local ShaderMath = T.require(T.Src.Presentation.ScreenEffects.ShaderMath)

	T.test("three.js rand and film grain follow the GLSL", function()
		-- rand(uv) = fract(sin(mod(dot(uv, (12.9898, 78.233)), PI)) * 43758.5453)
		local u, v = 0.3, 0.7
		local dt = u * 12.9898 + v * 78.233
		local sn = dt - math.pi * math.floor(dt / math.pi)
		local expected = math.sin(sn) * 43758.5453
		expected -= math.floor(expected)
		T.near(ShaderMath.rand(u, v), expected, 1e-12, "rand")
		local sum, n = 0, 0
		for i = 0, 99 do
			for j = 0, 99 do
				local r = ShaderMath.filmNoise(i / 100, j / 100, 0.37)
				T.truthy(r >= 0 and r < 1, "in [0, 1)")
				sum += r
				n += 1
			end
		end
		T.near(sum / n, 0.5, 0.02, "uniform on average")
		-- color = mix(base, base + base * clamp(0.1 + noise, 0, 1), intensity)
		T.near(ShaderMath.filmColor(0.5, 0.4, 1), 0.75, 1e-12, "film colour")
		T.near(ShaderMath.filmColor(0.5, 0.4, 0), 0.5, 1e-12, "intensity 0 keeps the base")
	end)

	T.test("Eskil vignette is an exact alpha overlay", function()
		local grey, alpha = ShaderMath.eskilVignette(0.5, 0.5, 1, 1)
		T.equal(grey, 0, "black at darkness 1")
		T.equal(alpha, 0, "clear in the centre")
		_, alpha = ShaderMath.eskilVignette(0, 0, 1, 1)
		T.near(alpha, 0.5, 1e-12, "corner = dot(uv, uv) = 0.5")
		-- mix(scene, grey, a) == scene * (1 - a) + grey * a for any scene colour.
		local scene = 0.8
		local mixed = scene + (grey - scene) * alpha
		T.near(mixed, scene * (1 - alpha) + grey * alpha, 1e-12, "alpha compositing")
		T.truthy(ShaderMath.smoothVignette(0.5, 0.5, 0.5, 0.5) > 0.99, "smooth vignette clear in the centre")
	end)

	T.test("lens distortion and eye adaptation match the pmndrs shaders", function()
		local u, v = ShaderMath.lensDistort(0.5, 0.5, 0.2, 0.2)
		T.near(u, 0.5, 1e-12, "centre fixed")
		u = ShaderMath.lensDistort(1, 0.5, 0.2, 0.2)
		T.near(u, 0.5 + 0.5 * 1.2, 1e-12, "edge pushed by (1 + k r^2)")
		local l = ShaderMath.adaptLuminance(1, 0.1, 1 / 60, 1)
		T.near(l, 1 + (0.1 - 1) * (1 - math.exp(-1 / 60)), 1e-12, "Pattanaik step")
		T.near(ShaderMath.adaptLuminance(0, 0, 1, 1), 0.01, 1e-12, "floored at minLuminance")
		T.near(ShaderMath.scanline(0.5, math.pi, 0, 0), 1, 1e-12, "sin(y * count)")
	end)

	-- The overlay on a mocked client.
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local env = Env.new()
	env.install()
	local images = {}
	local services = {
		AssetService = {
			CreateEditableImage = function(_, options)
				local image = { Size = options.Size, destroyed = false }
				function image:WritePixelsBuffer(_, size, pixels)
					self.pixels = pixels
					self.written = size
				end
				function image:Destroy()
					self.destroyed = true
				end
				table.insert(images, image)
				return image
			end,
		},
		Lighting = { ExposureCompensation = 0.25 },
	}
	function services.Lighting:GetSunDirection()
		return Vector3.new(0, 1, 0)
	end
	local baseGetService = env.game.GetService
	env.game.GetService = function(self, name)
		return services[name] or baseGetService(self, name)
	end
	_G.Content = { fromObject = function(object)
		return { object = object }
	end }
	_G.DateTime = { now = function()
		return {
			FormatUniversalTime = function(_, format)
				return "2026-11-09  03:21:44"
			end,
		}
	end }
	_G.UDim2 = { fromScale = function(x, y)
		return { x, y }
	end, fromOffset = function(x, y)
		return { x, y }
	end, new = function(...)
		return { ... }
	end }
	_G.UDim = { new = function(...)
		return { ... }
	end }
	_G.Vector2 = { new = function(x, y)
		return { X = x, Y = y }
	end, zero = { X = 0, Y = 0 } }
	_G.Color3 = { fromRGB = function(r, g, b)
		return { r, g, b }
	end }
	local require = T.isolatedRequire()
	local ScreenEffects = require(T.Src.Presentation.ScreenEffects)

	T.test("screen effects build the overlay and drive exposure", function()
		local gui = {}
		local effects = ScreenEffects.new({ parent = gui, getLuminance = function()
			return 0.05 -- a dark room
		end })
		effects.config.Scanlines.Enabled = true
		effects.config.Lens.Enabled = true
		effects:start()
		-- 1 vignette + 6 grain tiles + 1 scanline tile.
		T.equal(#images, 8, "images baked")
		local vignette = images[1]
		T.equal(buffer.len(vignette.pixels), 128 * 128 * 4, "RGBA pixels")
		-- Centre pixel clear, corner pixel at the Eskil alpha.
		local centre = (64 * 128 + 64) * 4
		T.truthy(buffer.readu8(vignette.pixels, centre + 3) <= 1, "centre clear")
		local cornerAlpha = buffer.readu8(vignette.pixels, 3) / 255
		local _, expected = ShaderMath.eskilVignette(0.5 / 128, 0.5 / 128, 1, 1)
		T.near(cornerAlpha, expected, 1 / 255, "corner alpha")
		T.truthy(effects.grain and effects.vignette and effects.scanlines and effects.hud and effects.lens, "layers")
		for _ = 1, 240 do
			env.step(1 / 60)
		end
		local lighting = services.Lighting
		T.truthy(lighting.ExposureCompensation > 0.25 + 0.5, "dark room raises exposure: " .. lighting.ExposureCompensation)
		T.truthy(string.find(effects.hud.Text, "BODYCAM", 1, true) ~= nil, "HUD label")
		T.truthy(string.find(effects.hud.Text, "2026-11-09", 1, true) ~= nil, "HUD timestamp")
		effects:stop()
		T.near(lighting.ExposureCompensation, 0.25, 1e-12, "exposure restored")
		T.truthy(images[1].destroyed, "images released")
	end)
end
