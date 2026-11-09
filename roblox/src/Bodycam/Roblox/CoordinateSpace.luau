-- Conversions between the X-Ray conventions the core keeps and Roblox.
--
-- X-Ray: left-handed, +X right, +Y up, +Z forward, metres. A view direction from
-- heading h and pitch p is (-cos p sin h, sin p, cos p cos h).
-- Roblox: right-handed, +X right, +Y up, -Z forward (LookVector), studs.
-- Mirroring Z maps one onto the other: heading h equals the Roblox yaw of
-- CFrame.fromOrientation(p, h, 0), and pitch p is the same in both.

local CoordinateSpace = {}

-- Roblox's documented avatar scale is 1 stud = 0.28 m. Games with other proportions
-- should pass their own studsPerMetre: every bodycam distance is authored in metres.
CoordinateSpace.DEFAULT_STUDS_PER_METRE = 1 / 0.28

-- Aim heading and pitch (radians, X-Ray convention) of a Roblox look vector.
function CoordinateSpace.aimFromLook(look: Vector3): (number, number)
	local horizontal = math.sqrt(look.X * look.X + look.Z * look.Z)
	local yaw = if horizontal > 1e-9 then math.atan2(-look.X, -look.Z) else 0
	local pitch = math.atan2(look.Y, horizontal)
	return yaw, pitch
end

-- The Roblox rotation of an aim (no roll).
function CoordinateSpace.aimRotation(yaw: number, pitch: number): CFrame
	return CFrame.fromOrientation(pitch, yaw, 0)
end

-- A camera-local X-Ray offset (right, up, forward) in metres as a Roblox local offset in studs.
function CoordinateSpace.localOffset(x: number, y: number, z: number, studsPerMetre: number): Vector3
	return Vector3.new(x, y, -z) * studsPerMetre
end

-- An X-Ray world vector (m/s or m) from a Roblox world vector (studs/s or studs).
function CoordinateSpace.toEngineVector(v: Vector3, studsPerMetre: number)
	return { x = v.X / studsPerMetre, y = v.Y / studsPerMetre, z = -v.Z / studsPerMetre }
end

-- A CameraDelta matrix ({ i, j, k } X-Ray camera-local axes) as a Roblox rotation.
function CoordinateSpace.deltaToCFrame(m): CFrame
	local i, j, k = m.i, m.j, m.k
	local right = Vector3.new(i[1], i[2], -i[3])
	local up = Vector3.new(j[1], j[2], -j[3])
	local back = Vector3.new(-k[1], -k[2], k[3]) -- Roblox ZVector is minus the look direction.
	return CFrame.fromMatrix(Vector3.zero, right, up, back)
end

-- Fmatrix::setHPB(h, p, b) (radians) as a Roblox rotation: the HUD offset rotation.
function CoordinateSpace.hpb(h: number, p: number, b: number): CFrame
	local sh, ch = math.sin(h), math.cos(h)
	local sp, cp = math.sin(p), math.cos(p)
	local sb, cb = math.sin(b), math.cos(b)
	local cc, cs, sc, ss = ch * cb, ch * sb, sh * cb, sh * sb
	return CoordinateSpace.deltaToCFrame({
		i = { cc - sp * ss, -cp * sb, sp * cs + sc },
		j = { sp * sc + cs, cp * cb, ss - sp * cc },
		k = { -cp * sh, sp, cp * ch },
	})
end

-- Roblox world directions of the aim's yaw-only frame: right and forward.
function CoordinateSpace.yawFrame(yaw: number): (Vector3, Vector3)
	return Vector3.new(math.cos(yaw), 0, -math.sin(yaw)), Vector3.new(-math.sin(yaw), 0, -math.cos(yaw))
end

-- The composed view of CameraRig:compose() over the aim camera (the camera scripts' CFrame).
-- Mirrors ClassicCameraRig::ComposeView: the lag delta and the head rotation are applied
-- camera-locally, the lag shift along the final view axes, then the footstep offset in the
-- yaw-only frame.
function CoordinateSpace.composeView(aim: CFrame, aimYaw: number, aimPitch: number, composition, studsPerMetre: number): CFrame
	local lag = composition.lag
	local delta = CoordinateSpace.aimRotation(aimYaw, aimPitch):Inverse() * CoordinateSpace.aimRotation(lag.yaw, lag.pitch)
	local view = aim * delta * CoordinateSpace.deltaToCFrame(composition.head_rotation)
	local shift = view:VectorToWorldSpace(CoordinateSpace.localOffset(lag.pos.x, lag.pos.y, lag.pos.z, studsPerMetre))
	local f = composition.footstep_offset
	local right, forward = CoordinateSpace.yawFrame(aimYaw)
	local footstep = (right * f.x + Vector3.yAxis * f.y + forward * f.z) * studsPerMetre
	return view.Rotation + (aim.Position + shift + footstep)
end

return CoordinateSpace
