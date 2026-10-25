#include "stdafx.h"
#include "r4_reticle_atlas.h"
#include "reticle_atlas.h"

// Authored glow size of a reticle atlas, read once per texture from its file: 0 = not an atlas, else the scale on
// the glow lobes (1 when unset). A vector reticle built from SVG (r4_reticle_vector.cpp) sets it while loading;
// until the texture has loaded nothing is read or cached, so a .dds beside that .svg cannot claim it first.
// An older atlas (MSDF, vector formats 9-11) or a newer one is logged once, when it is first drawn: the shaders
// recognise it and draw nothing.
float reticle_atlas_glow_size(CTexture* texture)
{
	// IsLoading() first: its acquire publishes the worker's writes, bLoaded among them.
	if (!texture || !texture->cName.size() || *texture->cName.c_str() == '$' || texture->IsLoading() || !texture->flags.bLoaded)
		return 0.f;
	float& size = texture->reticle_glow_size;
	if (size >= 0.f)
		return size;
	size = 0.f;
	string_path file;
	if (!FS.exist(file, "$game_textures$", texture->cName.c_str(), ".dds"))
		return size;
	IReader* F = FS.r_open(file);
	if (!F)
		return size;
	const ReticleAtlas::Header header = ReticleAtlas::Read(static_cast<const u8*>(F->pointer()), u64(_max(F->length(), 0)));
	FS.r_close(F);
	if (header.retired)
		Msg("! [RETICLE] %s.dds is an older reticle atlas format and draws nothing; rebuild it from its SVG "
			"(the engine repo's tools/reticle_vector)", texture->cName.c_str());
	else if (header.newer)
		Msg("! [RETICLE] %s.dds is a newer reticle atlas format than this engine draws; update the engine and the "
			"GAMMA patch together", texture->cName.c_str());
	size = header.glow_size;
	return size;
}
