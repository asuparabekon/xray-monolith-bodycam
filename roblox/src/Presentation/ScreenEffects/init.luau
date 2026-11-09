-- Bodycam screen look for Roblox: film grain, vignette, scanlines, eye adaptation,
-- colour grade, a bodycam HUD and an experimental glass lens.
--
-- Roblox has no custom post-process shaders and scripts cannot read the rendered
-- frame, so each effect is composited the closest way Roblox allows:
--   * Vignette: three.js's Eskil vignette is mix(scene, grey, k), which is exactly an
--     alpha overlay, so the baked image reproduces the shader 1:1.
--   * Grain: the FilmShader noise pattern is baked into a few tiles that cycle. The
--     shader brightens the scene by base * noise; an overlay cannot read the base, so
--     the grain is a light alpha overlay of the same noise.
--   * Scanlines: the pmndrs pattern as a dark alpha stripe tile (the source blends it
--     with OVERLAY, which ImageLabels cannot do).
--   * Eye adaptation: Pattanaik adaptation (pmndrs) on a luminance probed with
--     raycasts (sky openness and sun visibility), driving ExposureCompensation, plus a
--     small exposure hunt like a real bodycam's auto exposure.
--   * Lens: Roblox cannot distort the frame. The experimental Glass sphere trick
--     refracts the view at graphics quality 8 or higher, and hides transparent
--     objects, particles and decals behind it. Off by default; tune in Studio.
-- Ported math: ShaderMath.luau. Overlay, HUD, grade, probe and lens: written for this
-- project.

local AssetService = game:GetService("AssetService")
local Lighting = game:GetService("Lighting")
local Players = game:GetService("Players")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local ShaderMath = require(script.ShaderMath)

local ScreenEffects = {}
ScreenEffects.__index = ScreenEffects
ScreenEffects.ShaderMath = ShaderMath

function ScreenEffects.defaultConfig()
	return {
		Grain = {
			Enabled = true,
			Intensity = 0.35, -- FilmShader intensity (its default is 0.5)
			Opacity = 0.10, -- strongest overlay alpha at intensity 1
			TileSize = 128, -- pixels
			PixelScale = 2, -- screen pixels per grain pixel
			Frames = 6, -- tiles baked and cycled
			Fps = 24,
		},
		Vignette = {
			Enabled = true,
			Offset = 1.0, -- VignetteShader defaults
			Darkness = 1.0,
			Resolution = 128,
		},
		Scanlines = {
			Enabled = false,
			Density = 1.25, -- ScanlineEffect default: lines per screen pixel row
			Opacity = 0.06,
			ScrollSpeed = 0,
		},
		Exposure = {
			Enabled = true,
			Tau = 1.0, -- adaptation rate (pmndrs default)
			MinLuminance = 0.01,
			MiddleGrey = 0.6,
			Strength = 0.6, -- share of the EV correction applied
			MinEV = -1,
			MaxEV = 1.5,
			HuntEV = 0.05, -- small exposure hunting, like a bodycam's auto exposure
			HuntFrequency = 0.6,
			ProbeRate = 4, -- luminance probes per second
			ProbeDistance = 300, -- studs
		},
		ColorGrade = {
			Enabled = true,
			Saturation = -0.18,
			Contrast = 0.12,
			Brightness = 0,
			TintColor = Color3.fromRGB(232, 240, 255),
		},
		Hud = {
			Enabled = true,
			-- A fictional device. Do not imitate a real bodycam maker's mark or layout.
			Label = "BODYCAM  BC-0451",
			Format = "YYYY-MM-DD  HH:mm:ss",
			Utc = true,
			TextSize = 18,
			Rec = true,
		},
		Lens = {
			Enabled = false, -- experimental
			Distance = 0.12, -- studs in front of the camera
			Diameter = 0.35,
			Transparency = 0.6,
		},
	}
end

---------------------------------------------------------------------------
-- Baking images.
---------------------------------------------------------------------------

-- Builds an EditableImage from fn(x, y) -> r, g, b, a (0..1). Returns nil where
-- EditableImage is unavailable (older clients, or the experience may not use it).
function ScreenEffects.bake(width: number, height: number, fn)
	local ok, image = pcall(function()
		return AssetService:CreateEditableImage({ Size = Vector2.new(width, height) })
	end)
	if not ok or not image then
		return nil
	end
	local pixels = buffer.create(width * height * 4)
	local i = 0
	for y = 0, height - 1 do
		for x = 0, width - 1 do
			local r, g, b, a = fn(x, y)
			buffer.writeu8(pixels, i, math.clamp(math.floor(r * 255 + 0.5), 0, 255))
			buffer.writeu8(pixels, i + 1, math.clamp(math.floor(g * 255 + 0.5), 0, 255))
			buffer.writeu8(pixels, i + 2, math.clamp(math.floor(b * 255 + 0.5), 0, 255))
			buffer.writeu8(pixels, i + 3, math.clamp(math.floor(a * 255 + 0.5), 0, 255))
			i += 4
		end
	end
	local wrote = pcall(function()
		image:WritePixelsBuffer(Vector2.zero, Vector2.new(width, height), pixels)
	end)
	if not wrote then
		image:Destroy()
		return nil
	end
	return image
end

local function imageLabel(parent, name: string, zIndex: number)
	local label = Instance.new("ImageLabel")
	label.Name = name
	label.BackgroundTransparency = 1
	label.Size = UDim2.fromScale(1, 1)
	label.ZIndex = zIndex
	label.Parent = parent
	return label
end

local function setImage(label, image)
	label.ImageContent = Content.fromObject(image)
end

---------------------------------------------------------------------------
-- Lifecycle.
---------------------------------------------------------------------------

-- options:
--   config        ScreenEffects.defaultConfig() shape
--   parent        where the ScreenGui goes (default the local PlayerGui)
--   getLuminance  function(): number  scene luminance 0..1 (default: raycast probe)
--   getViewCFrame function(): CFrame? for the probe and lens (default Camera.CFrame)
--   rayParams     RaycastParams for the probe (default: exclude the local character)
function ScreenEffects.new(options)
	options = options or {}
	local self = setmetatable({}, ScreenEffects)
	self.options = options
	self.config = options.config or ScreenEffects.defaultConfig()
	self.time = 0
	self.adapted = 1
	self.luminance = 1
	self.probeIn = 0
	self.grainFrame = 1
	self.grainClock = 0
	self.started = false
	self.instances = {}
	return self
end

function ScreenEffects:_add(instance)
	table.insert(self.instances, instance)
	return instance
end

function ScreenEffects:start()
	if self.started then
		return
	end
	self.started = true
	local config = self.config
	local parent = self.options.parent
	if not parent then
		local player = Players.LocalPlayer
		parent = player and player:WaitForChild("PlayerGui")
	end

	local gui = self:_add(Instance.new("ScreenGui"))
	gui.Name = "BodycamScreen"
	gui.IgnoreGuiInset = true
	gui.ResetOnSpawn = false
	gui.DisplayOrder = 50
	gui.Parent = parent
	self.gui = gui

	-- Vignette (exact Eskil overlay).
	local v = config.Vignette
	if v.Enabled then
		local n = v.Resolution
		local image = ScreenEffects.bake(n, n, function(x, y)
			local grey, alpha = ShaderMath.eskilVignette((x + 0.5) / n, (y + 0.5) / n, v.Offset, v.Darkness)
			return grey, grey, grey, alpha
		end)
		if image then
			self.vignette = imageLabel(gui, "Vignette", 3)
			setImage(self.vignette, image)
			self:_add(image)
		end
	end

	-- Grain tiles.
	local g = config.Grain
	if g.Enabled then
		self.grainImages = {}
		local n = g.TileSize
		for frame = 1, g.Frames do
			local time = frame * 0.6180339887 -- spreads the frames through the noise
			local image = ScreenEffects.bake(n, n, function(x, y)
				local noise = ShaderMath.filmNoise((x + 0.5) / n, (y + 0.5) / n, time)
				local alpha = g.Opacity * g.Intensity * math.clamp(0.1 + noise, 0, 1)
				return noise, noise, noise, alpha
			end)
			if image then
				table.insert(self.grainImages, image)
				self:_add(image)
			end
		end
		if #self.grainImages > 0 then
			self.grain = imageLabel(gui, "Grain", 2)
			self.grain.ScaleType = Enum.ScaleType.Tile
			self.grain.TileSize = UDim2.fromOffset(n * g.PixelScale, n * g.PixelScale)
			setImage(self.grain, self.grainImages[1])
		end
	end

	-- Scanlines: a one-pixel-wide stripe tile. The source sets count = height * density
	-- over uv.y in 0..1, so each screen pixel row advances the phase by `density`
	-- radians. The tile spans a whole number of periods so it repeats without a seam.
	local s = config.Scanlines
	if s.Enabled then
		local period = 2 * math.pi / s.Density
		local periods = math.max(1, math.floor(64 / period + 0.5))
		local rows = math.max(2, math.floor(period * periods + 0.5))
		local density = 2 * math.pi * periods / rows
		local image = ScreenEffects.bake(1, rows, function(_, y)
			local line = ShaderMath.scanline(y + 0.5, density, 0, 0)
			return 0, 0, 0, s.Opacity * (0.5 - 0.5 * line)
		end)
		if image then
			self.scanlines = imageLabel(gui, "Scanlines", 1)
			self.scanlines.ScaleType = Enum.ScaleType.Tile
			self.scanlines.TileSize = UDim2.fromOffset(1, rows)
			setImage(self.scanlines, image)
			self:_add(image)
		end
	end

	-- Colour grade.
	local c = config.ColorGrade
	if c.Enabled then
		local grade = self:_add(Instance.new("ColorCorrectionEffect"))
		grade.Name = "BodycamGrade"
		grade.Saturation = c.Saturation
		grade.Contrast = c.Contrast
		grade.Brightness = c.Brightness
		grade.TintColor = c.TintColor
		grade.Parent = Lighting
	end

	-- HUD.
	local h = config.Hud
	if h.Enabled then
		local label = Instance.new("TextLabel")
		label.Name = "BodycamHud"
		label.BackgroundTransparency = 1
		label.AnchorPoint = Vector2.new(1, 0)
		label.Position = UDim2.new(1, -24, 0, 20)
		label.Size = UDim2.fromOffset(420, 44)
		label.Font = Enum.Font.Code
		label.TextSize = h.TextSize
		label.TextColor3 = Color3.fromRGB(235, 235, 235)
		label.TextStrokeTransparency = 0.6
		label.TextXAlignment = Enum.TextXAlignment.Right
		label.TextYAlignment = Enum.TextYAlignment.Top
		label.ZIndex = 5
		label.Parent = gui
		self.hud = label
		if h.Rec then
			local dot = Instance.new("Frame")
			dot.Name = "Rec"
			dot.BackgroundColor3 = Color3.fromRGB(220, 30, 30)
			dot.BorderSizePixel = 0
			dot.AnchorPoint = Vector2.new(1, 0)
			dot.Position = UDim2.new(1, -24, 0, 66)
			dot.Size = UDim2.fromOffset(10, 10)
			dot.ZIndex = 5
			dot.Parent = gui
			local corner = Instance.new("UICorner")
			corner.CornerRadius = UDim.new(1, 0)
			corner.Parent = dot
			self.recDot = dot
		end
	end

	-- Exposure baseline.
	self.baseExposure = Lighting.ExposureCompensation

	-- Lens.
	local l = config.Lens
	if l.Enabled then
		local lens = self:_add(Instance.new("Part"))
		lens.Name = "BodycamLens"
		lens.Shape = Enum.PartType.Ball
		lens.Size = Vector3.new(l.Diameter, l.Diameter, l.Diameter)
		lens.Material = Enum.Material.Glass
		lens.Transparency = l.Transparency
		lens.Anchored = true
		lens.CanCollide = false
		lens.CanQuery = false
		lens.CanTouch = false
		lens.CastShadow = false
		lens.Parent = Workspace.CurrentCamera
		self.lens = lens
	end

	self.bindName = "BodycamScreen" .. tostring(self)
	-- After the camera and the bodycam view (Camera + 1), so the lens follows the view.
	RunService:BindToRenderStep(self.bindName, Enum.RenderPriority.Last.Value, function(dt)
		self:update(dt)
	end)
end

function ScreenEffects:stop()
	if not self.started then
		return
	end
	self.started = false
	RunService:UnbindFromRenderStep(self.bindName)
	if self.baseExposure then
		Lighting.ExposureCompensation = self.baseExposure
	end
	for _, instance in self.instances do
		instance:Destroy()
	end
	self.instances = {}
	self.vignette, self.grain, self.scanlines, self.hud, self.recDot, self.lens = nil, nil, nil, nil, nil, nil
end

---------------------------------------------------------------------------
-- Frame.
---------------------------------------------------------------------------

function ScreenEffects:_viewCFrame(): CFrame?
	if self.options.getViewCFrame then
		return self.options.getViewCFrame()
	end
	local camera = Workspace.CurrentCamera
	return camera and camera.CFrame
end

-- Scene luminance 0..1 from raycasts: how much sky the camera sees and whether the sun
-- reaches it, scaled by daylight.
function ScreenEffects:probeLuminance(): number
	if self.options.getLuminance then
		return self.options.getLuminance()
	end
	local view = self:_viewCFrame()
	if not view then
		return 1
	end
	local params = self.options.rayParams
	if not params then
		params = RaycastParams.new()
		params.FilterType = Enum.RaycastFilterType.Exclude
		local player = Players.LocalPlayer
		params.FilterDescendantsInstances = { player and player.Character, self.lens }
	end
	local distance = self.config.Exposure.ProbeDistance
	local origin = view.Position
	local open = 0
	local directions = {
		Vector3.new(0, 1, 0),
		(view.LookVector + Vector3.new(0, 0.6, 0)).Unit,
		(view.RightVector + Vector3.new(0, 0.8, 0)).Unit,
		(-view.RightVector + Vector3.new(0, 0.8, 0)).Unit,
		(-view.LookVector + Vector3.new(0, 0.8, 0)).Unit,
	}
	for _, direction in directions do
		if not Workspace:Raycast(origin, direction * distance, params) then
			open += 1
		end
	end
	local sun = Lighting:GetSunDirection()
	local daylight = math.clamp(sun.Y * 4 + 0.15, 0.1, 1)
	local sunLit = if sun.Y > 0 and not Workspace:Raycast(origin, sun * distance, params) then 1 else 0
	return (0.08 + 0.92 * (0.65 * open / #directions + 0.35 * sunLit)) * daylight
end

function ScreenEffects:update(dt: number)
	self.time += dt
	local config = self.config

	-- Grain frames.
	if self.grain and self.grainImages then
		self.grainClock += dt
		local period = 1 / config.Grain.Fps
		if self.grainClock >= period then
			self.grainClock %= period
			self.grainFrame = self.grainFrame % #self.grainImages + 1
			setImage(self.grain, self.grainImages[self.grainFrame])
			-- Shift the tiles a little too, so the pattern never sits still.
			self.grain.Position = UDim2.fromOffset(-((self.grainFrame * 37) % 64), -((self.grainFrame * 53) % 64))
			self.grain.Size = UDim2.new(1, 64, 1, 64)
		end
	end

	-- Eye adaptation.
	local e = config.Exposure
	if e.Enabled then
		self.probeIn -= dt
		if self.probeIn <= 0 then
			self.probeIn = 1 / e.ProbeRate
			self.luminance = self:probeLuminance()
		end
		self.adapted = ShaderMath.adaptLuminance(self.adapted, self.luminance, dt, e.Tau, e.MinLuminance)
		local ev = math.log(e.MiddleGrey / self.adapted, 2) * e.Strength
		ev = math.clamp(ev, e.MinEV, e.MaxEV)
		ev += e.HuntEV * math.noise(self.time * e.HuntFrequency, 0.37)
		Lighting.ExposureCompensation = self.baseExposure + ev
	end

	-- HUD clock and REC blink.
	if self.hud then
		local h = config.Hud
		local now = DateTime.now()
		local stamp = if h.Utc then now:FormatUniversalTime(h.Format, "en-us") .. " Z" else now:FormatLocalTime(h.Format, "en-us")
		self.hud.Text = stamp .. "\n" .. h.Label
		if self.recDot then
			self.recDot.Visible = (self.time % 1) < 0.6
		end
	end

	-- Lens follows the final view.
	if self.lens then
		local view = self:_viewCFrame()
		if view then
			self.lens.CFrame = view * CFrame.new(0, 0, -config.Lens.Distance)
		end
	end
end

return ScreenEffects
