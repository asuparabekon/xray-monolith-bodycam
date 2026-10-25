// Vector reticles built from an SVG: a reticle drawing (<texture>.svg, layer rules in reticle_vector_svg.h) and
// its settings (<texture>.reticle.ini) become the reticle atlas format 12 texture that the GAMMA patch's
// reticle_vector.h draws (layout in that file's header comment). The only builder: the engine
// (r4_reticle_vector.cpp) builds it in memory and the command-line tool (tools/reticle_vector) writes it as .dds.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace reticle_vector
{
// The atlas layout the builder, the command-line tool's check and the engine's .dds reader share with the GAMMA
// patch's shaders (reticle_vector.h there documents every byte)
namespace layout
{
constexpr int VERSION = 12;
constexpr int RECORD_BYTES = 36;     // the settings record at the start of the descriptor stream
constexpr int PIECE_TEXELS = 5;      // descriptor texels per piece
constexpr int CURVE_TEXELS = 3;      // texels per quadratic curve
constexpr int DESC_WIDTH = 128;      // descriptor stream texels per row, from row 1
constexpr int GRID = 32;             // grid cells across the reticle each way
constexpr int GLOW_SIZE_BYTE = 11;   // settings record byte holding the glow size * 32
constexpr int ROLE_ILLUM = 1, ROLE_PAINT = 2, ROLE_GLOW = 3, ROLE_LIGHT = 4;
constexpr int FLAG_ILLUMINATES = 1, FLAG_PAINT = 2, FLAG_HOLO = 4, FLAG_FFP_PARTS = 8, FLAG_SFP_PARTS = 16, FLAG_DIAL_PARTS = 32;
}

// A file the settings name (the holographic mask), relative to the settings file; empty when it is missing.
using ReadFile = std::function<std::vector<std::uint8_t>(const std::string& relative)>;

struct Texture
{
	std::uint32_t width = 0, height = 0; // powers of two
	std::vector<std::uint8_t> rgba; // width * height texels, R first, row by row, one mip
	float glow_size = 1.f; // the authored glow size ([illumination] glow_size)
};

struct Report
{
	std::vector<std::string> errors; // any error means no texture
	std::vector<std::string> warnings; // the build went ahead, but something in the drawing draws nothing
	std::vector<std::string> notes; // what was built
};

// The drawn shapes before they are cut into pieces and encoded, for checking the encoded texture against them:
// quadratic curves ((x0, y0), (cx, cy), (x1, y1)) in reticle units (x right, y down), the filled side on the left.
using Quad = std::array<std::array<double, 2>, 3>;
struct DrawnShape
{
	int role; // 1 illum, 2 paint, 3 glow, 4 light
	int mark; // Mark Switch mark 1..15, 0 = every mark
	std::vector<std::vector<Quad>> contours;
};

// Builds the texture of `texture_name` (the game texture path, for messages). Returns false with the reasons in
// report.errors when the drawing or the settings are not usable. Reentrant: no state outside the call.
bool Build(const std::string& texture_name, const std::string& svg, const std::string& settings, const ReadFile& read, Texture& out,
	Report& report, std::vector<DrawnShape>* drawn = nullptr);

// The texture as a .dds file: uncompressed RGBA8, one mip.
std::vector<std::uint8_t> DdsFile(const Texture& texture);
}
