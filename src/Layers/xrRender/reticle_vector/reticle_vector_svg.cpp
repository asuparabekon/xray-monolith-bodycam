// SVG reader of the vector reticle build (layer rules in reticle_vector_svg.h): path data, basic shapes,
// transforms, solid fills and the Inkscape layers.
#include "reticle_vector_svg.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>
#include <utility>

#include "../../../3rd party/pugixml/pugixml.hpp"

namespace reticle_vector
{
namespace
{
const char* const SVG_NS = "http://www.w3.org/2000/svg";
const char* const INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape";
const double PI = 3.14159265358979323846;
const int MAX_MARK = 15;

// the points of a segment sampled n + 1 times ('L': its two ends)
std::vector<Point> segment_points(const Segment& s, int n)
{
	if (s.kind == 'L')
		return { s.p[0], s.p[1] };
	std::vector<Point> pts;
	for (int j = 0; j <= n; ++j)
	{
		const double t = double(j) / n, u = 1 - t;
		if (s.kind == 'Q')
		{
			const Point &p0 = s.p[0], &c = s.p[1], &p1 = s.p[2];
			pts.push_back({ u * u * p0[0] + 2 * u * t * c[0] + t * t * p1[0], u * u * p0[1] + 2 * u * t * c[1] + t * t * p1[1] });
		}
		else
		{
			const Point &p0 = s.p[0], &c0 = s.p[1], &c1 = s.p[2], &p1 = s.p[3];
			const double u3 = std::pow(u, 3), t3 = std::pow(t, 3);
			pts.push_back({ u3 * p0[0] + 3 * u * u * t * c0[0] + 3 * u * t * t * c1[0] + t3 * p1[0],
				u3 * p0[1] + 3 * u * u * t * c0[1] + 3 * u * t * t * c1[1] + t3 * p1[1] });
		}
	}
	return pts;
}

double radians(double x)
{
	return x * (PI / 180.0);
}

using Matrix = std::array<double, 6>;
const Matrix IDENTITY{ 1.0, 0.0, 0.0, 1.0, 0.0, 0.0 };

Matrix mat_mul(const Matrix& a, const Matrix& b)
{
	return { a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
		a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5] };
}

Point transform_point(const Matrix& m, const Point& p)
{
	return { m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5] };
}

bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

// the length of the number at i (sign, digits, fraction, exponent), 0 when there is none
size_t number_length(const std::string& s, size_t i)
{
	size_t j = i;
	if (j < s.size() && (s[j] == '-' || s[j] == '+'))
		++j;
	const size_t digits = j;
	while (j < s.size() && is_digit(s[j]))
		++j;
	if (j > digits)
	{
		if (j < s.size() && s[j] == '.')
		{
			++j;
			while (j < s.size() && is_digit(s[j]))
				++j;
		}
	}
	else
	{
		if (j + 1 < s.size() && s[j] == '.' && is_digit(s[j + 1]))
		{
			j += 2;
			while (j < s.size() && is_digit(s[j]))
				++j;
		}
		else
			return 0;
	}
	if (j < s.size() && (s[j] == 'e' || s[j] == 'E'))
	{
		size_t k = j + 1;
		if (k < s.size() && (s[k] == '-' || s[k] == '+'))
			++k;
		if (k < s.size() && is_digit(s[k]))
		{
			while (k < s.size() && is_digit(s[k]))
				++k;
			j = k;
		}
	}
	return j - i;
}

double finite_number(const std::string& text)
{
	std::string t = strip(text);
	if (!t.empty() && t[0] == '+')
		t.erase(0, 1);
	double v = 0;
	const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
	if (t.empty() || r.ec != std::errc() || r.ptr != t.data() + t.size())
	{
		if (r.ec == std::errc::result_out_of_range)
			throw Error("coordinates and values must be finite");
		throw Error("'" + text + "' is not a number");
	}
	if (!std::isfinite(v))
		throw Error("coordinates and values must be finite");
	return v;
}

// numbers (and, with commands, path command letters) separated only by whitespace and commas
std::vector<std::string> numeric_tokens(const std::string& text, bool commands = false)
{
	static const std::string COMMANDS = "MmLlHhVvCcSsQqTtAaZz";
	std::vector<std::string> tokens;
	size_t i = 0;
	while (i < text.size())
	{
		const char c = text[i];
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',')
		{
			++i;
			continue;
		}
		if (commands && COMMANDS.find(c) != std::string::npos)
		{
			tokens.emplace_back(1, c);
			++i;
			continue;
		}
		const size_t n = number_length(text, i);
		if (!n)
			throw Error("invalid numeric or path syntax");
		tokens.push_back(text.substr(i, n));
		i += n;
	}
	return tokens;
}

Matrix parse_transform(const std::string& text)
{
	Matrix m = IDENTITY;
	size_t i = 0;
	while (true)
	{
		const size_t open = text.find('(', i);
		if (open == std::string::npos)
			break;
		const size_t close = text.find(')', open);
		if (close == std::string::npos)
			break;
		// the transform's name ends where the whitespace before its parenthesis begins
		size_t end = open;
		while (end > i && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
			--end;
		size_t begin = end;
		while (begin > i && (std::isalnum((unsigned char)text[begin - 1]) || text[begin - 1] == '_'))
			--begin;
		if (begin == end || !strip(text.substr(i, begin - i), " \t\r\n,").empty())
			throw Error("invalid transform syntax");
		const std::string name = text.substr(begin, end - begin);
		std::vector<double> v;
		for (const auto& t : numeric_tokens(text.substr(open + 1, close - open - 1)))
			v.push_back(finite_number(t));
		const size_t n = v.size();
		Matrix t;
		auto bad = [&] { throw Error("invalid " + name + " transform arguments"); };
		if (name == "matrix")
		{
			if (n != 6)
				bad();
			t = { v[0], v[1], v[2], v[3], v[4], v[5] };
		}
		else if (name == "translate")
		{
			if (n != 1 && n != 2)
				bad();
			t = { 1, 0, 0, 1, v[0], n > 1 ? v[1] : 0 };
		}
		else if (name == "scale")
		{
			if (n != 1 && n != 2)
				bad();
			t = { v[0], 0, 0, n > 1 ? v[1] : v[0], 0, 0 };
		}
		else if (name == "rotate")
		{
			if (n != 1 && n != 3)
				bad();
			const double a = radians(v[0]);
			t = { std::cos(a), std::sin(a), -std::sin(a), std::cos(a), 0, 0 };
			if (n == 3)
				t = mat_mul(mat_mul({ 1, 0, 0, 1, v[1], v[2] }, t), { 1, 0, 0, 1, -v[1], -v[2] });
		}
		else if (name == "skewX")
		{
			if (n != 1)
				bad();
			t = { 1, 0, std::tan(radians(v[0])), 1, 0, 0 };
		}
		else if (name == "skewY")
		{
			if (n != 1)
				bad();
			t = { 1, std::tan(radians(v[0])), 0, 1, 0, 0 };
		}
		else
			bad();
		m = mat_mul(m, t);
		i = close + 1;
	}
	if (!strip(text.substr(std::min(i, text.size())), " \t\r\n,").empty())
		throw Error("invalid transform");
	for (double v : m)
		if (!std::isfinite(v))
			throw Error("invalid transform");
	return m;
}

using Style = std::map<std::string, std::string>;

Style style_of(const pugi::xml_node& el)
{
	static const char* const KEYS[] = { "fill", "fill-opacity", "opacity", "stroke", "stroke-opacity", "fill-rule", "mask", "clip-path",
		"filter", "display", "visibility" };
	Style style;
	for (const char* key : KEYS)
		if (const auto a = el.attribute(key))
			style[key] = strip(a.value());
	const std::string inline_style = el.attribute("style").value();
	size_t i = 0;
	while (i <= inline_style.size())
	{
		size_t j = inline_style.find(';', i);
		if (j == std::string::npos)
			j = inline_style.size();
		const std::string item = inline_style.substr(i, j - i);
		const size_t colon = item.find(':');
		if (colon != std::string::npos)
			style[strip(item.substr(0, colon))] = strip(item.substr(colon + 1));
		i = j + 1;
	}
	return style;
}

std::array<int, 3> parse_colour(const std::string& value)
{
	const std::string text = lower(strip(value));
	static const std::map<std::string, std::array<int, 3>> NAMED{ { "red", { 255, 0, 0 } }, { "blue", { 0, 0, 255 } },
		{ "black", { 0, 0, 0 } }, { "white", { 255, 255, 255 } }, { "lime", { 0, 255, 0 } }, { "green", { 0, 128, 0 } },
		{ "yellow", { 255, 255, 0 } } };
	if (const auto it = NAMED.find(text); it != NAMED.end())
		return it->second;
	auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
	auto all_hex = [](const std::string& s)
	{ return std::all_of(s.begin(), s.end(), [](char c) { return is_digit(c) || (c >= 'a' && c <= 'f'); }); };
	if (text.size() == 7 && text[0] == '#' && all_hex(text.substr(1)))
		return { hex(text[1]) * 16 + hex(text[2]), hex(text[3]) * 16 + hex(text[4]), hex(text[5]) * 16 + hex(text[6]) };
	if (text.size() == 4 && text[0] == '#' && all_hex(text.substr(1)))
		return { hex(text[1]) * 17, hex(text[2]) * 17, hex(text[3]) * 17 };
	// rgb(r, g, b)
	if (text.size() > 5 && text.compare(0, 4, "rgb(") == 0 && text.back() == ')')
	{
		std::array<int, 3> c{};
		const std::string inner = text.substr(4, text.size() - 5);
		size_t pos = 0;
		bool ok = true;
		for (int k = 0; k < 3 && ok; ++k)
		{
			const size_t comma = k < 2 ? inner.find(',', pos) : inner.size();
			if (comma == std::string::npos)
			{
				ok = false;
				break;
			}
			const std::string part = strip(inner.substr(pos, comma - pos));
			ok = !part.empty() && part.size() < 10 && std::all_of(part.begin(), part.end(), is_digit);
			if (ok)
				c[k] = std::stoi(part);
			pos = comma + 1;
		}
		if (ok && std::all_of(c.begin(), c.end(), [](int v) { return v >= 0 && v <= 255; }))
			return c;
	}
	throw Error("unsupported fill colour '" + value + "'");
}

double opacity_value(const Style& style, const char* key)
{
	const auto it = style.find(key);
	std::string text = it == style.end() ? "1" : it->second;
	const double value = !text.empty() && text.back() == '%' ? finite_number(text.substr(0, text.size() - 1)) / 100 : finite_number(text);
	if (!(value >= 0 && value <= 1))
		throw Error(std::string(key) + " must be between 0 and 1");
	return value;
}

std::string get(const Style& style, const char* key, const char* fallback)
{
	const auto it = style.find(key);
	return it == style.end() ? fallback : it->second;
}

std::vector<Segment> arc_to_cubics(Point p0, double rx, double ry, double phi, bool large, bool sweep, Point p1)
{
	if (rx == 0 || ry == 0 || p0 == p1)
		return { Segment{ 'L', { p0, p1 } } };
	const double cp = std::cos(phi), sp = std::sin(phi);
	const double dx = (p0[0] - p1[0]) / 2, dy = (p0[1] - p1[1]) / 2;
	const double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
	rx = std::abs(rx);
	ry = std::abs(ry);
	const double lam = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
	if (lam > 1)
	{
		rx = rx * std::sqrt(lam);
		ry = ry * std::sqrt(lam);
	}
	const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
	const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	const double co = std::sqrt(std::max(0.0, num / den)) * (large == sweep ? -1 : 1);
	const double cx1 = co * rx * y1 / ry, cy1 = -co * ry * x1 / rx;
	const double cx = cp * cx1 - sp * cy1 + (p0[0] + p1[0]) / 2;
	const double cy = sp * cx1 + cp * cy1 + (p0[1] + p1[1]) / 2;
	auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
	const double t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
	double dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
	if (!sweep && dt > 0)
		dt -= 2 * PI;
	else if (sweep && dt < 0)
		dt += 2 * PI;
	const int n = std::max(1, int(std::ceil(std::abs(dt) / (PI / 2) - 1e-9)));
	const double step = dt / n;
	const double k = 4.0 / 3.0 * std::tan(step / 4);
	auto point = [&](double t)
	{
		const double x = rx * std::cos(t), y = ry * std::sin(t);
		return Point{ cp * x - sp * y + cx, sp * x + cp * y + cy };
	};
	auto deriv = [&](double t)
	{
		const double x = -rx * std::sin(t), y = ry * std::cos(t);
		return Point{ cp * x - sp * y, sp * x + cp * y };
	};
	std::vector<Segment> out;
	Point start = p0;
	for (int i = 0; i < n; ++i)
	{
		const double a = t1 + i * step, b = t1 + (i + 1) * step;
		const Point end = i == n - 1 ? p1 : point(b);
		const Point da = deriv(a), db = deriv(b);
		out.push_back(Segment{
			'C', { start, Point{ start[0] + k * da[0], start[1] + k * da[1] }, Point{ end[0] - k * db[0], end[1] - k * db[1] }, end } });
		start = end;
	}
	return out;
}

std::vector<Contour> parse_path(const std::string& d)
{
	const auto tokens = numeric_tokens(d, true);
	std::vector<Contour> contours;
	Contour segs;
	size_t i = 0;
	char cmd = 0, last_cmd = 0;
	Point cur{ 0.0, 0.0 }, start{ 0.0, 0.0 }, last_ctrl{ 0.0, 0.0 };
	auto is_alpha = [](const std::string& t) { return std::isalpha((unsigned char)t[0]) != 0; };
	auto num = [&]()
	{
		if (i >= tokens.size() || is_alpha(tokens[i]))
			throw Error(std::string("missing coordinates for path command ") + cmd);
		return finite_number(tokens[i++]);
	};
	auto close = [&]()
	{
		if (!segs.empty())
		{
			if (segs.back().end() != start)
				segs.push_back(Segment{ 'L', { segs.back().end(), start } });
			contours.push_back(segs);
		}
		segs.clear();
	};
	while (i < tokens.size())
	{
		if (is_alpha(tokens[i]))
		{
			cmd = tokens[i][0];
			++i;
			if (!last_cmd && cmd != 'M' && cmd != 'm')
				throw Error("a path must begin with M or m");
			if (cmd == 'Z' || cmd == 'z')
			{
				close();
				cur = start;
				last_cmd = cmd;
				cmd = 0;
				continue;
			}
		}
		else if (!cmd)
			throw Error("path data does not start with a command");
		const bool rel = cmd >= 'a' && cmd <= 'z';
		const char c = char(rel ? cmd - 'a' + 'A' : cmd);
		if (!last_cmd && c != 'M')
			throw Error("a path must begin with M or m");
		const double ox = rel ? cur[0] : 0.0, oy = rel ? cur[1] : 0.0;
		Point p{};
		if (c == 'M')
		{
			close();
			const double x = ox + num();
			const double y = oy + num();
			cur = start = Point{ x, y };
			cmd = rel ? 'l' : 'L';
			last_cmd = 'M';
			continue;
		}
		if (c == 'L')
		{
			const double x = ox + num();
			p = Point{ x, oy + num() };
			segs.push_back(Segment{ 'L', { cur, p } });
		}
		else if (c == 'H')
		{
			p = Point{ (rel ? cur[0] : 0.0) + num(), cur[1] };
			segs.push_back(Segment{ 'L', { cur, p } });
		}
		else if (c == 'V')
		{
			p = Point{ cur[0], (rel ? cur[1] : 0.0) + num() };
			segs.push_back(Segment{ 'L', { cur, p } });
		}
		else if (c == 'C' || c == 'S')
		{
			Point c0;
			if (c == 'C')
			{
				const double x = ox + num();
				c0 = Point{ x, oy + num() };
			}
			else
			{
				const bool reflect = last_cmd == 'C' || last_cmd == 'c' || last_cmd == 'S' || last_cmd == 's';
				c0 = reflect ? Point{ 2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1] } : cur;
			}
			const double c1x = ox + num();
			const Point c1{ c1x, oy + num() };
			const double px = ox + num();
			p = Point{ px, oy + num() };
			segs.push_back(Segment{ 'C', { cur, c0, c1, p } });
			last_ctrl = c1;
		}
		else if (c == 'Q' || c == 'T')
		{
			Point q;
			if (c == 'Q')
			{
				const double x = ox + num();
				q = Point{ x, oy + num() };
			}
			else
			{
				const bool reflect = last_cmd == 'Q' || last_cmd == 'q' || last_cmd == 'T' || last_cmd == 't';
				q = reflect ? Point{ 2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1] } : cur;
			}
			const double px = ox + num();
			p = Point{ px, oy + num() };
			segs.push_back(Segment{ 'Q', { cur, q, p } });
			last_ctrl = q;
		}
		else if (c == 'A')
		{
			const double rx = num(), ry = num(), phi = radians(num());
			const double large = num(), sweep = num();
			if ((large != 0 && large != 1) || (sweep != 0 && sweep != 1))
				throw Error("arc flags must be 0 or 1");
			const double px = ox + num();
			p = Point{ px, oy + num() };
			for (const auto& s : arc_to_cubics(cur, rx, ry, phi, large == 1, sweep == 1, p))
				segs.push_back(s);
		}
		cur = p;
		last_cmd = cmd;
	}
	close();
	return contours;
}

std::vector<Contour> ellipse_contours(double cx, double cy, double rx, double ry)
{
	const double k = 4.0 / 3.0 * (std::sqrt(2.0) - 1);
	Contour segs;
	for (int j = 0; j < 4; ++j)
	{
		const double a = j * PI / 2, b = (j + 1) * PI / 2;
		const Point pa{ cx + rx * std::cos(a), cy + ry * std::sin(a) };
		const Point pb{ cx + rx * std::cos(b), cy + ry * std::sin(b) };
		const Point da{ -rx * std::sin(a), ry * std::cos(a) };
		const Point db{ -rx * std::sin(b), ry * std::cos(b) };
		segs.push_back(
			Segment{ 'C', { pa, Point{ pa[0] + k * da[0], pa[1] + k * da[1] }, Point{ pb[0] - k * db[0], pb[1] - k * db[1] }, pb } });
	}
	return { segs };
}

std::vector<Contour> element_contours(const pugi::xml_node& el, const std::string& tag)
{
	auto f = [&](const char* key)
	{
		const auto a = el.attribute(key);
		return a ? finite_number(a.value()) : 0.0;
	};
	if (tag == "path")
		return parse_path(el.attribute("d").value());
	if (tag == "rect")
	{
		const double x = f("x"), y = f("y"), w = f("width"), h = f("height");
		if (*el.attribute("rx").value() || *el.attribute("ry").value())
			throw Error("rounded rect: convert to path in Inkscape (Object to Path)");
		const Point p[4] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } };
		Contour c;
		for (int j = 0; j < 4; ++j)
			c.push_back(Segment{ 'L', { p[j], p[(j + 1) % 4] } });
		return { c };
	}
	if (tag == "circle")
		return ellipse_contours(f("cx"), f("cy"), f("r"), f("r"));
	if (tag == "ellipse")
		return ellipse_contours(f("cx"), f("cy"), f("rx"), f("ry"));
	if (tag == "polygon")
	{
		std::vector<double> v;
		for (const auto& t : numeric_tokens(el.attribute("points").value()))
			v.push_back(finite_number(t));
		if (v.size() < 6 || v.size() % 2)
			throw Error("a polygon needs at least three coordinate pairs");
		const size_t n = v.size() / 2;
		Contour c;
		for (size_t j = 0; j < n; ++j)
			c.push_back(Segment{ 'L', { Point{ v[2 * j], v[2 * j + 1] }, Point{ v[2 * ((j + 1) % n)], v[2 * ((j + 1) % n) + 1] } } });
		return { c };
	}
	throw Error("unsupported element <" + tag + ">; convert it to a path");
}

// namespace prefixes in scope, resolved like ElementTree resolves {uri}name
using Namespaces = std::map<std::string, std::string>;

Namespaces scope(const pugi::xml_node& el, Namespaces ns)
{
	for (const auto a : el.attributes())
	{
		const std::string name = a.name();
		if (name == "xmlns")
			ns[""] = a.value();
		else if (name.compare(0, 6, "xmlns:") == 0)
			ns[name.substr(6)] = a.value();
	}
	return ns;
}

// {uri, local name} of an element or attribute name; attributes without a prefix have no namespace
std::pair<std::string, std::string> qualified(const std::string& name, const Namespaces& ns, bool attribute)
{
	const size_t colon = name.find(':');
	const std::string prefix = colon == std::string::npos ? "" : name.substr(0, colon);
	const std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
	if (prefix.empty() && attribute)
		return { "", local };
	const auto it = ns.find(prefix);
	return { it == ns.end() ? "" : it->second, local };
}

std::string attribute_ns(const pugi::xml_node& el, const Namespaces& ns, const char* uri, const char* local)
{
	for (const auto a : el.attributes())
	{
		const auto q = qualified(a.name(), ns, true);
		if (q.first == uri && q.second == local)
			return a.value();
	}
	return {};
}

// "mark 1" or "mark 1, 3": the marks, empty when the label is not a mark list
std::vector<int> match_marks(const std::string& label)
{
	if (label.compare(0, 4, "mark") != 0 || label.size() == 4 || !std::isspace((unsigned char)label[4]))
		return {};
	std::vector<int> marks;
	size_t i = 4;
	while (true)
	{
		while (i < label.size() && std::isspace((unsigned char)label[i]))
			++i;
		size_t j = i;
		while (j < label.size() && is_digit(label[j]))
			++j;
		if (j == i || j - i > 2)
			return {};
		marks.push_back(std::stoi(label.substr(i, j - i)));
		while (j < label.size() && std::isspace((unsigned char)label[j]))
			++j;
		if (j == label.size())
			return marks;
		if (label[j] != ',')
			return {};
		i = j + 1;
	}
}

struct Walker
{
	SvgReticle& result;
	std::vector<Point> centre_points;

	// what the layers around an element say about its shapes
	struct Layers
	{
		std::string role;
		std::vector<int> marks;
		bool sfp = false, dial = false, centre = false;
	};

	static std::string local_tag(const pugi::xml_node& el, const Namespaces& ns, std::string* uri = nullptr)
	{
		const auto q = qualified(el.name(), ns, false);
		if (uri)
			*uri = q.first;
		return q.second;
	}

	std::string element_name(const pugi::xml_node& el, const Namespaces& ns) const
	{
		const std::string label = attribute_ns(el, ns, INKSCAPE_NS, "label");
		const std::string id = el.attribute("id").value();
		const std::string name = !id.empty() ? id : local_tag(el, ns);
		return label.empty() ? name : name + " (" + label + ")";
	}

	void walk(const pugi::xml_node& el, Namespaces ns, const Matrix& m, const Layers& layers, double opacity, const Style& parent_style)
	{
		ns = scope(el, ns);
		try
		{
			walk_element(el, ns, m, layers, opacity, parent_style);
		}
		catch (const std::exception& e)
		{
			result.errors.push_back(element_name(el, ns) + ": " + e.what());
		}
	}

	void walk_element(const pugi::xml_node& el, const Namespaces& ns, Matrix m, Layers layers, double inherited_opacity,
		const Style& parent_style)
	{
		static const char* const IGNORED[] = { "defs", "namedview", "metadata", "title", "desc", "style" };
		static const char* const INHERITED[] = { "fill", "fill-opacity", "stroke", "stroke-opacity", "fill-rule", "visibility" };
		const std::string tag = local_tag(el, ns);
		for (const char* t : IGNORED)
			if (tag == t)
				return;
		Style style;
		for (const char* key : INHERITED)
			if (const auto it = parent_style.find(key); it != parent_style.end())
				style[key] = it->second;
		for (const auto& kv : style_of(el))
			if (kv.second != "inherit")
				style[kv.first] = kv.second;
		if (get(style, "display", "") == "none" || get(style, "visibility", "") == "hidden")
			return;
		const double opacity = inherited_opacity * opacity_value(style, "opacity");
		m = mat_mul(m, parse_transform(el.attribute("transform").value()));
		for (const char* key : { "mask", "clip-path", "filter" })
			if (get(style, key, "none") != "none")
				throw Error(std::string("uses a ") + key + "; remove it, reticles are plain solid shapes");
		if (tag == "g" || tag == "svg")
		{
			const std::string label = lower(strip(attribute_ns(el, ns, INKSCAPE_NS, "label")));
			if (attribute_ns(el, ns, INKSCAPE_NS, "groupmode") == "layer")
			{
				const std::vector<int> marks = match_marks(label);
				if (label == "illum" || label == "paint" || label == "glow" || label == "light")
					layers.role = label;
				else if (label == "sfp")
				{
					if (layers.sfp)
					{
						result.errors.push_back("layer 'sfp' inside another 'sfp' layer");
						return;
					}
					layers.sfp = true;
				}
				else if (label == "dial")
				{
					if (layers.dial)
					{
						result.errors.push_back("layer 'dial' inside another 'dial' layer");
						return;
					}
					layers.dial = true;
				}
				else if (label == "center" || label == "centre")
					layers.centre = true;
				else if (!marks.empty())
				{
					for (int number : marks)
						if (number < 1 || number > MAX_MARK)
						{
							result.errors.push_back("layer '" + label + "': marks are numbered 1 to " + std::to_string(MAX_MARK));
							return;
						}
					if (!layers.marks.empty())
					{
						result.errors.push_back("layer '" + label + "': a mark layer can not sit inside another mark layer");
						return;
					}
					layers.marks = marks;
				}
				else if (label.compare(0, 4, "mark") == 0)
				{
					result.errors.push_back("layer '" + label + "': name it like 'mark 1' or 'mark 1, 3'");
					return;
				}
				else if (label.compare(0, 8, "calibrat") == 0)
				{
					result.errors.push_back("layer '" + label + "': the drawing's scale is 'scale' in the .reticle.ini");
					return;
				}
			}
			for (const auto child : el.children())
				if (child.type() == pugi::node_element)
					walk(child, ns, m, layers, opacity, style);
			return;
		}
		static const char* const SHAPES[] = { "path", "rect", "circle", "ellipse", "polygon" };
		const bool shape = std::any_of(std::begin(SHAPES), std::end(SHAPES), [&](const char* t) { return tag == t; });
		if (!shape)
		{
			result.errors.push_back(element_name(el, ns) + ": <" + tag + "> is not supported; convert it with Path > Object to Path");
			return;
		}
		std::vector<Contour> contours;
		try
		{
			contours = element_contours(el, tag);
			for (auto& c : contours)
				for (auto& s : c)
					for (int k = 0; k < s.points(); ++k)
					{
						s.p[k] = transform_point(m, s.p[k]);
						if (!std::isfinite(s.p[k][0]) || !std::isfinite(s.p[k][1]))
							throw Error("transformed coordinates must be finite");
					}
		}
		catch (const Error& e)
		{
			result.errors.push_back(element_name(el, ns) + ": " + e.what());
			return;
		}
		if (layers.centre)
		{
			for (const auto& c : contours)
				for (const auto& s : c)
					for (const auto& p : segment_points(s, 8))
						centre_points.push_back(p);
			return;
		}
		const std::string name = element_name(el, ns);
		const std::string fill = get(style, "fill", "#000000");
		if (fill.compare(0, 4, "url(") == 0)
		{
			result.errors.push_back(name + ": uses a gradient or pattern fill; use a solid fill");
			return;
		}
		if (get(style, "stroke", "none") != "none")
		{
			result.errors.push_back(name + ": has a stroke; use Path > Stroke to Path and give it a fill");
			return;
		}
		if (fill == "none")
			return;
		if (std::abs(opacity * opacity_value(style, "fill-opacity") - 1.0) > 1e-6)
		{
			result.errors.push_back(name + ": is partly transparent; use full opacity");
			return;
		}
		if (get(style, "fill-rule", "nonzero") != "nonzero")
		{
			result.errors.push_back(name + ": uses the even-odd fill rule; set Fill rule to nonzero in Fill and Stroke");
			return;
		}
		std::array<int, 3> colour;
		try
		{
			colour = parse_colour(fill);
		}
		catch (const Error& e)
		{
			result.errors.push_back(name + ": " + e.what());
			return;
		}
		if (layers.role.empty())
		{
			result.errors.push_back(name + ": no role layer; move this shape into an Inkscape layer named illum, paint, glow or light");
			return;
		}
		if (!contours.empty())
			result.shapes.push_back(SvgShape{ layers.role, colour, contours, name, layers.marks, layers.sfp, layers.dial });
	}
};

bool has_stylesheet(const pugi::xml_node& el, Namespaces ns)
{
	ns = scope(el, ns);
	std::string uri;
	if (Walker::local_tag(el, ns, &uri) == "style" && uri == SVG_NS && !strip(el.child_value()).empty())
		return true;
	for (const auto child : el.children())
		if (child.type() == pugi::node_element && has_stylesheet(child, ns))
			return true;
	return false;
}
}


std::string lower(std::string s)
{
	for (char& c : s)
		c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	return s;
}

std::string strip(const std::string& s, const char* chars)
{
	const auto b = s.find_first_not_of(chars);
	if (b == std::string::npos)
		return {};
	return s.substr(b, s.find_last_not_of(chars) - b + 1);
}

SvgReticle ReadSvg(const std::string& text)
{
	SvgReticle result;
	pugi::xml_document doc;
	const auto parsed = doc.load_buffer(text.data(), text.size(), pugi::parse_default | pugi::parse_ws_pcdata_single);
	if (!parsed)
	{
		result.errors.push_back(std::string("not a readable SVG: ") + parsed.description());
		return result;
	}
	const pugi::xml_node root = doc.document_element();
	const Namespaces ns = scope(root, {});
	std::string uri;
	if (Walker::local_tag(root, ns, &uri) != "svg" || uri != SVG_NS)
	{
		result.errors.push_back("not an SVG document");
		return result;
	}
	std::vector<double> vb;
	try
	{
		for (const auto& t : numeric_tokens(root.attribute("viewBox").value()))
			vb.push_back(finite_number(t));
	}
	catch (const Error&)
	{
		vb.clear();
	}
	if (vb.size() != 4 || vb[2] <= 0 || vb[3] <= 0)
	{
		result.errors.push_back("the SVG needs a viewBox (Inkscape: Document Properties, set a viewBox)");
		return result;
	}
	result.view_box = { vb[0], vb[1], vb[2], vb[3] };
	if (has_stylesheet(root, {}))
		result.errors.push_back("CSS stylesheets are not supported; apply styles directly to shapes or groups");

	Walker walker{ result, {} };
	walker.walk(root, {}, IDENTITY, {}, 1.0, {});
	if (result.shapes.empty() && result.errors.empty())
		result.errors.push_back("the SVG has no visible reticle shapes; add filled shapes to an illum, paint, glow or light layer");

	if (!walker.centre_points.empty())
	{
		double x0 = walker.centre_points[0][0], x1 = x0, y0 = walker.centre_points[0][1], y1 = y0;
		for (const auto& p : walker.centre_points)
		{
			x0 = std::min(x0, p[0]);
			x1 = std::max(x1, p[0]);
			y0 = std::min(y0, p[1]);
			y1 = std::max(y1, p[1]);
		}
		result.has_centre = true;
		result.centre = { (x0 + x1) / 2, (y0 + y1) / 2 };
	}
	return result;
}
}
