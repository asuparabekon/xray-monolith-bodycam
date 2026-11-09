-- Viewmodel: a first-person arms-and-weapon rig that hangs from the bodycam HUD camera.
--
-- Ported from synthranger/Viewmodel (MIT, Copyright (c) 2022 Synthranger; notice in
-- roblox/THIRD-PARTY-NOTICES.md): the rig check and cleanup, keyed animation loading
-- (LoadAnimation / GetAnimation / UnloadAnimation), culling, the arm colour and shirt
-- copy, and the FakeCamera camera animation.
--
-- Changed for this project:
--   * The BodycamController places the rig (from the HUD camera, so the weapon leads
--     and the view trails). The original pivoted it to Camera.CFrame itself.
--   * The FakeCamera animation is a view modifier: the view turns by the FakeCamera's
--     rotation against its rest pose. The original added a per-frame delta to
--     Camera.CFrame, which the bodycam rebuilds from the aim every frame.
--   * The arm copy runs on attach and on character change, not every frame, and also
--     knows R15 arm parts.
-- Added for this project: aiming down sights onto a sight attachment, the scale trick
-- against wall clipping, IKControl hand grips, and weapon mounting with a Motor6D.
--
-- Rig: a Model with a HumanoidRootPart (the pivot) and an AnimationController or
-- Humanoid. Optional: FakeCamera (BasePart, animated with the arms), "Left Arm" /
-- "Right Arm" or R15 arm parts, a "Shirt" Decal in each arm, a "Settings" ModuleScript.

local Players = game:GetService("Players")
local Workspace = game:GetService("Workspace")

local Viewmodel = {}
Viewmodel.__index = Viewmodel

-- Where a culled rig goes (from the original).
local CULL_CFRAME = CFrame.new(0, 1e29, 0)
Viewmodel.CULL_CFRAME = CULL_CFRAME

-- Viewmodel arm part -> the character parts its colour comes from, in order.
local ARM_SOURCES = {
	["Left Arm"] = { "Left Arm", "LeftUpperArm" },
	["Right Arm"] = { "Right Arm", "RightUpperArm" },
	LeftUpperArm = { "LeftUpperArm", "Left Arm" },
	LeftLowerArm = { "LeftLowerArm", "Left Arm" },
	LeftHand = { "LeftHand", "Left Arm" },
	RightUpperArm = { "RightUpperArm", "Right Arm" },
	RightLowerArm = { "RightLowerArm", "Right Arm" },
	RightHand = { "RightHand", "Right Arm" },
}

-- All settings can be changed at run time.
function Viewmodel.defaultSettings()
	return {
		AnimateCamera = true, -- turn the view with FakeCamera (original setting)
		CameraWeight = 1, -- share of the FakeCamera rotation the view takes
		UseCharacterShirt = true, -- original setting
		LeftArmColor = nil, -- Color3, nil for the character's (original setting)
		RightArmColor = nil,
		HipOffset = CFrame.identity, -- camera-local pivot placement when not aiming
		SightName = "Sight", -- Attachment searched in the rig (and mounted weapon)
		EyeRelief = 0.5, -- studs from the camera to the sight when fully aimed
		Scale = 1, -- Model:ScaleTo on attach; < 1 keeps the weapon out of walls
	}
end

local function findFirstOfClass(parent: Instance, className: string)
	return parent:FindFirstChildWhichIsA(className)
end

local function animatorHolder(model: Model)
	return findFirstOfClass(model, "AnimationController") or findFirstOfClass(model, "Humanoid")
end

-- Asserts what every rig needs (the original also required FakeCamera and both arms;
-- here those are optional).
function Viewmodel.checkRig(model: Model)
	assert(model and model:IsA("Model"), "Viewmodel needs a Model")
	assert(model:FindFirstChild("HumanoidRootPart"), "Viewmodel rig needs a HumanoidRootPart")
	assert(animatorHolder(model), "Viewmodel rig needs an AnimationController or a Humanoid")
end

local function cleanPart(part: Instance)
	if part:IsA("BasePart") then
		part.CanCollide = false
		part.CanQuery = false
		part.CanTouch = false
		part.CastShadow = false
		part.Massless = true
	end
end

-- Original cleanup plus query/touch off, massless parts and an anchored root.
local function cleanRig(model: Model)
	local root = model:FindFirstChild("HumanoidRootPart")
	model.PrimaryPart = root
	local saves = model:FindFirstChild("AnimSaves")
	if saves then
		saves:Destroy()
	end
	for _, obj in model:GetDescendants() do
		cleanPart(obj)
	end
	root.Anchored = true
end

local function getAnimator(model: Model)
	local holder = animatorHolder(model)
	local animator = findFirstOfClass(holder, "Animator")
	if not animator then
		animator = Instance.new("Animator")
		animator.Parent = holder
	end
	return animator
end

-- settings: a table shaped like defaultSettings() (missing keys use the defaults), or
-- nil to read the rig's Settings ModuleScript.
function Viewmodel.new(model: Model, settings)
	Viewmodel.checkRig(model)
	if settings == nil then
		local module = model:FindFirstChild("Settings")
		if module and module:IsA("ModuleScript") then
			settings = require(module)
		end
	end
	local merged = Viewmodel.defaultSettings()
	for key, value in settings or {} do
		merged[key] = value
	end
	cleanRig(model)
	local self = setmetatable({}, Viewmodel)
	self.Settings = merged
	self.Instance = model
	self.Animator = getAnimator(model)
	self.Animations = {}
	self.Culled = false
	self.ikControls = {}
	self.weapon = nil
	self.weaponJoint = nil
	self.controller = nil
	self.removers = {}
	self.cameraRotation = CFrame.identity
	self.scale = 1
	local fake = model:FindFirstChild("FakeCamera")
	self.restCamera = if fake then model.PrimaryPart.CFrame:Inverse() * fake.CFrame else nil
	return self
end

---------------------------------------------------------------------------
-- Animations (original API).
---------------------------------------------------------------------------

function Viewmodel:LoadAnimation(key: string, animation: Animation)
	assert(
		self.Animations[key] == nil,
		string.format("Animation key %s is already taken, unload this key first before loading a new one.", key)
	)
	assert(
		typeof(animation) == "Instance" and animation:IsA("Animation"),
		string.format("Animation argument is invalid (type %s)", typeof(animation))
	)
	local track = self.Animator:LoadAnimation(animation)
	self.Animations[key] = track
	return track
end

function Viewmodel:GetAnimation(key: string)
	return self.Animations[key]
end

function Viewmodel:UnloadAnimation(key: string)
	local track = self.Animations[key]
	assert(track, string.format("%s is not a valid member of this Viewmodel's animations", key))
	self.Animations[key] = nil
	track:Destroy()
end

-- Plays a loaded animation if there is one; returns the track.
function Viewmodel:Play(key: string, fadeTime: number?, weight: number?, speed: number?)
	local track = self.Animations[key]
	if track then
		track:Play(fadeTime, weight, speed)
	end
	return track
end

---------------------------------------------------------------------------
-- Appearance.
---------------------------------------------------------------------------

-- Copies arm colours and the shirt from a character (default the local player's).
function Viewmodel:copyAppearance(character: Model?)
	if character == nil then
		local player = Players.LocalPlayer
		character = player and player.Character
	end
	if not character then
		return
	end
	local settings = self.Settings
	local shirt = character:FindFirstChildWhichIsA("Shirt")
	for name, sources in ARM_SOURCES do
		local arm = self.Instance:FindFirstChild(name)
		if arm and arm:IsA("BasePart") then
			local override = if string.find(name, "Left", 1, true) then settings.LeftArmColor else settings.RightArmColor
			local source
			for _, sourceName in sources do
				source = character:FindFirstChild(sourceName)
				if source then
					break
				end
			end
			if override then
				arm.Color = override
			elseif source and source:IsA("BasePart") then
				arm.Color = source.Color
			end
			local decal = settings.UseCharacterShirt and arm:FindFirstChild("Shirt")
			if decal then
				if shirt then
					decal.Texture = shirt.ShirtTemplate
					decal.Color3 = shirt.Color3
					decal.Transparency = 0
				else
					decal.Transparency = 1
				end
			end
		end
	end
end

---------------------------------------------------------------------------
-- Weapon and hands.
---------------------------------------------------------------------------

-- Mounts a weapon model on the rig with a Motor6D (named after the weapon) from
-- `jointPart` (default the HumanoidRootPart) to the weapon's PrimaryPart, so arm
-- animations can move the gun. Returns the joint.
function Viewmodel:mountWeapon(weapon: Model, c0: CFrame?, jointPart: BasePart?)
	self:unmountWeapon()
	assert(weapon and weapon.PrimaryPart, "mountWeapon needs a Model with a PrimaryPart")
	for _, obj in weapon:GetDescendants() do
		cleanPart(obj)
	end
	weapon.PrimaryPart.Anchored = false
	local part0 = jointPart or self.Instance.PrimaryPart
	local joint = Instance.new("Motor6D")
	joint.Name = weapon.Name
	joint.Part0 = part0
	joint.Part1 = weapon.PrimaryPart
	joint.C0 = c0 or CFrame.identity
	joint.Parent = part0
	weapon.Parent = self.Instance
	self.weapon = weapon
	self.weaponJoint = joint
	self:setGripsFrom(weapon)
	return joint
end

function Viewmodel:unmountWeapon()
	if self.weaponJoint then
		self.weaponJoint:Destroy()
		self.weaponJoint = nil
	end
	if self.weapon then
		self.weapon.Parent = nil
		self.weapon = nil
	end
	self:setHandTarget("Left", nil)
	self:setHandTarget("Right", nil)
end

local function armChain(model: Model, side: string)
	local upper = model:FindFirstChild(side .. "UpperArm")
	local hand = model:FindFirstChild(side .. "Hand")
	if upper and hand then
		return upper, hand
	end
	-- R6-style single-part arm: the shoulder turns the arm to put the grip on target.
	local arm = model:FindFirstChild(side .. " Arm")
	if arm then
		return arm, arm:FindFirstChild(side .. "GripAttachment") or arm
	end
	return nil, nil
end

-- Pins a hand (side "Left" | "Right") to `target` (Attachment or BasePart) with an
-- IKControl, or releases it (target nil). Returns the IKControl.
function Viewmodel:setHandTarget(side: string, target: Instance?, weight: number?)
	local ik = self.ikControls[side]
	if target == nil then
		if ik then
			ik.Enabled = false
			ik.Target = nil
		end
		return ik
	end
	local root, effector = armChain(self.Instance, side)
	if not (root and effector) then
		return nil
	end
	if not ik then
		ik = Instance.new("IKControl")
		ik.Name = side .. "HandGrip"
		ik.Type = Enum.IKControlType.Transform
		ik.Parent = animatorHolder(self.Instance)
		self.ikControls[side] = ik
	end
	ik.ChainRoot = root
	ik.EndEffector = effector
	ik.Target = target
	ik.Weight = weight or 1
	ik.Enabled = true
	return ik
end

-- Pins the hands to "LeftGrip" / "RightGrip" attachments under `instance`, if any.
function Viewmodel:setGripsFrom(instance: Instance)
	for _, side in { "Left", "Right" } do
		local grip = instance:FindFirstChild(side .. "Grip", true)
		self:setHandTarget(side, grip)
	end
end

---------------------------------------------------------------------------
-- Placement.
---------------------------------------------------------------------------

function Viewmodel:_findSight()
	local name = self.Settings.SightName
	if self.weapon then
		local sight = self.weapon:FindFirstChild(name, true)
		if sight then
			return sight
		end
	end
	return self.Instance:FindFirstChild(name, true)
end

-- The pivot seen from the sight, in unscaled studs, from the rig's current pose.
function Viewmodel:sightToPivot(): CFrame?
	local sight = self:_findSight()
	if not (sight and sight:IsA("Attachment")) then
		return nil
	end
	local relative = sight.WorldCFrame:Inverse() * self.Instance:GetPivot()
	local scale = self.scale
	if scale ~= 1 then
		relative = CFrame.new(relative.Position / scale) * relative.Rotation
	end
	return relative
end

-- Camera-local placement (unscaled) for an aim-down-sights blend 0..1: the hip offset
-- at 0; at 1 the sight sits on the screen centre, EyeRelief in front of the camera.
function Viewmodel:placement(ads: number): CFrame
	local settings = self.Settings
	local hip = settings.HipOffset
	if ads <= 0 then
		return hip
	end
	local fromSight = self:sightToPivot()
	if not fromSight then
		return hip
	end
	local aimed = CFrame.new(0, 0, -settings.EyeRelief) * fromSight
	return hip:Lerp(aimed, math.min(ads, 1))
end

-- The FakeCamera's turn against its rest pose (identity without a FakeCamera).
function Viewmodel:cameraTurn(): CFrame
	local fake = self.Instance:FindFirstChild("FakeCamera")
	local root = self.Instance.PrimaryPart
	if not (self.restCamera and fake and root) then
		return CFrame.identity
	end
	local now = root.CFrame:Inverse() * fake.CFrame
	local turn = (self.restCamera:Inverse() * now).Rotation
	local weight = self.Settings.CameraWeight
	if weight ~= 1 then
		turn = CFrame.identity:Lerp(turn, math.clamp(weight, 0, 1))
	end
	return turn
end

-- Hands the rig to a BodycamController: parents it under the camera, scales it,
-- registers the placement and the camera animation. Returns self.
function Viewmodel:attach(controller, parent: Instance?)
	self:detach()
	self.controller = controller
	local model = self.Instance
	model.Parent = parent or Workspace.CurrentCamera
	local scale = self.Settings.Scale or 1
	if scale ~= self.scale and model.ScaleTo then
		model:ScaleTo(scale)
	end
	self.scale = scale
	self:copyAppearance()
	local player = Players.LocalPlayer
	if player and player.CharacterAdded then
		local connection = player.CharacterAdded:Connect(function(character)
			self:copyAppearance(character)
		end)
		table.insert(self.removers, function()
			connection:Disconnect()
		end)
	end
	table.insert(self.removers, controller:addViewModifier(function(view)
		if self.Culled or not self.Settings.AnimateCamera then
			self.cameraRotation = CFrame.identity
			return view
		end
		self.cameraRotation = self:cameraTurn()
		return view * self.cameraRotation
	end))
	if not self.Culled then
		self:_hand()
	end
	return self
end

function Viewmodel:_placementFn()
	if not self._placementCallback then
		self._placementCallback = function(info)
			return self:placement(info and info.ads or 0)
		end
	end
	return self._placementCallback
end

function Viewmodel:_hand()
	local controller = self.controller
	if controller then
		controller:setViewmodel(self.Instance, self:_placementFn(), self.scale)
	end
end

function Viewmodel:_release()
	local controller = self.controller
	if controller and controller.options.viewmodel == self.Instance then
		controller:setViewmodel(nil)
	end
	self.Instance:PivotTo(CULL_CFRAME)
end

function Viewmodel:detach()
	for _, remove in self.removers do
		remove()
	end
	self.removers = {}
	if self.controller then
		self:_release()
		self.controller = nil
	end
end

-- Hides (true) or shows the rig. A culled rig is moved far away and not placed.
function Viewmodel:Cull(state: boolean)
	if self.Culled == state then
		return
	end
	self.Culled = state
	if state then
		self:_release()
	else
		self:_hand()
	end
end

function Viewmodel:Destroy()
	self:detach()
	for key in table.clone(self.Animations) do
		self:UnloadAnimation(key)
	end
	self.Instance:Destroy()
end

return Viewmodel
