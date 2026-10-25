#pragma once
#include <cstdint>
#include <cstring>

#include "../xrRender/reticle_vector/reticle_vector.h"

namespace ReticleAtlas
{
struct Header
{
	bool atlas = false;   // a reticle atlas of this format (reticle_vector::layout::VERSION) the shaders draw
	bool retired = false; // an older reticle atlas (MSDF formats 2-8, vector formats 9-11): recognised, never drawn
	bool newer = false;   // a reticle atlas of a newer format than this engine and its shaders know: never drawn
	float glow_size = 0;  // the authored glow size (settings record byte 11 / 32, 1 when unset) of an atlas
};

// Reads the header of a reticle atlas .dds file (layout: the GAMMA patch's gamedata/shaders/r3/reticle_vector.h).
// Shader validation remains authoritative for everything else. The atlas is RGBA8, R in the low byte: the header
// in row 0 and the settings record from row 1. Older atlases began "RMSDFATL" (RGBA8, or BC5 with the bytes in
// the 4x4 blocks' endpoints R0, G0, R1, G1).
inline Header Read(const std::uint8_t* data, std::uint64_t length)
{
	Header header;
	auto read32 = [&](std::uint64_t offset) {
		std::uint32_t v = 0;
		if (offset + 4 <= length)
			std::memcpy(&v, data + offset, 4);
		return v;
	};
	const std::uint32_t width = read32(16);
	const bool dds = read32(0) == 0x20534444 /* "DDS " */ && width >= 8 && width <= 16384;
	const bool rgba8 =
		dds && (read32(80) & 0x40) && read32(88) == 32 && read32(92) == 0x000000FF && read32(96) == 0x0000FF00 && read32(100) == 0x00FF0000;
	const bool bc5 = dds && (read32(80) & 0x4) && read32(84) == 0x30315844 /* "DX10" */ && read32(128) == 83;
	if (rgba8 && length >= 128 + 8)
	{
		const std::uint8_t* head = data + 128;
		if (!std::memcmp(head, "RETICLE", 7))
		{
			if (head[7] != reticle_vector::layout::VERSION)
			{
				header.retired = head[7] < reticle_vector::layout::VERSION;
				header.newer = !header.retired;
				return header;
			}
			header.atlas = true;
			header.glow_size = 1.f;
			const std::uint64_t record = 128 + std::uint64_t(width) * 4; // row 1
			const int glow = reticle_vector::layout::GLOW_SIZE_BYTE;
			if (record + glow + 1 <= length && data[record + glow])
				header.glow_size = data[record + glow] / 32.f;
			return header;
		}
		header.retired = !std::memcmp(head, "RMSDFATL", 8);
		return header;
	}
	if (bc5 && length >= 148 + 32)
	{
		const std::uint8_t* b0 = data + 148;
		const std::uint8_t* b1 = data + 148 + 16;
		const std::uint8_t r0[4] = { b0[0], b0[8], b0[1], b0[9] }, r1[4] = { b1[0], b1[8], b1[1], b1[9] };
		header.retired = !std::memcmp(r0, "RMSD", 4) && !std::memcmp(r1, "FATL", 4);
	}
	return header;
}
}
