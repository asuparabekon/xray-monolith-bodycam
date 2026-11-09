-- Smoothing that does not depend on the frame rate. Pure Luau.
--
-- approach, follow and shortestAngle are ported from InuFirstPerson's Math module
-- (MIT, Copyright (c) 2026 inu (@inutataaaaa); notice in roblox/THIRD-PARTY-NOTICES.md).
-- alpha is added for this project.

local Smoothing = {}

local TWO_PI = 2 * math.pi

-- Moves `current` toward a target that holds still during the step: the gap shrinks to
-- 1/e every `timeConstant` seconds. 0 or less snaps to the target.
function Smoothing.approach(current: number, target: number, timeConstant: number, dt: number): number
	if timeConstant <= 0 then
		return target
	end
	return target + (current - target) * math.exp(-dt / timeConstant)
end

-- A first-order lag behind a target that moved in a straight line from `targetBefore`
-- to `targetAfter` during the step. Exact for that path, so a steady motion leaves the
-- same lag (speed * timeConstant) at any frame rate.
function Smoothing.follow(
	current: number,
	targetBefore: number,
	targetAfter: number,
	timeConstant: number,
	dt: number
): number
	if timeConstant <= 0 or dt <= 0 then
		return targetAfter
	end
	local lead = (targetAfter - targetBefore) / dt * timeConstant
	return targetAfter - lead + (current - targetBefore + lead) * math.exp(-dt / timeConstant)
end

-- The angle wrapped into [-pi, pi), the shortest turn with the same effect.
function Smoothing.shortestAngle(angle: number): number
	return (angle + math.pi) % TWO_PI - math.pi
end

-- The share of the gap `approach` closes in one step, for CFrame:Lerp.
function Smoothing.alpha(timeConstant: number, dt: number): number
	if timeConstant <= 0 then
		return 1
	end
	return 1 - math.exp(-dt / timeConstant)
end

return Smoothing
