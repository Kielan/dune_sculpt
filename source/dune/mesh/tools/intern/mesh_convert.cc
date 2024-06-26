/**
 * mesh conversion functions.
 *
 * section mesh_conv_shapekey Converting Shape Keys
 *
 * When converting to/from a Mesh/BMesh you can optionally pass a shape key to edit.
 * This has the effect of editing the shape key-block rather than the original mesh vertex coords
 * (although additional geometry is still allowed and uses fallback locations on converting).
 *
 * While this works for any mesh/bmesh this is made use of by entering and exiting edit-mode.
 *
 * There are comments in code but this should help explain the general
 * intention as to how this works converting from/to mesh.
 * subsection user_pov User Perspective
 *
 * - Editmode operations when a shape key-block is active edits only that key-block.
 * - The first Basis key-block always matches the Mesh verts.
 * - Changing vertex locations of _any_ Basis
 *   will apply offsets to those shape keys using this as their Basis.
 *
 * subsection enter_editmode Entering EditMode - mesh_bm_from_me
 *
 * - The active key-block is used for Mesh vertex locations on entering edit-mode.
 *   So obviously the meshes vertex locations remain unchanged and the shape key
 *   itself is not being edited directly.
 *   Simply the MeshVert.co is a initialized from active shape key (when its set).
 * - All key-blocks are added as CustomData layers (read code for details).
 *
 * subsection exit_editmode Exiting EditMode - mesh_bm_to_me
 *
 * This is where the most confusing code is! Won't attempt to document the details here,
 * for that read the code.
 * But basics are as follows.
 *
 * - Vertex locations (possibly modified from initial active key-block)
 *   are copied directly into MeshVert.co
 *   (special confusing note that these may be restored later, when editing the 'Basis', read on).
 * - if the 'Key' is relative, and the active key-block is the basis for ANY other key-blocks -
 *   get an array of offsets between the new vertex locations and the original shape key
 *   (before entering edit-mode), these offsets get applied later on to inactive key-blocks
 *   using the active one (which we are editing) as their Basis.
 *
 * Copying the locations back to the shape keys is quite confusing...
 * One main area of confusion is that when editing a 'Basis' key-block 'me->key->refkey'
 * The coords are written into the mesh, from the users perspective the Basis coords are written
 * into the mesh when exiting edit-mode.
 *
 * When _not_ editing the 'Basis', the original vertex locations
 * (stored in the mesh and unchanged during edit-mode), are copied back into the mesh.
 *
 * This has the effect from the users POV of leaving the mesh un-touched,
 * and only editing the active shape key-block.
 *
 * subsection other_notes Other Notes
 *
 * Other details noted here which might not be so obvious:
 *
 * - The CD_SHAPEKEY layer is only used in edit-mode,
 *   and the Mesh.key is only used in object-mode.
 *   Although the CD_SHAPEKEY custom-data layer is converted into #Key data-blocks for each
 *   undo-step while in edit-mode.
 * - The CD_SHAPE_KEYINDEX layer is used to check if vertices existed when entering edit-mode.
 *   Values of the indices are only used for shape-keys when the #CD_SHAPEKEY layer can't be found,
 *   allowing coordinates from the #Key to be used to prevent data-loss.
 *   These indices are also used to maintain correct indices for hook modifiers and vertex parents.
 */

#include "types_key.h"
#include "types_mesh.h"
#include "types_meshdata.h"
#include "types_modifier.h"
#include "types_object.h"

#include "mem_guardedalloc.h"

#include "lib_alloca.h"
#include "lib_array.hh"
#include "lib_index_range.hh"
#include "lib_listbase.h"
#include "lib_math_vector.h"
#include "lib_span.hh"

#include "dune_customdata.h"
#include "dune_mesh.h"
#include "dune_mesh_runtime.h"
#include "dune_multires.h"

#include "dune_key.h"
#include "dune_main.h"

#include "dgraph_query.h"

#include "mesh.h"
#include "intern/mesh_private.h" /* For element checking. */

#include "CLG_log.h"

static CLG_LogRef LOG = {"mesh.mesh.convert"};

using dune::Array;
using dune::IndexRange;
using dune::Span;

void mesh_cd_flag_ensure(Mesh *mesh, Mesh *mesh, const char cd_flag)
{
  const char cd_flag_all = mesh_cd_flag_from_mesh(mesh) | cd_flag;
  mesh_cd_flag_apply(mesh, cd_flag_all);
  if (mesh) {
    mesh->cd_flag = cd_flag_all;
  }
}

void mesh_cd_flag_apply(Mesh *mesh, const char cd_flag)
{
  /* CustomData_mesh_init_pool() must run first */
  lib_assert(mesh->vdata.totlayer == 0 || mesh->vdata.pool != nullptr);
  lib_assert(mesh->edata.totlayer == 0 || mesh->edata.pool != nullptr);
  lib_assert(mesh->pdata.totlayer == 0 || mesh->pdata.pool != nullptr);

  if (cd_flag & MESH_CDFLAG_VERT_WEIGHT) {
    if (!CustomData_has_layer(&mesh->vdata, CD_BWEIGHT)) {
      mesh_data_layer_add(mesh, &mesh->vdata, CD_BWEIGHT);
    }
  }
  else {
    if (CustomData_has_layer(&mesh->vdata, CD_BWEIGHT)) {
      mesh_data_layer_free(mesh, &mesh->vdata, CD_BWEIGHT);
    }
  }

  if (cd_flag & MESH_CDFLAG_VERT_CREASE) {
    if (!CustomData_has_layer(&mesh->vdata, CD_CREASE)) {
      mesh_data_layer_add(mesh, &mesh->vdata, CD_CREASE);
    }
  }
  else {
    if (CustomData_has_layer(&mesh->vdata, CD_CREASE)) {
      mesh_data_layer_free(mesh, &mesh->vdata, CD_CREASE);
    }
  }

  if (cd_flag & MESH_CDFLAG_EDGE_BWEIGHT) {
    if (!CustomData_has_layer(&mesh->edata, CD_BWEIGHT)) {
      mesh_data_layer_add(mesh, &mesh>edata, CD_BWEIGHT);
    }
  }
  else {
    if (CustomData_has_layer(&mesh->edata, CD_BWEIGHT)) {
      mesh_data_layer_free(mesh, &mesh->edata, CD_BWEIGHT);
    }
  }

  if (cd_flag & MESH_CDFLAG_EDGE_CREASE) {
    if (!CustomData_has_layer(&mesh->edata, CD_CREASE)) {
      mesh_data_layer_add(mesh, &mesh->edata, CD_CREASE);
    }
  }
  else {
    if (CustomData_has_layer(&mesh->edata, CD_CREASE)) {
      mesh_data_layer_free(mesh, &mesh->edata, CD_CREASE);
    }
  }
}

char mesh_cd_flag_from_mesh(Mesh *mesh)
{
  char cd_flag = 0;
  if (CustomData_has_layer(&mesh->vdata, CD_BWEIGHT)) {
    cd_flag |= MESH_CDFLAG_VERT_WEIGHT;
  }
  if (CustomData_has_layer(&mesh->vdata, CD_CREASE)) {
    cd_flag |= MESH_CDFLAG_VERT_CREASE;
  }
  if (CustomData_has_layer(&mesh->edata, CD_BWEIGHT)) {
    cd_flag |= MESH_CDFLAG_EDGE_BWEIGHT;
  }
  if (CustomData_has_layer(&mesh->edata, CD_CREASE)) {
    cd_flag |= MESH_CDFLAG_EDGE_CREASE;
  }
  return cd_flag;
}

/* Static function for alloc (duplicate in modifiers_mesh.c) */
static MeshFace *mesh_face_create_from_mpoly(Mesh &mesh,
                                             Span<MeshLoop> loops,
                                             Span<MeshVert *> vtable,
                                             Span<MeshEdge *> etable)
{
  Array<MeshVert *, MESH_DEFAULT_NGON_STACK_SIZE> verts(loops.size());
  Array<MeshEdge *, MESH_DEFAULT_NGON_STACK_SIZE> edges(loops.size());

  for (const int i : loops.index_range()) {
    verts[i] = vtable[loops[i].v];
    edges[i] = etable[loops[i].e];
  }

  return mesh_face_create(&msh, verts.data(), edges.data(), loops.size(), nullptr, MESH_CREATE_SKIP_CD);
}

void mesh_from_me(Mesh *mesh, const Mesh *me, const struct MeshFromParams *params)
{
  const bool is_new = !(mesh->totvert || (mesh->vdata.totlayer || mesh->edata.totlayer ||
                                        mesh->pdata.totlayer || mesh->ldata.totlayer));
  KeyBlock *actkey;
  float(*keyco)[3] = nullptr;
  CustomData_MeshMasks mask = CD_MASK_MESH;
  CustomData_MeshMasks_update(&mask, &params->cd_mask_extra);

  if (!me || !me->totvert) {
    if (me && is_new) { /* No verts? still copy custom-data layout. */
      CustomData_copy(&me->vdata, &mesh->vdata, mask.vmask, CD_DEFAULT, 0);
      CustomData_copy(&me->edata, &mesh->edata, mask.emask, CD_DEFAULT, 0);
      CustomData_copy(&me->ldata, &mesh->ldata, mask.lmask, CD_DEFAULT, 0);
      CustomData_copy(&me->pdata, &mesh->pdata, mask.pmask, CD_DEFAULT, 0);

      CustomData_mesh_init_pool(&mesh->vdata, me->totvert, MESH_VERT);
      CustomData_mesh_init_pool(&mesh->edata, me->totedge, MESH_EDGE);
      CustomData_mesh_init_pool(&mesh->ldata, me->totloop, MESH_LOOP);
      CustomData_mesh_init_pool(&mesh->pdata, me->totpoly, MESH_FACE);
    }
    return; /* Sanity check. */
  }

  /* Only copy normals to the new Mesh if they are not already dirty. This avoids unnecessary
   * work, but also accessing normals on an incomplete mesh, for example when restoring undo steps
   * in edit mode. */
  const float(*vert_normals)[3] = nullptr;
  if (params->calc_vert_normal) {
    vert_normals = dune_mesh_vertex_normals_ensure(me);
  }

  if (is_new) {
    CustomData_copy(&me->vdata, &mesh->vdata, mask.vmask, CD_CALLOC, 0);
    CustomData_copy(&me->edata, &mesh->edata, mask.emask, CD_CALLOC, 0);
    CustomData_copy(&me->ldata, &mesh->ldata, mask.lmask, CD_CALLOC, 0);
    CustomData_copy(&me->pdata, &mesh->pdata, mask.pmask, CD_CALLOC, 0);
  }
  else {
    CustomData_mesh_merge(&me->vdata, &mesh->vdata, mask.vmask, CD_CALLOC, mesh, MESH_VERT);
    CustomData_mesh_merge(&me->edata, &mesh->edata, mask.emask, CD_CALLOC, mesh, MESH_EDGE);
    CustomData_mesh_merge(&me->ldata, &mesh->ldata, mask.lmask, CD_CALLOC, mesh, MESH_LOOP);
    CustomData_mesh_merge(&me->pdata, &mesh->pdata, mask.pmask, CD_CALLOC, mesh, MESH_FACE);
  }

  /* -------------------------------------------------------------------- */
  /* Shape Key */
  int tot_shape_keys = 0;
  if (me->key != nullptr && dgraph_is_original_id(&me->id)) {
    /* Evaluated meshes can be topologically inconsistent with their shape keys.
     * Shape keys are also already integrated into the state of the evaluated
     * mesh, so considering them here would kind of apply them twice. */
    tot_shape_keys = lib_listbase_count(&me->key->block);

    /* Original meshes must never contain a shape-key custom-data layers.
     *
     * This may happen if and object's mesh data is accidentally
     * set to the output from the modifier stack, causing it to be an "original" ID,
     * even though the data isn't fully compatible (hence this assert).
     *
     * This results in:
     * - The newly created Mesh having twice the number of custom-data layers.
     * - When converting the Mesh back to a regular mesh,
     *   At least one of the extra shape-key blocks will be created in #Mesh.key
     *   depending on the value of CustomDataLayer.uid.
     *
     * We could support mixing both kinds of data if there is a compelling use-case for it.
     * At the moment it's simplest to assume all original meshes use the key-block and meshes
     * that are evaluated (through the modifier stack for example) use custom-data layers.
     */
    lib_assert(!CustomData_has_layer(&me->vdata, CD_SHAPEKEY));
  }
  if (is_new == false) {
    tot_shape_keys = min_ii(tot_shape_keys, CustomData_number_of_layers(&mesh->vdata, CD_SHAPEKEY));
  }
  const float(**shape_key_table)[3] = tot_shape_keys ? (const float(**)[3])lib_array_alloca(
                                                           shape_key_table, tot_shape_keys) :
                                                       nullptr;

  if ((params->active_shapekey != 0) && tot_shape_keys > 0) {
    actkey = static_cast<KeyBlock *>(lib_findlink(&me->key->block, params->active_shapekey - 1));
  }
  else {
    actkey = nullptr;
  }

  if (is_new) {
    if (tot_shape_keys || params->add_key_index) {
      CustomData_add_layer(&mesh->vdata, CD_SHAPE_KEYINDEX, CD_ASSIGN, nullptr, 0);
    }
  }

  if (tot_shape_keys) {
    if (is_new) {
      /* Check if we need to generate unique ids for the shape-keys.
       * This also exists in the file reading code, but is here for a sanity check. */
      if (!me->key->uidgen) {
        fprintf(stderr,
                "%s had to generate shape key uid's in a situation we shouldn't need to! "
                "(mesh internal error)\n",
                __func__);

        me->key->uidgen = 1;
        LISTBASE_FOREACH (KeyBlock *, block, &me->key->block) {
          block->uid = me->key->uidgen++;
        }
      }
    }

    if (actkey && actkey->totelem == me->totvert) {
      keyco = params->use_shapekey ? static_cast<float(*)[3]>(actkey->data) : nullptr;
      if (is_new) {
        mesh->shapenr = params->active_shapekey;
      }
    }

    int i;
    KeyBlock *block;
    for (i = 0, block = static_cast<KeyBlock *>(me->key->block.first); i < tot_shape_keys;
         block = block->next, i++) {
      if (is_new) {
        CustomData_add_layer_named(&mesh->vdata, CD_SHAPEKEY, CD_ASSIGN, nullptr, 0, block->name);
        int j = CustomData_get_layer_index_n(&mesh->vdata, CD_SHAPEKEY, i);
        mesh->vdata.layers[j].uid = block->uid;
      }
      shape_key_table[i] = static_cast<const float(*)[3]>(block->data);
    }
  }

  if (is_new) {
    CustomData_mesh_init_pool(&mesh->vdata, me->totvert, MESH_VERT);
    CustomData_mesh_init_pool(&mesh->edata, me->totedge, MESH_EDGE);
    CustomData_mesh_init_pool(&mesh->ldata, me->totloop, MESH_LOOP);
    CustomData_mesh_init_pool(&mesh->pdata, me->totpoly, MESH_FACE);
  }
  mesh_cd_flag_apply(mesh, me->cd_flag | (is_new ? 0 : mesh_cd_flag_from_mesh(m)));

  /* Only copy these values over if the source mesh is flagged to be using them.
   * Even if `mesh` has these layers, they may have been added from another mesh, when `!is_new`. */
  const int cd_vert_bweight_offset = (me->cd_flag & ME_CDFLAG_VERT_WEIGHT) ?
                                         CustomData_get_offset(&mesh->vdata, CD_WEIGHT) :
                                         -1;
  const int cd_edge_weight_offset = (me->cd_flag & ME_CDFLAG_EDGE_BWEIGHT) ?
                                         CustomData_get_offset(&mesh->edata, CD_BWEIGHT) :
                                         -1;
  const int cd_edge_crease_offset = (me->cd_flag & ME_CDFLAG_EDGE_CREASE) ?
                                        CustomData_get_offset(&mesh->edata, CD_CREASE) :
                                        -1;
  const int cd_shape_key_offset = tot_shape_keys ? CustomData_get_offset(&mesh->vdata, CD_SHAPEKEY) :
                                                   -1;
  const int cd_shape_keyindex_offset = is_new && (tot_shape_keys || params->add_key_index) ?
                                           CustomData_get_offset(&mesh->vdata, CD_SHAPE_KEYINDEX) :
                                           -1;

  Span<MeshVert> mvert{me->mvert, me->totvert};
  Array<MeshVert *> vtable(me->totvert);
  for (const int i : mvert.index_range()) {
    MeshVert *v = vtable[i] = mesh_vert_create(
        mesh, keyco ? keyco[i] : mvert[i].co, nullptr, MESH_CREATE_SKIP_CD);
    mesh_elem_index_set(v, i); /* set_ok */

    /* Transfer flag. */
    v->head.hflag = mesh_vert_flag_from_mflag(mvert[i].flag & ~SELECT);

    /* This is necessary for selection counts to work properly. */
    if (mvert[i].flag & SELECT) {
      mesh_vert_select_set(mesh, v, true);
    }

    if (vert_normals) {
      copy_v3_v3(v->no, vert_normals[i]);
    }

    /* Copy Custom Data */
    CustomData_to_mesh_block(&me->vdata, &mesh->vdata, i, &v->head.data, true);

    if (cd_vert_bweight_offset != -1) {
      MESH_ELEM_CD_SET_FLOAT(v, cd_vert_bweight_offset, (float)mvert[i].bweight / 255.0f);
    }

    /* Set shape key original index. */
    if (cd_shape_keyindex_offset != -1) {
      MESH_ELEM_CD_SET_INT(v, cd_shape_keyindex_offset, i);
    }

    /* Set shape-key data. */
    if (tot_shape_keys) {
      float(*co_dst)[3] = (float(*)[3])MESH_ELEM_CD_GET_VOID_P(v, cd_shape_key_offset);
      for (int j = 0; j < tot_shape_keys; j++, co_dst++) {
        copy_v3_v3(*co_dst, shape_key_table[j][i]);
      }
    }
  }
  if (is_new) {
    mesh->elem_index_dirty &= ~MESH_VERT; /* Added in order, clear dirty flag. */
  }

  Span<MeshEdge> medge{me->medge, me->totedge};
  Array<MeshEdge *> etable(me->totedge);
  for (const int i : medge.index_range()) {
    MesgEdge *e = etable[i] = mesh_edge_create(
        mesh, vtable[medge[i].v1], vtable[medge[i].v2], nullptr, MESH_CREATE_SKIP_CD);
    mesh_elem_index_set(e, i); /* set_ok */

    /* Transfer flags. */
    e->head.hflag = mesh_edge_flag_from_mflag(medge[i].flag & ~SELECT);

    /* This is necessary for selection counts to work properly. */
    if (medge[i].flag & SELECT) {
      mesh_edge_select_set(mesh, e, true);
    }

    /* Copy Custom Data */
    CustomData_to_mesh_block(&me->edata, &bm->edata, i, &e->head.data, true);

    if (cd_edge_bweight_offset != -1) {
      MESH_ELEM_CD_SET_FLOAT(e, cd_edge_bweight_offset, (float)medge[i].weight / 255.0f);
    }
    if (cd_edge_crease_offset != -1) {
      MESH_ELEM_CD_SET_FLOAT(e, cd_edge_crease_offset, (float)medge[i].crease / 255.0f);
    }
  }
  if (is_new) {
    mesh->elem_index_dirty &= ~MESH_EDGE; /* Added in order, clear dirty flag. */
  }

  Span<MeshPoly> mpoly{me->mpoly, me->totpoly};
  Span<MeshLoop> mloop{me->mloop, me->totloop};

  /* Only needed for selection. */

  Array<MeshFace *> ftable;
  if (me->mselect && me->totselect != 0) {
    ftable.reinitialize(me->totpoly);
  }

  int totloops = 0;
  for (const int i : mpoly.index_range()) {
    MeshFace *f = mesh_face_create_from_mpoly(
        *mesh, mloop.slice(mpoly[i].loopstart, mpoly[i].totloop), vtable, etable);
    if (!ftable.is_empty()) {
      ftable[i] = f;
    }

    if (UNLIKELY(f == nullptr)) {
      printf(
          "%s: Warning! Bad face in mesh"
          " \"%s\" at index %d!, skipping\n",
          __func__,
          me->id.name + 2,
          i);
      continue;
    }

    /* Don't use 'i' since we may have skipped the face. */
    mesh_elem_index_set(f, mesh->totface - 1); /* set_ok */

    /* Transfer flag. */
    f->head.hflag = mesh_face_flag_from_mflag(mpoly[i].flag & ~ME_FACE_SEL);

    /* This is necessary for selection counts to work properly. */
    if (mpoly[i].flag & MESH_FACE_SEL) {
      mesh_face_select_set(bm, f, true);
    }

    f->mat_nr = mpoly[i].mat_nr;
    if (i == me->act_face) {
      mesh->act_face = f;
    }

    int j = mpoly[i].loopstart;
    MeshLoop *l_first = MESH_FACE_FIRST_LOOP(f);
    MeshLoop *l_iter = l_first;
    do {
      /* Don't use 'j' since we may have skipped some faces, hence some loops. */
      mesh_elem_index_set(l_iter, totloops++); /* set_ok */

      /* Save index of corresponding MeehLoop. */
      CustomData_to_mesh_block(&me->ldata, &mesh->ldata, j++, &l_iter->head.data, true);
    } while ((l_iter = l_iter->next) != l_first);

    /* Copy Custom Data */
    CustomData_to_mesh_block(&me->pdata, &mesh->pdata, i, &f->head.data, true);

    if (params->calc_face_normal) {
      mesh_face_normal_update(f);
    }
  }
  if (is_new) {
    mesh->elem_index_dirty &= ~(MESH_FACE | MESH_LOOP); /* Added in order, clear dirty flag. */
  }

  /* -------------------------------------------------------------------- */
  /* MSelect clears the array elements (to avoid adding multiple times).
   *
   * Take care to keep this last and not use (v/e/ftable) after this.
   */

  if (me->mselect && me->totselect != 0) {
    for (const int i : IndexRange(me->totselect)) {
      const MSelect &msel = me->mselect[i];

      MeshElem **ele_p;
      switch (msel.type) {
        case MESH_VSEL:
          ele_p = (MeshElem **)&vtable[msel.index];
          break;
        case MESH_ESEL:
          ele_p = (MeshElem **)&etable[msel.index];
          break;
        case MESH_FSEL:
          ele_p = (MeshElem **)&ftable[msel.index];
          break;
        default:
          continue;
      }

      if (*ele_p != nullptr) {
        mesh_select_history_store_notest(bm, *ele_p);
        *ele_p = nullptr;
      }
    }
  }
  else {
    mesh_select_history_clear(mesh);
  }
}

/** Mesh -> Mesh **/
static MeshVert **mesh_to_mesh_vertex_map(Mesh *mesh, int ototvert)
{
  const int cd_shape_keyindex_offset = CustomData_get_offset(&mesh->vdata, CD_SHAPE_KEYINDEX);
  MeshVert **vertMap = nullptr;
  MeshVert *eve;
  int i = 0;
  MeshIter iter;

  /* Caller needs to ensure this. */
  lib_assert(ototvert > 0);

  vertMap = static_cast<MeshVert **>(mesh_callocn(sizeof(*vertMap) * ototvert, "vertMap"));
  if (cd_shape_keyindex_offset != -1) {
    MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      const int keyi = MESH_ELEM_CD_GET_INT(eve, cd_shape_keyindex_offset);
      if ((keyi != ORIGINDEX_NONE) && (keyi < ototvert) &&
          /* Not fool-proof, but chances are if we have many verts with the same index,
           * we will want to use the first one,
           * since the second is more likely to be a duplicate. */
          (vertMap[keyi] == nullptr)) {
        vertMap[keyi] = eve;
      }
    }
  }
  else {
    MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      if (i < ototvert) {
        vertMap[i] = eve;
      }
      else {
        break;
      }
    }
  }

  return vertMap;
}

/* -------------------------------------------------------------------- */
/** Edit-Mesh to Shape Key Conversion
 *
 * There are some details relating to using data from shape keys that need to be
 * considered carefully for shape key synchronization logic.
 *
 * Key Block Usage
 * ***************
 *
 * Key blocks (data in Mesh.key must be used carefully).
 *
 * They can be used to query which key blocks are relative to the basis
 * since it's not possible to add/remove/reorder key blocks while in edit-mode.
 *
 * Key Block Coordinates
 * =====================
 *
 * Key blocks locations must *not* be used. This was done from v2.67 to 3.0,
 * causing bugs T35170 & T44415.
 *
 * Shape key synchronizing could work under the assumption that the key-block is
 * fixed-in-place when entering edit-mode allowing them to be used as a reference when exiting.
 * It often does work but isn't reliable since for e.g. rendering may flush changes
 * from the edit-mesh to the key-block (there are a handful of other situations where
 * changes may be flushed, see editors_flush_edits and related functions).
 * When using undo, it's not known if the data in key-block is from the past or future,
 * so just don't use this data as it causes pain and suffering for users and developers alike.
 *
 * Instead, use the shape-key values stored in CD_SHAPEKEY since they are reliably
 * based on the original locations, unless explicitly manipulated.
 * It's important to write the final shape-key values back to the CD_SHAPEKEY so applying
 * the difference between the original-basis and the new coordinates isn't done multiple times.
 * Therefore editor_editors_flush_edits and other flushing calls will update both the #Mesh.key
 * and the edit-mode CD_SHAPEKEY custom-data layers.
 *
 * WARNING: There is an exception to the rule of ignoring coordinates in the destination:
 * that is when shape-key data in `bm` can't be found (which is itself an error/exception).
 * In this case our own rule is violated as the alternative is losing the shape-data entirely.
 *
 * Flushing Coordinates Back to the #BMesh
 * ---------------------------------------
 *
 * The edit-mesh may be flushed back to the #Mesh and Key used to generate it.
 * When this is done, the new values are written back to the Mesh's CD_SHAPEKEY as well.
 * This is necessary when editing basis-shapes so the difference in shape keys
 * is not applied multiple times. If it were important to avoid it could be skipped while
 * exiting edit-mode (as the entire Mesh is freed in that case), however it's just copying
 * back a `float[3]` so the work to check if it's necessary isn't worth the overhead.
 *
 * In general updating the Mesh's CD_SHAPEKEY makes shake-key logic easier to reason about
 * since it means flushing data back to the mesh has the same behavior as exiting and entering
 * edit-mode (a more common operation). Meaning there is one less corner-case to have to consider.
 *
 * Exceptional Cases
 * *****************
 *
 * There are some situations that should not happen in typical usage but are
 * still handled in this code, since failure to handle them could lose user-data.
 * These could be investigated further since if they never happen in practice,
 * we might consider removing them. However, the possibility of an mesh directly
 * being modified by Python or some other low level logic that changes key-blocks
 * means there is a potential this to happen so keeping code to these cases remain supported.
 *
 * - Custom Data & Mesh Key Block Synchronization.
 *   Key blocks in `me->key->block` should always have an associated
 *   CD_SHAPEKEY layer in `mesh->vdata`.
 *   If they don't there are two fall-backs for setting the location,
 *   - Use the value from the original shape key
 *     WARNING: this is technically incorrect! (see note on "Key Block Usage").
 *   - Use the current vertex location,
 *     Also not correct but it's better then having it zeroed for e.g.
 *
 * - Missing key-index layer.
 *   In this case the basis key wont apply it's deltas to other keys and in the case
 *   a shape-key layer is missing, its coordinates will be initialized from the edit-mesh
 *   vertex locations instead of attempting to remap the shape-keys coordinates.
 *
 * These cases are considered abnormal and shouldn't occur in typical usage.
 * A warning is logged in this case to help troubleshooting bugs with shape-keys.
 **/

/**
 * Returns custom-data shape-key index from a key-block or -1
 * could split this out into a more generic function.
 */
static int mesh_to_mesh_shape_layer_index_from_kb(Mesh *mesh, KeyBlock *currkey)
{
  int i;
  int j = 0;

  for (i = 0; i < mesh->vdata.totlayer; i++) {
    if (mesh->vdata.layers[i].type == CD_SHAPEKEY) {
      if (currkey->uid == mesh->vdata.layers[i].uid) {
        return j;
      }
      j++;
    }
  }
  return -1;
}

/**
 * Update `key` with shape key data stored in `bm`.
 *
 * param mesh: The source Mesh.
 * param key: The destination key.
 * param mvert: The destination vertex array (in some situations it's coordinates are updated).
 * param active_shapekey_to_mvert: When editing a non-basis shape key, the coordinates for the
 * basis are typically copied into the `mvert` array since it makes sense for the meshes
 * vertex coordinates to match the "Basis" key.
 * When enabled, skip this step and copy MeshVert.co directly to MeshVert.co,
 * See MeshToMeshParams.active_shapekey_to_mvert doc-string.
 */
static void mesh_to_mesh_shape(Mesh *mesh,
                               Key *key,
                               MeshVert *mvert,
                               const bool active_shapekey_to_mvert)
{
  KeyBlock *actkey = static_cast<KeyBlock *>(lib_findlink(&key->block, mesh->shapenr - 1));

  /* It's unlikely this ever remains false, check for correctness. */
  bool actkey_has_layer = false;

  /* Go through and find any shape-key custom-data layers
   * that might not have corresponding KeyBlocks, and add them if necessary. */
  for (int i = 0; i < mesh->vdata.totlayer; i++) {
    if (mesh->vdata.layers[i].type != CD_SHAPEKEY) {
      continue;
    }

    KeyBlock *currkey;
    for (currkey = (KeyBlock *)key->block.first; currkey; currkey = currkey->next) {
      if (currkey->uid == mesh->vdata.layers[i].uid) {
        break;
      }
    }

    if (currkey) {
      if (currkey == actkey) {
        actkey_has_layer = true;
      }
    }
    else {
      currkey = dune_keyblock_add(key, mesh->vdata.layers[i].name);
      currkey->uid = mesh->vdata.layers[i].uid;
    }
  }

  const int cd_shape_keyindex_offset = CustomData_get_offset(&bm->vdata, CD_SHAPE_KEYINDEX);
  MeshIter iter;
  MeshVert *eve;
  float(*ofs)[3] = nullptr;

  /* Editing the basis key updates others. */
  if ((key->type == KEY_RELATIVE) &&
      /* The shape-key coordinates used from entering edit-mode are used. */
      (actkey_has_layer == true) &&
      /* Original key-indices are only used to check the vertex existed when entering edit-mode. */
      (cd_shape_keyindex_offset != -1) &&
      /* Offsets are only needed if the current shape is a basis for others. */
      dune_keyblock_is_basis(key, mesh->shapenr - 1)) {

    lib_assert(actkey != nullptr); /* Assured by `actkey_has_layer` check. */
    const int actkey_uuid = mesh_to_mesh_shape_layer_index_from_kb(mesh, actkey);

    /* Since `actkey_has_layer == true`, this must never fail. */
    lib_assert(actkey_uuid != -1);

    const int cd_shape_offset = CustomData_get_n_offset(&mesh->vdata, CD_SHAPEKEY, actkey_uuid);

    ofs = static_cast<float(*)[3]>(mem_mallocn(sizeof(float[3]) * mesh->totvert, __func__));
    int i;
    MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      const int keyi = MESH_ELEM_CD_GET_INT(eve, cd_shape_keyindex_offset);
      /* Check the vertex existed when entering edit-mode (otherwise don't apply an offset). */
      if (keyi != ORIGINDEX_NONE) {
        float *co_orig = (float *)MESH_ELEM_CD_GET_VOID_P(eve, cd_shape_offset);
        /* Could use 'eve->co' or the destination #MVert.co, they're the same at this point. */
        sub_v3_v3v3(ofs[i], eve->co, co_orig);
      }
      else {
        /* If there are new vertices in the mesh, we can't propagate the offset
         * because it will only work for the existing vertices and not the new
         * ones, creating a mess when doing e.g. subdivide + translate. */
        mem_freen(ofs);
        ofs = nullptr;
        break;
      }
    }
  }

  /* Without this, the real mesh coordinates (uneditable) as soon as you create the Basis shape.
   * while users might not notice since the shape-key is applied in the viewport,
   * exporters for example may still use the underlying coordinates, see: T30771 & T96135.
   *
   * Needed when editing any shape that isn't the (`key->refkey`), the vertices in `me->mvert`
   * currently have vertex coordinates set from the current-shape (initialized from meshVert.co).
   * In this case it's important to overwrite these coordinates with the basis-keys coordinates. */
  bool update_vertex_coords_from_refkey = false;
  int cd_shape_offset_refkey = -1;
  if (active_shapekey_to_mvert == false) {
    if ((actkey != key->refkey) && (cd_shape_keyindex_offset != -1)) {
      const int refkey_uuid = mesh_to_mesh_shape_layer_index_from_kb(mesh, key->refkey);
      if (refkey_uuid != -1) {
        cd_shape_offset_refkey = CustomData_get_n_offset(&mesh->vdata, CD_SHAPEKEY, refkey_uuid);
        if (cd_shape_offset_refkey != -1) {
          update_vertex_coords_from_refkey = true;
        }
      }
    }
  }

  LISTBASE_FOREACH (KeyBlock *, currkey, &key->block) {
    int keyi;
    float(*currkey_data)[3];

    const int currkey_uuid = bm_to_mesh_shape_layer_index_from_kb(bm, currkey);
    const int cd_shape_offset = (currkey_uuid == -1) ?
                                    -1 :
                                    CustomData_get_n_offset(&bm->vdata, CD_SHAPEKEY, currkey_uuid);

    /* Common case, the layer data is available, use it where possible. */
    if (cd_shape_offset != -1) {
      const bool apply_offset = (ofs != nullptr) && (currkey != actkey) &&
                                (mesh->shapenr - 1 == currkey->relative);

      if (currkey->data && (currkey->totelem == mesh->totvert)) {
        /* Use memory in-place. */
      }
      else {
        currkey->data = mem_reallocn(currkey->data, key->elemsize * bm->totvert);
        currkey->totelem = mesh->totvert;
      }
      currkey_data = (float(*)[3])currkey->data;

      int i;
      MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
        float *co_orig = (float *)BM_ELEM_CD_GET_VOID_P(eve, cd_shape_offset);

        if (currkey == actkey) {
          copy_v3_v3(currkey_data[i], eve->co);

          if (update_vertex_coords_from_refkey) {
            lib_assert(actkey != key->refkey);
            keyi = MESH_ELEM_CD_GET_INT(eve, cd_shape_keyindex_offset);
            if (keyi != ORIGINDEX_NONE) {
              float *co_refkey = (float *)MESH_ELEM_CD_GET_VOID_P(eve, cd_shape_offset_refkey);
              copy_v3_v3(mvert[i].co, co_refkey);
            }
          }
        }
        else {
          copy_v3_v3(currkey_data[i], co_orig);
        }

        /* Propagate edited basis offsets to other shapes. */
        if (apply_offset) {
          add_v3_v3(currkey_data[i], ofs[i]);
        }

        /* Apply back new coordinates shape-keys that have offset into Mesh.
         * Otherwise, in case we call again mesh_bm_to_me on same Mesh,
         * we'll apply diff from previous call to mesh_bm_to_me,
         * to shape-key values from original creation of the #BMesh. See T50524. */
        copy_v3_v3(co_orig, currkey_data[i]);
      }
    }
    else {
      /* No original layer data, use fallback information. */
      if (currkey->data && (cd_shape_keyindex_offset != -1)) {
        CLOG_WARN(&LOG,
                  "Found shape-key but no CD_SHAPEKEY layers to read from, "
                  "using existing shake-key data where possible");
      }
      else {
        CLOG_WARN(&LOG,
                  "Found shape-key but no CD_SHAPEKEY layers to read from, "
                  "using basis shape-key data");
      }

      currkey_data = static_cast<float(*)[3]>(
          mem_mallocn(key->elemsize * mesh->totvert, "currkey->data"));

      int i;
      Mesh_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {

        if ((currkey->data != nullptr) && (cd_shape_keyindex_offset != -1) &&
            ((keyi = MESH_ELEM_CD_GET_INT(eve, cd_shape_keyindex_offset)) != ORIGINDEX_NONE) &&
            (keyi < currkey->totelem)) {
          /* Reconstruct keys via vertices original key indices.
           * WARNING: `currkey->data` is known to be unreliable as the edit-mesh
           * coordinates may be flushed back to the shape-key when exporting or rendering.
           * This is a last resort! If this branch is running as part of regular usage
           * it can be considered a bug. */
          const float(*oldkey)[3] = static_cast<const float(*)[3]>(currkey->data);
          copy_v3_v3(currkey_data[i], oldkey[keyi]);
        }
        else {
          /* Fail! fill in with dummy value. */
          copy_v3_v3(currkey_data[i], eve->co);
        }
      }

      currkey->totelem = mesh->totvert;
      if (currkey->data) {
        mem_freen(currkey->data);
      }
      currkey->data = currkey_data;
    }
  }

  if (ofs) {
    mem_freen(ofs);
  }
}

LIB_INLINE void mesh_quick_edgedraw_flag(MeshEdge *med, MeshEdge *e)
{
  /* This is a cheap way to set the edge draw, its not precise and will
   * pick the first 2 faces an edge uses.
   * The dot comparison is a little arbitrary, but set so that a 5 subdivisions
   * ico-sphere won't vanish but 6 subdivisions will (as with pre-bmesh Blender). */

  if (/* (med->flag & ME_EDGEDRAW) && */ /* Assume to be true. */
      (e->l && (e->l != e->l->radial_next)) &&
      (dot_v3v3(e->l->f->no, e->l->radial_next->f->no) > 0.9995f)) {
    med->flag &= ~ME_EDGEDRAW;
  }
  else {
    med->flag |= ME_EDGEDRAW;
  }
}

void mesh_bm_to_me(Main *main, BMesh *bm, Mesh *me, const struct BMeshToMeshParams *params)
{
  MeshEdge *med;
  MeshVert *v, *eve;
  MeshEdge *e;
  MeshFace *f;
  MeshIter iter;
  int i, j;

  const int cd_vert_bweight_offset = CustomData_get_offset(&bm->vdata, CD_BWEIGHT);
  const int cd_edge_bweight_offset = CustomData_get_offset(&bm->edata, CD_BWEIGHT);
  const int cd_edge_crease_offset = CustomData_get_offset(&bm->edata, CD_CREASE);
  const int cd_shape_keyindex_offset = CustomData_get_offset(&bm->vdata, CD_SHAPE_KEYINDEX);

  const int ototvert = me->totvert;

  /* Free custom data. */
  CustomData_free(&me->vdata, me->totvert);
  CustomData_free(&me->edata, me->totedge);
  CustomData_free(&me->fdata, me->totface);
  CustomData_free(&me->ldata, me->totloop);
  CustomData_free(&me->pdata, me->totpoly);

  /* Add new custom data. */
  me->totvert = mesh->totvert;
  me->totedge = mesh->totedge;
  me->totloop = mesh->totloop;
  me->totpoly = mesh->totface;
  /* Will be overwritten with a valid value if 'dotess' is set, otherwise we
   * end up with 'me->totface' and me->mface == nullptr which can crash T28625. */
  me->totface = 0;
  me->act_face = -1;

  {
    CustomData_MeshMasks mask = CD_MASK_MESH;
    CustomData_MeshMasks_update(&mask, &params->cd_mask_extra);
    CustomData_copy(&mesh->vdata, &me->vdata, mask.vmask, CD_CALLOC, me->totvert);
    CustomData_copy(&mesh->edata, &me->edata, mask.emask, CD_CALLOC, me->totedge);
    CustomData_copy(&mesh->ldata, &me->ldata, mask.lmask, CD_CALLOC, me->totloop);
    CustomData_copy(&mesh->pdata, &me->pdata, mask.pmask, CD_CALLOC, me->totpoly);
  }

  MeshVert *mvert = mesh->totvert ? (MeshVert *)mem_callocn(sizeof(MeshVert) * mesh->totvert, "mesh_to_me.vert") :
                               nullptr;
  MeshEdge *medge = mesh->totedge ? (MeshEdge *)MEM_callocN(sizeof(MeshEdge) * mesh->totedge, "mesh_to_me.edge") :
                               nullptr;
  MeshLoop *mloop = mesh->totloop ? (MeshLoop *)MEM_callocN(sizeof(MeshLoop) * mesh->totloop, "mesh_to_me.loop") :
                               nullptr;
  MeshPoly *mpoly = mesh->totface ? (MeshPoly *)MEM_callocN(sizeof(MeshPoly) * mesh->totface, "mesh_to_me.poly") :
                               nullptr;

  CustomData_add_layer(&me->vdata, CD_MVERT, CD_ASSIGN, mvert, me->totvert);
  CustomData_add_layer(&me->edata, CD_MEDGE, CD_ASSIGN, medge, me->totedge);
  CustomData_add_layer(&me->ldata, CD_MLOOP, CD_ASSIGN, mloop, me->totloop);
  CustomData_add_layer(&me->pdata, CD_MPOLY, CD_ASSIGN, mpoly, me->totpoly);

  /* Clear normals on the mesh completely, since the original vertex and polygon count might be
   * different than the Mesh's. */
  dune_mesh_clear_derived_normals(me);

  me->cd_flag = mesh_cd_flag_from_mesh(mesh);

  /* This is called again, 'dotess' arg is used there. */
  dune_mesh_update_customdata_pointers(me, false);

  i = 0;
  MESH_ITER (v, &iter, mesh, MESH_VERTS_OF_MESH) {
    copy_v3_v3(mvert->co, v->co);

    mvert->flag = mesh_vert_flag_to_mflag(v);

    mesh_elem_index_set(v, i); /* set_inline */

    /* Copy over custom-data. */
    CustomData_from_bmesh_block(&mesh->vdata, &me->vdata, v->head.data, i);

    if (cd_vert_bweight_offset != -1) {
      mvert->bweight = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(v, cd_vert_bweight_offset);
    }

    i++;
    mvert++;

    MESH_CHECK_ELEMENT(v);
  }
  mesh->elem_index_dirty &= ~MESH_VERT;

  med = medge;
  i = 0;
  MESH_ITER (e, &iter, mesh, MESH_EDGES_OF_MESH) {
    med->v1 = mesh_elem_index_get(e->v1);
    med->v2 = mesh_elem_index_get(e->v2);

    med->flag = mesh_edge_flag_to_mflag(e);

    mesh_elem_index_set(e, i); /* set_inline */

    /* Copy over custom-data. */
    CustomData_from_bmesh_block(&bm->edata, &me->edata, e->head.data, i);

    bmesh_quick_edgedraw_flag(med, e);

    if (cd_edge_crease_offset != -1) {
      med->crease = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(e, cd_edge_crease_offset);
    }
    if (cd_edge_bweight_offset != -1) {
      med->bweight = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(e, cd_edge_bweight_offset);
    }

    i++;
    med++;
    MESH_CHECK_ELEMENT(e);
  }
  mesh->elem_index_dirty &= ~MESH_EDGE;

  i = 0;
  j = 0;
  MESH_ITER (f, &iter, mesh, MESH_FACES_OF_MESH) {
    MeshLoop *l_iter, *l_first;
    mpoly->loopstart = j;
    mpoly->totloop = f->len;
    mpoly->mat_nr = f->mat_nr;
    mpoly->flag = mesh_face_flag_to_mflag(f);

    l_iter = l_first = MESH_FACE_FIRST_LOOP(f);
    do {
      mloop->e = mesh_elem_index_get(l_iter->e);
      mloop->v = mesh_elem_index_get(l_iter->v);

      /* Copy over custom-data. */
      CustomData_from_bmesh_block(&mesh->ldata, &me->ldata, l_iter->head.data, j);

      j++;
      mloop++;
      MESH_CHECK_ELEMENT(l_iter);
      MESH_CHECK_ELEMENT(l_iter->e);
      MESH_CHECK_ELEMENT(l_iter->v);
    } while ((l_iter = l_iter->next) != l_first);

    if (f == bm->act_face) {
      me->act_face = i;
    }

    /* Copy over custom-data. */
    CustomData_from_bmesh_block(&bm->pdata, &me->pdata, f->head.data, i);

    i++;
    mpoly++;
    MESH_CHECK_ELEMENT(f);
  }

  /* Patch hook indices and vertex parents. */
  if (params->calc_object_remap && (ototvert > 0)) {
    lib_assert(bmain != nullptr);
    MeshVert **vertMap = nullptr;

    LISTBASE_FOREACH (Object *, ob, &bmain->objects) {
      if ((ob->parent) && (ob->parent->data == me) && ELEM(ob->partype, PARVERT1, PARVERT3)) {

        if (vertMap == nullptr) {
          vertMap = mesh_to_mesh_vertex_map(bm, ototvert);
        }

        if (ob->par1 < ototvert) {
          eve = vertMap[ob->par1];
          if (eve) {
            ob->par1 = mesh_elem_index_get(eve);
          }
        }
        if (ob->par2 < ototvert) {
          eve = vertMap[ob->par2];
          if (eve) {
            ob->par2 = mesh_elem_index_get(eve);
          }
        }
        if (ob->par3 < ototvert) {
          eve = vertMap[ob->par3];
          if (eve) {
            ob->par3 = mesh_elem_index_get(eve);
          }
        }
      }
      if (ob->data == me) {
        LISTBASE_FOREACH (ModifierData *, md, &ob->modifiers) {
          if (md->type == eModifierType_Hook) {
            HookModifierData *hmd = (HookModifierData *)md;

            if (vertMap == nullptr) {
              vertMap = bm_to_mesh_vertex_map(bm, ototvert);
            }

            for (i = j = 0; i < hmd->totindex; i++) {
              if (hmd->indexar[i] < ototvert) {
                eve = vertMap[hmd->indexar[i]];

                if (eve) {
                  hmd->indexar[j++] = mesh_elem_index_get(eve);
                }
              }
              else {
                j++;
              }
            }

            hmd->totindex = j;
          }
        }
      }
    }

    if (vertMap) {
      mem_freen(vertMap);
    }
  }

  dune_mesh_update_customdata_pointers(me, false);

  {
    me->totselect = lib_listbase_count(&(bm->selected));

    MEM_SAFE_FREE(me->mselect);
    if (me->totselect != 0) {
      me->mselect = static_cast<MeshSelect *>(
          mem_mallocn(sizeof(MeshSelect) * me->totselect, "Mesh selection history"));
    }

    LISTBASE_FOREACH_INDEX (MeshEditSelection *, selected, &bm->selected, i) {
      if (selected->htype == MESH_VERT) {
        me->mselect[i].type = ME_VSEL;
      }
      else if (selected->htype == MESH_EDGE) {
        me->mselect[i].type = ME_ESEL;
      }
      else if (selected->htype == MESH_FACE) {
        me->mselect[i].type = ME_FSEL;
      }

      me->mselect[i].index = mesh_elem_index_get(selected->ele);
    }
  }

  if (me->key) {
    mesh_to_mesh_shape(mesh, me->key, me->mvert, params->active_shapekey_to_mvert);
  }

  /* Run this even when shape keys aren't used since it may be used for hooks or vertex parents. */
  if (params->update_shapekey_indices) {
    /* We have written a new shape key, if this mesh is _not_ going to be freed,
     * update the shape key indices to match the newly updated. */
    if (cd_shape_keyindex_offset != -1) {
      MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
        NESH_ELEM_CD_SET_INT(eve, cd_shape_keyindex_offset, i);
      }
    }
  }

  /* Topology could be changed, ensure CD_MDISPS are ok. */
  multires_topology_changed(me);

  /* To be removed as soon as COW is enabled by default. */
  dune_mesh_runtime_clear_geometry(me);
}

void mesh_bm_to_me_for_eval(Mesh *mesh, Mesh *me, const CustomData_MeshMasks *cd_mask_extra)
{
  /* Must be an empty mesh. */
  lib_assert(me->totvert == 0);
  lib_assert(cd_mask_extra == nullptr || (cd_mask_extra->vmask & CD_MASK_SHAPEKEY) == 0);

  me->totvert = mesh->totvert;
  me->totedge = mesh->totedge;
  me->totface = 0;
  me->totloop = mesh->totloop;
  me->totpoly = mesh->totface;

  CustomData_add_layer(&me->vdata, CD_MVERT, CD_CALLOC, nullptr, mesh->totvert);
  CustomData_add_layer(&me->edata, CD_MEDGE, CD_CALLOC, nullptr, mesh->totedge);
  CustomData_add_layer(&me->ldata, CD_MLOOP, CD_CALLOC, nullptr, mesh->totloop);
  CustomData_add_layer(&me->pdata, CD_MPOLY, CD_CALLOC, nullptr, mesh->totface);

  /* Don't process shape-keys, we only feed them through the modifier stack as needed,
   * e.g. for applying modifiers or the like. */
  CustomData_MeshMasks mask = CD_MASK_DERIVEDMESH;
  if (cd_mask_extra != nullptr) {
    CustomData_MeshMasks_update(&mask, cd_mask_extra);
  }
  mask.vmask &= ~CD_MASK_SHAPEKEY;
  CustomData_merge(&bm->vdata, &me->vdata, mask.vmask, CD_CALLOC, me->totvert);
  CustomData_merge(&bm->edata, &me->edata, mask.emask, CD_CALLOC, me->totedge);
  CustomData_merge(&bm->ldata, &me->ldata, mask.lmask, CD_CALLOC, me->totloop);
  CustomData_merge(&bm->pdata, &me->pdata, mask.pmask, CD_CALLOC, me->totpoly);

  dune_mesh_update_customdata_pointers(me, false);

  MeshIter iter;
  MeshVert *eve;
  MeshEdge *eed;
  MeshFace *efa;
  MeshVert *mvert = me->mvert;
  MeshEdge *medge = me->medge;
  MeshLoop *mloop = me->mloop;
  MeshPoly *mpoly = me->mpoly;
  unsigned int i, j;

  const int cd_vert_bweight_offset = CustomData_get_offset(&bm->vdata, CD_BWEIGHT);
  const int cd_edge_bweight_offset = CustomData_get_offset(&bm->edata, CD_BWEIGHT);
  const int cd_edge_crease_offset = CustomData_get_offset(&bm->edata, CD_CREASE);

  /* Clear normals on the mesh completely, since the original vertex and polygon count might be
   * different than the Mesh's. */
  dune_mesh_clear_derived_normals(me);

  me->runtime.deformed_only = true;

  MESH_INDEX_ITER (eve, &iter, mesh, MESH_VERTS_OF_MESH, i) {
    MeshVert *mv = &mvert[i];

    copy_v3_v3(mv->co, eve->co);

    mesh_elem_index_set(eve, i); /* set_inline */

    mv->flag = mesh_vert_flag_to_mflag(eve);

    if (cd_vert_bweight_offset != -1) {
      mv->bweight = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(eve, cd_vert_bweight_offset);
    }

    CustomData_from_bmesh_block(&mesh->vdata, &me->vdata, eve->head.data, i);
  }
  bm->elem_index_dirty &= ~MESH_VERT;

  MESH_ITER_MESH_INDEX (eed, &iter, bm, BM_EDGES_OF_MESH, i) {
    MEdge *med = &medge[i];

    mesh_elem_index_set(eed, i); /* set_inline */

    med->v1 = mesh_elem_index_get(eed->v1);
    med->v2 = mesh_elem_index_get(eed->v2);

    med->flag = mesh_edge_flag_to_mflag(eed);

    /* Handle this differently to editmode switching,
     * only enable draw for single user edges rather than calculating angle. */
    if ((med->flag & ME_EDGEDRAW) == 0) {
      if (eed->l && eed->l == eed->l->radial_next) {
        med->flag |= ME_EDGEDRAW;
      }
    }

    if (cd_edge_crease_offset != -1) {
      med->crease = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(eed, cd_edge_crease_offset);
    }
    if (cd_edge_bweight_offset != -1) {
      med->bweight = MESH_ELEM_CD_GET_FLOAT_AS_UCHAR(eed, cd_edge_bweight_offset);
    }

    CustomData_from_mesh_block(&mesh->edata, &me->edata, eed->head.data, i);
  }
  mesh->elem_index_dirty &= ~MESH_EDGE;

  j = 0;
  MESH_INDEX_ITER (efa, &iter, mesh, MESH_FACES_OF_MESH, i) {
    MeshLoop *l_iter;
    MeshLoop *l_first;
    MeshPoly *mp = &mpoly[i];

    mesh_elem_index_set(efa, i); /* set_inline */

    mp->totloop = efa->len;
    mp->flag = mesh_face_flag_to_mflag(efa);
    mp->loopstart = j;
    mp->mat_nr = efa->mat_nr;

    l_iter = l_first = MESH_FACE_FIRST_LOOP(efa);
    do {
      mloop->v = mesh_elem_index_get(l_iter->v);
      mloop->e = mesh_elem_index_get(l_iter->e);
      CustomData_from_mesh_block(&mesh->ldata, &me->ldata, l_iter->head.data, j);

      mesh_elem_index_set(l_iter, j); /* set_inline */

      j++;
      mloop++;
    } while ((l_iter = l_iter->next) != l_first);

    CustomData_from_mesh_block(&mesh->pdata, &me->pdata, efa->head.data, i);
  }
  mesh->elem_index_dirty &= ~(MESH_FACE | MESH_LOOP);

  me->cd_flag = mesh_cd_flag_from_bmesh(bm);
}
