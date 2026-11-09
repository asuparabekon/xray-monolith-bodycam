-- Port of src/xrGame/bodycam_reference/speed_curve.h, plus a stride clock for hosts
-- without a footstep schedule (Roblox has no step-sound phase like the X-Ray actor).

local GaitSpeed = {}

-- Reference's gait speeds (m/s) the footstep camera data was recorded at.
GaitSpeed.REFERENCE_RUN = 1.25
GaitSpeed.REFERENCE_SPRINT = 3.25

-- Maps host movement speed onto Reference's gait speeds, anchored on the host's
-- walk ("run" in X-Ray) and sprint speeds: host walk -> Reference walk, host sprint
-- -> Reference sprint, linear between, proportional below and above.
-- Host speeds may be in any unit (studs/s); the result is in Reference m/s.
function GaitSpeed.referenceEquivalentSpeed(speed: number, hostRun: number, hostSprint: number?): number
	if speed ~= speed then
		return 0
	end
	speed = math.max(speed, 0)
	if not (hostRun > 0.05) then
		return speed
	end
	local run, sprint = GaitSpeed.REFERENCE_RUN, GaitSpeed.REFERENCE_SPRINT
	local engineSprint = hostSprint or hostRun
	if speed <= hostRun or not (engineSprint > hostRun + 0.05) then
		return run * speed / hostRun
	end
	if speed <= engineSprint then
		return run + (speed - hostRun) / (engineSprint - hostRun) * (sprint - run)
	end
	return sprint * speed / engineSprint
end

-- A stride clock: advances a foot phase (left contact 0, right contact 0.5) from the
-- Reference-equivalent speed. One phase unit is a full stride (two steps).
local StrideClock = {}
StrideClock.__index = StrideClock
GaitSpeed.StrideClock = StrideClock

-- strideWalk / strideSprint: metres per full stride at Reference walk and sprint speed.
function StrideClock.new(strideWalk: number?, strideSprint: number?)
	return setmetatable({ phase = 0, strideWalk = strideWalk or 1.32, strideSprint = strideSprint or 2.03 }, StrideClock)
end

-- Returns the phase while striding, nil when standing or airborne.
function StrideClock:update(dt: number, referenceSpeed: number, grounded: boolean): number?
	if not grounded or referenceSpeed < 0.1 then
		return nil
	end
	local t = math.clamp(
		(referenceSpeed - GaitSpeed.REFERENCE_RUN) / (GaitSpeed.REFERENCE_SPRINT - GaitSpeed.REFERENCE_RUN),
		0,
		1
	)
	local stride = self.strideWalk + (self.strideSprint - self.strideWalk) * t
	self.phase = (self.phase + dt * referenceSpeed / stride) % 1
	return self.phase
end

return GaitSpeed
