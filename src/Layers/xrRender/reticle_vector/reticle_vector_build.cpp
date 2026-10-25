// The vector reticle build: settings, shapes in reticle units, contour orientation, per-cell pieces, the shape
// grid and the atlas layout (reticle_vector.h: layout). Values round half to even.
#include "reticle_vector.h"
#include "reticle_vector_svg.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "../../../3rd party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace reticle_vector
{
namespace
{
using namespace layout;
const double TOLERANCE = 0.001; // largest distance of a quadratic from the cubic it replaces, reticle units
const int MAX_PIECES = 4095;    // the grid lists hold a 12-bit piece index

// round half to even
long long round_even(double v)
{
	return (long long)std::nearbyint(v);
}

std::string number_text(double v)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%g", v);
	return buffer;
}

bool parse_double(const std::string& text, double& v)
{
	std::string t = strip(text);
	if (!t.empty() && t[0] == '+')
		t.erase(0, 1);
	const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
	return !t.empty() && r.ec == std::errc() && r.ptr == t.data() + t.size();
}

std::vector<std::string> split_list(const std::string& text, char by = ',')
{
	std::vector<std::string> out;
	size_t i = 0;
	while (i <= text.size())
	{
		size_t j = text.find(by, i);
		if (j == std::string::npos)
			j = text.size();
		const std::string v = strip(text.substr(i, j - i));
		if (!v.empty())
			out.push_back(v);
		i = j + 1;
	}
	return out;
}

// --- settings (<texture>.reticle.ini) -----------------------------------------------------------------------

// [section] and key = value lines, both case-insensitive (as the Reticle Editor reads them); ; and # line comments,
// an inline ; after whitespace
using Ini = std::map<std::string, std::map<std::string, std::string>>;

Ini parse_ini(const std::string& text)
{
	Ini ini;
	std::string section;
	size_t i = 0;
	while (i < text.size())
	{
		size_t j = text.find('\n', i);
		if (j == std::string::npos)
			j = text.size();
		std::string line = text.substr(i, j - i);
		i = j + 1;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (const std::string whole = strip(line); whole.empty() || whole[0] == ';' || whole[0] == '#')
			continue;
		for (size_t k = 1; k < line.size(); ++k)
			if (line[k] == ';' && (line[k - 1] == ' ' || line[k - 1] == '\t'))
			{
				line.resize(k);
				break;
			}
		const std::string stripped = strip(line);
		if (stripped[0] == '[' && stripped.back() == ']')
		{
			section = lower(strip(stripped.substr(1, stripped.size() - 2)));
			ini[section];
			continue;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos || section.empty())
			throw Error("settings: line '" + stripped + "' is neither [section] nor key = value");
		ini[section][lower(strip(line.substr(0, eq)))] = strip(line.substr(eq + 1));
	}
	return ini;
}

std::string get(const Ini& ini, const char* section, const char* key, const char* fallback = "")
{
	const auto s = ini.find(section);
	if (s != ini.end())
		if (const auto k = s->second.find(key); k != s->second.end() && !strip(k->second).empty())
			return strip(k->second);
	return fallback;
}

double number(const std::string& text, const std::string& key, double lo, double hi)
{
	double v;
	if (!parse_double(text, v))
		throw Error(key + " '" + text + "': a number from " + number_text(lo) + " to " + number_text(hi));
	if (!(v >= lo && v <= hi))
		throw Error(key + " " + number_text(v) + " is outside " + number_text(lo) + " to " + number_text(hi));
	return v;
}

double finite(const std::string& text, const std::string& key, bool positive)
{
	double v;
	if (!parse_double(text, v))
		throw Error(key + ": enter a number");
	if (!std::isfinite(v) || (positive && v <= 0))
		throw Error(key + ": enter a finite " + (positive ? "positive " : "") + "number");
	return v;
}

// Every key the settings may hold; anything else is an error (a misspelt key must not pass silently)
const std::map<std::string, std::set<std::string>> KEYS{ { "reticle", { "scale", "units", "legacy_span", "calibrated_magnification" } },
	{ "illumination", { "colour", "luminance", "brightness_steps", "glow_strength", "glow_size", "fibre", "tritium" } },
	{ "holo", { "softness", "level", "ghost_level", "ghost_offset", "fringe", "flicker" } },
	{ "mask", { "image", "depth", "size", "eye_shift", "animate" } } };

struct Hologram
{
	double softness = 1.25, level = 0.75, ghost_level = 0, ghost_x = 0, ghost_y = 0, fringe = 0, flicker = 0;
};

struct Mask
{
	bool present = false;
	std::string image;
	double depth = 1.0, size = 10.0, eye_shift = 0.0;
	double animate = 0.0; // the grain's changes per second, 0 = still
};

struct Settings
{
	double scale = 0; // SVG user units per reticle unit
	std::string units = "moa";
	double legacy_span = 0; // 0 = true angles, else the 3dss scale mode
	bool has_calibrated_magnification = false;
	double calibrated_magnification = 1;
	bool drawing_colours = false; // [illumination] colour = drawing
	double luminance = 8, glow_strength = 1, glow_size = 1;
	std::vector<double> steps;
	// lit by daylight through a fibre (times the sky's luminance) and a tritium glow (scene luminance) instead of a
	// battery: always on, no Mark Switch brightness
	double fibre = 0, tritium = 0;
	bool holo = false;
	Hologram hologram;
	Mask mask;
};

Settings parse_settings(const std::string& text)
{
	const Ini ini = parse_ini(text);
	for (const auto& [section, keys] : ini)
	{
		const auto known = KEYS.find(section);
		if (known == KEYS.end())
			throw Error("settings: unknown section [" + section + "]");
		for (const auto& kv : keys)
			if (!known->second.count(kv.first))
				throw Error("settings: [" + section + "] has no key '" + kv.first + "'");
	}
	Settings s;
	const std::string scale = get(ini, "reticle", "scale");
	if (scale.empty())
		throw Error("settings: [reticle] scale, the drawing's SVG units per reticle unit, is missing");
	s.scale = finite(scale, "scale", true);
	s.units = lower(get(ini, "reticle", "units", "moa"));
	if (s.units == "mils" || s.units == "mrad")
		s.units = "mil";
	if (s.units != "moa" && s.units != "mil")
		throw Error("units '" + s.units + "': use moa or mil");
	if (const std::string span = get(ini, "reticle", "legacy_span"); !span.empty())
		s.legacy_span = number(span, "legacy_span", 1, 8191);
	if (const std::string m = get(ini, "reticle", "calibrated_magnification"); !m.empty())
	{
		s.calibrated_magnification = number(m, "calibrated_magnification", 0.01, 655);
		s.has_calibrated_magnification = true;
	}
	const std::string colour = lower(get(ini, "illumination", "colour", "sight"));
	if (colour != "sight" && colour != "drawing")
		throw Error("colour '" + colour + "': use sight (the sight's Mark Switch colours) or drawing (the SVG's fills)");
	s.drawing_colours = colour == "drawing";
	s.luminance = number(get(ini, "illumination", "luminance", "8"), "luminance", 0.5, 64);
	for (const auto& v : split_list(get(ini, "illumination", "brightness_steps")))
		s.steps.push_back(number(v, "brightness_steps", 0, 1));
	if (s.steps.size() > 8)
		throw Error("brightness_steps: at most 8 steps");
	if (const std::string v = get(ini, "illumination", "fibre"); !v.empty())
		s.fibre = number(v, "fibre", 0, 7.9);
	if (const std::string v = get(ini, "illumination", "tritium"); !v.empty())
		s.tritium = number(v, "tritium", 0, 255);
	const bool daylight = !get(ini, "illumination", "fibre").empty() || !get(ini, "illumination", "tritium").empty();
	if (daylight && s.fibre <= 0 && s.tritium <= 0)
		throw Error("fibre and tritium are both 0: the reticle would never light");
	if (daylight && (!get(ini, "illumination", "luminance").empty() || !s.steps.empty()))
		throw Error("luminance and brightness_steps set a battery's light; a fibre and tritium sight has neither");
	s.glow_strength = number(get(ini, "illumination", "glow_strength", "1"), "glow_strength", 0, 7.9);
	s.glow_size = number(get(ini, "illumination", "glow_size", "1"), "glow_size", 0.25, 4);
	s.holo = ini.count("holo") != 0;
	struct Range
	{
		const char* key;
		double lo, hi;
		double Hologram::* field;
	};
	static const Range RANGES[] = { { "softness", 1, 4, &Hologram::softness }, { "level", 0.05, 2.5, &Hologram::level },
		{ "ghost_level", 0, 0.5, &Hologram::ghost_level }, { "fringe", 0, 2.5, &Hologram::fringe }, { "flicker", 0, 1, &Hologram::flicker } };
	for (const auto& r : RANGES)
		if (const std::string text = get(ini, "holo", r.key); !text.empty())
			s.hologram.*r.field = number(text, r.key, r.lo, r.hi);
	const auto offset = split_list(get(ini, "holo", "ghost_offset"));
	if (!offset.empty())
	{
		if (offset.size() != 2)
			throw Error("ghost_offset: two numbers, x and y, e.g. 3, -2");
		s.hologram.ghost_x = number(offset[0], "ghost_offset", -15, 15);
		s.hologram.ghost_y = number(offset[1], "ghost_offset", -15, 15);
	}
	if (ini.count("mask"))
	{
		if (!s.holo)
			throw Error("[mask] is the grain of a hologram; add a [holo] section");
		s.mask.present = true;
		s.mask.image = get(ini, "mask", "image");
		if (s.mask.image.empty())
			throw Error("[mask] image, the grey PNG, is missing");
		if (const std::string v = get(ini, "mask", "depth"); !v.empty())
			s.mask.depth = number(v, "mask depth", 0, 1);
		if (const std::string v = get(ini, "mask", "size"); !v.empty())
			s.mask.size = number(v, "mask size", 0.0625, 4095);
		if (const std::string v = get(ini, "mask", "eye_shift"); !v.empty())
			s.mask.eye_shift = number(v, "mask eye_shift", 0, 2.55);
		if (const std::string v = get(ini, "mask", "animate"); !v.empty())
			s.mask.animate = number(v, "mask animate", 0, 15.9);
	}
	return s;
}

// --- shapes in reticle units ------------------------------------------------------------------------------

using QContour = std::vector<Quad>;

// a drawn shape for one Mark Switch mark, before it is cut into pieces
struct Shape
{
	int role, mark;
	bool sfp, dial; // a dial part keeps its size too
	std::array<int, 3> colour;
	std::vector<Contour> contours; // reticle units, y down
	std::string source;            // the SVG element, for messages
};

// The bore centre is the middle of the "center" layer's mark, else the middle of the drawing. A shape in
// several Mark Switch marks becomes one shape per mark.
std::vector<Shape> reticle_shapes(const SvgReticle& svg, const Settings& s)
{
	const auto& vb = svg.view_box;
	const Point origin = svg.has_centre ? svg.centre : Point{ vb[0] + vb[2] / 2, vb[1] + vb[3] / 2 };
	const double k = 1.0 / s.scale;
	std::vector<Shape> shapes;
	for (const auto& sh : svg.shapes)
	{
		std::vector<Contour> contours = sh.contours;
		for (auto& c : contours)
			for (auto& seg : c)
				for (int j = 0; j < seg.points(); ++j)
					seg.p[j] = { (seg.p[j][0] - origin[0]) * k, (seg.p[j][1] - origin[1]) * k };
		static const std::map<std::string, int> ROLES{ { "illum", ROLE_ILLUM }, { "paint", ROLE_PAINT }, { "glow", ROLE_GLOW },
			{ "light", ROLE_LIGHT } };
		const int role = ROLES.at(sh.role);
				for (int mark : sh.marks.empty() ? std::vector<int>{ 0 } : sh.marks)
			shapes.push_back({ role, mark, sh.sfp || sh.dial, sh.dial, sh.colour, contours, sh.source });
	}
	return shapes;
}

// --- geometry ----------------------------------------------------------------------------------------------

Point add(const Point& a, const Point& b)
{
	return { a[0] + b[0], a[1] + b[1] };
}
Point sub(const Point& a, const Point& b)
{
	return { a[0] - b[0], a[1] - b[1] };
}
Point mul(const Point& a, double k)
{
	return { a[0] * k, a[1] * k };
}
Point div(const Point& a, double k)
{
	return { a[0] / k, a[1] / k };
}
double norm(const Point& a)
{
	return std::sqrt(a[0] * a[0] + a[1] * a[1]);
}

void cubic_quads(const Point& p0, const Point& c0, const Point& c1, const Point& p1, int depth, QContour& out)
{
	const Point d = sub(add(sub(p1, mul(c1, 3.0)), mul(c0, 3.0)), p0); // p1 - 3 c1 + 3 c0 - p0
	const double error = std::sqrt(3.0) / 36 * norm(d);
	if (error <= TOLERANCE || depth >= 12)
	{
		out.push_back({ p0, div(sub(sub(mul(add(c0, c1), 3.0), p0), p1), 4), p1 });
		return;
	}
	const Point a = div(add(p0, c0), 2), b = div(add(c0, c1), 2), c = div(add(c1, p1), 2);
	const Point dd = div(add(a, b), 2), e = div(add(b, c), 2);
	const Point m = div(add(dd, e), 2);
	cubic_quads(p0, a, dd, m, depth + 1, out);
	cubic_quads(m, e, c, p1, depth + 1, out);
}

QContour quads_of(const Contour& c)
{
	QContour out;
	for (const auto& s : c)
	{
		if (s.kind == 'L')
			out.push_back({ s.p[0], div(add(s.p[0], s.p[1]), 2), s.p[1] });
		else if (s.kind == 'Q')
			out.push_back({ s.p[0], s.p[1], s.p[2] });
		else
			cubic_quads(s.p[0], s.p[1], s.p[2], s.p[3], 0, out);
	}
	return out;
}

Point bezier(const Quad& q, double t)
{
	return add(q[0], mul(add(mul(sub(q[1], q[0]), 2.0), mul(add(sub(q[0], mul(q[1], 2.0)), q[2]), t)), t));
}

std::vector<Point> polyline(const QContour& c, int n)
{
	std::vector<Point> pts;
	for (const auto& q : c)
		for (int j = 0; j < n; ++j)
			pts.push_back(bezier(q, j * (1.0 / n)));
	return pts;
}

int winding(const Point& point, const std::vector<std::vector<Point>>& polygons)
{
	int w = 0;
	for (const auto& poly : polygons)
		for (size_t i = 0; i < poly.size(); ++i)
		{
			const Point& a = poly[i];
			const Point& b = poly[(i + 1) % poly.size()];
			const bool up = a[1] <= point[1] && b[1] > point[1];
			const bool down = a[1] > point[1] && b[1] <= point[1];
			const double cross = (b[0] - a[0]) * (point[1] - a[1]) - (point[0] - a[0]) * (b[1] - a[1]);
			w += (up && cross > 0) ? 1 : 0;
			w -= (down && cross < 0) ? 1 : 0;
		}
	return w;
}

// The filled side (nonzero, over all of the shape's contours) on the left. A contour takes its side from the
// first of its segments with fill on exactly one side, probed closer and closer so a thin shape is not taken for
// a gap. A contour with fill on both sides all round (inside another contour of the shape) or on neither (no area)
// is no boundary: it is left out and counted in `dropped`.
std::vector<QContour> oriented(const std::vector<QContour>& contours, int& dropped)
{
	std::vector<std::vector<Point>> polygons;
	for (const auto& c : contours)
		polygons.push_back(polyline(c, 16));
	std::vector<QContour> out;
	for (const auto& c : contours)
	{
		double x0 = c[0][0][0], x1 = x0, y0 = c[0][0][1], y1 = y0;
		for (const auto& p : c)
			for (const auto& v : p)
			{
				x0 = std::min(x0, v[0]);
				x1 = std::max(x1, v[0]);
				y0 = std::min(y0, v[1]);
				y1 = std::max(y1, v[1]);
			}
		const double span = std::max(std::max(x1 - x0, y1 - y0), 1e-6);
		int side = 0; // 1 filled on the left, -1 on the right
		for (size_t k = 0; k < c.size() && !side; ++k)
		{
			const Quad& q = c[k];
			const Point tangent = sub(q[2], q[0]);
			if (norm(tangent) == 0)
				continue;
			const Point mid = bezier(q, 0.5);
			const Point normal = div(Point{ -tangent[1], tangent[0] }, norm(tangent));
			for (double d = span * 1e-4; d >= span * 1e-10 && !side; d *= 0.01)
			{
				const bool left = winding(add(mid, mul(normal, d)), polygons) != 0;
				const bool right = winding(sub(mid, mul(normal, d)), polygons) != 0;
				if (left != right)
					side = left ? 1 : -1;
			}
		}
		if (side > 0)
			out.push_back(c);
		else if (side < 0)
		{
			QContour r;
			for (auto it = c.rbegin(); it != c.rend(); ++it)
				r.push_back({ (*it)[2], (*it)[1], (*it)[0] });
			out.push_back(r);
		}
		else
			++dropped;
	}
	return out;
}

// --- pieces per grid cell ------------------------------------------------------------------------------------

const double EPS = 1e-9;

std::pair<Quad, Quad> split_quad(const Quad& q, double t)
{
	const Point ab = add(q[0], mul(sub(q[1], q[0]), t)), bc = add(q[1], mul(sub(q[2], q[1]), t));
	const Point p = add(ab, mul(sub(bc, ab), t));
	return { Quad{ q[0], ab, p }, Quad{ p, bc, q[2] } };
}

std::vector<double> crossings(const Quad& q, int axis, double value)
{
	const double a = q[0][axis] - 2 * q[1][axis] + q[2][axis];
	const double b = 2 * (q[1][axis] - q[0][axis]);
	const double c = q[0][axis] - value;
	std::vector<double> roots;
	if (std::abs(a) < EPS)
	{
		if (std::abs(b) >= EPS)
			roots.push_back(-c / b);
	}
	else
	{
		const double d = b * b - 4 * a * c;
		if (d < 0)
			return {};
		const double s = std::sqrt(d);
		roots = { (-b - s) / (2 * a), (-b + s) / (2 * a) };
	}
	std::vector<double> out;
	for (double t : roots)
		if (EPS < t && t < 1 - EPS)
			out.push_back(t);
	return out;
}

struct Line
{
	int axis;
	double value;
};

std::vector<Quad> pieces_of(const Quad& q, const std::vector<Line>& lines)
{
	std::set<double> ts;
	for (const auto& l : lines)
		for (double t : crossings(q, l.axis, l.value))
			ts.insert(t);
	std::vector<Quad> out;
	Quad rest = q;
	double done = 0.0;
	for (double t : ts)
	{
		const auto parts = split_quad(rest, (t - done) / (1 - done));
		out.push_back(parts.first);
		rest = parts.second;
		done = t;
	}
	out.push_back(rest);
	return out;
}

Point clamp(const Point& p, const Point& lo, const Point& hi)
{
	return { std::min(std::max(p[0], lo[0]), hi[0]), std::min(std::max(p[1], lo[1]), hi[1]) };
}

bool border_line(const Point& p0, const Point& p1, const Point& lo, const Point& hi, Line& line)
{
	for (int axis = 0; axis < 2; ++axis)
		for (double value : { lo[axis], hi[axis] })
			if (std::abs(p0[axis] - value) < 1e-12 && std::abs(p1[axis] - value) < 1e-12)
			{
				line = { axis, value };
				return true;
			}
	return false;
}

std::pair<QContour, int> clip_contour(const QContour& contour, const Point& lo, const Point& hi)
{
	const std::vector<Line> lines{ { 0, lo[0] }, { 0, hi[0] }, { 1, lo[1] }, { 1, hi[1] } };
	struct Item
	{
		bool curve;
		Quad q;
		Point a, b;
	};
	std::vector<Item> out;
	int inside = 0;
	for (const auto& q : contour)
		for (const auto& s : pieces_of(q, lines))
		{
			const Point mid = bezier(s, 0.5);
			if (mid[0] >= lo[0] - 1e-12 && mid[1] >= lo[1] - 1e-12 && mid[0] <= hi[0] + 1e-12 && mid[1] <= hi[1] + 1e-12)
			{
				out.push_back({ true, s, {}, {} });
				++inside;
			}
			else
			{
				const Point a = clamp(s[0], lo, hi), b = clamp(s[2], lo, hi);
				if (norm(sub(b, a)) > 1e-12)
					out.push_back({ false, {}, a, b });
			}
		}
	std::vector<Item> merged;
	for (const auto& item : out)
	{
		if (!item.curve && !merged.empty() && !merged.back().curve)
		{
			const Point a0 = merged.back().a, b0 = merged.back().b;
			Line l0{}, l1{}, l2{};
			if (border_line(a0, b0, lo, hi, l0) && border_line(b0, item.b, lo, hi, l1) && border_line(item.a, item.b, lo, hi, l2) &&
				l0.axis == l1.axis && l0.value == l1.value && l0.axis == l2.axis && l0.value == l2.value)
			{
				merged.back().b = item.b;
				continue;
			}
		}
		merged.push_back(item);
	}
	QContour curves;
	for (const auto& item : merged)
	{
		if (item.curve)
			curves.push_back(item.q);
		else if (norm(sub(item.b, item.a)) > 1e-12)
			curves.push_back({ item.a, div(add(item.a, item.b), 2), item.b });
	}
	return { curves, inside };
}

double area(const QContour& curves)
{
	std::vector<Point> pts;
	for (const auto& q : curves)
		for (int j = 0; j < 8; ++j)
			pts.push_back(bezier(q, j * (1.0 / 8)));
	double sum = 0;
	for (size_t i = 0; i < pts.size(); ++i)
	{
		const Point& a = pts[i];
		const Point& b = pts[(i + 1) % pts.size()];
		sum += a[0] * b[1] - b[0] * a[1];
	}
	return 0.5 * sum;
}

std::vector<std::vector<QContour>> split_shape(const std::vector<QContour>& contours, double radius)
{
	double x0 = contours[0][0][0][0], x1 = x0, y0 = contours[0][0][0][1], y1 = y0;
	for (const auto& c : contours)
		for (const auto& q : c)
			for (const auto& p : q)
			{
				x0 = std::min(x0, p[0]);
				x1 = std::max(x1, p[0]);
				y0 = std::min(y0, p[1]);
				y1 = std::max(y1, p[1]);
			}
	const double size = 2 * radius / GRID;
	auto cell = [&](double v) { return std::min(std::max(int(std::floor((v + radius) / size)), 0), GRID - 1); };
	double whole = 0;
	for (const auto& c : contours)
		whole += area(c);
	whole = std::abs(whole);
	std::vector<std::vector<QContour>> pieces;
	for (int cy = cell(y0); cy <= cell(y1); ++cy)
		for (int cx = cell(x0); cx <= cell(x1); ++cx)
		{
			const Point lo{ cx * size - radius, cy * size - radius };
			const Point hi{ lo[0] + size, lo[1] + size };
			std::vector<QContour> piece;
			double total = 0;
			for (const auto& c : contours)
			{
				auto [curves, n] = clip_contour(c, lo, hi);
				if (!curves.empty() && (n || std::abs(area(curves)) > 1e-12))
				{
					total += area(curves);
					piece.push_back(curves);
				}
			}
			if (!piece.empty() && std::abs(total) > 1e-9 * std::max(whole, 1e-12))
				pieces.push_back(piece);
		}
	return pieces;
}

// --- format 12 layout (gamedata/shaders/r3/reticle_vector.h in the GAMMA patch) -----------------------------

struct Piece
{
	int role, mark;
	bool sfp, dial;
	std::array<int, 3> colour;
	int group;
	std::vector<QContour> contours;
};

void push16(std::vector<std::uint8_t>& b, int v)
{
	b.push_back(std::uint8_t(v & 255));
	b.push_back(std::uint8_t((v >> 8) & 255));
}

int checked(long long v, long long lo, long long hi, const char* what)
{
	if (v < lo || v > hi)
		throw Error(std::string(what) + " out of range");
	return int(v);
}

struct MaskImage
{
	std::vector<std::uint8_t> pixels;
	int width = 0, height = 0;
};

// the 36-byte settings record
std::vector<std::uint8_t> settings_record(const Settings& s, double radius, int marks)
{
	auto u8 = [](double v, double scale) { return std::uint8_t(checked(round_even(v * scale), 0, 255, "u8 setting")); };
	auto s8 = [](double v, double scale) { return std::uint8_t(checked(round_even(v * scale), -128, 127, "s8 setting") & 255); };
	auto u16 = [](double v, double scale) { return checked(round_even(v * scale), 0, 65535, "u16 setting"); };
	std::vector<std::uint8_t> r;
	push16(r, u16(s.calibrated_magnification, 100));
	push16(r, u16(radius, 16));
	r.push_back(std::uint8_t(marks));
	r.push_back(s.drawing_colours ? 1 : 0);
	push16(r, u16(s.legacy_span, 8));
	push16(r, u16(s.luminance, 16));
	r.push_back(u8(s.glow_strength, 32));
	r.push_back(u8(s.glow_size, 32));
	r.push_back(std::uint8_t(s.steps.size()));
	for (size_t i = 0; i < 8; ++i)
		r.push_back(i < s.steps.size() ? u8(s.steps[i], 255) : 0);
	const Hologram& h = s.hologram;
	for (std::uint8_t v : { u8(h.softness, 32), u8(h.level, 100), u8(h.ghost_level, 255), s8(h.ghost_x, 8), s8(h.ghost_y, 8),
			 u8(h.fringe, 100), u8(h.flicker, 255) })
		r.push_back(v);
	const Mask& m = s.mask;
	r.push_back(m.present ? u8(m.depth, 255) : 0);
	push16(r, m.present ? u16(m.size, 16) : 0);
	r.push_back(m.present ? u8(m.eye_shift, 100) : 0);
	r.push_back(m.present ? u8(m.animate, 16) : 0);
	r.push_back(u8(s.fibre, 32));
	push16(r, u16(s.tritium, 256));
	return r;
}

int code(double v, double scale)
{
	return std::max(-32767, std::min(32767, int(round_even(v / scale))));
}

int log2i(unsigned v)
{
	int n = 0;
	while ((1u << n) < v)
		++n;
	return n;
}

int byte_sum(const std::vector<std::uint8_t>& bytes)
{
	int sum = 0;
	for (auto b : bytes)
		sum += b;
	return sum;
}

Texture encode_atlas(const std::vector<Piece>& pieces, const std::vector<std::uint8_t>& record, int unit, int flags, const MaskImage* mask_image)
{
	if (pieces.empty() || (int)pieces.size() > MAX_PIECES)
		throw Error(std::to_string(pieces.size()) + " pieces; a vector reticle holds 1 to " + std::to_string(MAX_PIECES));
	const double scale = (record[2] | (record[3] << 8)) / 16.0 / 32767;

	// the pieces: role, flags (bit 0 sfp, bit 1 dial, bits 4-7 mark), r, g, b, 0, u16 group, u16 first curve, u16 curve count,
	// s16 bounds min x, min y, max x, max y
	std::vector<std::uint8_t> descriptors;
	std::vector<std::array<int, 2>> points;
	std::vector<std::array<int, 4>> bounds;
	for (const auto& s : pieces)
	{
		const size_t first = points.size() / 3;
		for (const auto& contour : s.contours)
		{
			const size_t start = points.size();
			for (const auto& q : contour)
			{
				std::array<std::array<int, 2>, 3> c;
				for (int k = 0; k < 3; ++k)
					c[k] = { code(q[k][0], scale), code(q[k][1], scale) };
				if (points.size() > start)
					c[0] = points.back();
				points.insert(points.end(), c.begin(), c.end());
			}
			points.back() = points[start];
		}
		int x0 = 32767, y0 = 32767, x1 = -32767, y1 = -32767;
		for (size_t i = first * 3; i < points.size(); ++i)
		{
			x0 = std::min(x0, points[i][0]);
			y0 = std::min(y0, points[i][1]);
			x1 = std::max(x1, points[i][0]);
			y1 = std::max(y1, points[i][1]);
		}
		bounds.push_back({ x0, y0, x1, y1 });
		descriptors.push_back(std::uint8_t(s.role));
		descriptors.push_back(std::uint8_t((s.sfp ? 1 : 0) | (s.dial ? 2 : 0) | (s.mark << 4)));
		for (int c : s.colour)
			descriptors.push_back(std::uint8_t(c));
		descriptors.push_back(0);
		push16(descriptors, s.group);
		push16(descriptors, int(first));
		push16(descriptors, int(points.size() / 3 - first));
		for (int v : { x0, y0, x1, y1 })
			push16(descriptors, v & 0xFFFF);
	}
	const size_t curves = points.size() / 3;
	if (curves > 65535)
		throw Error(std::to_string(curves) + " curves; a vector reticle holds at most 65535");

	// the shape grid: per cell, the pieces whose bounds come within one cell of it, each with its mark
	const double cell_size = 2 * 32767.0 / GRID;
	std::vector<std::vector<int>> members(GRID * GRID);
	for (size_t i = 0; i < bounds.size(); ++i)
	{
		const auto& b = bounds[i];
		const int fx = std::max(0, int(std::floor((b[0] + 32767) / cell_size)) - 1);
		const int fy = std::max(0, int(std::floor((b[1] + 32767) / cell_size)) - 1);
		const int lx = std::min(GRID - 1, int(std::floor((b[2] + 32767) / cell_size)) + 1);
		const int ly = std::min(GRID - 1, int(std::floor((b[3] + 32767) / cell_size)) + 1);
		for (int y = fy; y <= ly; ++y)
			for (int x = fx; x <= lx; ++x)
				members[y * GRID + x].push_back(int(i));
	}
	std::vector<std::uint8_t> cells, lists;
	for (const auto& listed : members)
	{
		push16(cells, int(lists.size() / 2));
		push16(cells, int(listed.size()));
		for (int i : listed)
			push16(lists, i | (pieces[i].mark << 12));
	}
	const size_t entries = lists.size() / 2;
	if (entries > 65535)
		throw Error("grid lists of " + std::to_string(entries) + " entries; a vector reticle holds at most 65535");

	std::vector<std::uint8_t> stream = record;
	stream.insert(stream.end(), descriptors.begin(), descriptors.end());
	for (const auto& p : points)
	{
		push16(stream, p[0] & 0xFFFF);
		push16(stream, p[1] & 0xFFFF);
	}
	stream.insert(stream.end(), cells.begin(), cells.end());
	stream.insert(stream.end(), lists.begin(), lists.end());
	stream.resize((stream.size() + 3) / 4 * 4, 0);

	const int rows = int((stream.size() / 4 + DESC_WIDTH - 1) / DESC_WIDTH);
	const int mask_w = mask_image ? mask_image->width : 0, mask_h = mask_image ? mask_image->height : 0;
	const int width = std::max(DESC_WIDTH, mask_w);
	const int mask_row = 1 + rows;
	const int height = 1 << std::max(4, log2i(unsigned(mask_row + mask_h)));

	Texture t;
	t.width = width;
	t.height = height;
	t.rgba.assign(size_t(width) * height * 4, 0);
	for (size_t i = 0; i < stream.size() / 4; ++i)
		std::memcpy(&t.rgba[((1 + i / DESC_WIDTH) * width + i % DESC_WIDTH) * 4], &stream[i * 4], 4);
	int mean = 0;
	if (mask_image)
	{
		double sum = 0;
		for (int y = 0; y < mask_h; ++y)
			for (int x = 0; x < mask_w; ++x)
			{
				std::uint8_t* texel = &t.rgba[((size_t(mask_row) + y) * width + x) * 4];
				texel[0] = mask_image->pixels[size_t(y) * mask_w + x];
				texel[3] = 255;
				sum += texel[0];
			}
		mean = int(std::max<long long>(1, round_even(sum / (double(mask_w) * mask_h))));
	}
	const int count = int(pieces.size());
	std::vector<std::uint8_t> head{ 'R', 'E', 'T', 'I', 'C', 'L', 'E', std::uint8_t(VERSION),
		std::uint8_t(log2i(width)), std::uint8_t(log2i(height)), std::uint8_t(unit), std::uint8_t(flags) };
	push16(head, count);
	push16(head, int(curves));
	push16(head, int(entries));
	head.push_back(std::uint8_t(mask_w ? log2i(mask_w) : 0));
	head.push_back(std::uint8_t(mask_h ? log2i(mask_h) : 0));
	push16(head, mask_row);
	head.push_back(std::uint8_t(mean));
	head.push_back(0);
	const int header_sum = byte_sum(head), record_sum = byte_sum(record);
	push16(head, record_sum & 0xFFFF);
	push16(head, header_sum & 0xFFFF);
	std::memcpy(t.rgba.data(), head.data(), head.size());
	return t;
}

// The mask as grey bytes (ITU-R 601-2 luma, fixed point)
MaskImage decode_mask(const std::vector<std::uint8_t>& file)
{
	int w = 0, h = 0, channels = 0;
	if (!stbi_info_from_memory(file.data(), int(file.size()), &w, &h, &channels))
		throw Error(std::string("mask image: ") + stbi_failure_reason());
	const bool grey = channels <= 2;
	std::uint8_t* data = stbi_load_from_memory(file.data(), int(file.size()), &w, &h, &channels, grey ? 1 : 3);
	if (!data)
		throw Error(std::string("mask image: ") + stbi_failure_reason());
	MaskImage m;
	m.width = w;
	m.height = h;
	m.pixels.resize(size_t(w) * h);
	for (size_t i = 0; i < m.pixels.size(); ++i)
	{
		if (grey)
			m.pixels[i] = data[i];
		else
		{
			const unsigned r = data[i * 3], g = data[i * 3 + 1], b = data[i * 3 + 2];
			m.pixels[i] = std::uint8_t((r * 19595 + g * 38470 + b * 7471 + 0x8000) >> 16);
		}
	}
	stbi_image_free(data);
	if (w < 2 || h < 2 || w > 1024 || h > 1024 || (w & (w - 1)) || (h & (h - 1)))
		throw Error("mask image: a power of two from 2 to 1024 in each direction, not " + std::to_string(w) + " x " + std::to_string(h));
	return m;
}
}

bool Build(const std::string& texture_name, const std::string& svg_text, const std::string& settings_text, const ReadFile& read,
	Texture& out, Report& report, std::vector<DrawnShape>* drawn_out)
{
	try
	{
		const Settings s = parse_settings(settings_text);
		SvgReticle svg = ReadSvg(svg_text);
		if (!svg.errors.empty())
		{
			report.errors.insert(report.errors.end(), svg.errors.begin(), svg.errors.end());
			return false;
		}
		const std::vector<Shape> shapes = reticle_shapes(svg, s);

		struct Drawn
		{
			const Shape* shape;
			std::vector<QContour> contours;
		};
		std::vector<Drawn> drawn;
		for (const auto& sh : shapes)
		{
			std::vector<QContour> quads;
			for (const auto& c : sh.contours)
				quads.push_back(quads_of(c));
			int dropped = 0;
			auto contours = oriented(quads, dropped);
			if (dropped)
				report.warnings.push_back(sh.source + ": " + std::to_string(dropped) + " of its " + std::to_string(quads.size()) +
					" contours lie inside its other contours or have no area, and draw nothing");
			if (!contours.empty())
				drawn.push_back({ &sh, contours });
		}
		if (drawn.empty())
			throw Error("no filled shapes to draw");

		double reach = 0;
		int marks = 1, flags = s.holo ? FLAG_HOLO : 0;
		for (const auto& d : drawn)
		{
			for (const auto& c : d.contours)
				for (const auto& q : c)
					for (const auto& p : q)
						reach = std::max({ reach, std::abs(p[0]), std::abs(p[1]) });
			marks = std::max(marks, d.shape->mark);
			flags |= d.shape->dial ? FLAG_DIAL_PARTS : (d.shape->sfp ? FLAG_SFP_PARTS : FLAG_FFP_PARTS);
			if (d.shape->role == ROLE_ILLUM || d.shape->role == ROLE_GLOW || d.shape->role == ROLE_LIGHT)
				flags |= FLAG_ILLUMINATES;
			if (d.shape->role == ROLE_PAINT || d.shape->role == ROLE_GLOW)
				flags |= FLAG_PAINT;
		}
		if (s.has_calibrated_magnification && !(flags & (FLAG_SFP_PARTS | FLAG_DIAL_PARTS)))
			throw Error("calibrated_magnification is for the parts in an 'sfp' or 'dial' layer; the drawing has none");
		if ((s.fibre > 0 || s.tritium > 0) && !(flags & FLAG_ILLUMINATES))
			throw Error("fibre and tritium light the 'illum', 'glow' and 'light' shapes; the drawing has none");
		if (s.has_calibrated_magnification && s.legacy_span > 0)
			throw Error("calibrated_magnification: with legacy_span the sfp parts follow the 3DSS lens and match the "
						"rest at the lowest zoom; remove one of the two");
		const double radius = (std::floor(reach * 16) + 1) / 16;
		const auto record = settings_record(s, radius, marks);

		std::vector<Piece> pieces;
		for (size_t i = 0; i < drawn.size(); ++i)
		{
			const Shape& sh = *drawn[i].shape;
			for (auto& contours : split_shape(drawn[i].contours, radius))
				pieces.push_back({ sh.role, sh.mark, sh.sfp, sh.dial, sh.colour, int(i), contours });
			if (drawn_out)
				drawn_out->push_back({ sh.role, sh.mark, drawn[i].contours });
		}

		MaskImage mask_image;
		if (s.mask.present)
		{
			const auto file = read ? read(s.mask.image) : std::vector<std::uint8_t>();
			if (file.empty())
				throw Error("mask image '" + s.mask.image + "' not found next to the settings");
			mask_image = decode_mask(file);
		}
		out = encode_atlas(pieces, record, s.units == "moa" ? 1 : 2, flags, s.mask.present ? &mask_image : nullptr);
		out.glow_size = float(s.glow_size);
		report.notes.push_back(std::to_string(drawn.size()) + " shapes in " + std::to_string(pieces.size()) + " pieces, " +
			std::to_string(out.width) + " x " + std::to_string(out.height) + " RGBA8" +
			((flags & FLAG_SFP_PARTS) ? ((flags & FLAG_FFP_PARTS) ? ", dual focal plane" : ", second focal plane") : "") +
			((flags & FLAG_DIAL_PARTS) ? ", turning dial" : ""));
		return true;
	}
	catch (const std::exception& e)
	{
		if (drawn_out)
			drawn_out->clear();
		report.errors.push_back(texture_name.empty() ? e.what() : texture_name + ": " + e.what());
		return false;
	}
}

std::vector<std::uint8_t> DdsFile(const Texture& t)
{
	std::vector<std::uint8_t> f{ 'D', 'D', 'S', ' ' };
	auto put32 = [&](std::uint32_t v) {
		for (int i = 0; i < 4; ++i)
			f.push_back(std::uint8_t(v >> (8 * i)));
	};
	for (std::uint32_t v : { 124u, 0x1u | 0x2u | 0x4u | 0x1000u | 0x8u, t.height, t.width, t.width * 4, 0u, 1u })
		put32(v);
	f.insert(f.end(), 44, 0);
	for (std::uint32_t v : { 32u, 0x41u, 0u, 32u, 0xFFu, 0xFF00u, 0xFF0000u, 0xFF000000u, 0x1000u, 0u, 0u, 0u, 0u })
		put32(v);
	f.insert(f.end(), t.rgba.begin(), t.rgba.end());
	return f;
}
}
