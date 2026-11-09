-- Roblox adapter: the counterpart of src/xrGame/bodycam_actor_camera.cpp and
-- bodycam_actor_movement.cpp. Reads the local character and the camera once per
-- frame, runs BodycamCore, and applies the composed view and the viewmodel offset.
--
-- Client only (LocalScript). Usage:
--   local Bodycam = require(ReplicatedStorage.Bodycam)
--   local controller = Bodycam.controller().new({ viewmodel = workspace.Viewmodel })
--   controller:start()
--   controller:setAiming(true, 0.5) ; controller:notifyShot(1)
--
-- Camera feedback: the default camera scripts read Camera.CFrame to build the next
-- frame's camera. The controller restores the aim camera before they run
-- (RenderPriority.Camera - 1) and applies the bodycam view after them
-- (RenderPriority.Camera + 1), so the visual lag never feeds back into the aim.

local Players = game:GetService("Players")
local RunService = game:GetService("RunService")
local Workspace = game:GetService("Workspace")

local Core = script.Parent.Parent.Core
local BodycamCore = require(Core.BodycamCore)
local CameraRig = require(Core.CameraRig)
local Settings = require(Core.Settings)
local Simulation = require(Core.Simulation)
local GaitSpeed = require(Core.GaitSpeed)
local MathUtil = require(Core.MathUtil)
local CoordinateSpace = require(script.Parent.CoordinateSpace)

local MoveFlags = Simulation.MoveFlags
local DEG = math.rad(1)

local BodycamController = {}
BodycamController.__index = BodycamController

local nextId = 0

-- options (all optional):
--   config            Settings.defaultConfig() table; shared, editable at runtime
--   studsPerMetre     scale of every authored distance (default 1 / 0.28)
--   walkSpeed         studs/s of a normal walk, the gait anchor (default Humanoid.WalkSpeed;
--                     set it when your sprint changes WalkSpeed)
--   sprintSpeed       studs/s that counts as full sprint (default walkSpeed * 1.6)
--   isSprinting       function(): boolean   (default: never)
--   isCrouching       function(): boolean   (default: never)
--   isLowered         function(): boolean   weapon holstered/safe (default: never)
--   isFirearm         function(): boolean   a gun is equipped (default: true)
--   inCombat          function(): boolean   suppresses lowering (default: never)
--   applyMovementInertia  drive Humanoid:Move with Movement Inertia (default false)
--   viewmodel         Model with a PrimaryPart, placed each frame (optional)
--   viewmodelOffset   CFrame of the viewmodel relative to the camera at hip (default identity)
--   gripPivot         Vector3 metres (right, up, forward) the arm IK turns about
--   footPhase         function(): number?  the game's own step phase (left 0, right .5);
--                     default: a stride clock from the planar speed
--   rayParams         RaycastParams for the footstep wall check (default: exclude character)
--   isLeaning         function(): boolean   lean key held; feeds the head roll (default: never)
--
-- Integration hooks (used by FPSys and FPSController):
--   controller:kickAim(yaw, pitch)      turn the aim without the mouse (recoil, scripts)
--   controller:addViewModifier(fn)      fn(view: CFrame, info) -> CFrame, after the bodycam view
--   controller:onFootstep(fn)           fn(side: "Left" | "Right", info) on each foot contact
--   controller.frameInfo                what the last frame read (speeds, flags, ads, ...)
function BodycamController.new(options)
	options = options or {}
	local self = setmetatable({}, BodycamController)
	nextId += 1
	self.id = nextId
	self.options = options
	self.config = options.config or Settings.defaultConfig()
	self.core = BodycamCore.new(self.config)
	self.studsPerMetre = options.studsPerMetre or CoordinateSpace.DEFAULT_STUDS_PER_METRE
	self.strideClock = GaitSpeed.StrideClock.new()
	self.frameInput = { camera = CameraRig.newInput() }
	self.frameCount = 0
	self.ads = { active = false, blend = 0, target = 0, speed = 6 }
	self.aimCFrame = nil
	self.prevAim = nil
	self.viewCFrame = nil
	self.connections = {}
	self.started = false
	self.pendingKick = { yaw = 0, pitch = 0 }
	self.viewModifiers = {}
	self.footstepListeners = {}
	self.lastFootPhase = nil
	self.frameInfo = nil
	return self
end

---------------------------------------------------------------------------
-- Game-facing API.
---------------------------------------------------------------------------

-- Aim down sights. blendSpeed is the ADS blend rate in 1/s (default 6).
function BodycamController:setAiming(active: boolean, blendSpeed: number?)
	self.ads.active = active
	self.ads.target = if active then 1 else 0
	self.ads.speed = blendSpeed or self.ads.speed
end

-- Or drive the ADS blend directly (0..1), e.g. from the weapon's aim animation.
function BodycamController:setAdsBlend(blend: number)
	self.ads.blend = math.clamp(blend, 0, 1)
	self.ads.target = self.ads.blend
end

-- A shot. power 1 is a normal rifle shot.
function BodycamController:notifyShot(power: number?)
	self.core:notifyShot(power or 1, self.ads.active)
end

-- Per-weapon hip offset: pos metres (right, up, forward), rot degrees (yaw, pitch, roll).
function BodycamController:setWeaponProfile(pos, rot, blendSpeed: number?)
	self.core:setViewmodelProfile(pos, rot, blendSpeed)
end

function BodycamController:clearWeaponProfile(blendSpeed: number?)
	self.core:clearViewmodelProfile(blendSpeed)
end

-- Apply a preset: 0 off, 1 balanced, 2 defaults.
function BodycamController:applyPreset(preset: number): boolean
	return Settings.applyPreset(self.config, preset)
end

-- The last composed camera CFrame (what the player sees), and the aim CFrame (where
-- bullets should go). Raycast weapons should shoot along the aim, not the view.
function BodycamController:getViewCFrame(): CFrame?
	return self.viewCFrame
end

function BodycamController:getAimCFrame(): CFrame?
	return self.aimCFrame
end

-- Turns the aim by these angles (radians; yaw positive turns left, pitch positive looks
-- up) before the camera scripts run next frame. Like the engine's script camera deltas,
-- the turn rebases the bodycam lag instead of counting as mouse movement, so recoil
-- moves the view at once and does not throw the arms.
function BodycamController:kickAim(yaw: number, pitch: number)
	self.pendingKick.yaw += yaw
	self.pendingKick.pitch += pitch
end

-- Adds a view layer applied after the bodycam composition every frame. Layers run in
-- the order added. Returns a function that removes the layer.
function BodycamController:addViewModifier(fn)
	table.insert(self.viewModifiers, fn)
	return function()
		local index = table.find(self.viewModifiers, fn)
		if index then
			table.remove(self.viewModifiers, index)
		end
	end
end

-- Calls fn(side, info) when the stride reaches a foot contact. Returns a disconnect function.
function BodycamController:onFootstep(fn)
	table.insert(self.footstepListeners, fn)
	return function()
		local index = table.find(self.footstepListeners, fn)
		if index then
			table.remove(self.footstepListeners, index)
		end
	end
end

---------------------------------------------------------------------------
-- Lifecycle.
---------------------------------------------------------------------------

function BodycamController:start()
	if self.started then
		return
	end
	self.started = true
	local before = "BodycamRestore" .. self.id
	local after = "BodycamApply" .. self.id
	RunService:BindToRenderStep(before, Enum.RenderPriority.Camera.Value - 1, function()
		self:_restoreAim()
	end)
	RunService:BindToRenderStep(after, Enum.RenderPriority.Camera.Value + 1, function(dt)
		self:_step(dt)
	end)
	self.boundNames = { before, after }
	table.insert(
		self.connections,
		Players.LocalPlayer.CharacterAdded:Connect(function()
			self.core:reset()
			self.prevAim = nil
			self.aimCFrame = nil
		end)
	)
end

function BodycamController:stop()
	if not self.started then
		return
	end
	self.started = false
	for _, name in self.boundNames do
		RunService:UnbindFromRenderStep(name)
	end
	for _, connection in self.connections do
		connection:Disconnect()
	end
	self.connections = {}
	self:_restoreAim()
	self.aimCFrame = nil
	self.prevAim = nil
end

---------------------------------------------------------------------------
-- Frame.
---------------------------------------------------------------------------

local PITCH_LIMIT = math.rad(89)

function BodycamController:_restoreAim()
	local camera = Workspace.CurrentCamera
	if not (camera and self.aimCFrame) then
		self.pendingKick.yaw, self.pendingKick.pitch = 0, 0
		return
	end
	local kick = self.pendingKick
	if kick.yaw ~= 0 or kick.pitch ~= 0 then
		local yaw, pitch = CoordinateSpace.aimFromLook(self.aimCFrame.LookVector)
		local newPitch = math.clamp(pitch + kick.pitch, -PITCH_LIMIT, PITCH_LIMIT)
		local turned = CFrame.new(self.aimCFrame.Position) * CoordinateSpace.aimRotation(yaw + kick.yaw, newPitch)
		self.core:rebaseLookDelta(kick.yaw, newPitch - pitch)
		self.aimCFrame = turned
		-- The kick is not mouse movement: the next frame's mouse delta starts from here.
		self.prevAim = turned
		kick.yaw, kick.pitch = 0, 0
	end
	camera.CFrame = self.aimCFrame
end

function BodycamController:_emitFootsteps(phase: number?, info)
	local previous = self.lastFootPhase
	self.lastFootPhase = phase
	if phase == nil or previous == nil or #self.footstepListeners == 0 then
		return
	end
	local function crossed(contact: number): boolean
		local delta = (phase - previous) % 1
		local distance = (contact - previous) % 1
		return delta > 0 and delta <= 0.5 and distance > 0 and distance <= delta
	end
	for _, side in { "Left", "Right" } do
		if crossed(if side == "Left" then 0 else 0.5) then
			for _, listener in self.footstepListeners do
				task.spawn(listener, side, info)
			end
		end
	end
end

local function call(fn, default)
	if fn == nil then
		return default
	end
	return fn()
end

-- Movement keys relative to the aim's yaw frame, as ESimMoveFlags.
local function moveFlagsFrom(moveDirection: Vector3, yaw: number): number
	local flags = 0
	if moveDirection.Magnitude < 0.05 then
		return flags
	end
	local right, forward = CoordinateSpace.yawFrame(yaw)
	local f = moveDirection:Dot(forward)
	local r = moveDirection:Dot(right)
	if f > 0.3 then
		flags += MoveFlags.Forward
	elseif f < -0.3 then
		flags += MoveFlags.Back
	end
	if r > 0.3 then
		flags += MoveFlags.Right
	elseif r < -0.3 then
		flags += MoveFlags.Left
	end
	return flags
end

local AIRBORNE = {
	[Enum.HumanoidStateType.Freefall] = MoveFlags.Fall,
	[Enum.HumanoidStateType.Jumping] = MoveFlags.Jump,
	[Enum.HumanoidStateType.Climbing] = MoveFlags.Climb,
	[Enum.HumanoidStateType.Landed] = MoveFlags.Landing,
	[Enum.HumanoidStateType.Swimming] = MoveFlags.Fall,
}

function BodycamController:_horizontalClearance(origin: Vector3, yaw: number, character)
	local params = self.options.rayParams
	if not params then
		params = RaycastParams.new()
		params.FilterType = Enum.RaycastFilterType.Exclude
		params.FilterDescendantsInstances = { character, self.options.viewmodel }
		params.IgnoreWater = true
	end
	local spm = self.studsPerMetre
	return function(right: number, forward: number, maxDistance: number): number?
		local r, f = CoordinateSpace.yawFrame(yaw)
		local direction = (r * right + f * forward) * (maxDistance * spm)
		local hit = Workspace:Raycast(origin, direction, params)
		return if hit then (hit.Position - origin).Magnitude / spm else nil
	end
end

function BodycamController:_step(dt: number)
	local camera = Workspace.CurrentCamera
	local player = Players.LocalPlayer
	local character = player.Character
	local humanoid = character and character:FindFirstChildOfClass("Humanoid")
	local root = humanoid and humanoid.RootPart
	if not camera or not humanoid or not root or humanoid.Health <= 0 then
		self.aimCFrame = nil
		return
	end
	local options = self.options
	local spm = self.studsPerMetre
	local aim = camera.CFrame
	self.aimCFrame = aim
	local yaw, pitch = CoordinateSpace.aimFromLook(aim.LookVector)

	-- A camera that jumped (respawn, cutscene, teleport) starts the bodycam over.
	if self.prevAim and (aim.Position - self.prevAim.Position).Magnitude > 50 then
		self.core:reset(yaw, pitch, self.ads.blend)
		self.prevAim = nil
	end
	if self.prevAim then
		local prevYaw, prevPitch = CoordinateSpace.aimFromLook(self.prevAim.LookVector)
		self.core:addMouseLookDelta(MathUtil.angleDifference(yaw, prevYaw), pitch - prevPitch)
	end
	self.prevAim = aim

	-- ADS blend.
	local ads = self.ads
	ads.blend = MathUtil.smoothFloat(ads.blend, ads.target, ads.speed, dt)
	if math.abs(ads.blend - ads.target) < 1e-3 then
		ads.blend = ads.target
	end

	-- Movement state.
	local state = humanoid:GetState()
	local moveFlags = moveFlagsFrom(humanoid.MoveDirection, yaw)
	local sprinting = call(options.isSprinting, false)
	local crouching = call(options.isCrouching, false)
	if sprinting and moveFlags ~= 0 then
		moveFlags += MoveFlags.Sprint
	end
	if crouching then
		moveFlags += MoveFlags.Crouch
	end
	local airborne = AIRBORNE[state]
	if airborne then
		moveFlags += airborne
	end
	local grounded = humanoid.FloorMaterial ~= Enum.Material.Air and airborne ~= MoveFlags.Fall

	local walkSpeed = humanoid.WalkSpeed
	local walkAnchor = options.walkSpeed or walkSpeed
	local sprintSpeed = options.sprintSpeed or walkAnchor * 1.6
	local velocity = root.AssemblyLinearVelocity
	local planar = Vector3.new(velocity.X, 0, velocity.Z).Magnitude

	-- Movement Inertia: either drive the humanoid, or only measure its speed.
	local targetVelocity = humanoid.MoveDirection * walkSpeed
	if options.applyMovementInertia then
		local eased = self.core:updateMovement(
			dt,
			moveFlags,
			ads.active,
			CoordinateSpace.toEngineVector(targetVelocity, spm)
		)
		-- Runs after the ControlModule's Move this frame, so the eased move wins.
		-- A move vector shorter than 1 walks proportionally slower.
		if walkSpeed > 0 then
			local world = Vector3.new(eased.x, 0, -eased.z) * spm
			humanoid:Move(world / walkSpeed, false)
		end
	else
		local target = targetVelocity.Magnitude / spm
		local actual = planar / spm
		self.core:setMovementSpeed(target, actual, if target > 1e-5 then math.min(actual / target, 1) else 0)
	end

	local referenceSpeed = GaitSpeed.referenceEquivalentSpeed(planar, walkAnchor, sprintSpeed)
	local footPhase
	if options.footPhase then
		footPhase = options.footPhase()
	else
		footPhase = self.strideClock:update(dt, referenceSpeed, grounded and moveFlags ~= 0)
	end

	-- The frame.
	self.frameCount += 1
	local frame = self.frameInput
	local cam = frame.camera
	cam.frame = self.frameCount
	cam.apply = true
	cam.aim_yaw = yaw
	cam.aim_pitch = pitch
	cam.dt = dt
	cam.paused = false
	cam.ads.active = ads.active
	cam.ads.blend = ads.blend
	cam.strafe = (if bit32.band(moveFlags, MoveFlags.Right) ~= 0 then 1 else 0)
		- (if bit32.band(moveFlags, MoveFlags.Left) ~= 0 then 1 else 0)
	cam.leaning = call(options.isLeaning, false)
	cam.sprinting = bit32.band(moveFlags, MoveFlags.Sprint) ~= 0
	cam.velocity = CoordinateSpace.toEngineVector(velocity, spm)
	cam.grounded = grounded
	cam.reference_speed = referenceSpeed
	cam.foot_phase = footPhase
	frame.move_flags = moveFlags
	frame.accelerated = true -- Roblox has no walk/run toggle; slow walk = not accelerated.
	frame.weapon_lowered = call(options.isLowered, false)
	frame.combat = call(options.inCombat, false)
	frame.firearm_equipped = call(options.isFirearm, true)

	local host = { horizontalClearance = self:_horizontalClearance(aim.Position, yaw, character) }
	local composition = self.core:update(frame, host)

	local info = {
		dt = dt,
		aim = aim,
		yaw = yaw,
		pitch = pitch,
		humanoid = humanoid,
		rootPart = root,
		character = character,
		moveFlags = moveFlags,
		grounded = grounded,
		sprinting = cam.sprinting,
		crouching = crouching,
		planarSpeed = planar,
		referenceSpeed = referenceSpeed,
		footPhase = footPhase,
		ads = ads.blend,
		aiming = ads.active,
		studsPerMetre = spm,
		state = state,
	}
	self.frameInfo = info
	self:_emitFootsteps(footPhase, info)

	local view = aim
	if composition.composes then
		view = CoordinateSpace.composeView(aim, yaw, pitch, composition, spm)
	end
	for _, modifier in self.viewModifiers do
		view = modifier(view, info) or view
	end
	if view ~= aim then
		camera.CFrame = view
	end
	self.viewCFrame = view
	self:_placeViewmodel(view)
end

-- The viewmodel follows the HUD camera: the composed view, plus the bodycam HUD
-- offset (fire impulse, lowering, weapon profile) and the STALKER 2 arm pose turned
-- about the grip. This approximates the engine's HUD arm solver, which is not ported.
function BodycamController:_placeViewmodel(view: CFrame)
	local model = self.options.viewmodel
	if not model or not model.PrimaryPart then
		return
	end
	local spm = self.studsPerMetre
	local offset = self.options.viewmodelOffset or CFrame.identity
	local hud = CFrame.identity
	local pos, rot = self.core:getHudOffset(1)
	if pos then
		hud = CFrame.new(CoordinateSpace.localOffset(pos.x, pos.y, pos.z, spm))
			* CoordinateSpace.hpb(rot.x * DEG, rot.y * DEG, rot.z * DEG)
	end
	local arm = self.core:getArmPose()
	if arm then
		local pivotMetres = self.options.gripPivot or Vector3.new(0, -0.12, 0.35)
		local pivot = CoordinateSpace.localOffset(pivotMetres.X, pivotMetres.Y, pivotMetres.Z, spm)
		local w, a = arm.wrist_rot, arm.arm_follow_rot
		local turn = CoordinateSpace.hpb(w.x + a.x, w.y + a.y, w.z + a.z)
		hud = hud * CFrame.new(pivot) * turn * CFrame.new(-pivot)
	end
	model:PivotTo(view * hud * offset)
end

return BodycamController
