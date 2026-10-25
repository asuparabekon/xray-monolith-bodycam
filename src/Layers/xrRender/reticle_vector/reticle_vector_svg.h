// The SVG reader of the vector reticle build: the filled shapes of a reticle drawing by Inkscape layer.
// Layers "illum", "paint", "glow" and "light" give a shape its role; "mark N" (or "mark 1, 3") the Mark Switch
// marks it shows on; "sfp" makes it keep its size when the sight zooms (second focal plane); "dial" makes it keep
// its size and turn about the centre with the zoom (a magnification dial, as the 3DSS 1P59); "center" holds a
// mark whose middle is the bore centre and is not drawn. The layers nest in any order. Strokes, partial
// opacity, even-odd fills, gradients, masks, clip paths, filters and stylesheets are reported as errors.
#pragma once

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace reticle_vector
{
using Point = std::array<double, 2>;

// a path segment in user units, y down: kind 'L' (p[0], p[1]), 'Q' (p[0..2]) or 'C' (p[0..3])
struct Segment
{
	char kind;
	std::array<Point, 4> p;
	int points() const { return kind == 'L' ? 2 : (kind == 'Q' ? 3 : 4); }
	const Point& end() const { return p[points() - 1]; }
};
using Contour = std::vector<Segment>;

struct SvgShape
{
	std::string role; // illum, paint, glow or light
	std::array<int, 3> colour; // fill, sRGB bytes
	std::vector<Contour> contours;
	std::string source; // the element, for messages
	std::vector<int> marks; // Mark Switch marks 1..15 it shows on, empty = every mark
	bool sfp = false; // keeps its size with the zoom (second focal plane)
	bool dial = false; // keeps its size and turns with the zoom
};

struct SvgReticle
{
	std::array<double, 4> view_box{};
	std::vector<SvgShape> shapes;
	bool has_centre = false;
	Point centre{};
	std::vector<std::string> errors;
};

// Parses `text`; problems are collected in result.errors instead of stopping at the first one.
SvgReticle ReadSvg(const std::string& text);

// shared by the reader and the builder
struct Error : std::runtime_error
{
	using std::runtime_error::runtime_error;
};
std::string lower(std::string s);
std::string strip(const std::string& s, const char* chars = " \t\r\n");
}
