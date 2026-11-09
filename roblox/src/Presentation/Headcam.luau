-- Headcam: switches the bodycam between its eye view (Bodycam mode) and a helmet camera
-- mounted on the head (Headcam mode): a wide lens, the player's own body in view, and
-- the view turned by the head's real animation. Also spectates other players through
-- their head camera. Written for this project.
--
-- Headcam mode is a view modifier on the BodycamController: the aim (where shots go)
-- stays the camera scripts' first-person aim, so hit detection does not change. The
-- view is placed at Head.CFrame * mount, turned partly with the head, and keeps a share
-- of the bodycam's own lag and bob (the head animation supplies most of the motion).
--
-- Spectating reads only the other character's replicated Head. With the Realism
-- package running, heads already follow each player's replicated look angles, so the
-- spectator sees where that player looks; no extra remote is needed.

local Players = game:GetService("Players")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local Smoothing = require(script.Parent.Smoothing)

local Headcam = {}
Headcam.__index = Headcam

Headcam.Mode = table.freeze({ Bodycam = "Bodycam", Headcam = "Headcam" })
local Mode = Headcam.Mode

-- Attachments that put an accessory on the head (hidden with the head).
local HEAD_ATTACHMENTS = {
	FaceCenterAttachment = true,
	FaceFrontAttachment = true,
	HairAttachment = true,
	HatAttachment = true,
}

local MAX_FOV = 120 -- Roblox's FieldOfView limit (vertical degrees)

local nextId = 0

function Headcam.defaultSettings()
	return {
		-- Head-local mount: x right, y up, -z forward (studs). A helmet camera on the
		-- right side of the head, slightly in front of the face.
		mount = CFrame.new(0.32, 0.32, -0.5),
		fov = 100, -- vertical degrees; about 130 horizontal at 16:9
		headFollow = 0.8, -- share of the head's turn the view takes (0 = aim, 1 = helmet)
		headSmoothing = 0.08, -- seconds: takes the jitter out of the head animation
		keepLayers = 0.3, -- share of the bodycam lag, bob and roll kept on top
		blendTime = 0.12, -- seconds: switching between modes
		wallRadius = 0.25, -- the mount is pulled toward the head when it would enter a wall
		wallMargin = 0.1,
		showBody = true, -- set the own body's LocalTransparencyModifier to 0 in headcam
		hideHead = true, -- and the head and its accessories to 1
		spectateSmoothing = 0.1, -- seconds, on another player's head rotation
	}
end

-- The vertical FieldOfView that gives a horizontal angle (degrees) at an aspect ratio.
function Headcam.verticalFov(horizontal: number, aspect: number): number
	local half = math.rad(horizontal) / 2
	return math.deg(2 * math.atan(math.tan(half) / aspect))
end

function Headcam.horizontalFov(vertical: number, aspect: number): number
	local half = math.rad(vertical) / 2
	return math.deg(2 * math.atan(math.tan(half) * aspect))
end

-- options:
--   controller     BodycamController (required)
--   settings       table shaped like defaultSettings() (missing keys use the defaults)
--   mode           starting mode (default Bodycam)
--   onModeChanged  fn(mode) when setMode changes the mode
--   rayParams      RaycastParams for the mount's wall check (default excludes the
--                  character and the viewmodel)
--   applyFov       true: write Camera.FieldOfView from fovFor(baseFov) each frame
--                  (for games without a WeaponController driving the FOV)
--   baseFov        default 70
function Headcam.new(options)
	assert(options and options.controller, "Headcam needs a BodycamController")
	local self = setmetatable({}, Headcam)
	self.options = options
	self.controller = options.controller
	local settings = Headcam.defaultSettings()
	for key, value in options.settings or {} do
		settings[key] = value
	end
	self.settings = settings
	self.mode = options.mode or Mode.Bodycam
	self.blend = if self.mode == Mode.Headcam then 1 else 0
	self.headTurn = nil
	self.saved = {} -- part -> LocalTransparencyModifier before headcam
	self.removers = {}
	self.spectating = nil
	self.spectateRotation = nil
	self.spectateSaved = {}
	nextId += 1
	self.bindName = "HeadcamSpectate" .. nextId
	return self
end

function Headcam:getMode(): string
	return self.mode
end

function Headcam:setMode(mode: string)
	assert(mode == Mode.Bodycam or mode == Mode.Headcam, "unknown headcam mode " .. tostring(mode))
	if mode == self.mode then
		return
	end
	self.mode = mode
	if self.options.onModeChanged then
		task.spawn(self.options.onModeChanged, mode)
	end
end

function Headcam:toggle(): string
	self:setMode(if self.mode == Mode.Headcam then Mode.Bodycam else Mode.Headcam)
	return self.mode
end

-- 0 (bodycam) .. 1 (headcam), eased.
function Headcam:getBlend(): number
	return self.blend
end

-- The FOV for a weapon's target FOV: the headcam lens keeps the scope's zoom ratio.
-- hipFov is the weapon's unzoomed FOV (default the target itself).
function Headcam:fovFor(target: number, hipFov: number?): number
	local blend = self.blend
	if blend <= 0 then
		return target
	end
	local zoom = target / (hipFov or target)
	local wide = math.clamp(self.settings.fov * zoom, 1, MAX_FOV)
	return target + (wide - target) * blend
end

---------------------------------------------------------------------------
-- Mount.
---------------------------------------------------------------------------

function Headcam:_rayParams(character)
	if self.options.rayParams then
		return self.options.rayParams
	end
	local params = self._params
	if not params then
		params = RaycastParams.new()
		params.FilterType = Enum.RaycastFilterType.Exclude
		params.IgnoreWater = true
		self._params = params
	end
	params.FilterDescendantsInstances = { character, self.controller.options.viewmodel }
	return params
end

-- `target`, pulled back toward `origin` when a sphere moved from origin would hit a
-- wall on the way.
function Headcam:safePosition(origin: Vector3, target: Vector3, character): Vector3
	local offset = target - origin
	local distance = offset.Magnitude
	if distance < 1e-4 then
		return target
	end
	local settings = self.settings
	local hit = Workspace:Spherecast(origin, settings.wallRadius, offset, self:_rayParams(character))
	if not hit then
		return target
	end
	local travelled = hit.Distance or ((hit.Position - origin).Magnitude - settings.wallRadius)
	local reach = math.clamp(travelled - settings.wallMargin, 0, distance)
	return origin + offset.Unit * reach
end

-- The headcam view for this frame from the head, the aim and the bodycam view.
function Headcam:headView(head: BasePart, aim: CFrame, view: CFrame, character, dt: number): CFrame
	local settings = self.settings
	local mount = head.CFrame * settings.mount
	local aimRotation = aim.Rotation
	local relative = aimRotation:Inverse() * mount.Rotation
	if self.headTurn == nil then
		self.headTurn = relative
	else
		self.headTurn = self.headTurn:Lerp(relative, Smoothing.alpha(settings.headSmoothing, dt))
	end
	local turn = CFrame.identity:Lerp(self.headTurn, math.clamp(settings.headFollow, 0, 1))
	local keep = CFrame.identity:Lerp(aim:Inverse() * view, math.clamp(settings.keepLayers, 0, 1))
	local position = self:safePosition(head.Position, mount.Position, character)
	return CFrame.new(position) * aimRotation * turn * keep
end

---------------------------------------------------------------------------
-- Body visibility.
---------------------------------------------------------------------------

local function isHeadPart(part: BasePart): boolean
	if part.Name == "Head" then
		return true
	end
	local accessory = part.Parent
	if accessory and accessory:IsA("Accessory") then
		for name in HEAD_ATTACHMENTS do
			if part:FindFirstChild(name) then
				return true
			end
		end
	end
	return false
end

-- Sets each part's LocalTransparencyModifier, remembering the first value it replaced.
local function setTransparency(saved, character, hideHead: boolean)
	for _, part in character:GetDescendants() do
		if part:IsA("BasePart") then
			local value = if hideHead and isHeadPart(part) then 1 else 0
			if part.LocalTransparencyModifier ~= value then
				if saved[part] == nil then
					saved[part] = part.LocalTransparencyModifier
				end
				part.LocalTransparencyModifier = value
			end
		end
	end
end

local function restoreTransparency(saved)
	for part, value in saved do
		part.LocalTransparencyModifier = value
	end
	table.clear(saved)
end

---------------------------------------------------------------------------
-- Frame.
---------------------------------------------------------------------------

function Headcam:_modify(view: CFrame, info): CFrame
	local settings = self.settings
	local dt = info.dt or 0
	local target = if self.mode == Mode.Headcam then 1 else 0
	self.blend = Smoothing.approach(self.blend, target, settings.blendTime, dt)
	if math.abs(self.blend - target) < 1e-3 then
		self.blend = target
	end
	local character = info.character
	local head = character and character:FindFirstChild("Head")
	if self.blend <= 0 or not (head and head:IsA("BasePart")) then
		self.headTurn = nil
		restoreTransparency(self.saved)
		return view
	end
	if settings.showBody and self.blend >= 0.5 then
		setTransparency(self.saved, character, settings.hideHead)
	elseif self.blend < 0.5 then
		restoreTransparency(self.saved)
	end
	local headView = self:headView(head, info.aim, view, character, dt)
	if self.options.applyFov then
		local camera = Workspace.CurrentCamera
		if camera then
			local base = self.options.baseFov or 70
			camera.FieldOfView = self:fovFor(base, base)
		end
	end
	if self.blend >= 1 then
		return headView
	end
	return view:Lerp(headView, self.blend)
end

function Headcam:start()
	self:stop()
	table.insert(self.removers, self.controller:addViewModifier(function(view, info)
		return self:_modify(view, info)
	end))
end

function Headcam:stop()
	for _, remove in self.removers do
		remove()
	end
	self.removers = {}
	restoreTransparency(self.saved)
	self.headTurn = nil
	self:spectate(nil)
end

---------------------------------------------------------------------------
-- Spectating.
---------------------------------------------------------------------------

local function characterOf(target)
	if target == nil then
		return nil
	end
	if target:IsA("Player") then
		return target.Character
	end
	return target
end

-- One spectating frame: the camera on the target's head mount.
function Headcam:_spectateStep(dt: number)
	local camera = Workspace.CurrentCamera
	local character = characterOf(self.spectating)
	local head = character and character:FindFirstChild("Head")
	if not (camera and head and head:IsA("BasePart")) then
		return
	end
	local settings = self.settings
	local mount = head.CFrame * settings.mount
	if self.spectateRotation == nil then
		self.spectateRotation = mount.Rotation
	else
		self.spectateRotation =
			self.spectateRotation:Lerp(mount.Rotation, Smoothing.alpha(settings.spectateSmoothing, dt))
	end
	if self.spectateCharacter ~= character then
		restoreTransparency(self.spectateSaved)
		self.spectateCharacter = character
	end
	-- Hide only the spectated head (local to this client).
	if settings.hideHead then
		for _, part in character:GetDescendants() do
			if part:IsA("BasePart") and isHeadPart(part) and part.LocalTransparencyModifier ~= 1 then
				if self.spectateSaved[part] == nil then
					self.spectateSaved[part] = part.LocalTransparencyModifier
				end
				part.LocalTransparencyModifier = 1
			end
		end
	end
	camera.CameraType = Enum.CameraType.Scriptable
	camera.CFrame = CFrame.new(self:safePosition(head.Position, mount.Position, character)) * self.spectateRotation
	camera.FieldOfView = math.clamp(settings.fov, 1, MAX_FOV)
end

-- Watches another player (Player or character Model) through their head camera, or
-- stops (nil) and gives the camera back to the camera scripts. Spectate while the local
-- character is dead or the BodycamController is stopped: a running controller would
-- take the spectator camera for the player's aim.
function Headcam:spectate(target)
	if self.spectating ~= nil then
		RunService:UnbindFromRenderStep(self.bindName)
		restoreTransparency(self.spectateSaved)
		local camera = Workspace.CurrentCamera
		if camera and self.cameraState then
			camera.CameraType = self.cameraState.CameraType
			camera.FieldOfView = self.cameraState.FieldOfView
		end
		self.spectating = nil
		self.spectateRotation = nil
		self.spectateCharacter = nil
		self.cameraState = nil
	end
	if target == nil then
		return
	end
	local player = Players.LocalPlayer
	assert(target ~= player, "spectate another player, not the local one")
	local camera = Workspace.CurrentCamera
	if camera then
		self.cameraState = { CameraType = camera.CameraType, FieldOfView = camera.FieldOfView }
	end
	self.spectating = target
	RunService:BindToRenderStep(self.bindName, Enum.RenderPriority.Camera.Value + 2, function(dt)
		self:_spectateStep(dt)
	end)
end

function Headcam:getSpectating()
	return self.spectating
end

return Headcam
