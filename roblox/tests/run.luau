-- Test entry point. Runs every *.spec.luau under roblox/tests outside Roblox.
--
-- The host must provide four globals (roblox/tools/luau-runner provides them):
--   __fs_list(dir) -> { names }, __fs_isdir(path) -> bool, __fs_read(path) -> string,
--   __load(source, chunkname) -> function
-- Usage: luau-runner roblox/tests/run.luau [filter]
--
-- ModuleScripts are mapped onto a minimal fake instance tree, so source files keep the
-- Roblox form `require(script.Parent.Module)`.

local args = __args or {}
local selfPath = args[1] or "roblox/tests/run.luau"
local testsDir = string.match(selfPath, "^(.*)/[^/]+$") or "."
local rootDir = testsDir .. "/.."
local filter = args[2]

---------------------------------------------------------------------------
-- Fake instance tree.
---------------------------------------------------------------------------

local Node = {}
local nodeData = setmetatable({}, { __mode = "k" })

local function makeNode(name, path, parent, isModule, isDir)
	local data = { name = name, path = path, parent = parent, isModule = isModule, isDir = isDir, children = nil }
	local node = setmetatable({}, Node)
	nodeData[node] = data
	return node
end

local function stripExt(name)
	return (string.gsub(string.gsub(name, "%.luau$", ""), "%.lua$", ""))
end

local function loadChildren(node)
	local d = nodeData[node]
	if d.children then
		return d.children
	end
	d.children = {}
	if not d.isDir then
		return d.children
	end
	for _, entry in __fs_list(d.path) do
		local full = d.path .. "/" .. entry
		if __fs_isdir(full) then
			local hasInit = pcall(__fs_read, full .. "/init.luau")
			d.children[entry] = makeNode(entry, full, node, hasInit, true)
		elseif string.match(entry, "%.luau?$") and not string.match(entry, "^init%.luau?$") then
			local name = stripExt(entry)
			d.children[name] = makeNode(name, full, node, true, false)
		end
	end
	return d.children
end

Node.__index = function(node, key)
	local d = nodeData[node]
	if key == "Name" then
		return d.name
	elseif key == "Parent" then
		return d.parent
	elseif key == "FindFirstChild" then
		return function(self, name)
			return loadChildren(self)[name]
		end
	elseif key == "GetChildren" then
		return function(self)
			local list = {}
			for _, child in loadChildren(self) do
				table.insert(list, child)
			end
			return list
		end
	end
	local child = loadChildren(node)[key]
	if child == nil then
		error(string.format("%s is not a valid member of %s", tostring(key), d.path), 2)
	end
	return child
end
Node.__tostring = function(node)
	return nodeData[node].path
end

-- A require with its own module cache: specs that install different Roblox mocks
-- load fresh copies of the modules that read services at load time.
local function makeRequire()
	local cache = {}
	local function requireNode(node)
		local d = nodeData[node]
		if d == nil then
			error("require expects a ModuleScript node", 2)
		end
		if cache[node] ~= nil then
			return cache[node]
		end
		local file = if d.isDir then d.path .. "/init.luau" else d.path
		local fn = __load(__fs_read(file), file)
		local env = setmetatable({ script = node, require = requireNode }, { __index = _G })
		setfenv(fn, env)
		cache[node] = fn()
		return cache[node]
	end
	return requireNode
end
local requireNode = makeRequire()

local function folder(path, name)
	return makeNode(name, path, nil, false, true)
end

---------------------------------------------------------------------------
-- Tiny test framework.
---------------------------------------------------------------------------

local results = { passed = 0, failed = 0, failures = {} }
local current = ""

local T = {}

function T.test(name, fn)
	local full = current .. " > " .. name
	local ok, err = xpcall(fn, debug.traceback)
	if ok then
		results.passed += 1
	else
		results.failed += 1
		table.insert(results.failures, full .. "\n" .. tostring(err))
	end
end

local function fmt(v)
	if type(v) == "number" then
		return string.format("%.10g", v)
	end
	return tostring(v)
end

function T.equal(actual, expected, message)
	if actual ~= expected then
		error(string.format("%s: expected %s, got %s", message or "equal", fmt(expected), fmt(actual)), 2)
	end
end

function T.near(actual, expected, tolerance, message)
	tolerance = tolerance or 1e-6
	if type(actual) ~= "number" or actual ~= actual or math.abs(actual - expected) > tolerance then
		error(
			string.format("%s: expected %s, got %s (tolerance %g)", message or "near", fmt(expected), fmt(actual), tolerance),
			2
		)
	end
end

function T.truthy(value, message)
	if not value then
		error(message or "expected a truthy value", 2)
	end
end

-- The source tree as instances, and the golden data folder.
T.Src = folder(rootDir .. "/src", "src")
T.Bodycam = T.Src.Bodycam
T.Golden = folder(testsDir .. "/golden", "golden")
T.Tests = folder(testsDir, "tests")
T.require = requireNode
T.isolatedRequire = makeRequire

---------------------------------------------------------------------------
-- Run.
---------------------------------------------------------------------------

local specs = {}
for _, entry in __fs_list(testsDir) do
	if string.match(entry, "%.spec%.luau$") and (filter == nil or string.find(entry, filter, 1, true)) then
		table.insert(specs, entry)
	end
end

for _, spec in specs do
	current = spec
	local path = testsDir .. "/" .. spec
	local fn = __load(__fs_read(path), path)
	setfenv(fn, setmetatable({ require = requireNode }, { __index = _G }))
	local ok, err = xpcall(function()
		fn()(T)
	end, debug.traceback)
	if not ok then
		results.failed += 1
		table.insert(results.failures, spec .. " (load)\n" .. tostring(err))
	end
end

for _, failure in results.failures do
	print("FAIL " .. failure)
end
print(string.format("%d passed, %d failed", results.passed, results.failed))
if results.failed > 0 then
	error("tests failed", 0)
end
