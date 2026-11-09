-- FPSys camera component, rebuilt as a bodycam view layer.
--
-- From the FPSys model: walk bob, idle breathing, strafe tilt, mouse look sway,
-- landing kick and mouse-drift motion blur, with the model's spring settings.
-- New: lean (roll plus a wall-checked sideways shift, a body layer so the weapon leans
-- too) and a per-shot camera shake.
--
-- Changes from the model:
--  * The effects are a BodycamController view modifier instead of writing
--    Camera.CFrame in RenderStepped. Writing the camera after the camera scripts
--    made every effect feed back into the next frame's aim; the modifier changes
--    only the view, and the bodycam controller restores the aim each frame.
--  * The walk bob is a view offset, not Humanoid.CameraOffset, so it does not move
--    the aim (where shots go) either.
--  * The landing kick no longer scales with frame time (the model multiplied it by
--    dt * 60) and its rebound is a timer instead of task.delay.
--  * Springs run on frame time, so pausing or hitching does not jump them.
--  * The humanoid state is read every frame, so it survives respawns (the model
--    connected StateChanged only on the first character).

local Lighting = game:GetService("Lighting")
local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

local Spring = require(script.Parent.Spring)
local DefaultConfig = require(script.Parent.Config)

local CameraEffects = {}
CameraEffects.__index = CameraEffects

local LANDED = Enum.HumanoidStateType.Landed
local OFF_STATES = {
	[Enum.HumanoidStateType.Jumping] = true,
	[Enum.HumanoidStateType.Freefall] = true,
}
local XZ = Vector3.new(1, 0, 1)
local REBOUND_DELAY = 0.1

local function lerp(a: number, b: number, t: number): number
	return a + (b - a) * t
end

-- options:
--   config         FPSys Config table (default: FPSys.Config)
--   bodycam        the BodycamController, for the "Auto" overlap rule (featureEnabled)
--   getLean        function(): number  -1 (left) .. 1 (right), default 0
--   getMouseDelta  function(): Vector2 (default UserInputService:GetMouseDelta)
--   random         Random object for the shot shake (default Random.new())
--   blurParent     where the BlurEffect goes (default Lighting); false for no blur
function CameraEffects.new(options)
	options = options or {}
	local self = setmetatable({}, CameraEffects)
	self.options = options
	self.config = options.config or DefaultConfig
	self.random = options.random or Random.new()
	self.time = 0
	local clock = function()
		return self.time
	end
	local cam = self.config.Camera
	local lean = self.config.Lean
	local shake = self.config.Shake
	self.springs = {
		Speed = Spring.new(0, clock),
		BreathAlpha = Spring.new(1, clock),
		Tilt = Spring.new(0, clock),
		LookSway = Spring.new(Vector3.new(0, 0, 0), clock),
		LandEffect = Spring.new(0, clock),
		Lean = Spring.new(0, clock),
		Shake = Spring.new(Vector3.new(0, 0, 0), clock),
	}
	local s = self.springs
	s.Speed.s = 16
	s.BreathAlpha.s = 8
	s.BreathAlpha.d = 0.9
	s.Tilt.s = cam.TiltSpeed
	s.Tilt.d = cam.TiltDamper
	s.LookSway.s = cam.LookSwaySpeed
	s.LookSway.d = cam.LookSwayDamper
	s.LandEffect.s = cam.LandEffectSpeed
	s.LandEffect.d = cam.LandEffectDamper
	s.Lean.s = lean.Speed
	s.Lean.d = lean.Damper
	s.Shake.s = shake.Speed
	s.Shake.d = shake.Damper
	self.cameraTick = 0
	self.drift = 0
	self.previousState = nil
	self.reboundIn = nil
	self.leanOffset = 0 -- studs, view right
	self.leanRoll = 0 -- radians
	if options.blurParent ~= false and self.config.Enabled.MotionBlur then
		self.blur = Instance.new("BlurEffect")
		self.blur.Name = "FPSysMotionBlur"
		self.blur.Size = 0
		self.blur.Parent = options.blurParent or Lighting
	end
	return self
end

function CameraEffects:destroy()
	if self.blur then
		self.blur:Destroy()
		self.blur = nil
	end
end

-- Whether an FPSys effect runs, after the overlap rule.
function CameraEffects:enabled(name: string): boolean
	if not self.config.Enabled[name] then
		return false
	end
	local bodycam = self.options.bodycam
	if self.config.Overlap == "Auto" and bodycam then
		if name == "WalkBob" and bodycam:featureEnabled("HeadBob") then
			return false
		end
		if name == "StrafeTilt" and bodycam:featureEnabled("HeadRoll") then
			return false
		end
	end
	return true
end

-- A shot: kicks the camera by a random amount in the Shake ranges (degrees), scaled.
function CameraEffects:shake(scale: number?)
	if not self:enabled("Shake") then
		return
	end
	local cfg = self.config.Shake
	local r = self.random
	local k = (scale or 1) * cfg.Speed
	self.springs.Shake:Impulse(Vector3.new(
		r:NextNumber(cfg.Pitch[1], cfg.Pitch[2]) * k,
		r:NextNumber(cfg.Yaw[1], cfg.Yaw[2]) * k,
		r:NextNumber(cfg.Roll[1], cfg.Roll[2]) * k
	))
end

-- The lean this frame: sideways offset (studs, view right) and roll (radians).
function CameraEffects:getLean(): (number, number)
	return self.leanOffset, self.leanRoll
end

function CameraEffects:_mouseDelta()
	if self.options.getMouseDelta then
		return self.options.getMouseDelta()
	end
	return UserInputService:GetMouseDelta()
end

-- Studs of free space from origin along direction (unit), up to maxDistance.
function CameraEffects:_clearance(origin: Vector3, direction: Vector3, maxDistance: number, info): number
	local params = RaycastParams.new()
	params.FilterType = Enum.RaycastFilterType.Exclude
	params.FilterDescendantsInstances = { info.character }
	params.IgnoreWater = true
	local hit = Workspace:Raycast(origin, direction * maxDistance, params)
	return if hit then (hit.Position - origin).Magnitude else maxDistance
end

-- Advances the spring clock once per frame, whichever layer runs first.
function CameraEffects:_advance(info)
	if self.lastInfo ~= info then
		self.lastInfo = info
		self.time += info.dt
	end
end

-- The body layer: the lean (roll plus a sideways shift stopped short of walls), applied
-- to the view and the weapon alike.
function CameraEffects:bodyOffset(info): CFrame?
	self:_advance(info)
	local s = self.springs
	local leanCfg = self.config.Lean
	local leanInput = if self:enabled("Lean") and self.options.getLean then self.options.getLean() else 0
	s.Lean.t = math.clamp(leanInput, -1, 1)
	local lean = s.Lean.p
	local shift = lean * leanCfg.Offset
	if math.abs(shift) > 1e-3 then
		local aim = info.aim
		local side = aim.RightVector * (if shift > 0 then 1 else -1)
		local free = self:_clearance(aim.Position, side, math.abs(shift) + leanCfg.WallClearance, info)
		local allowed = math.max(free - leanCfg.WallClearance, 0)
		shift = math.clamp(shift, -allowed, allowed)
	end
	self.leanOffset = shift
	self.leanRoll = -math.rad(lean * leanCfg.Angle)
	if math.abs(shift) < 1e-4 and math.abs(self.leanRoll) < 1e-5 then
		return nil
	end
	return CFrame.new(shift, 0, 0) * CFrame.Angles(0, 0, self.leanRoll)
end

-- The view layer: BodycamController calls this with the bodycam view each frame.
function CameraEffects:apply(view: CFrame, info): CFrame
	local dt = info.dt
	self:_advance(info)
	local cam = self.config.Camera
	local s = self.springs
	local humanoid, root = info.humanoid, info.rootPart
	local state = info.state
	local active = not OFF_STATES[state]
	local adsScale = lerp(1, cam.AdsScale, info.ads or 0)

	-- Landing kick: down, then back up after a moment.
	if state == LANDED and self.previousState ~= LANDED and self:enabled("LandEffect") then
		s.LandEffect:Impulse(-cam.LandEffectVelocity)
		self.reboundIn = REBOUND_DELAY
	end
	self.previousState = state
	if self.reboundIn then
		self.reboundIn -= dt
		if self.reboundIn <= 0 then
			s.LandEffect:Impulse(cam.LandEffectVelocity)
			self.reboundIn = nil
		end
	end

	local relative = root.CFrame:VectorToObjectSpace(root.AssemblyLinearVelocity)
	local speed = (relative * XZ).Magnitude
	local moveLocal = root.CFrame:VectorToObjectSpace(humanoid.MoveDirection)
	local mouse = self:_mouseDelta()

	s.Speed.t = if active then speed else 0
	s.BreathAlpha.t = if not active then 0 elseif speed > 0.1 then 0 else 1
	s.Tilt.t = if self:enabled("StrafeTilt") then -moveLocal.X else 0
	if self:enabled("LookSway") then
		s.LookSway.t = Vector3.new(
			math.clamp(mouse.X / cam.LookSwayMouseDamper.X, -cam.LookSwayMaxAngle.X, cam.LookSwayMaxAngle.X),
			math.clamp(mouse.Y / cam.LookSwayMouseDamper.Y, -cam.LookSwayMaxAngle.Y, cam.LookSwayMaxAngle.Y),
			0
		)
	else
		s.LookSway.t = Vector3.new(0, 0, 0)
	end

	-- Motion blur from mouse drift.
	if self.blur then
		self.drift = lerp(self.drift, math.clamp(mouse.X, cam.DriftMin, cam.DriftMax), math.min(cam.BaseMult * dt, 1))
		self.blur.Size = math.clamp(math.abs(self.drift * cam.BlurMult) * adsScale, 0, cam.MaxBlur)
	end

	-- Walk bob and breathing as a view offset.
	local speedP = s.Speed.p
	self.cameraTick += dt * speedP
	local offset = Vector3.new(0, 0, 0)
	if self:enabled("WalkBob") then
		offset += Vector3.new(speedP / 64 * math.sin(self.cameraTick / 2.15), speedP / 82 * math.cos(self.cameraTick / 1.65), 0)
	end
	if self:enabled("Breathing") then
		local alpha = s.BreathAlpha.p
		local t = self.time * cam.BreathingSpeed
		offset += Vector3.new(
			alpha * (cam.BreathingScale.X / 10 * math.sin(t)),
			alpha * (cam.BreathingScale.Y / 10 * math.cos(t)),
			0
		)
	end
	offset *= adsScale

	local look = s.LookSway.p
	local shakeP = if self:enabled("Shake") then s.Shake.p else Vector3.new(0, 0, 0)
	local pitch = (math.rad(s.LandEffect.p) + math.rad(look.Y)) * adsScale + math.rad(shakeP.X)
	local roll = (math.rad(s.Tilt.p * cam.TiltAngle) + math.rad(look.X)) * adsScale + math.rad(shakeP.Z)
	local yaw = math.rad(shakeP.Y)

	return view * CFrame.new(offset.X, offset.Y, 0) * CFrame.Angles(pitch, yaw, roll)
end

return CameraEffects
