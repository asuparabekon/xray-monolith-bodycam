-- Port of src/xrGame/bodycam_camera_rig.h/.cpp: the Classic camera. The look lag
-- (Camera Decoupling's deadzone, or the gun-first follow), the camera shift, the
-- Reference head roll and shake, the Footstep Camera, and the view they compose.
--
-- Engine conventions are kept inside the rig: aim yaw/pitch in radians (heading,
-- pitch of the view direction), camera-local offsets as x right, y up, z forward
-- in metres. Roblox/CoordinateSpace.luau turns a composition into a CFrame.

local MathUtil = require(script.Parent.MathUtil)
local Vec3 = require(script.Parent.Vec3)
local Reference = script.Parent.Parent.Reference
local ClassicFollow = require(Reference.ClassicFollow)
local FootstepCamera = require(Reference.FootstepCamera)
local ReferenceHead = require(Reference.ReferenceHead)
local CameraDelta = require(Reference.CameraDelta)

local clamp = MathUtil.clamp
local lerp = MathUtil.lerp
local degToRad = MathUtil.degToRad
local normalizeAngle = MathUtil.normalizeAngle
local angleDifference = MathUtil.angleDifference
local smoothAngle = MathUtil.smoothAngle
local smoothVector = MathUtil.smoothVector
local followRate = MathUtil.followRate
local clampVector = MathUtil.clampVector
local EPSILON = MathUtil.EPSILON
local EPS_S = 1.0e-6 -- xrCore EPS_S

-- The camera position follows at this share of the angle follow speed.
local CAMERA_POSITION_RATE_SHARE = 0.8
local CAMERA_PITCH_SHIFT_SHARE = 0.55 -- Vertical look shift relative to horizontal.
local CAMERA_FORWARD_SHIFT_SHARE = 0.65 -- Forward movement shift relative to sideways.
local CAMERA_ADS_MOVE_SHIFT_SCALE = 0.35 -- Movement shift kept while aiming.
-- Metres kept between the footstep camera and a wall.
local FOOTSTEP_WALL_CLEARANCE = 0.1
-- Metres; a smaller horizontal footstep offset is not traced against walls.
local FOOTSTEP_MINIMUM_TRACE = 0.001

local CameraRig = {}
CameraRig.__index = CameraRig

---------------------------------------------------------------------------
-- Settings (C++ struct defaults).
---------------------------------------------------------------------------

local function hipModeDefaults()
	return {
		inner_gain = 0,
		follow_speed = 8,
		follow_damping = 0.85,
		deadzone_yaw = 1.5,
		deadzone_pitch = 1,
		softzone_yaw = 4,
		softzone_pitch = 3,
		max_yaw = 19,
		max_pitch = 19,
		pos = 0.025,
	}
end

-- Aiming keeps the camera closer to the gun and lets it catch up faster.
local function adsModeDefaults()
	local ads = hipModeDefaults()
	ads.follow_speed = 14
	ads.follow_damping = 1
	ads.deadzone_yaw = 0.8
	ads.deadzone_pitch = 0.55
	ads.softzone_yaw = 1.8
	ads.softzone_pitch = 1.2
	ads.max_yaw = 3
	ads.max_pitch = 2
	ads.pos = 0.006
	return ads
end

function CameraRig.defaultCameraSettings()
	return {
		enable = true,
		follow_lag = true, -- The gun-first follow takes the place of the deadzone lag.
		hip = hipModeDefaults(),
		ads = adsModeDefaults(),
		move_pos = 0.020,
		follow = { lag_speed_min = 2, lag_speed_max = 16, lag_response = 5, control_smoothing = 25 },
	}
end

-- Reference head motion, part of Camera Decoupling: roll and shake (shake is view-only).
function CameraRig.defaultHeadSettings()
	return { roll = 0.43, shake = 1 }
end

-- Recorded Reference head motion on the footstep schedule.
function CameraRig.defaultFootstepSettings()
	return { enable = true, bob = 1, posture = 2, sprint_lunge = 2, ads = 1 }
end

-- The lag model the rig runs: Follow needs the feature on.
local function usesFollowLag(camera): boolean
	return camera.follow_lag and camera.enable
end
CameraRig.usesFollowLag = usesFollowLag

---------------------------------------------------------------------------
-- Rig input.
---------------------------------------------------------------------------

-- One frame of the host's camera and character state (CameraRigInput).
function CameraRig.newInput()
	return {
		frame = 0, -- Stamps the HUD share.
		apply = true, -- The view may move.
		aim_yaw = 0, -- The camera's aim, radians.
		aim_pitch = 0,
		dt = 0,
		paused = false,
		ads = { active = false, blend = 0 },
		strafe = 0, -- -1 left, 1 right.
		leaning = false,
		sprinting = false,
		inertia_speed = 0, -- Movement Inertia's actual speed, m/s.
		velocity = Vec3.new(), -- m/s, engine world space (x, y up, z = forward at heading 0).
		grounded = true,
		-- The planar speed on Reference's gait speeds, m/s.
		reference_speed = 0,
		foot_phase = nil, -- The leg cycle phase while it strides (left contact 0, right 0.5).
	}
end

---------------------------------------------------------------------------
-- Deadzone lag.
---------------------------------------------------------------------------

local function softRamp(value: number, width: number): number
	if width <= EPSILON then
		return 1
	end
	local t = clamp(value / width, 0, 1)
	return t * t * (3 - 2 * t)
end

local function calcDesiredAngle(current: number, target: number, deadzone: number, softzone: number, innerGain: number): number
	local err = angleDifference(target, current)
	local absError = math.abs(err)
	local inside = normalizeAngle(current + err * innerGain)
	if absError <= deadzone then
		return inside
	end
	local ramp = softRamp(absError - deadzone, softzone)
	local full = normalizeAngle(target - clamp(err, -deadzone, deadzone))
	return normalizeAngle(inside + angleDifference(full, inside) * ramp)
end

-- Camera follow parameters, blended from hip to ADS. Angles in radians.
local function blendCameraResponse(settings, adsBlend: number)
	local hip, ads = settings.hip, settings.ads
	return {
		deadzone_yaw = degToRad(lerp(hip.deadzone_yaw, ads.deadzone_yaw, adsBlend)),
		deadzone_pitch = degToRad(lerp(hip.deadzone_pitch, ads.deadzone_pitch, adsBlend)),
		softzone_yaw = degToRad(lerp(hip.softzone_yaw, ads.softzone_yaw, adsBlend)),
		softzone_pitch = degToRad(lerp(hip.softzone_pitch, ads.softzone_pitch, adsBlend)),
		max_yaw = degToRad(lerp(hip.max_yaw, ads.max_yaw, adsBlend)),
		max_pitch = degToRad(lerp(hip.max_pitch, ads.max_pitch, adsBlend)),
		follow_speed = lerp(hip.follow_speed, ads.follow_speed, adsBlend),
		follow_damping = lerp(hip.follow_damping, ads.follow_damping, adsBlend),
		camera_pos = lerp(hip.pos, ads.pos, adsBlend),
		inner_gain = lerp(hip.inner_gain, ads.inner_gain, adsBlend),
	}
end

local posTarget = Vec3.new()

-- The camera trails the aim inside a deadzone and soft zone, limited to a
-- maximum lag. Its position shifts with the lag and with movement, and
-- carries the sprint sway. With the feature off only the sway remains.
local function updateDeadzoneLag(settings, state, input, followLag: boolean, signals, sway, output)
	if not state.initialized then
		state.yaw = input.aim_yaw
		state.pitch = input.aim_pitch
		state.initialized = true
	end
	-- The look and movement shift ends with the feature; only sway may remain.
	if state.was_enabled and not settings.enable then
		Vec3.set(state.pos, 0, 0, 0)
	end
	state.was_enabled = settings.enable
	local r = blendCameraResponse(settings, input.ads.blend)

	local angleRate = followRate(r.follow_speed, r.follow_damping)
	local desiredYaw = calcDesiredAngle(state.yaw, input.aim_yaw, r.deadzone_yaw, r.softzone_yaw, r.inner_gain)
	local desiredPitch = calcDesiredAngle(state.pitch, input.aim_pitch, r.deadzone_pitch, r.softzone_pitch, r.inner_gain)
	state.yaw = smoothAngle(state.yaw, desiredYaw, angleRate, input.dt)
	state.pitch = smoothAngle(state.pitch, desiredPitch, angleRate, input.dt)

	local yawError = angleDifference(input.aim_yaw, state.yaw)
	local pitchError = angleDifference(input.aim_pitch, state.pitch)
	if math.abs(yawError) > r.max_yaw then
		state.yaw = normalizeAngle(input.aim_yaw - clamp(yawError, -r.max_yaw, r.max_yaw))
	end
	if math.abs(pitchError) > r.max_pitch then
		state.pitch = normalizeAngle(input.aim_pitch - clamp(pitchError, -r.max_pitch, r.max_pitch))
	end

	-- How far the camera trails the aim, as a share of the allowed lag (-1..1).
	local limitedYawError = angleDifference(input.aim_yaw, state.yaw)
	local limitedPitchError = angleDifference(input.aim_pitch, state.pitch)
	local yawNorm = if r.max_yaw > EPSILON then clamp(limitedYawError / r.max_yaw, -1, 1) else 0
	local pitchNorm = if r.max_pitch > EPSILON then clamp(limitedPitchError / r.max_pitch, -1, 1) else 0

	-- Gun-first follow computes the look shift from its own camera lag.
	local lookPos = if settings.enable and not followLag then r.camera_pos else 0
	local movePos = if settings.enable then settings.move_pos else 0
	local moveScale = if input.ads.active then CAMERA_ADS_MOVE_SHIFT_SCALE else 1
	Vec3.set(
		posTarget,
		-yawNorm * lookPos - signals.move_intent.x * movePos * moveScale,
		pitchNorm * lookPos * CAMERA_PITCH_SHIFT_SHARE,
		-signals.move_intent.z * movePos * CAMERA_FORWARD_SHIFT_SHARE * moveScale
	)
	Vec3.add(posTarget, sway.offset)
	smoothVector(state.pos, posTarget, followRate(r.follow_speed * CAMERA_POSITION_RATE_SHARE, r.follow_damping), input.dt)
	clampVector(state.pos, lookPos + movePos + sway.reach)

	output.yaw = if settings.enable then state.yaw else input.aim_yaw
	output.pitch = (if settings.enable then state.pitch else input.aim_pitch) + sway.pitch
	output.sprint_pitch = sway.pitch
	Vec3.assign(output.pos, state.pos)
end

---------------------------------------------------------------------------
-- The rig.
---------------------------------------------------------------------------

local function newCameraState()
	return { initialized = false, was_enabled = false, yaw = 0, pitch = 0, pos = Vec3.new() }
end

function CameraRig.new()
	local self = setmetatable({}, CameraRig)
	self.camera = newCameraState()
	self.followLag = ClassicFollow.Follow.new()
	self.lookTranslation = ClassicFollow.LookTranslation.new()
	self.followOutput = ClassicFollow.newOutput()
	self.footstep = FootstepCamera.new()
	self.head = ReferenceHead.new()
	-- The head has moved since its last reset. Resetting rebuilds its effects,
	-- so a head that is already reset is left alone.
	self.headMoved = false
	-- What the HUD shares: the footstep offset and the head roll (not the shake).
	self.footstepOffset = Vec3.new() -- yaw frame: x right, y up, z forward
	self.headRoll = CameraDelta.IDENTITY
	self.hudShareFrame = -2
	self.composition = {
		lag = { yaw = 0, pitch = 0, pos = Vec3.new(), sprint_pitch = 0 },
		-- Camera-local rotation from head roll and shake (CameraDelta matrix).
		head_rotation = CameraDelta.IDENTITY,
		-- Footstep offset in the aim's yaw frame (x right, y up, z forward), metres.
		footstep_offset = self.footstepOffset,
		composes = false, -- The rig moves the view this frame; otherwise the native camera stays.
	}
	return self
end

function CameraRig:_resetFollow()
	self.followLag:reset()
	self.lookTranslation:reset()
	self.followOutput = ClassicFollow.newOutput()
end

function CameraRig:_resetHead()
	if not self.headMoved then
		return
	end
	self.head:reset()
	self.headMoved = false
end

-- Without an aim the lag starts from the first frame's aim.
function CameraRig:reset(yaw: number?, pitch: number?)
	self.camera = newCameraState()
	self:_resetFollow()
	self.footstep:reset()
	self:_resetHead()
	Vec3.set(self.footstepOffset, 0, 0, 0)
	self.headRoll = CameraDelta.IDENTITY
	if yaw and pitch then
		self.camera.yaw = yaw
		self.camera.pitch = pitch
		self.camera.initialized = true
	end
end

-- A script turned the camera: the deadzone lag turns with it, the follow lag
-- starts again from the displayed angles.
function CameraRig:rebase(yawDelta: number, pitchDelta: number)
	if self.camera.initialized then
		self.camera.yaw = normalizeAngle(self.camera.yaw + yawDelta)
		self.camera.pitch = normalizeAngle(self.camera.pitch + pitchDelta)
	end
	self:_resetFollow()
end

-- The head motion the HUD camera shares, from the current or the previous frame only.
-- Returns offset (yaw frame Vec3), roll (CameraDelta matrix), or nil.
function CameraRig:hudShare(frame: number)
	if self.hudShareFrame + 1 < frame then
		return nil
	end
	return self.footstepOffset, self.headRoll
end

-- The gun-first follow and the look shift it derives from its own camera lag.
-- Both are held reset while it does not own the frame. Returns look shift x, y or nil.
function CameraRig:_updateFollowLag(camera, input, enabled: boolean): (number?, number?)
	local tuning = camera.follow
	self.followOutput = self.followLag:update({
		yaw = input.aim_yaw,
		pitch = input.aim_pitch,
		dt = input.dt,
		enabled = enabled,
		aiming = input.ads.active,
		strafe = input.strafe,
		speedMetres = input.inertia_speed,
		tuning = {
			cameraLagSlowSpeed = tuning.lag_speed_min,
			cameraLagFastSpeed = tuning.lag_speed_max,
			cameraLagRate = tuning.lag_response,
			controlSmoothSpeed = tuning.control_smoothing,
		},
	})
	if not self.followOutput.ownsFollow then
		self.lookTranslation:reset()
		return nil, nil
	end
	local blend = input.ads.blend
	local x, y = self.lookTranslation:update(
		input.aim_yaw,
		input.aim_pitch,
		self.followOutput.cameraYaw,
		self.followOutput.cameraPitch,
		lerp(camera.hip.max_yaw, camera.ads.max_yaw, blend),
		lerp(camera.hip.max_pitch, camera.ads.max_pitch, blend),
		lerp(camera.hip.pos, camera.ads.pos, blend),
		input.dt,
		lerp(camera.hip.follow_speed, camera.ads.follow_speed, blend) * CAMERA_POSITION_RATE_SHARE
	)
	return x, y
end

-- Both lag models run every frame, whichever is selected. The deadzone lag
-- keeps advancing under Follow: its position spring carries the movement shift
-- and the sprint sway, a switch back continues from its state, and Follow's
-- angles reach the output as a change on top of the deadzone angles.
function CameraRig:_updateLag(settings, input, signals, sway)
	local followLag = usesFollowLag(settings.camera)
	local cam = self.camera
	-- Follow starts from the angles the deadzone lag displays.
	if followLag and not self.followOutput.ownsFollow then
		self.followLag:seed(
			input.aim_yaw,
			input.aim_pitch,
			if cam.initialized then cam.yaw else input.aim_yaw,
			if cam.initialized then cam.pitch else input.aim_pitch
		)
	end
	local lag = self.composition.lag
	updateDeadzoneLag(settings.camera, cam, input, followLag, signals, sway, lag)

	local shiftX, shiftY = self:_updateFollowLag(settings.camera, input, followLag)
	if shiftX == nil then
		return
	end
	-- Follow takes over the angles and adds its look shift; the sprint sway the
	-- deadzone lag added stays.
	lag.pos.x += shiftX
	lag.pos.y += shiftY
	lag.yaw += self.followOutput.cameraYaw - cam.yaw
	lag.pitch += self.followOutput.cameraPitch - cam.pitch
	cam.yaw = self.followOutput.cameraYaw
	cam.pitch = self.followOutput.cameraPitch
end

-- Recorded Reference head motion on the footstep schedule, as an offset in the
-- yaw-only frame. Horizontal travel is limited before walls.
-- host.horizontalClearance(right, forward, maxDistance) -> number? receives a unit
-- direction in the yaw frame and returns the distance to the first wall, or nil.
function CameraRig:_updateFootstep(settings, input, host)
	-- Yaw-only basis of the engine: dir = (-sin h, 0, cos h), right = (cos h, 0, sin h).
	local h = input.aim_yaw
	local dirX, dirZ = -math.sin(h), math.cos(h)
	local rightX, rightZ = math.cos(h), math.sin(h)
	local v = input.velocity
	local ads = 1 + (settings.ads - 1) * input.ads.blend
	local offset = self.footstep:update({
		dt = input.dt,
		paused = input.paused,
		footPhase = input.foot_phase,
		forward = v.x * dirX + v.z * dirZ,
		right = v.x * rightX + v.z * rightZ,
		speed = input.reference_speed,
		sprinting = input.sprinting,
		grounded = input.grounded,
		bobScale = settings.bob * ads,
		postureScale = settings.posture * ads,
		lungeScale = settings.sprint_lunge * ads,
	}).offset
	local x, y, z = offset[1], offset[2], offset[3]

	local length = math.sqrt(x * x + z * z)
	if length > FOOTSTEP_MINIMUM_TRACE and host and host.horizontalClearance then
		local range = host.horizontalClearance(x / length, z / length, length + FOOTSTEP_WALL_CLEARANCE)
		if range then
			local allowed = math.max(range - FOOTSTEP_WALL_CLEARANCE, 0)
			if allowed < length then
				x *= allowed / length
				z *= allowed / length
			end
		end
	end
	Vec3.set(self.footstepOffset, x, y, z)
end

-- Reference head roll and shake. The HUD receives the roll only; the shake is view-only.
function CameraRig:_updateHead(settings, input)
	local head = self.head:update({
		yaw = input.aim_yaw,
		pitch = input.aim_pitch,
		dt = input.dt,
		paused = input.paused,
		strafe = input.strafe,
		speedMetres = input.reference_speed,
		aiming = input.ads.active,
		leaning = input.leaning,
	})
	self.headMoved = true
	local roll = head.roll * settings.roll
	local shake = settings.shake
	self.headRoll = CameraDelta.fromRotation(0, 0, roll)
	return CameraDelta.fromRotation(head.shake.pitch * shake, head.shake.yaw * shake, roll + head.shake.roll * shake)
end

-- settings = { camera = defaultCameraSettings(), head = defaultHeadSettings(),
--              footstep = defaultFootstepSettings(), sprint_sway = bool }
-- signals = Simulation state.signals; sway = Simulation output.sprint_sway.
-- Each camera feature works alone: Camera Decoupling (look lag, roll and
-- shake), Footstep Camera, and the Sprint Transition camera sway.
function CameraRig:compose(settings, input, signals, sway, host)
	self:_updateLag(settings, input, signals, sway)
	local composition = self.composition
	composition.composes = false

	local cameraEnabled = settings.camera.enable
	local footstepEnabled = settings.footstep.enable
	if not footstepEnabled then
		self.footstep:reset()
		Vec3.set(self.footstepOffset, 0, 0, 0)
	end
	local lag = composition.lag
	local posSq = lag.pos.x * lag.pos.x + lag.pos.y * lag.pos.y + lag.pos.z * lag.pos.z
	local sprintSway = not cameraEnabled
		and settings.sprint_sway
		and (math.abs(lag.sprint_pitch) > EPS_S or posSq > EPS_S * EPS_S)
	if not cameraEnabled then
		self:_resetHead()
		self.headRoll = CameraDelta.IDENTITY
	end
	if not input.apply or (not cameraEnabled and not footstepEnabled and not sprintSway) then
		return composition
	end

	if footstepEnabled then
		self:_updateFootstep(settings.footstep, input, host)
	end
	composition.head_rotation = if cameraEnabled then self:_updateHead(settings.head, input) else CameraDelta.IDENTITY
	self.hudShareFrame = input.frame
	composition.composes = true
	return composition
end

return CameraRig
