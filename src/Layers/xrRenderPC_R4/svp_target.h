#ifndef svp_targetH
#define svp_targetH
#pragma once

// pip SVP scene render extent for the color/depth/MV targets, exact svp_height at gate 0
u32 svp_render_extent();

// pip the SVP target extent, one policy for the allocation and the camera projection
void svp_target_wh(u32& svp_w, u32& svp_h);

// Learn the settled lens diameter for next-frame adaptive target sizing.
void svp_track_disc(u32 width, u32 height);

// Optic epoch the learned disc belongs to.
u32 svp_disc_epoch();

#endif
