-- Bodycam settings: the registry of every tunable value, its range and its default,
-- plus presets. Pure Luau.
--
--   Settings.default()                       nested table: lookLag, head, weapon,
--                                            inertia, stride
--   Settings.set(config, "group.name", v)    validates and clamps; returns ok, err
--   Settings.get(config, "group.name")
--   Settings.list()                          { path, kind, default, min, max } rows
--   Settings.applyPreset(config, name)       "Off" | "Subtle" | "Default" | "Strong"
--
-- Units: studs, seconds, Hz; angles in degrees where the name ends in Deg.

local Settings = {}

-- Registry rows: group, name, default, min, max, tag.
-- tag "amp" values scale with the Subtle/Strong presets; "cone" is the lag cone.
local ROWS = {
	-- Look lag.
	{ "lookLag", "enabled", true },
	{ "lookLag", "followHz", 5, 0.5, 30 },
	{ "lookLag", "followHzAds", 11, 0.5, 30 },
	{ "lookLag", "deadBandDeg", 0.35, 0, 3 },
	{ "lookLag", "maxLagDeg", 7, 0, 10, "cone" },
	{ "lookLag", "maxLagDegAds", 1.5, 0, 10, "cone" },
	{ "lookLag", "turnStiffening", 0.6, 0, 3 },
	{ "lookLag", "stiffenRate", 6, 0.5, 30 }, -- rad/s for full stiffening
	{ "lookLag", "lagShift", 0.06, 0, 0.5, "amp" },
	{ "lookLag", "lagShiftAds", 0.015, 0, 0.5, "amp" },
	{ "lookLag", "moveShift", 0.05, 0, 0.5, "amp" },
	{ "lookLag", "moveShiftForward", 0.03, 0, 0.5, "amp" },

	-- Head motion.
	{ "head", "enabled", true },
	{ "head", "bobEnabled", true },
	{ "head", "bobDown", 0.07, 0, 0.5, "amp" },
	{ "head", "bobSide", 0.05, 0, 0.5, "amp" },
	{ "head", "bobNodDeg", 0.35, 0, 3, "amp" },
	{ "head", "sprintBobScale", 2.2, 1, 4 },
	{ "head", "bobAds", 0.4, 0, 1 },
	{ "head", "bobRestTime", 0.25, 0.02, 2 },
	{ "head", "wallMargin", 0.25, 0, 2 },
	{ "head", "rollEnabled", true },
	{ "head", "strafeRollDeg", 1.8, 0, 10, "amp" },
	{ "head", "turnRollDeg", 2.5, 0, 10, "amp" },
	{ "head", "turnRollRate", 4, 0.5, 20 }, -- rad/s for the full turn roll
	{ "head", "rollHz", 3, 0.2, 20 },
	{ "head", "sprintLeanEnabled", true },
	{ "head", "sprintPitchDeg", 2, 0, 10, "amp" },
	{ "head", "sprintDrop", 0.12, 0, 1, "amp" },
	{ "head", "sprintEaseTime", 0.35, 0.02, 2 },
	{ "head", "sprintLunge", 0.18, 0, 1, "amp" },
	{ "head", "sprintLungeDown", 0.06, 0, 1, "amp" },
	{ "head", "sprintLungePeak", 0.15, 0.01, 1 },
	{ "head", "sprintLungeTime", 0.6, 0.05, 2 },
	{ "head", "landEnabled", true },
	{ "head", "landDrop", 0.12, 0, 1, "amp" },
	{ "head", "landRefSpeed", 50, 1, 200 }, -- fall speed (studs/s) for one landDrop
	{ "head", "landMaxScale", 2, 0, 5 },
	{ "head", "landHz", 4, 0.2, 20 },
	{ "head", "landDamping", 0.6, 0.05, 2 },

	-- Weapon motion.
	{ "weapon", "enabled", true },
	{ "weapon", "lowerEnabled", true },
	{ "weapon", "lowerDrop", 0.12, 0, 1, "amp" },
	{ "weapon", "lowerBack", 0.08, 0, 1, "amp" },
	{ "weapon", "lowerPitchDeg", 7, 0, 30, "amp" },
	{ "weapon", "lowerInTime", 0.6, 0.02, 3 },
	{ "weapon", "lowerOutTime", 0.25, 0.02, 3 },
	{ "weapon", "lowerHoldAfterShot", 0.8, 0, 5 },
	{ "weapon", "lowerHoldAfterAds", 0.2, 0, 5 },
	{ "weapon", "holsterEnabled", true },
	{ "weapon", "holsterDrop", 0.25, 0, 2 },
	{ "weapon", "holsterPitchDeg", 25, 0, 80 },
	{ "weapon", "holsterTime", 0.3, 0.02, 3 },
	{ "weapon", "kickEnabled", true },
	{ "weapon", "kickHz", 7, 0.5, 30 },
	{ "weapon", "kickDamping", 0.75, 0.05, 2 },
	{ "weapon", "kickBack", 0.05, 0, 0.5, "amp" },
	{ "weapon", "kickUp", 0.015, 0, 0.5, "amp" },
	{ "weapon", "kickPitchDeg", 2.2, 0, 20, "amp" },
	{ "weapon", "kickRollDeg", 0.8, 0, 10, "amp" },
	{ "weapon", "kickAds", 0.35, 0, 1 },
	{ "weapon", "kickMaxBack", 0.25, 0, 2 },
	{ "weapon", "kickMaxPitchDeg", 10, 0, 45 },
	{ "weapon", "wristEnabled", true },
	{ "weapon", "wristRollDeg", 9, 0, 30, "amp" },
	{ "weapon", "wristPitchDeg", 5, 0, 30, "amp" },
	{ "weapon", "rateHz", 12, 0.5, 60 },
	{ "weapon", "wristFullRate", 6, 0.5, 30 }, -- rad/s for the full wrist turn
	{ "weapon", "wristHz", 4, 0.2, 20 },
	{ "weapon", "wristAds", 0.3, 0, 1 },
	{ "weapon", "profileEnabled", true },
	{ "weapon", "profileRate", 8, 0.1, 60 },

	-- Movement inertia.
	{ "inertia", "enabled", true },
	{ "inertia", "accelTime", 0.22, 0.01, 2 },
	{ "inertia", "decelTime", 0.16, 0.01, 2 },
	{ "inertia", "stopTime", 0.12, 0.01, 2 },
	{ "inertia", "turnPenalty", 0.5, 0, 3 },
	{ "inertia", "bypassAiming", false },

	-- Stride clock.
	{ "stride", "enabled", true },
	{ "stride", "strideLength", 4.6, 0.5, 20 },
	{ "stride", "sprintStrideLength", 6.8, 0.5, 20 },
	{ "stride", "minSpeed", 0.5, 0, 5 },
}

Settings.GROUPS = { "lookLag", "head", "weapon", "inertia", "stride" }

local byPath = {}
local ordered = {}
for _, row in ROWS do
	local group, name, default, min, max, tag = row[1], row[2], row[3], row[4], row[5], row[6]
	local entry = {
		path = group .. "." .. name,
		group = group,
		name = name,
		kind = type(default),
		default = default,
		min = min,
		max = max,
		tag = tag,
	}
	byPath[entry.path] = entry
	table.insert(ordered, entry)
end

function Settings.default()
	local config = {}
	for _, group in Settings.GROUPS do
		config[group] = {}
	end
	for _, entry in ordered do
		config[entry.group][entry.name] = entry.default
	end
	return config
end

-- Every registered setting, for building an options menu.
function Settings.list()
	local rows = {}
	for _, entry in ordered do
		table.insert(rows, {
			path = entry.path,
			kind = entry.kind,
			default = entry.default,
			min = entry.min,
			max = entry.max,
		})
	end
	return rows
end

-- The registry row for a path, or nil.
function Settings.describe(path: string)
	local entry = byPath[path]
	if not entry then
		return nil
	end
	return { path = entry.path, kind = entry.kind, default = entry.default, min = entry.min, max = entry.max }
end

local function ensureGroup(config, group)
	local t = config[group]
	if type(t) ~= "table" then
		t = {}
		config[group] = t
	end
	return t
end

-- Sets one value. Numbers are clamped to the registered range.
-- Returns true, or false and a reason (unknown path, wrong type, not a number).
function Settings.set(config, path: string, value): (boolean, string?)
	local entry = byPath[path]
	if not entry then
		return false, "unknown setting " .. tostring(path)
	end
	if type(value) ~= entry.kind then
		return false, string.format("%s expects a %s, got %s", path, entry.kind, type(value))
	end
	if entry.kind == "number" then
		if value ~= value or value == math.huge or value == -math.huge then
			return false, path .. " must be a finite number"
		end
		value = math.clamp(value, entry.min, entry.max)
	end
	ensureGroup(config, entry.group)[entry.name] = value
	return true
end

function Settings.get(config, path: string)
	local entry = byPath[path]
	if not entry then
		return nil
	end
	local t = config[entry.group]
	local v = if type(t) == "table" then t[entry.name] else nil
	if v == nil then
		return entry.default
	end
	return v
end

Settings.PRESETS = {
	Off = { enabled = false, amp = 1, cone = 1 },
	Subtle = { enabled = true, amp = 0.5, cone = 0.5 },
	Default = { enabled = true, amp = 1, cone = 1 },
	Strong = { enabled = true, amp = 1.5, cone = 1.5 },
}

-- Resets config to a preset. "Off" turns every feature off (values stay default);
-- the others turn every feature on and scale amplitudes and the lag cone from the
-- defaults, clamped to the ranges. Returns true, or false for an unknown name.
function Settings.applyPreset(config, name: string): (boolean, string?)
	local preset = Settings.PRESETS[name]
	if not preset then
		return false, "unknown preset " .. tostring(name)
	end
	for _, entry in ordered do
		local group = ensureGroup(config, entry.group)
		local value = entry.default
		if entry.kind == "boolean" then
			if entry.name == "enabled" or string.match(entry.name, "Enabled$") then
				value = preset.enabled
			end
		elseif entry.tag == "amp" then
			value = math.clamp(value * preset.amp, entry.min, entry.max)
		elseif entry.tag == "cone" then
			value = math.clamp(value * preset.cone, entry.min, entry.max)
		end
		group[entry.name] = value
	end
	return true
end

return Settings
