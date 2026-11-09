-- Shader math ported to Luau, for overlay textures and exposure.
--
--   rand, film grain:    three.js src/renderers/shaders/ShaderChunk/common.glsl.js and
--                        examples/jsm/shaders/FilmShader.js (MIT, Copyright (c) 2010-2026
--                        three.js authors)
--   vignette (Eskil):    three.js examples/jsm/shaders/VignetteShader.js (MIT)
--   vignette (smooth),   pmndrs/postprocessing src/effects/glsl/vignette.frag,
--   scanlines, lens      scanlines.frag, lens-distortion.frag and
--   distortion, eye      src/materials/glsl/adaptive-luminance.frag (Zlib, Copyright (c)
--   adaptation:          2015 Raoul van Rüschen)
-- See roblox/THIRD-PARTY-NOTICES.md.
--
-- Roblox cannot run these per pixel on the rendered frame. The functions are used to
-- bake overlay images (EditableImage) and to drive Lighting.ExposureCompensation; see
-- ScreenEffects/init.luau for how each is composited.

local ShaderMath = {}

local PI = 3.141592653589793

local function fract(x: number): number
	return x - math.floor(x)
end
ShaderMath.fract = fract

-- GLSL mod: x - y * floor(x / y).
local function mod(x: number, y: number): number
	return x - y * math.floor(x / y)
end

-- three.js `rand(vec2 uv)`.
function ShaderMath.rand(u: number, v: number): number
	local a, b, c = 12.9898, 78.233, 43758.5453
	local dt = u * a + v * b
	local sn = mod(dt, PI)
	return fract(math.sin(sn) * c)
end
local rand = ShaderMath.rand

-- FilmShader: the grain sample at uv and time, and the shader's colour for a base colour.
function ShaderMath.filmNoise(u: number, v: number, time: number): number
	return rand(fract(u + time), fract(v + time))
end

function ShaderMath.filmColor(base: number, noise: number, intensity: number): number
	local color = base + base * math.clamp(0.1 + noise, 0, 1)
	return base + (color - base) * intensity
end

-- VignetteShader ("Eskil's vignette"): mix(texel, vec3(1 - darkness), dot(uv, uv)).
-- As an overlay this is exact: colour (1 - darkness) at alpha = the mix factor.
function ShaderMath.eskilVignette(u: number, v: number, offset: number, darkness: number): (number, number)
	local x, y = (u - 0.5) * offset, (v - 0.5) * offset
	return 1 - darkness, math.clamp(x * x + y * y, 0, 1)
end

local function smoothstep(e0: number, e1: number, x: number): number
	local t = math.clamp((x - e0) / (e1 - e0), 0, 1)
	return t * t * (3 - 2 * t)
end

-- pmndrs vignette technique 0: colour *= smoothstep(0.8, offset * 0.799, d * (darkness + offset)).
-- Returns the multiplier; as a black overlay, alpha = 1 - multiplier.
function ShaderMath.smoothVignette(u: number, v: number, offset: number, darkness: number): number
	local dx, dy = u - 0.5, v - 0.5
	local d = math.sqrt(dx * dx + dy * dy)
	return smoothstep(0.8, offset * 0.799, d * (darkness + offset))
end

-- pmndrs scanlines: vec2(sin(y * count), cos(y * count)).xyx, y scrolled by time * speed.
function ShaderMath.scanline(v: number, count: number, time: number?, scrollSpeed: number?): number
	local y = v + (time or 0) * (scrollSpeed or 0)
	return math.sin(y * count)
end

-- pmndrs lens distortion: where the output pixel at uv samples the input.
-- distortion = (kx, ky), principal point (px, py), focal length (fx, fy), skew.
function ShaderMath.lensDistort(
	u: number,
	v: number,
	kx: number,
	ky: number,
	px: number?,
	py: number?,
	fx: number?,
	fy: number?,
	skew: number?
): (number, number)
	px, py, fx, fy, skew = px or 0, py or 0, fx or 1, fy or 1, skew or 0
	local xn, yn = 2 * (u - 0.5), 2 * (v - 0.5)
	local r2 = xn * xn + yn * yn
	local dx, dy = (1 + kx * r2) * xn, (1 + ky * r2) * yn
	-- kk = [[fx, 0, 0], [skew * fx, fy, 0], [px, py, 1]] (columns), times (dx, dy, 1).
	local ox = fx * dx + skew * fx * dy + px
	local oy = fy * dy + py
	return ox * 0.5 + 0.5, oy * 0.5 + 0.5
end

-- Pattanaik eye adaptation (adaptive-luminance.frag):
-- adapted = l0 + (l1 - l0) * (1 - exp(-dt * tau)), both floored at minLuminance.
function ShaderMath.adaptLuminance(previous: number, current: number, dt: number, tau: number, minLuminance: number?): number
	local floor = minLuminance or 0.01
	local l0, l1 = math.max(floor, previous), math.max(floor, current)
	return l0 + (l1 - l0) * (1 - math.exp(-dt * tau))
end

return ShaderMath
