#pragma once

/**
 * param defgrp_index: Vertex group index, -1 for no vertex groups.
 *
 * All edge tags must be cleared.
 * Behavior matches mod_solidify.c
 */
void mesh_wireframe(Mesh *mesh,
                       float offset,
                       float offset_fac,
                       float offset_fac_vg,
                       bool use_replace,
                       bool use_boundary,
                       bool use_even_offset,
                       bool use_relative_offset,
                       bool use_crease,
                       float crease_weight,
                       int defgrp_index,
                       bool defgrp_invert,
                       short mat_offset,
                       short mat_max,
                       bool use_tag);
