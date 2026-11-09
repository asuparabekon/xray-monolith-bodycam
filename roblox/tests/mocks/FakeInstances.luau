-- Fake Roblox instances for rig tests: Model, BasePart, Attachment, Animator, Decal and
-- plain containers with a working Parent, FindFirstChild (recursive), GetDescendants,
-- Model PivotTo / GetPivot / ScaleTo and Attachment WorldCFrame. Only what the
-- Presentation rig code uses.

local RobloxMath = require(script.Parent.RobloxMath)
local CFrame = RobloxMath.CFrame

local Fake = {}

local PARTS = { Part = true, MeshPart = true, UnionOperation = true }

local function isA(className, wanted)
	if className == wanted or wanted == "Instance" then
		return true
	end
	if wanted == "BasePart" then
		return PARTS[className] == true
	end
	return false
end

local methods = {}

function methods:IsA(className)
	return isA(self.ClassName, className)
end

function methods:GetChildren()
	return table.clone(self.__children)
end

function methods:GetDescendants()
	local list = {}
	local function walk(node)
		for _, child in node.__children do
			table.insert(list, child)
			walk(child)
		end
	end
	walk(self)
	return list
end

function methods:FindFirstChild(name, recursive)
	for _, child in self.__children do
		if child.Name == name then
			return child
		end
	end
	if recursive then
		for _, child in self.__children do
			local found = child:FindFirstChild(name, true)
			if found then
				return found
			end
		end
	end
	return nil
end

function methods:FindFirstChildWhichIsA(className)
	for _, child in self.__children do
		if child:IsA(className) then
			return child
		end
	end
	return nil
end
methods.FindFirstChildOfClass = methods.FindFirstChildWhichIsA

function methods:Destroy()
	self.Parent = nil
	self.destroyed = true
end

function methods:GetPivot()
	if self.ClassName == "Model" then
		local primary = self.PrimaryPart
		return if primary then primary.CFrame else (self.WorldPivot or CFrame.identity)
	end
	return self.CFrame
end

function methods:PivotTo(target)
	self.pivots = (self.pivots or 0) + 1
	local delta = target * self:GetPivot():Inverse()
	if self.ClassName ~= "Model" then
		self.CFrame = target
		return
	end
	for _, obj in self:GetDescendants() do
		if obj:IsA("BasePart") then
			obj.CFrame = delta * obj.CFrame
		end
	end
	if not self.PrimaryPart then
		self.WorldPivot = target
	end
end

-- Scales part positions about the pivot and attachment offsets (absolute scale).
function methods:ScaleTo(scale)
	local factor = scale / (self.scale or 1)
	self.scale = scale
	local pivot = self:GetPivot()
	for _, obj in self:GetDescendants() do
		if obj:IsA("BasePart") then
			local relative = pivot:Inverse() * obj.CFrame
			obj.CFrame = pivot * CFrame.new(relative.Position * factor) * relative.Rotation
		elseif obj.ClassName == "Attachment" then
			obj.CFrame = CFrame.new(obj.CFrame.Position * factor) * obj.CFrame.Rotation
		end
	end
end
function methods:GetScale()
	return self.scale or 1
end

-- Animator: tracks that count plays.
function methods:LoadAnimation(animation)
	local track = { Animation = animation, plays = 0, destroyed = false }
	function track:Play(...)
		self.plays += 1
		self.args = { ... }
	end
	function track:Destroy()
		self.destroyed = true
	end
	self.loaded = (self.loaded or 0) + 1
	return track
end

local function computed(inst, key)
	if key == "WorldCFrame" and inst.ClassName == "Attachment" then
		local parent = inst.Parent
		return (if parent and parent.CFrame then parent.CFrame else CFrame.identity) * inst.CFrame
	elseif key == "Position" and inst.CFrame then
		return inst.CFrame.Position
	end
	return nil
end

function Fake.new(className, name, props)
	local data = {
		ClassName = className,
		Name = name or className,
		__children = {},
		__instance = true,
	}
	if className == "Attachment" then
		data.CFrame = CFrame.identity
	elseif PARTS[className] then
		data.CFrame = CFrame.identity
		data.LocalTransparencyModifier = 0
		data.CanCollide = true
		data.CastShadow = true
	end
	local inst = setmetatable({}, {
		__index = function(self, key)
			local value = data[key]
			if value ~= nil then
				return value
			end
			value = computed(self, key)
			if value ~= nil then
				return value
			end
			return methods[key]
		end,
		__newindex = function(self, key, value)
			if key == "Parent" then
				local old = data.Parent
				if old then
					local siblings = old.__children
					local i = table.find(siblings, self)
					if i then
						table.remove(siblings, i)
					end
				end
				if value and value.__children then
					table.insert(value.__children, self)
				end
			end
			data[key] = value
		end,
		__tostring = function()
			return data.Name
		end,
	})
	for key, value in props or {} do
		inst[key] = value
	end
	return inst
end

-- A child of `parent`.
function Fake.add(parent, className, name, props)
	local inst = Fake.new(className, name, props)
	inst.Parent = parent
	return inst
end

return Fake
