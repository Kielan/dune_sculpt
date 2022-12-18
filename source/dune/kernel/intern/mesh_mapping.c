/** \file
 * \ingroup bke
 *
 * Functions for accessing mesh connectivity data.
 * eg: polys connected to verts, UV's connected to verts.
 */

#include "MEM_guardedalloc.h"

#include "DNA_meshdata_types.h"
#include "DNA_vec_types.h"

#include "BLI_bitmap.h"
#include "BLI_buffer.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_customdata.h"
#include "BKE_mesh_mapping.h"
#include "BLI_memarena.h"

#include "BLI_strict_flags.h"

/* -------------------------------------------------------------------- */
/** \name Mesh Connectivity Mapping
 * \{ */

/* ngon version wip, based on BM_uv_vert_map_create */
UvVertMap *BKE_mesh_uv_vert_map_create(const MPoly *mpoly,
                                       const MLoop *mloop,
                                       const MLoopUV *mloopuv,
                                       uint totpoly,
                                       uint totvert,
                                       const float limit[2],
                                       const bool selected,
                                       const bool use_winding)
{
  UvVertMap *vmap;
  UvMapVert *buf;
  const MPoly *mp;
  uint a;
  int i, totuv, nverts;

  bool *winding = NULL;
  BLI_buffer_declare_static(vec2f, tf_uv_buf, BLI_BUFFER_NOP, 32);

  totuv = 0;

  /* generate UvMapVert array */
  mp = mpoly;
  for (a = 0; a < totpoly; a++, mp++) {
    if (!selected || (!(mp->flag & ME_HIDE) && (mp->flag & ME_FACE_SEL))) {
      totuv += mp->totloop;
    }
  }

  if (totuv == 0) {
    return NULL;
  }

  vmap = (UvVertMap *)MEM_callocN(sizeof(*vmap), "UvVertMap");
  buf = vmap->buf = (UvMapVert *)MEM_callocN(sizeof(*vmap->buf) * (size_t)totuv, "UvMapVert");
  vmap->vert = (UvMapVert **)MEM_callocN(sizeof(*vmap->vert) * totvert, "UvMapVert*");
  if (use_winding) {
    winding = MEM_callocN(sizeof(*winding) * totpoly, "winding");
  }

  if (!vmap->vert || !vmap->buf) {
    BKE_mesh_uv_vert_map_free(vmap);
    return NULL;
  }

  mp = mpoly;
  for (a = 0; a < totpoly; a++, mp++) {
    if (!selected || (!(mp->flag & ME_HIDE) && (mp->flag & ME_FACE_SEL))) {
      float(*tf_uv)[2] = NULL;

      if (use_winding) {
        tf_uv = (float(*)[2])BLI_buffer_reinit_data(&tf_uv_buf, vec2f, (size_t)mp->totloop);
      }

      nverts = mp->totloop;

      for (i = 0; i < nverts; i++) {
        buf->loop_of_poly_index = (unsigned short)i;
        buf->poly_index = a;
        buf->separate = 0;
        buf->next = vmap->vert[mloop[mp->loopstart + i].v];
        vmap->vert[mloop[mp->loopstart + i].v] = buf;

        if (use_winding) {
          copy_v2_v2(tf_uv[i], mloopuv[mpoly[a].loopstart + i].uv);
        }

        buf++;
      }

      if (use_winding) {
        winding[a] = cross_poly_v2(tf_uv, (uint)nverts) > 0;
      }
    }
  }

  /* sort individual uvs for each vert */
  for (a = 0; a < totvert; a++) {
    UvMapVert *newvlist = NULL, *vlist = vmap->vert[a];
    UvMapVert *iterv, *v, *lastv, *next;
    const float *uv, *uv2;
    float uvdiff[2];

    while (vlist) {
      v = vlist;
      vlist = vlist->next;
      v->next = newvlist;
      newvlist = v;

      uv = mloopuv[mpoly[v->poly_index].loopstart + v->loop_of_poly_index].uv;
      lastv = NULL;
      iterv = vlist;

      while (iterv) {
        next = iterv->next;

        uv2 = mloopuv[mpoly[iterv->poly_index].loopstart + iterv->loop_of_poly_index].uv;
        sub_v2_v2v2(uvdiff, uv2, uv);

        if (fabsf(uv[0] - uv2[0]) < limit[0] && fabsf(uv[1] - uv2[1]) < limit[1] &&
            (!use_winding || winding[iterv->poly_index] == winding[v->poly_index])) {
          if (lastv) {
            lastv->next = next;
          }
          else {
            vlist = next;
          }
          iterv->next = newvlist;
          newvlist = iterv;
        }
        else {
          lastv = iterv;
        }

        iterv = next;
      }

      newvlist->separate = 1;
    }

    vmap->vert[a] = newvlist;
  }

  if (use_winding) {
    MEM_freeN(winding);
  }

  BLI_buffer_free(&tf_uv_buf);

  return vmap;
}

UvMapVert *BKE_mesh_uv_vert_map_get_vert(UvVertMap *vmap, uint v)
{
  return vmap->vert[v];
}

void BKE_mesh_uv_vert_map_free(UvVertMap *vmap)
{
  if (vmap) {
    if (vmap->vert) {
      MEM_freeN(vmap->vert);
    }
    if (vmap->buf) {
      MEM_freeN(vmap->buf);
    }
    MEM_freeN(vmap);
  }
}

/**
 * Generates a map where the key is the vertex and the value is a list
 * of polys or loops that use that vertex as a corner. The lists are allocated
 * from one memory pool.
 *
 * Wrapped by #BKE_mesh_vert_poly_map_create & BKE_mesh_vert_loop_map_create
 */
static void mesh_vert_poly_or_loop_map_create(MeshElemMap **r_map,
                                              int **r_mem,
                                              const MPoly *mpoly,
                                              const MLoop *mloop,
                                              int totvert,
                                              int totpoly,
                                              int totloop,
                                              const bool do_loops)
{
  MeshElemMap *map = MEM_callocN(sizeof(MeshElemMap) * (size_t)totvert, __func__);
  int *indices, *index_iter;
  int i, j;

  indices = index_iter = MEM_mallocN(sizeof(int) * (size_t)totloop, __func__);

  /* Count number of polys for each vertex */
  for (i = 0; i < totpoly; i++) {
    const MPoly *p = &mpoly[i];

    for (j = 0; j < p->totloop; j++) {
      map[mloop[p->loopstart + j].v].count++;
    }
  }

  /* Assign indices mem */
  for (i = 0; i < totvert; i++) {
    map[i].indices = index_iter;
    index_iter += map[i].count;

    /* Reset 'count' for use as index in last loop */
    map[i].count = 0;
  }

  /* Find the users */
  for (i = 0; i < totpoly; i++) {
    const MPoly *p = &mpoly[i];

    for (j = 0; j < p->totloop; j++) {
      uint v = mloop[p->loopstart + j].v;

      map[v].indices[map[v].count] = do_loops ? p->loopstart + j : i;
      map[v].count++;
    }
  }

  *r_map = map;
  *r_mem = indices;
}
