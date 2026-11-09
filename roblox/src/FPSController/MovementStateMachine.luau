-- Movement states for a first-person character: gait (idle, walking, sprinting,
-- in air, sliding) and pose (standing, crouching, prone), with eased speed changes
-- between gaits. Pure logic: no Roblox API; MovementController.luau applies it.
--
-- Written for this project. The feature set follows the Kinemation FPS Animation
-- Framework demo (gait speeds, crouch, prone, slide, jump rules, transition events);
-- no Kinemation code is used.

local MovementStateMachine = {}
MovementStateMachine.__index = MovementStateMachine

MovementStateMachine.Gait = table.freeze({
	Idle = "Idle",
	Walking = "Walking",
	Sprinting = "Sprinting",
	InAir = "InAir",
	Sliding = "Sliding",
})
MovementStateMachine.Pose = table.freeze({ Standing = "Standing", Crouching = "Crouching", Prone = "Prone" })
local Gait, Pose = MovementStateMachine.Gait, MovementStateMachine.Pose

function MovementStateMachine.defaultSettings()
	return {
		-- studs/s
		walkSpeed = 16,
		sprintSpeed = 25.6,
		crouchSpeed = 8,
		proneSpeed = 3.5,
		-- Seconds to go from one gait speed to the next (eased).
		accelerationTime = 0.25,
		-- Sprint needs the stick or keys mostly forward.
		sprintForwardMin = 0.5,
		sprintSideMax = 0.6,
		slide = {
			startSpeed = 34, -- studs/s at the start of a slide
			endSpeed = 8,
			duration = 0.85,
			endCrouched = true, -- stay crouched after a slide
		},
		-- Input is ignored for this long while going prone or getting up.
		proneTransition = 0.6,
		-- How far the camera drops for each pose, studs.
		crouchDrop = 1.3,
		proneDrop = 2.6,
		dropSpeed = 10, -- 1/s
	}
end

-- onEvent(name): StartMoving, StopMoving, SprintStarted, SprintEnded, Crouched,
-- Uncrouched, ProneStarted, ProneEnded, Jumped, Fell, Landed, SlideStarted, SlideEnded.
function MovementStateMachine.new(settings, onEvent)
	local self = setmetatable({}, MovementStateMachine)
	self.settings = settings or MovementStateMachine.defaultSettings()
	self.onEvent = onEvent or function() end
	self.gait = Gait.Idle
	self.pose = Pose.Standing
	self.sprintHeld = false
	self.jumpRequested = false
	self.inputLock = 0
	self.moving = false
	self.speed = 0
	self.speedFrom = 0
	self.speedTo = 0
	self.speedProgress = 1
	self.slideTime = 0
	self.drop = 0
	-- Extra condition for sprinting, prone and sliding (e.g. not reloading).
	self.canAct = function()
		return true
	end
	return self
end

function MovementStateMachine:_emit(name: string)
	self.onEvent(name)
end

function MovementStateMachine:isSprinting(): boolean
	return self.gait == Gait.Sprinting
end

function MovementStateMachine:isCrouching(): boolean
	return self.pose == Pose.Crouching
end

function MovementStateMachine:isProne(): boolean
	return self.pose == Pose.Prone
end

function MovementStateMachine:isSliding(): boolean
	return self.gait == Gait.Sliding
end

function MovementStateMachine:_setGait(gait: string)
	if self.gait == gait then
		return
	end
	local previous = self.gait
	self.gait = gait
	if previous == Gait.Sprinting then
		self:_emit("SprintEnded")
	elseif previous == Gait.Sliding then
		self:_emit("SlideEnded")
	end
	if gait == Gait.Sprinting then
		self:_emit("SprintStarted")
	elseif gait == Gait.Sliding then
		self:_emit("SlideStarted")
	end
end

function MovementStateMachine:_setPose(pose: string)
	if self.pose == pose then
		return
	end
	local previous = self.pose
	self.pose = pose
	if previous == Pose.Prone then
		self:_emit("ProneEnded")
	elseif previous == Pose.Crouching then
		self:_emit("Uncrouched")
	end
	if pose == Pose.Prone then
		self:_emit("ProneStarted")
	elseif pose == Pose.Crouching then
		self:_emit("Crouched")
	end
end

function MovementStateMachine:setSprint(held: boolean)
	self.sprintHeld = held
end

-- Crouch toggle. `canStand` says whether there is room to stand up.
function MovementStateMachine:toggleCrouch(canStand: boolean): boolean
	if self.inputLock > 0 or (self.gait ~= Gait.Idle and self.gait ~= Gait.Walking) then
		return false
	end
	if self.pose == Pose.Standing then
		self:_setPose(Pose.Crouching)
		return true
	end
	if not canStand then
		return false
	end
	if self.pose == Pose.Prone then
		self.inputLock = self.settings.proneTransition
	end
	self:_setPose(Pose.Standing)
	return true
end

function MovementStateMachine:toggleProne(canStand: boolean): boolean
	if self.inputLock > 0 or self.gait == Gait.Sprinting or self.gait == Gait.InAir or self.gait == Gait.Sliding then
		return false
	end
	if self.pose == Pose.Prone then
		if not canStand then
			return false
		end
		self:_setPose(Pose.Standing)
	else
		if not self.canAct() then
			return false
		end
		self:_setPose(Pose.Prone)
	end
	self.inputLock = self.settings.proneTransition
	return true
end

-- Jump: from crouch or prone it stands up instead. Returns true when the host
-- should make the character jump.
function MovementStateMachine:requestJump(canStand: boolean): boolean
	if self.inputLock > 0 or self.gait == Gait.InAir then
		return false
	end
	if self.pose ~= Pose.Standing then
		if canStand then
			if self.pose == Pose.Prone then
				self.inputLock = self.settings.proneTransition
			end
			self:_setPose(Pose.Standing)
		end
		return false
	end
	if self.gait == Gait.Sliding then
		self:_setGait(Gait.Walking)
	end
	self.jumpRequested = true
	return true
end

-- Slide: only from a standing sprint.
function MovementStateMachine:requestSlide(): boolean
	if self.gait ~= Gait.Sprinting or self.pose ~= Pose.Standing or not self.canAct() then
		return false
	end
	self.slideTime = 0
	self:_setGait(Gait.Sliding)
	self:_setPose(Pose.Crouching)
	return true
end

local function smoothstep(t: number): number
	t = math.clamp(t, 0, 1)
	return t * t * (3 - 2 * t)
end

function MovementStateMachine:_targetSpeed(): number
	local s = self.settings
	if self.gait == Gait.Idle then
		return 0
	elseif self.gait == Gait.Sprinting then
		return s.sprintSpeed
	elseif self.pose == Pose.Prone then
		return s.proneSpeed
	elseif self.pose == Pose.Crouching then
		return s.crouchSpeed
	end
	return s.walkSpeed
end

-- input = { dt, moveX, moveY (-1..1, right / forward), grounded, canStand }
-- Returns the speed the host should move at (studs/s).
function MovementStateMachine:update(input)
	local dt = input.dt
	local s = self.settings
	self.inputLock = math.max(0, self.inputLock - dt)
	local moveX, moveY = input.moveX or 0, input.moveY or 0
	if self.inputLock > 0 then
		moveX, moveY = 0, 0
	end
	local moving = math.abs(moveX) > 0.05 or math.abs(moveY) > 0.05
	if moving ~= self.moving then
		self.moving = moving
		self:_emit(if moving then "StartMoving" else "StopMoving")
	end

	-- Air.
	if not input.grounded then
		if self.gait ~= Gait.InAir then
			self:_setGait(Gait.InAir)
			self:_emit(if self.jumpRequested then "Jumped" else "Fell")
		end
		self.jumpRequested = false
	elseif self.gait == Gait.InAir then
		self:_setGait(Gait.Idle)
		self:_emit("Landed")
	end

	-- Slide runs its course.
	if self.gait == Gait.Sliding then
		self.slideTime += dt
		local t = self.slideTime / s.slide.duration
		self.speed = s.slide.startSpeed + (s.slide.endSpeed - s.slide.startSpeed) * smoothstep(t)
		if t >= 1 then
			self:_setGait(if moving then Gait.Walking else Gait.Idle)
			if not s.slide.endCrouched and input.canStand then
				self:_setPose(Pose.Standing)
			end
			self.speedFrom, self.speedTo, self.speedProgress = self.speed, self:_targetSpeed(), 0
		end
	elseif self.gait ~= Gait.InAir then
		local sprintable = self.sprintHeld
			and self.pose == Pose.Standing
			and moveY >= s.sprintForwardMin
			and math.abs(moveX) <= s.sprintSideMax
			and self.canAct()
		if sprintable then
			self:_setGait(Gait.Sprinting)
		elseif moving then
			self:_setGait(Gait.Walking)
		else
			self:_setGait(Gait.Idle)
		end
	end

	-- Ease between gait speeds.
	if self.gait ~= Gait.Sliding then
		local target = self:_targetSpeed()
		if self.gait == Gait.InAir then
			target = self.speedTo -- keep the take-off speed in the air
		end
		if target ~= self.speedTo then
			self.speedFrom, self.speedTo, self.speedProgress = self.speed, target, 0
		end
		if s.accelerationTime > 0 then
			self.speedProgress = math.min(1, self.speedProgress + dt / s.accelerationTime)
		else
			self.speedProgress = 1
		end
		self.speed = self.speedFrom + (self.speedTo - self.speedFrom) * smoothstep(self.speedProgress)
	end

	-- Camera drop for the pose.
	local dropTarget = if self.pose == Pose.Prone then s.proneDrop elseif self.pose == Pose.Crouching then s.crouchDrop else 0
	self.drop += (dropTarget - self.drop) * (1 - math.exp(-s.dropSpeed * dt))
	return self.speed
end

return MovementStateMachine
