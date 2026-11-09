-- Bodycam Controller: the Roblox adapter. Runs BodycamCore for the local character
-- and applies it to the camera and an optional viewmodel.
--
-- Frame order:
--   RenderPriority.Camera - 1  restore the camera to last frame's aim (pending kickAim
--                              applied first, look lag rebased), so the camera scripts
--                              never read the bodycam view back
--   RenderPriority.Camera      Roblox's camera scripts turn the aim
--   RenderPriority.Camera + 1  read the aim, run the core, write the view, place the
--                              viewmodel, fire footstep events
--
-- The aim (getAimCFrame) is where shots go. The view (getViewCFrame) trails it and
-- moves with the head. The HUD camera (getHudCFrame) is the aim moved by the head and
-- the body layers but not the lag; the viewmodel hangs from it.

local Players = game:GetService("Players")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local Settings = require(script.Parent.Settings)
local BodycamCore = require(script.Parent.BodycamCore)
local HeadMotion = require(script.Parent.HeadMotion)
local Aim = require(script.Parent.Aim)

local Controller = {}
Controller.__index = Controller

local DEFAULT_STUDS_PER_METRE = 1 / 0.28
local JUMP_RESET = 50 -- studs
local MAX_PITCH = math.rad(89)
local DEFAULT_ADS_SPEED = 6 -- blend per second

local AIRBORNE = {
	Freefall = true,
	Jumping = true,
	Flying = true,
	FallingDown = true,
	Ragdoll = true,
	Swimming = true,
}

-- Feature names for featureEnabled: group and optional sub-switch.
local FEATURES = {
	LookLag = { "lookLag" },
	HeadMotion = { "head" },
	HeadBob = { "head", "bobEnabled" },
	HeadRoll = { "head", "rollEnabled" },
	SprintLean = { "head", "sprintLeanEnabled" },
	LandingDip = { "head", "landEnabled" },
	WeaponMotion = { "weapon" },
	WeaponLower = { "weapon", "lowerEnabled" },
	Holster = { "weapon", "holsterEnabled" },
	FireKick = { "weapon", "kickEnabled" },
	WristRoll = { "weapon", "wristEnabled" },
	WeaponProfile = { "weapon", "profileEnabled" },
	MovementInertia = { "inertia" },
	StrideClock = { "stride" },
}
Controller.FEATURES = FEATURES

local nextId = 0

-- Whether two CFrames are the same placement (Roblox keeps CFrames in single
-- precision, so a written value can read back slightly different).
local function sameCFrame(a: CFrame, b: CFrame): boolean
	if a == b then
		return true
	end
	return (a.Position - b.Position).Magnitude < 1e-3
		and (a.LookVector - b.LookVector).Magnitude < 1e-4
		and (a.UpVector - b.UpVector).Magnitude < 1e-4
end

-- options:
--   viewmodel             Model with a PrimaryPart (optional); placed each frame
--   viewmodelOffset       CFrame or fn(info) -> CFrame: camera-local placement of the
--                         viewmodel before the modifiers (default identity)
--   viewmodelScale        number: the viewmodel's Model scale (default 1); see setViewmodel
--   gripPivot             Vector3, camera-local studs: the wrist turn's pivot
--   walkSpeed, sprintSpeed  studs/s (default 16, 25.6)
--   isSprinting, isCrouching, isLowered, isLeaning   functions -> bool
--   footPhase             function -> number? (the game's foot phase; default stride clock)
--   applyMovementInertia  bool: drive Humanoid:Move with the eased velocity (default false)
--   config                Settings table (default Settings.default())
--   studsPerMetre         default 1 / 0.28
--   rayParams             RaycastParams for the head's wall checks (default: excludes
--                         the character and the viewmodel)
function Controller.new(options)
	options = options or {}
	local self = setmetatable({}, Controller)
	self.options = options
	self.config = options.config or Settings.default()
	self.studsPerMetre = options.studsPerMetre or DEFAULT_STUDS_PER_METRE
	self.walkSpeed = options.walkSpeed or 16
	self.sprintSpeed = options.sprintSpeed or self.walkSpeed * 1.6
	self.viewmodelOffset = options.viewmodelOffset or CFrame.identity
	self.viewmodelScale = options.viewmodelScale or 1
	self.gripPivot = options.gripPivot or Vector3.new(0.35, -0.35, -1.1)
	local rng = Random.new()
	self.core = BodycamCore.new(self.config, {
		random = function()
			return rng:NextNumber()
		end,
	})
	self.ads = { active = false, blend = 0 }
	self.adsSpeed = DEFAULT_ADS_SPEED
	self.adsManual = false
	self.bodyModifiers = {}
	self.viewModifiers = {}
	self.viewmodelModifiers = {}
	self.footstepListeners = {}
	self.pendingYaw, self.pendingPitch = 0, 0
	self.frameInfo = nil
	self.aim, self.view, self.hud = nil, nil, nil
	self.written = nil
	self.character = nil
	self.started = false
	nextId += 1
	self.bindName = "BodycamController" .. nextId
	return self
end

---------------------------------------------------------------------------
-- Lifecycle.
---------------------------------------------------------------------------

function Controller:start()
	if self.started then
		return
	end
	self.started = true
	self.pendingYaw, self.pendingPitch = 0, 0
	self.written = nil
	self.aim = nil
	self.character = nil
	local camera = Enum.RenderPriority.Camera.Value
	RunService:BindToRenderStep(self.bindName .. "Restore", camera - 1, function()
		self:_restore()
	end)
	RunService:BindToRenderStep(self.bindName .. "Apply", camera + 1, function(dt)
		self:_apply(dt)
	end)
end

function Controller:stop()
	if not self.started then
		return
	end
	self.started = false
	RunService:UnbindFromRenderStep(self.bindName .. "Restore")
	RunService:UnbindFromRenderStep(self.bindName .. "Apply")
	-- Leave the camera on the aim, not the view.
	local camera = Workspace.CurrentCamera
	if camera and self.aim and self.written ~= nil and sameCFrame(camera.CFrame, self.written) then
		camera.CFrame = self.aim
	end
	self.written = nil
end

---------------------------------------------------------------------------
-- Game-facing API.
---------------------------------------------------------------------------

-- Aim key state; the blend eases at blendSpeed per second (default 6).
function Controller:setAiming(active: boolean, blendSpeed: number?)
	self.ads.active = active and true or false
	self.adsManual = false
	if blendSpeed then
		self.adsSpeed = blendSpeed
	end
end

-- Sets the ADS blend directly (a game driving its own ADS animation). It holds until
-- the next setAiming call.
function Controller:setAdsBlend(blend: number)
	self.ads.blend = math.clamp(blend, 0, 1)
	self.adsManual = true
end

function Controller:notifyShot(power: number?)
	self.core:notifyShot(power, self.ads.blend)
end

-- pos {x, y, z} studs camera-local, rot {x, y, z} degrees (pitch, yaw, roll).
function Controller:setWeaponProfile(pos, rot, blendSpeed: number?)
	self.core:setWeaponProfile(pos, rot, blendSpeed)
end

function Controller:clearWeaponProfile(blendSpeed: number?)
	self.core:clearWeaponProfile(blendSpeed)
end

function Controller:applyPreset(name: string): boolean
	return (Settings.applyPreset(self.config, name))
end

-- Turns the aim (radians; yaw positive = left, pitch positive = up). Applied before
-- the camera scripts next frame; the view turns with it (not lag).
function Controller:kickAim(yaw: number, pitch: number)
	self.pendingYaw += yaw or 0
	self.pendingPitch += pitch or 0
end

function Controller:getAimCFrame(): CFrame?
	if self.aim then
		return self.aim
	end
	local camera = Workspace.CurrentCamera
	return camera and camera.CFrame
end

function Controller:getViewCFrame(): CFrame?
	return self.view or self:getAimCFrame()
end

function Controller:getHudCFrame(): CFrame?
	return self.hud or self:getAimCFrame()
end

local function addTo(list, fn)
	table.insert(list, fn)
	return function()
		local i = table.find(list, fn)
		if i then
			table.remove(list, i)
		end
	end
end

-- fn(info) -> CFrame?: a camera-local offset applied to the view and the HUD camera.
function Controller:addBodyModifier(fn)
	return addTo(self.bodyModifiers, fn)
end

-- fn(view, info) -> CFrame: changes the view only.
function Controller:addViewModifier(fn)
	return addTo(self.viewModifiers, fn)
end

-- fn(localCFrame, info) -> CFrame: changes the viewmodel's camera-local placement.
function Controller:addViewmodelModifier(fn)
	return addTo(self.viewmodelModifiers, fn)
end

-- Sets or clears (nil) the viewmodel the controller places each frame.
--   placement  CFrame or fn(info) -> CFrame, camera-local, before the modifiers
--              (nil keeps the current one)
--   scale      the scale the model was given with Model:ScaleTo (nil keeps the current
--              one). The final camera-local position is multiplied by it, so a model
--              shrunk toward the camera looks the same on screen but reaches less far
--              into walls (the scale trick).
function Controller:setViewmodel(model, placement, scale: number?)
	self.options.viewmodel = model
	if placement ~= nil then
		self.viewmodelOffset = placement
	end
	if scale ~= nil then
		self.viewmodelScale = scale
	end
end

-- fn(side "Left" | "Right", info) on each foot contact.
function Controller:onFootstep(fn)
	return addTo(self.footstepListeners, fn)
end

function Controller:featureEnabled(name: string): boolean
	local feature = FEATURES[name]
	if not feature then
		return false
	end
	local group = self.config[feature[1]]
	if type(group) ~= "table" or not group.enabled then
		return false
	end
	if feature[2] then
		return group[feature[2]] == true
	end
	return true
end

---------------------------------------------------------------------------
-- Frame.
---------------------------------------------------------------------------

local function call(fn)
	if fn then
		return fn() and true or false
	end
	return false
end

function Controller:_character()
	local player = Players.LocalPlayer
	local character = player and player.Character
	if not character then
		return nil
	end
	local humanoid = character:FindFirstChildOfClass("Humanoid")
	local root = humanoid and humanoid.RootPart
	if not (humanoid and root) or humanoid.Health <= 0 then
		return nil
	end
	return character, humanoid, root
end

-- Camera - 1: put the camera back on the aim, then apply pending kicks.
function Controller:_restore()
	local camera = Workspace.CurrentCamera
	if not camera then
		return
	end
	-- Restore only when the camera still holds the view we wrote; anything else moved
	-- it on purpose and becomes the aim.
	if self.aim and self.written ~= nil and sameCFrame(camera.CFrame, self.written) then
		camera.CFrame = self.aim
	end
	local ky, kp = self.pendingYaw, self.pendingPitch
	self.pendingYaw, self.pendingPitch = 0, 0
	if ky ~= 0 or kp ~= 0 then
		local current = camera.CFrame
		local yaw, pitch = Aim.aimFromLook(current.LookVector)
		local newPitch = math.clamp(pitch + kp, -MAX_PITCH, MAX_PITCH)
		local kicked = CFrame.new(current.Position) * Aim.aimRotation(yaw + ky, newPitch)
		camera.CFrame = kicked
		self.aim = kicked
		self.core:kick(ky, newPitch - pitch)
	end
end

function Controller:_rayParams(character)
	if self.options.rayParams then
		return self.options.rayParams
	end
	local params = self._defaultRayParams
	if not params then
		params = RaycastParams.new()
		params.FilterType = Enum.RaycastFilterType.Exclude
		params.IgnoreWater = true
		self._defaultRayParams = params
	end
	params.FilterDescendantsInstances = { character, self.options.viewmodel }
	return params
end

-- Free space (studs) beside the head, up to `reach`, left and right.
function Controller:_sideClearance(aim: CFrame, character, reach: number): (number, number)
	local params = self:_rayParams(character)
	local origin = aim.Position
	local right = aim.RightVector
	local function cast(direction)
		local hit = Workspace:Raycast(origin, direction * reach, params)
		return if hit then (hit.Position - origin).Magnitude else reach
	end
	return cast(-right), cast(right)
end

function Controller:_updateAds(dt: number)
	if self.adsManual then
		return
	end
	local target = if self.ads.active then 1 else 0
	local step = self.adsSpeed * dt
	local b = self.ads.blend
	if b < target then
		b = math.min(b + step, target)
	else
		b = math.max(b - step, target)
	end
	self.ads.blend = b
end

local function rotation(pitch: number, yaw: number, roll: number): CFrame
	return CFrame.fromOrientation(pitch, yaw, roll)
end

-- Camera + 1: read the aim, run the core, compose and write the view.
function Controller:_apply(dt: number)
	local camera = Workspace.CurrentCamera
	if not camera then
		return
	end
	local aim = camera.CFrame
	local yaw, pitch = Aim.aimFromLook(aim.LookVector)
	local character, humanoid, root = self:_character()
	self:_updateAds(dt)

	if not character then
		-- No character: the camera is left to the camera scripts.
		self.aim, self.view, self.hud = aim, aim, aim
		self.written = aim
		self.character = nil
		self.frameInfo = nil
		return
	end

	-- Respawn or a camera jump resets all motion.
	local jumped = self.lastAimPosition ~= nil and (aim.Position - self.lastAimPosition).Magnitude > JUMP_RESET
	if character ~= self.character or jumped then
		self.core:reset(yaw, pitch)
		self.character = character
	end
	self.lastAimPosition = aim.Position

	local options = self.options
	local state = humanoid:GetState()
	local stateName = if state then state.Name else ""
	local grounded = humanoid.FloorMaterial ~= Enum.Material.Air and not AIRBORNE[stateName]
	local climbing = stateName == "Climbing"
	local sprinting = call(options.isSprinting)
	local crouching = call(options.isCrouching)
	local leaning = call(options.isLeaning)
	local lowered = call(options.isLowered)
	local velocity = root.AssemblyLinearVelocity
	local walkSpeedNow = humanoid.WalkSpeed
	local move = humanoid.MoveDirection * walkSpeedNow
	local phase = if options.footPhase then options.footPhase() else nil

	-- Wall checks beside the head, only while the bob moves sideways.
	local clearLeft, clearRight = nil, nil
	local headCfg = self.config.head
	if self:featureEnabled("HeadBob") and self.core.head.bobAmp > 1e-3 then
		local reach = HeadMotion.maxSide(headCfg) + headCfg.wallMargin
		clearLeft, clearRight = self:_sideClearance(aim, character, reach)
	end

	local out = self.core:update({
		dt = dt,
		yaw = yaw,
		pitch = pitch,
		ads = self.ads.blend,
		aiming = self.ads.active,
		grounded = grounded,
		sprinting = sprinting,
		crouching = crouching,
		climbing = climbing,
		lowered = lowered,
		leaning = leaning,
		velX = velocity.X,
		velY = velocity.Y,
		velZ = velocity.Z,
		moveX = move.X,
		moveZ = move.Z,
		walkSpeed = self.walkSpeed,
		sprintSpeed = self.sprintSpeed,
		footPhase = phase,
		clearLeft = clearLeft,
		clearRight = clearRight,
	})

	-- Movement inertia drives the humanoid when asked to.
	if options.applyMovementInertia and not out.inertiaBypassed and walkSpeedNow > 0 then
		humanoid:Move(Vector3.new(out.moveX, 0, out.moveZ) / walkSpeedNow, false)
	end

	-- The view: the aim turned by the look lag, then the lag shift and head motion.
	-- Parts that are exactly zero are skipped, so with every feature off the view is
	-- the aim itself and the camera is not written.
	local view = aim
	if out.viewYaw ~= yaw or out.viewPitch ~= pitch then
		view *= Aim.aimRotation(yaw, pitch):Inverse() * Aim.aimRotation(out.viewYaw, out.viewPitch)
	end
	local hx, hy, hz = out.headX, out.headY, out.headZ
	local vx, vy, vz = out.lagX + hx, out.lagY + hy, out.lagZ + hz
	if vx ~= 0 or vy ~= 0 or vz ~= 0 then
		view *= CFrame.new(vx, vy, vz)
	end
	if out.headPitch ~= 0 or out.headRoll ~= 0 then
		view *= rotation(out.headPitch, 0, out.headRoll)
	end
	-- The HUD camera: head position and roll only, no lag.
	local hud = aim
	if hx ~= 0 or hy ~= 0 or hz ~= 0 then
		hud *= CFrame.new(hx, hy, hz)
	end
	if out.headRoll ~= 0 then
		hud *= rotation(0, 0, out.headRoll)
	end

	local info = {
		dt = dt,
		aim = aim,
		yaw = yaw,
		pitch = pitch,
		humanoid = humanoid,
		rootPart = root,
		character = character,
		grounded = grounded,
		sprinting = sprinting,
		crouching = crouching,
		leaning = leaning,
		lowered = lowered,
		planarSpeed = out.planarSpeed,
		footPhase = out.footPhase,
		ads = self.ads.blend,
		aiming = self.ads.active,
		studsPerMetre = self.studsPerMetre,
		state = state,
		speedFraction = out.speedFraction,
	}
	self.aim = aim
	self.frameInfo = info

	for _, fn in table.clone(self.bodyModifiers) do
		local offset = fn(info)
		if offset then
			view *= offset
			hud *= offset
		end
	end
	for _, fn in table.clone(self.viewModifiers) do
		view = fn(view, info) or view
	end

	if view ~= aim then
		camera.CFrame = view
	end
	self.written = view
	self.view, self.hud = view, hud
	info.view, info.hud = view, hud

	-- The viewmodel hangs from the HUD camera.
	local viewmodel = options.viewmodel
	if viewmodel and viewmodel.PrimaryPart then
		local grip = self.gripPivot
		local weaponOffset = CFrame.new(out.weaponX, out.weaponY, out.weaponZ)
			* rotation(out.weaponPitch, out.weaponYaw, out.weaponRoll)
		local wrist = CFrame.new(grip) * rotation(out.wristPitch, 0, out.wristRoll) * CFrame.new(-grip)
		local placement = self.viewmodelOffset
		if type(placement) == "function" then
			placement = placement(info) or CFrame.identity
		end
		local localCFrame = weaponOffset * wrist * placement
		for _, fn in table.clone(self.viewmodelModifiers) do
			localCFrame = fn(localCFrame, info) or localCFrame
		end
		local scale = self.viewmodelScale
		if scale ~= 1 then
			localCFrame = CFrame.new(localCFrame.Position * scale) * localCFrame.Rotation
		end
		viewmodel:PivotTo(hud * localCFrame)
	end

	if out.footstep then
		for _, fn in table.clone(self.footstepListeners) do
			task.spawn(fn, out.footstep, info)
		end
	end
end

return Controller
