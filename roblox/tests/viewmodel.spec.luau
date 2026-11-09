-- Presentation.Viewmodel: the synthranger/Viewmodel port (rig check, cleanup, keyed
-- animations, culling, arm copy, FakeCamera) and the additions (aim down sights, the
-- scale trick, IK grips, weapon mounting), on fake instances and the mock controller.
return function(T)
	local Env = T.require(T.Tests.mocks.RobloxEnv)
	local Fake = T.require(T.Tests.mocks.FakeInstances)
	local env = Env.new()
	env.install()
	local require = T.isolatedRequire()
	local Bodycam = require(T.Src.Bodycam)
	local Viewmodel = require(T.Src.Presentation).viewmodel()

	local function close(a, b, tolerance, message)
		T.truthy((a - b).Magnitude < (tolerance or 1e-6), (message or "vectors") .. string.format(
			": (%.4f, %.4f, %.4f) vs (%.4f, %.4f, %.4f)", a.X, a.Y, a.Z, b.X, b.Y, b.Z
		))
	end

	-- An R6-style arms rig 1 stud in front of the root, with a sight above the right arm.
	local function makeRig(options)
		options = options or {}
		local model = Fake.new("Model", "Rifle")
		local root = Fake.add(model, "Part", "HumanoidRootPart")
		local controller = Fake.add(model, "AnimationController")
		Fake.add(controller, "Animator")
		Fake.add(model, "Folder", "AnimSaves")
		local left = Fake.add(model, "Part", "Left Arm", { CFrame = CFrame.new(-0.5, -0.5, -1) })
		local right = Fake.add(model, "Part", "Right Arm", { CFrame = CFrame.new(0.5, -0.5, -1) })
		Fake.add(left, "Attachment", "LeftGripAttachment")
		Fake.add(left, "Decal", "Shirt")
		Fake.add(right, "Decal", "Shirt")
		Fake.add(right, "Attachment", "Sight", { CFrame = CFrame.new(-0.5, 0.75, -0.2) })
		if options.fakeCamera ~= false then
			Fake.add(model, "Part", "FakeCamera", { CFrame = CFrame.new(0, 0.2, 0) })
		end
		return model, root, left, right
	end

	T.test("the rig is checked and cleaned as in the original", function()
		local bad = Fake.new("Model")
		Fake.add(bad, "AnimationController")
		T.truthy(not pcall(Viewmodel.new, bad, {}), "a rig without a HumanoidRootPart is refused")

		local model, root, left = makeRig()
		local vm = Viewmodel.new(model, {})
		T.equal(model.PrimaryPart, root, "PrimaryPart is the root")
		T.equal(model:FindFirstChild("AnimSaves"), nil, "AnimSaves removed")
		T.equal(left.CanCollide, false, "no collision")
		T.equal(left.CastShadow, false, "no shadow from the arms")
		T.equal(root.Anchored, true, "the root is anchored")
		T.truthy(vm.Animator and vm.Animator.ClassName == "Animator", "uses the rig's Animator")
		T.equal(vm.Settings.EyeRelief, Viewmodel.defaultSettings().EyeRelief, "missing settings use defaults")
	end)

	T.test("animations load by key, refuse a taken key, play and unload", function()
		local vm = Viewmodel.new((makeRig()), {})
		local animation = Fake.new("Animation", "Reload")
		local track = vm:LoadAnimation("Reload", animation)
		T.equal(vm:GetAnimation("Reload"), track, "GetAnimation")
		T.truthy(not pcall(vm.LoadAnimation, vm, "Reload", animation), "a taken key is refused")
		T.truthy(not pcall(vm.LoadAnimation, vm, "Bad", {}), "a non-Animation is refused")
		vm:Play("Reload", 0.1)
		T.equal(track.plays, 1, "Play plays the track")
		T.equal(vm:Play("Missing"), nil, "Play on an unloaded key does nothing")
		vm:UnloadAnimation("Reload")
		T.equal(vm:GetAnimation("Reload"), nil, "unloaded")
		T.truthy(track.destroyed, "the track is destroyed")
	end)

	T.test("arm colours and the shirt come from the character", function()
		local model, _, left, right = makeRig()
		local vm = Viewmodel.new(model, { RightArmColor = "override" })
		local character = Fake.new("Model", "Character")
		Fake.add(character, "Part", "LeftUpperArm", { Color = "skin" })
		Fake.add(character, "Shirt", "Shirt", { ShirtTemplate = "rbxassetid://1", Color3 = "white" })
		vm:copyAppearance(character)
		T.equal(left.Color, "skin", "R15 character colour onto an R6 arm")
		T.equal(right.Color, "override", "the setting overrides")
		T.equal(left:FindFirstChild("Shirt").Texture, "rbxassetid://1", "shirt template copied")

		local bare = Fake.new("Model", "Bare")
		vm:copyAppearance(bare)
		T.equal(left:FindFirstChild("Shirt").Transparency, 1, "no shirt hides the sleeve decal")
	end)

	T.test("aiming puts the sight on the screen centre, EyeRelief ahead", function()
		local model = makeRig()
		local vm = Viewmodel.new(model, { HipOffset = CFrame.new(0.3, -0.4, -0.2), EyeRelief = 0.6 })
		model:PivotTo(vm:placement(0))
		close(model:GetPivot().Position, Vector3.new(0.3, -0.4, -0.2), 1e-6, "hip placement")

		model:PivotTo(vm:placement(1))
		local sight = model:FindFirstChild("Sight", true).WorldCFrame
		close(sight.Position, Vector3.new(0, 0, -0.6), 1e-6, "sight on the centre line")
		close(sight.LookVector, Vector3.new(0, 0, -1), 1e-6, "sight looks along the camera")

		model:PivotTo(vm:placement(0.5))
		local half = model:FindFirstChild("Sight", true).WorldCFrame.Position
		T.truthy(math.abs(half.X) > 1e-3 and math.abs(half.X) < 0.5, "half way blends")
	end)

	T.test("the scale trick keeps the sight's screen position", function()
		local model = makeRig()
		local vm = Viewmodel.new(model, { EyeRelief = 0.6, Scale = 0.5 })
		local unscaled = vm:sightToPivot()
		model:ScaleTo(0.5)
		vm.scale = 0.5
		close(vm:sightToPivot().Position, unscaled.Position, 1e-9, "sight offset in unscaled studs")

		-- The controller scales the final camera-local position by the model scale.
		local placement = vm:placement(1)
		model:PivotTo(CFrame.new(placement.Position * 0.5) * placement.Rotation)
		local sight = model:FindFirstChild("Sight", true).WorldCFrame.Position
		close(sight, Vector3.new(0, 0, -0.3), 1e-6, "half as far, same direction")
	end)

	T.test("the controller places the rig from the HUD camera and scales it", function()
		local model = makeRig({ fakeCamera = false })
		local vm = Viewmodel.new(model, { HipOffset = CFrame.new(0.4, -0.5, -0.8) })
		local controller = Bodycam.controller().new({})
		controller:start()
		vm:attach(controller)
		T.equal(model.Parent, env.camera, "parented under the camera")
		T.equal(controller.options.viewmodel, model, "handed to the controller")
		for _ = 1, 30 do
			env.step(1 / 60)
		end
		local hud = controller:getHudCFrame()
		local placedLocal = hud:Inverse() * model:GetPivot()
		close(placedLocal.Position, Vector3.new(0.4, -0.5, -0.8), 0.05, "hip placement at rest")

		controller:setViewmodel(model, nil, 0.5)
		env.step(1 / 60)
		local scaledLocal = controller:getHudCFrame():Inverse() * model:GetPivot()
		close(scaledLocal.Position, placedLocal.Position * 0.5, 0.01, "position scaled by the model scale")
		controller:setViewmodel(model, nil, 1)

		vm:Cull(true)
		T.equal(controller.options.viewmodel, nil, "a culled rig is not placed")
		close(model:GetPivot().Position, Viewmodel.CULL_CFRAME.Position, 1, "culled rig moved away")
		vm:Cull(false)
		T.equal(controller.options.viewmodel, model, "shown again")
		vm:detach()
		T.equal(controller.options.viewmodel, nil, "detached")
		controller:stop()
	end)

	T.test("the FakeCamera turns the view against its rest pose", function()
		local model = makeRig()
		local vm = Viewmodel.new(model, {})
		T.near(vm:cameraTurn().LookVector.Z, -1, 1e-9, "no turn at rest")
		local fake = model:FindFirstChild("FakeCamera")
		local angle = math.rad(10)
		fake.CFrame = fake.CFrame * CFrame.Angles(angle, 0, 0)
		local look = vm:cameraTurn().LookVector
		T.near(look.Y, math.sin(angle), 1e-6, "pitch up by the FakeCamera's turn")
		vm.Settings.CameraWeight = 0.5
		T.near(vm:cameraTurn().LookVector.Y, math.sin(angle / 2), 1e-6, "weight halves it")
		-- Moving the whole rig does not turn the view.
		model:PivotTo(CFrame.new(5, 2, 1) * CFrame.Angles(0, 1, 0))
		T.near(vm:cameraTurn().LookVector.Y, math.sin(angle / 2), 1e-6, "relative to the rig")
	end)

	T.test("a mounted weapon gets a Motor6D, its sight and IK grips", function()
		local model = makeRig()
		local vm = Viewmodel.new(model, {})
		local gun = Fake.new("Model", "M4")
		local handle = Fake.add(gun, "Part", "Handle")
		gun.PrimaryPart = handle
		Fake.add(handle, "Attachment", "LeftGrip")
		Fake.add(handle, "Attachment", "Sight", { CFrame = CFrame.new(0, 0.3, 0) })
		local joint = vm:mountWeapon(gun, CFrame.new(0.2, -0.3, -1))
		T.equal(joint.ClassName, "Motor6D", "a Motor6D")
		T.equal(joint.Part0, model.PrimaryPart, "from the root")
		T.equal(joint.Part1, handle, "to the weapon's PrimaryPart")
		T.equal(gun.Parent, model, "the weapon is in the rig")
		T.equal(handle.CastShadow, false, "weapon parts cleaned")
		T.equal(vm:_findSight().Parent, handle, "the weapon's sight wins")

		local ik = vm.ikControls.Left
		T.truthy(ik and ik.Enabled, "left hand pinned to LeftGrip")
		T.equal(ik.ChainRoot.Name, "Left Arm", "R6 chain root")
		T.equal(ik.EndEffector.Name, "LeftGripAttachment", "R6 end effector")
		T.equal(vm.ikControls.Right, nil, "no RightGrip, no right IK")

		vm:unmountWeapon()
		T.equal(gun.Parent, nil, "unmounted")
		T.equal(ik.Enabled, false, "grip released")
	end)
end
