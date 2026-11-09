-- Every module loads and a default frame runs end to end.
return function(T)
	local Bodycam = T.require(T.Bodycam)

	T.test("modules load", function()
		T.truthy(Bodycam.BodycamCore)
		T.truthy(Bodycam.Reference.FootstepCamera)
	end)

	T.test("default frames run", function()
		local core = Bodycam.BodycamCore.new()
		core:reset(0.3, -0.1, 0)
		local frame = { camera = Bodycam.CameraRig.newInput(), move_flags = Bodycam.MoveFlags.Forward, accelerated = true }
		for i = 1, 120 do
			local cam = frame.camera
			cam.frame = i
			cam.dt = 1 / 60
			cam.aim_yaw = 0.3 + 0.01 * i
			cam.aim_pitch = -0.1
			cam.reference_speed = 1.25
			cam.velocity = { x = 0, y = 0, z = 1.25 }
			cam.foot_phase = (i / 60) % 1
			core:updateMovement(cam.dt, frame.move_flags, false, { x = 0, y = 0, z = 1.25 })
			local c = core:update(frame, nil)
			T.truthy(c.composes, "rig composes")
			T.truthy(c.lag.yaw == c.lag.yaw, "finite yaw")
		end
	end)
end
