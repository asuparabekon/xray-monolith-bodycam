-- Presentation.Headcam: bodycam / helmet camera modes on the mock controller (mount,
-- head turn, kept bodycam layers, wall safety, body visibility, FOV) and spectating.
return function(T)
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local Fake = T.require(T.Tests.mocks.FakeInstances)

	local function close(a, b, tolerance, message)
		T.truthy((a - b).Magnitude < (tolerance or 1e-6), (message or "vectors") .. string.format(
			": (%.4f, %.4f, %.4f) vs (%.4f, %.4f, %.4f)", a.X, a.Y, a.Z, b.X, b.Y, b.Z
		))
	end

	-- A fresh mock world with a head, a torso and a hat on the local character.
	local function world(settings)
		local env = Env.new()
		env.install()
		local require = T.isolatedRequire()
		local Bodycam = require(T.Src.Bodycam)
		local Headcam = require(T.Src.Presentation).headcam()
		local head = Fake.new("Part", "Head", { CFrame = CFrame.new(0, 4.5, 0), LocalTransparencyModifier = 1 })
		local torso = Fake.new("Part", "UpperTorso", { CFrame = CFrame.new(0, 3, 0), LocalTransparencyModifier = 1 })
		local hat = Fake.new("Accessory", "Cap")
		local handle = Fake.add(hat, "Part", "Handle", { LocalTransparencyModifier = 1 })
		Fake.add(handle, "Attachment", "HatAttachment")
		env.characterParts = { head, torso, hat }
		env.camera.CFrame = CFrame.new(0, 4.5, 0)
		local controller = Bodycam.controller().new({})
		local modes = {}
		local headcam = Headcam.new({
			controller = controller,
			settings = settings,
			onModeChanged = function(mode)
				table.insert(modes, mode)
			end,
		})
		controller:start()
		headcam:start()
		return {
			env = env,
			controller = controller,
			headcam = headcam,
			Headcam = Headcam,
			head = head,
			torso = torso,
			handle = handle,
			modes = modes,
		}
	end

	local function run(w, seconds)
		for _ = 1, math.floor(seconds * 60 + 0.5) do
			w.env.step(1 / 60)
		end
	end

	T.test("FOV helpers convert between vertical and horizontal angles", function()
		local w = world()
		w.controller:stop()
		local Headcam = w.Headcam
		local vertical = Headcam.verticalFov(130, 16 / 9)
		T.near(vertical, 100.7, 0.1, "130 horizontal at 16:9")
		T.near(Headcam.horizontalFov(vertical, 16 / 9), 130, 1e-9, "round trip")
	end)

	T.test("bodycam mode leaves the view alone", function()
		local w = world()
		run(w, 0.5)
		T.equal(w.headcam:getBlend(), 0, "no blend")
		close(w.env.camera.CFrame.Position, Vector3.new(0, 4.5, 0), 0.2, "camera at the eye")
		T.equal(w.head.LocalTransparencyModifier, 1, "first-person transparency untouched")
		w.controller:stop()
	end)

	T.test("headcam mode mounts the view on the head and keeps the aim", function()
		local w = world({ keepLayers = 0 })
		run(w, 0.2)
		local aimBefore = w.controller:getAimCFrame()
		T.equal(w.headcam:toggle(), "Headcam", "toggled")
		T.equal(w.modes[1], "Headcam", "mode change reported")
		run(w, 1.5)
		T.equal(w.headcam:getBlend(), 1, "fully blended")
		local mount = (w.head.CFrame * w.headcam.settings.mount).Position
		close(w.env.camera.CFrame.Position, mount, 1e-3, "camera at the head mount")
		close(w.controller:getAimCFrame().Position, aimBefore.Position, 1e-3, "the aim stays at the eye")
		close(w.controller:getAimCFrame().LookVector, aimBefore.LookVector, 1e-3, "and keeps its direction")
		w.controller:stop()
	end)

	T.test("the view turns with the head by headFollow, smoothed", function()
		local w = world({ keepLayers = 0, headFollow = 0.5, headSmoothing = 0.05 })
		w.headcam:setMode("Headcam")
		run(w, 1)
		w.head.CFrame = CFrame.new(0, 4.5, 0) * CFrame.Angles(0, math.rad(30), 0)
		w.env.step(1 / 60)
		local look = w.env.camera.CFrame.LookVector
		local first = math.deg(math.atan2(-look.X, -look.Z))
		T.truthy(first > 0 and first < 14, "a sudden head turn is smoothed: " .. first)
		run(w, 1)
		look = w.env.camera.CFrame.LookVector
		T.near(math.deg(math.atan2(-look.X, -look.Z)), 15, 0.2, "half the head's 30 degrees")
		w.controller:stop()
	end)

	T.test("a wall pulls the mount back toward the head", function()
		local w = world({ keepLayers = 0, wallRadius = 0.25, wallMargin = 0.1 })
		table.insert(w.env.walls, function(origin, direction)
			return { Position = origin + direction.Unit * 0.55, Distance = 0.3 }
		end)
		w.headcam:setMode("Headcam")
		run(w, 1.5)
		local distance = (w.env.camera.CFrame.Position - w.head.Position).Magnitude
		T.near(distance, 0.2, 1e-3, "sphere travel minus the margin")
		w.controller:stop()
	end)

	T.test("the own body shows, the head hides, and both restore", function()
		local w = world()
		w.headcam:setMode("Headcam")
		run(w, 1)
		T.equal(w.torso.LocalTransparencyModifier, 0, "body visible")
		T.equal(w.head.LocalTransparencyModifier, 1, "head hidden")
		T.equal(w.handle.LocalTransparencyModifier, 1, "hat hidden")
		w.headcam:setMode("Bodycam")
		run(w, 1)
		T.equal(w.headcam:getBlend(), 0, "back to bodycam")
		T.equal(w.torso.LocalTransparencyModifier, 1, "first-person value restored")
		w.controller:stop()
	end)

	T.test("the wide lens keeps the scope's zoom ratio", function()
		local w = world({ fov = 100 })
		T.equal(w.headcam:fovFor(70, 70), 70, "bodycam: the weapon's FOV")
		w.headcam:setMode("Headcam")
		run(w, 1.5)
		T.near(w.headcam:fovFor(70, 70), 100, 1e-9, "hip: the headcam lens")
		T.near(w.headcam:fovFor(35, 70), 50, 1e-9, "a 2x scope halves it")
		w.headcam.settings.fov = 200
		T.equal(w.headcam:fovFor(70, 70), 120, "clamped to Roblox's limit")
		w.controller:stop()
	end)

	T.test("spectating puts the camera on another player's head and gives it back", function()
		local w = world()
		w.controller:stop()
		local other = Fake.new("Model", "Other")
		local head = Fake.add(other, "Part", "Head", { CFrame = CFrame.new(10, 4.5, 0) * CFrame.Angles(0, 1, 0) })
		local camera = w.env.camera
		local typeBefore = camera.CameraType
		w.headcam:spectate(other)
		T.equal(w.headcam:getSpectating(), other, "spectating")
		w.env.step(1 / 60)
		close(camera.CFrame.Position, (head.CFrame * w.headcam.settings.mount).Position, 1e-6, "on the mount")
		close(camera.CFrame.LookVector, head.CFrame.LookVector, 1e-6, "looking where the head looks")
		T.equal(camera.CameraType, Enum.CameraType.Scriptable, "scripted camera")
		T.equal(head.LocalTransparencyModifier, 1, "their head hidden for us")
		w.headcam:spectate(nil)
		T.equal(camera.CameraType, typeBefore, "camera type restored")
		T.equal(head.LocalTransparencyModifier, 0, "their head shown again")
		local bound = 0
		for _, b in w.env.bound do
			if string.find(b.name, "HeadcamSpectate", 1, true) then
				bound += 1
			end
		end
		T.equal(bound, 0, "render step unbound")
	end)
end
