// Command-line front end of the engine's vector reticle build (src/Layers/xrRender/reticle_vector): writes the
// reticle atlas .dds the engine builds in memory when it finds <texture>.svg.
//
//   svg_to_vector <drawing.svg> <out.dds> [--check]
//   svg_to_vector --all <reticles dir> <textures dir> [--check]
//
// The settings are <drawing>.reticle.ini beside the drawing. --all builds every <name>.svg under the reticles
// folder into <textures dir>/<name>.dds, the name being the game texture path (as the engine finds them).
// --check first makes the header and checksum checks the shaders make, then draws every Mark Switch pattern with
// the shaders' coverage (a copy of gamedata/shaders/r3/reticle_vector.h in the GAMMA patch) from the built texture
// and compares it with the exact area of each drawn shape inside each pixel, over the whole reticle and at 20x on
// its centre. It fails above 0.01 plus the coordinate step in pixels. The shapes it compares against are the
// builder's oriented contours: it checks the encoding, and the builder warns about any contour it leaves out.
// Exit 0 with the notes on stdout (warnings on stderr); 1 with the errors on stderr.
#include "../../src/Layers/xrRender/reticle_vector/reticle_vector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;
using reticle_vector::DrawnShape;
namespace layout = reticle_vector::layout;
using reticle_vector::Texture;

namespace
{
std::string read_text(const fs::path& path)
{
	std::ifstream f(path, std::ios::binary);
	std::ostringstream s;
	s << f.rdbuf();
	return s.str();
}

std::vector<std::uint8_t> read_bytes(const fs::path& path)
{
	std::ifstream f(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

bool write_dds(const fs::path& path, const Texture& t)
{
	const auto bytes = reticle_vector::DdsFile(t);
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary);
	f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
	return bool(f);
}

// --- the shaders' coverage (reticle_vector.h), in float as on the GPU ---------------------------------------

struct float2
{
	float x, y;
};
float2 operator+(float2 a, float2 b) { return { a.x + b.x, a.y + b.y }; }
float2 operator-(float2 a, float2 b) { return { a.x - b.x, a.y - b.y }; }
float2 operator*(float2 a, float k) { return { a.x * k, a.y * k }; }
float saturate(float v) { return std::min(std::max(v, 0.f), 1.f); }

struct Atlas
{
	const Texture& t;
	unsigned pieces = 0, curves = 0, entries = 0, marks = 1;
	float coord_scale = 0;

	unsigned byte(unsigned i) const
	{
		const unsigned k = i >> 2;
		return t.rgba[((1 + k / layout::DESC_WIDTH) * t.width + k % layout::DESC_WIDTH) * 4 + (i & 3)];
	}
	unsigned u16(unsigned i) const { return byte(i) | (byte(i + 1) << 8); }
	float2 point(unsigned texel) const
	{
		auto s = [](unsigned v) { return float(int(v) - (v >= 32768 ? 65536 : 0)); };
		return { s(u16(texel * 4)), s(u16(texel * 4 + 2)) };
	}
	static constexpr unsigned RECORD_TEXELS = layout::RECORD_BYTES / 4;
	unsigned piece(unsigned i) const { return RECORD_TEXELS + layout::PIECE_TEXELS * i; }
	unsigned curve_base() const { return RECORD_TEXELS + layout::PIECE_TEXELS * pieces; }
	unsigned grid_base() const { return curve_base() + layout::CURVE_TEXELS * curves; }
};

// The header checks reticle_vector_open makes in the shaders: an atlas failing any of them draws nothing in game
std::string invalid(const Texture& t)
{
	auto head = [&](unsigned i) { return unsigned(t.rgba[i]); };
	auto head16 = [&](unsigned i) { return head(i) | (head(i + 1) << 8); };
	if (std::memcmp(t.rgba.data(), "RETICLE", 7) || head(7) != layout::VERSION)
		return "not a format " + std::to_string(layout::VERSION) + " reticle atlas";
	unsigned header_sum = 0;
	for (unsigned i = 0; i < 24; ++i)
		header_sum += head(i);
	const unsigned flags = head(11), pieces = head16(12);
	if ((1u << head(8)) != t.width || (1u << head(9)) != t.height || t.width < unsigned(layout::DESC_WIDTH))
		return "header size";
	if ((head(10) != 1 && head(10) != 2) || !(flags & (layout::FLAG_FFP_PARTS | layout::FLAG_SFP_PARTS | layout::FLAG_DIAL_PARTS)))
		return "header unit or flags";
	if (pieces < 1 || pieces > 4095)
		return "piece count";
	if (head16(26) != (header_sum & 0xFFFF))
		return "header checksum";
	const Atlas a{ t };
	unsigned record_sum = 0;
	for (unsigned i = 0; i < unsigned(layout::RECORD_BYTES); ++i)
		record_sum += a.byte(i);
	if (head16(24) != (record_sum & 0xFFFF))
		return "settings checksum";
	if (a.byte(4) < 1 || a.byte(4) > 15 || a.byte(5) > 1 || a.u16(2) == 0)
		return "settings marks, colours or radius";
	const unsigned curves = head16(14), entries = head16(16), mask_w = head(18), mask_row = head16(20);
	const unsigned stream_end = Atlas::RECORD_TEXELS + layout::PIECE_TEXELS * pieces + layout::CURVE_TEXELS * curves +
		layout::GRID * layout::GRID + (entries + 1) / 2;
	const unsigned rows = 1 + (stream_end + layout::DESC_WIDTH - 1) / layout::DESC_WIDTH;
	if (rows > (mask_w ? mask_row : t.height))
		return "descriptor stream overruns the texture or the mask";
	if (mask_w && ((1u << mask_w) > t.width || mask_row + (1u << head(19)) > t.height))
		return "mask outside the texture";
	return {};
}

Atlas open(const Texture& t)
{
	Atlas a{ t };
	auto head16 = [&](int i) { return unsigned(t.rgba[i] | (t.rgba[i + 1] << 8)); };
	a.pieces = head16(12);
	a.curves = head16(14);
	a.entries = head16(16);
	a.coord_scale = float(a.u16(2) / 16.0 / 32767.0);
	a.marks = a.byte(4);
	return a;
}

float2 bezier(float2 A, float2 B, float2 C, float s)
{
	const float2 b = (B - A) * 2.f;
	const float2 c = A - B * 2.f + C;
	return A + (b + c * s) * s;
}

float mean_clamp(float x0, float x1)
{
	if (std::abs(x1 - x0) < 1e-5f)
		return saturate(0.5f * (x0 + x1));
	auto g = [](float x) { return x <= 0.f ? 0.f : (x >= 1.f ? x - 0.5f : 0.5f * x * x); };
	return (g(x1) - g(x0)) / (x1 - x0);
}

float edge(float2 a, float2 b)
{
	const float ya = saturate(a.y), yb = saturate(b.y);
	if (ya == yb)
		return 0.f;
	const float k = (b.x - a.x) / (b.y - a.y);
	return (yb - ya) * (1.f - mean_clamp(a.x + k * (ya - a.y), a.x + k * (yb - a.y)));
}

float solve(float ay, float by, float cy, float target, float t0, float t1)
{
	const float c = cy - target;
	float root;
	if (std::abs(ay) < 1e-9f * std::max(std::abs(by), 1e-20f))
		root = -c / by;
	else
	{
		const float q = -0.5f * (by + (by >= 0.f ? 1.f : -1.f) * std::sqrt(std::max(by * by - 4.f * ay * c, 0.f)));
		const float r1 = q / ay;
		const float r2 = q != 0.f ? c / q : r1;
		root = (r1 >= t0 - 1e-4f && r1 <= t1 + 1e-4f) ? r1 : r2;
	}
	return std::min(std::max(root, t0), t1);
}

float monotone_piece(float2 A, float2 B, float2 C, float t0, float t1)
{
	const float ay = A.y - 2.f * B.y + C.y, by = 2.f * (B.y - A.y);
	const float y0 = bezier(A, B, C, t0).y, y1 = bezier(A, B, C, t1).y;
	if (y0 == y1 || std::max(y0, y1) <= 0.f || std::min(y0, y1) >= 1.f)
		return 0.f;
	float ta = t0, tb = t1;
	if (y0 < 0.f || y0 > 1.f)
		ta = solve(ay, by, A.y, y0 < 0.f ? 0.f : 1.f, t0, t1);
	if (y1 < 0.f || y1 > 1.f)
		tb = solve(ay, by, A.y, y1 < 0.f ? 0.f : 1.f, t0, t1);
	float area = 0.f;
	float2 previous = bezier(A, B, C, ta);
	for (int s = 1; s <= 4; ++s)
	{
		const float2 next = bezier(A, B, C, ta + (tb - ta) * (float(s) / 4));
		area += edge(previous, next);
		previous = next;
	}
	return area;
}

// the pixel is the axis-aligned square of side px centred on p
float shape_coverage(const Atlas& a, unsigned first, unsigned count, float2 p, float px)
{
	float area = 0.f;
	for (unsigned c = first; c < first + count; ++c)
	{
		const unsigned k = a.curve_base() + 3 * c;
		auto to_pixel = [&](unsigned texel) { return (a.point(texel) * a.coord_scale - p) * (1.f / px) + float2{ 0.5f, 0.5f }; };
		const float2 A = to_pixel(k), B = to_pixel(k + 1), C = to_pixel(k + 2);
		if (std::max({ A.y, B.y, C.y }) <= 0.f || std::min({ A.y, B.y, C.y }) >= 1.f)
			continue;
		const float ay = A.y - 2.f * B.y + C.y;
		const float turn = std::abs(ay) > 1e-12f ? (A.y - B.y) / ay : -1.f;
		if (turn > 0.f && turn < 1.f)
			area += monotone_piece(A, B, C, 0.f, turn) + monotone_piece(A, B, C, turn, 1.f);
		else
			area += monotone_piece(A, B, C, 0.f, 1.f);
	}
	return std::abs(area);
}

// the coverage slot of a role: illum, paint (glow is paint too), light
int slot(int role)
{
	return role == layout::ROLE_ILLUM ? 0 : (role == layout::ROLE_LIGHT ? 2 : 1);
}

// coverage per slot of mark `pattern`, every plane (ffp, sfp, the dial unturned) at p
std::array<float, 3> shader_coverage(const Atlas& a, float2 p, float px, unsigned pattern)
{
	std::array<float, 3> m{};
	const float radius = a.coord_scale * 32767.f, reach = 0.5f * px;
	if (std::abs(p.x) > radius + reach || std::abs(p.y) > radius + reach)
		return m;
	const unsigned grid = layout::GRID;
	const float cell = 2.f * radius / grid;
	const bool listed = reach <= cell;
	const unsigned lists = a.grid_base() + grid * grid;
	unsigned first = 0, count = a.pieces;
	if (listed)
	{
		const unsigned cx = unsigned(std::min(std::max(std::floor((p.x + radius) / cell), 0.f), float(grid - 1)));
		const unsigned cy = unsigned(std::min(std::max(std::floor((p.y + radius) / cell), 0.f), float(grid - 1)));
		const unsigned g = (a.grid_base() + cy * grid + cx) * 4;
		first = a.u16(g);
		count = first <= a.entries ? std::min(a.u16(g + 2), a.entries - first) : 0;
	}
	for (unsigned plane = 0; plane < 3; ++plane)
	{
		int group = -1, role = 0;
		float sum = 0.f;
		auto flush = [&]() {
			if (group >= 0 && sum > 0.f)
			{
				float& r = m[slot(role)];
				r = 1.f - (1.f - r) * (1.f - saturate(sum));
			}
			sum = 0.f;
		};
		for (unsigned j = 0; j < count; ++j)
		{
			unsigned i = j;
			if (listed)
			{
				const unsigned entry = a.u16(lists * 4 + (first + j) * 2);
				if ((entry >> 12) != 0 && (entry >> 12) != pattern)
					continue;
				i = entry & 0xFFF;
			}
			if (i >= a.pieces)
				continue;
			const unsigned k = a.piece(i);
			const float2 lo = a.point(k + 3) * a.coord_scale, hi = a.point(k + 4) * a.coord_scale;
			if (p.x + reach < lo.x || p.y + reach < lo.y || p.x - reach > hi.x || p.y - reach > hi.y)
				continue;
			const unsigned flags = a.byte(k * 4 + 1), mark = flags >> 4, r = a.byte(k * 4);
			const unsigned curves_first = a.u16(k * 4 + 8), curves_count = a.u16(k * 4 + 10);
			const int g = int(a.u16(k * 4 + 6));
			const unsigned piece_plane = (flags & 1) + ((flags >> 1) & flags & 1); // 0 ffp, 1 sfp, 2 dial
			if (!((mark == 0 || mark == pattern) && piece_plane == plane && r >= 1 && r <= 5))
				continue;
			const float c = shape_coverage(a, curves_first, curves_count, p, px);
			if (g != group)
			{
				flush();
				group = g;
				role = int(r);
			}
			sum += c;
		}
		flush();
	}
	return m;
}

// --- the exact coverage of the drawn shapes -----------------------------------------------------------------

using Polygon = std::vector<std::array<double, 2>>;

struct Exact
{
	int role, mark;
	std::vector<Polygon> polygons; // the contours flattened finely
	double x0, y0, x1, y1;
};

std::vector<Exact> flatten(const std::vector<DrawnShape>& drawn)
{
	std::vector<Exact> out;
	for (const auto& d : drawn)
	{
		Exact e{ d.role, d.mark, {}, 1e30, 1e30, -1e30, -1e30 };
		for (const auto& c : d.contours)
		{
			Polygon poly;
			for (const auto& q : c)
				for (int j = 0; j < 64; ++j)
				{
					const double t = j / 64.0, u = 1 - t;
					const std::array<double, 2> pt{ u * u * q[0][0] + 2 * u * t * q[1][0] + t * t * q[2][0],
						u * u * q[0][1] + 2 * u * t * q[1][1] + t * t * q[2][1] };
					poly.push_back(pt);
					e.x0 = std::min(e.x0, pt[0]);
					e.y0 = std::min(e.y0, pt[1]);
					e.x1 = std::max(e.x1, pt[0]);
					e.y1 = std::max(e.y1, pt[1]);
				}
			e.polygons.push_back(poly);
		}
		out.push_back(e);
	}
	return out;
}

// the signed area of a polygon inside the box [lo, hi] (Sutherland-Hodgman: exact for any polygon against a box)
double clipped_area(const Polygon& poly, double x0, double y0, double x1, double y1)
{
	Polygon cur = poly, next;
	for (int side = 0; side < 4 && !cur.empty(); ++side)
	{
		const int axis = side / 2;
		const double bound = side == 0 ? x0 : side == 1 ? x1 : side == 2 ? y0 : y1;
		const bool keep_above = side == 0 || side == 2;
		auto inside = [&](const std::array<double, 2>& v) { return keep_above ? v[axis] >= bound : v[axis] <= bound; };
		next.clear();
		for (size_t i = 0; i < cur.size(); ++i)
		{
			const auto& a = cur[i];
			const auto& b = cur[(i + 1) % cur.size()];
			const bool ia = inside(a), ib = inside(b);
			if (ia)
				next.push_back(a);
			if (ia != ib)
			{
				const double t = (bound - a[axis]) / (b[axis] - a[axis]);
				next.push_back({ a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t });
			}
		}
		std::swap(cur, next);
	}
	double area = 0;
	for (size_t i = 0; i < cur.size(); ++i)
	{
		const auto& a = cur[i];
		const auto& b = cur[(i + 1) % cur.size()];
		area += a[0] * b[1] - b[0] * a[1];
	}
	return 0.5 * area;
}

std::array<double, 3> exact_coverage(const std::vector<Exact>& shapes, double x, double y, double px, unsigned pattern)
{
	std::array<double, 3> m{};
	const double x0 = x - px / 2, y0 = y - px / 2, x1 = x + px / 2, y1 = y + px / 2;
	for (const auto& s : shapes)
	{
		if ((s.mark != 0 && unsigned(s.mark) != pattern) || s.x1 < x0 || s.y1 < y0 || s.x0 > x1 || s.y0 > y1)
			continue;
		double area = 0;
		for (const auto& poly : s.polygons)
			area += clipped_area(poly, x0, y0, x1, y1);
		const double c = std::min(std::abs(area) / (px * px), 1.0);
		double& r = m[slot(s.role)];
		r = 1 - (1 - r) * (1 - c);
	}
	return m;
}

bool check(const Texture& t, const std::vector<DrawnShape>& drawn, std::string& report)
{
	if (const std::string why = invalid(t); !why.empty())
	{
		report = "check: the shaders would reject this atlas: " + why + "  FAIL\n";
		return false;
	}
	const Atlas a = open(t);
	const auto shapes = flatten(drawn);
	const double radius = a.coord_scale * 32767.0;
	bool ok = true;
	std::ostringstream out;
	for (unsigned pattern = 1; pattern <= a.marks; ++pattern)
		for (double zoom : { 1.0, 20.0 })
		{
			const int n = 256;
			const double px = 2 * radius / zoom / n;
			const double limit = 0.01 + a.coord_scale / px;
			double worst = 0;
			int wx = 0, wy = 0;
			for (int iy = 0; iy < n; ++iy)
				for (int ix = 0; ix < n; ++ix)
				{
					const double x = -radius / zoom + (ix + 0.5) * px, y = -radius / zoom + (iy + 0.5) * px;
					const auto s = shader_coverage(a, { float(x), float(y) }, float(px), pattern);
					const auto e = exact_coverage(shapes, x, y, px, pattern);
					for (int r = 0; r < 3; ++r)
						if (std::abs(s[r] - e[r]) > worst)
						{
							worst = std::abs(s[r] - e[r]);
							wx = ix;
							wy = iy;
						}
				}
			char line[160];
			std::snprintf(line, sizeof(line), "check mark %u, %gx: largest error %.4f at pixel %d, %d (limit %.4f)%s\n", pattern,
				zoom, worst, wx, wy, limit, worst > limit ? "  FAIL" : "");
			out << line;
			ok = ok && worst <= limit;
		}
	report = out.str();
	return ok;
}

// builds one drawing; false with the reasons on stderr
bool build(const fs::path& svg, const fs::path& dds, const std::string& texture_name, bool with_check)
{
	fs::path ini = svg;
	ini.replace_extension(".reticle.ini");
	const fs::path folder = ini.parent_path();
	const auto read = [&](const std::string& relative) { return read_bytes(folder / relative); };
	Texture texture;
	reticle_vector::Report report;
	std::vector<DrawnShape> drawn;
	const bool ok = reticle_vector::Build(texture_name, read_text(svg), read_text(ini), read, texture, report, with_check ? &drawn : nullptr);
	for (const auto& w : report.warnings)
		std::fprintf(stderr, "warning: %s: %s\n", texture_name.c_str(), w.c_str());
	if (!ok)
	{
		for (const auto& e : report.errors)
			std::fprintf(stderr, "error: %s\n", e.c_str());
		return false;
	}
	if (!write_dds(dds, texture))
	{
		std::fprintf(stderr, "error: could not write %s\n", dds.string().c_str());
		return false;
	}
	for (const auto& n : report.notes)
		std::printf("%s: %s\n", texture_name.c_str(), n.c_str());
	if (with_check)
	{
		std::string lines;
		const bool passed = check(texture, drawn, lines);
		std::printf("%s", lines.c_str());
		if (!passed)
		{
			std::fprintf(stderr, "error: %s: the coverage check failed\n", texture_name.c_str());
			return false;
		}
	}
	return true;
}
}

int main(int argc, char** argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);
	const bool with_check = std::find(args.begin(), args.end(), "--check") != args.end();
	args.erase(std::remove(args.begin(), args.end(), "--check"), args.end());
	if (args.size() == 3 && args[0] == "--all")
	{
		const fs::path source = args[1], textures = args[2];
		std::vector<fs::path> drawings;
		for (const auto& entry : fs::recursive_directory_iterator(source))
			if (entry.is_regular_file() && entry.path().extension() == ".svg")
				drawings.push_back(entry.path());
		std::sort(drawings.begin(), drawings.end());
		if (drawings.empty())
		{
			std::fprintf(stderr, "error: no .svg under %s\n", source.string().c_str());
			return 1;
		}
		bool ok = true;
		for (const auto& svg : drawings)
		{
			fs::path name = fs::relative(svg, source);
			name.replace_extension();
			std::string texture = name.generic_string();
			std::replace(texture.begin(), texture.end(), '/', '\\');
			fs::path dds = textures / name;
			dds += ".dds";
			ok = build(svg, dds, texture, with_check) && ok;
		}
		return ok ? 0 : 1;
	}
	if (args.size() == 2)
		return build(args[0], args[1], fs::path(args[0]).stem().string(), with_check) ? 0 : 1;
	std::fprintf(stderr, "usage: svg_to_vector <drawing.svg> <out.dds> [--check]\n"
						 "       svg_to_vector --all <reticles dir> <textures dir> [--check]\n");
	return 2;
}
