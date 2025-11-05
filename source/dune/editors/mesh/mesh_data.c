#include "mem_guardedalloc.h"

#include "types_mesh_types.h"
#include "types_meshdata_types.h"
#include "types_object_types.h"
#include "types_scene_types.h"
#include "types_view3d_types.h"

#include "lib_alloca.h"
#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_cx.h"
#include "dune_customdata.h"
#include "dune_editmesh.h"
#include "dune_mesh.h"
#include "dune_mesh_runtime.h"
#include "dune_report.h"

#include "graph.h"

#include "api_access.h"
#include "api_define.h"
#include "api_prototypes.h"

#include "wm_api.h"
#include "wm_types.h"

#include "ed_mesh.h"
#include "ed_object.h"
#include "ed_paint.h"
#include "ed_screen.h"
#include "ed_uvedit.h"
#include "ed_view3d.h"

#include "mesh_intern.h" /* own include */

static CustomData *mesh_customdata_get_type(Mesh *me, const char htype, int *r_tot)
{
  CustomData *data;
  Mesh *m = (me->edit_mesh) ? me->edit_mesh->bm : NULL;
  int tot;

  switch (htype) {
    case M_VERT:
      if (m) {
        data = &m->vdata;
        tot = m->totvert;
      }
      else {
        data = &me->vdata;
        tot = me->totvert;
      }
      break;
    case M_EDGE:
      if (m) {
        data = &m->edata;
        tot = m->totedge;
      }
      else {
        data = &me->edata;
        tot = me->totedge;
      }
      break;
    case M_LOOP:
      if (m) {
        data = &m->ldata;
        tot = m->totloop;
      }
      else {
        data = &me->ldata;
        tot = me->totloop;
      }
      break;
    case M_FACE:
      if (m) {
        data = &m->pdata;
        tot = m->totface;
      }
      else {
        data = &me->pdata;
        tot = me->totpoly;
      }
      break;
    default:
      lib_assert(0);
      tot = 0;
      data = NULL;
      break;
  }

  *r_tot = tot;
  return data;
}

#define GET_CD_DATA(me, data) ((me)->edit_mesh ? &(me)->edit_mesh->mesh->data : &(me)->data)
static void delete_customdata_layer(Mesh *me, CustomDataLayer *layer)
{
  const int type = layer->type;
  CustomData *data;
  int layer_idx, tot, n;

  char htype = M_FACE;
  if (elem(type, CD_MLOOPCOL, CD_MLOOPUV)) {
    htype = M_LOOP;
  }
  else if (elem(type, CD_PROP_COLOR)) {
    htype = M_VERT;
  }

  data = mesh_customdata_get_type(me, htype, &tot);
  layer_idx = CustomData_get_layer_idx(data, type);
  n = (layer - &data->layers[layer_idx]);
  lib_assert(n >= 0 && (n + layer_idx) < data->totlayer);

  if (me->edit_mesh) {
    m_data_layer_free_n(me->edit_mesh->mesh, data, type, n);
  }
  else {
    CustomData_free_layer(data, type, tot, layer_idx + n);
    dune_mesh_update_customdata_ptrs(me, true);
  }
}

static void mesh_uv_reset_arr(float **fuv, const int len)
{
  if (len == 3) {
    fuv[0][0] = 0.0;
    fuv[0][1] = 0.0;

    fuv[1][0] = 1.0;
    fuv[1][1] = 0.0;

    fuv[2][0] = 1.0;
    fuv[2][1] = 1.0;
  }
  else if (len == 4) {
    fuv[0][0] = 0.0;
    fuv[0][1] = 0.0;

    fuv[1][0] = 1.0;
    fuv[1][1] = 0.0;

    fuv[2][0] = 1.0;
    fuv[2][1] = 1.0;

    fuv[3][0] = 0.0;
    fuv[3][1] = 1.0;
    /* Make sure we ignore 2-sided faces. */
  }
  else if (len > 2) {
    float fac = 0.0f, dfac = 1.0f / (float)len;

    dfac *= (float)M_PI * 2.0f;

    for (int i = 0; i < len; i++) {
      fuv[i][0] = 0.5f * sinf(fac) + 0.5f;
      fuv[i][1] = 0.5f * cosf(fac) + 0.5f;

      fac += dfac;
    }
  }
}

static void mesh_uv_reset_mface(MFace *f, const int cd_loop_uv_offset)
{
  float **fuv = lib_arr_alloca(fuv, f->len);
  MIter liter;
  MLoop *l;
  int i;

  M_ITER_ELEM_IDX (l, &liter, f, M_LOOPS_OF_FACE, i) {
    fuv[i] = ((MLoopUV *)M_ELEM_CD_GET_VOID_P(l, cd_loop_uv_offset))->uv;
  }

  mesh_uv_reset_arr(fuv, f->len);
}

static void mesh_uv_reset_mface(MPoly *mp, MLoopUV *mloopuv)
{
  float **fuv = lib_arr_alloca(fuv, mp->totloop);

  for (int i = 0; i < mp->totloop; i++) {
    fuv[i] = mloopuv[mp->loopstart + i].uv;
  }

  mesh_uv_reset_array(fuv, mp->totloop);
}

void ed_mesh_uv_loop_reset_ex(struct Mesh *me, const int layernum)
{
  MEditMesh *em = me->edit_mesh;

  if (em) {
    /* Collect BMesh UVs */
    const int cd_loop_uv_offset = CustomData_get_n_offset(&em->mesh->ldata, CD_MLOOPUV, layernum);

    MFace *efa;
    MIter iter;

    lib_assert(cd_loop_uv_offset != -1);

    M_ITER_MESH (efa, &iter, em->mesh, M_FACES_OF_MESH) {
      if (!m_elem_flag_test(efa, M_ELEM_SEL)) {
        continue;
      }

      mesh_uv_reset_bmface(efa, cd_loop_uv_offset);
    }
  }
  else {
    /* Collect Mesh UVs */
    lib_assert(CustomData_has_layer(&me->ldata, CD_MLOOPUV));
    MLoopUV *mloopuv = CustomData_get_layer_n(&me->ldata, CD_MLOOPUV, layernum);

    for (int i = 0; i < me->totpoly; i++) {
      mesh_uv_reset_mface(&me->mpoly[i], mloopuv);
    }
  }

  graph_id_tag_update(&me->id, 0);
}

void ed_mesh_uv_loop_reset(struct Cx *C, struct Mesh *me)
{
  /* could be ldata or pdata */
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int layernum = CustomData_get_active_layer(ldata, CD_MLOOPUV);
  ed_mesh_uv_loop_reset_ex(me, layernum);

  wm_event_add_notifier(C, NC_GEOM | ND_DATA, me);
}

int ed_mesh_uv_texture_add(
    Mesh *me, const char *name, const bool active_set, const bool do_init, ReportList *reports)
{
  /* NOTE: keep in sync with #ED_mesh_color_add. */

  MEditMesh *em;
  int layernum_dst;

  bool is_init = false;

  if (me->edit_mesh) {
    em = me->edit_mesh;

    layernum_dst = CustomData_number_of_layers(&em->bm->ldata, CD_MLOOPUV);
    if (layernum_dst >= MAX_MTFACE) {
      dune_reportf(reports, RPT_WARNING, "Cannot add more than %i UV maps", MAX_MTFACE);
      return -1;
    }

    /* CD_MLOOPUV */
    mesh_data_layer_add_named(em->mesh, &em->mesh->ldata, CD_MLOOPUV, name);
    /* copy data from active UV */
    if (layernum_dst && do_init) {
      const int layernum_src = CustomData_get_active_layer(&em->mesh->ldata, CD_MLOOPUV);
      mesh_data_layer_copy(em->mesh, &em->mesh->ldata, CD_MLOOPUV, layernum_src, layernum_dst);

      is_init = true;
    }
    if (active_set || layernum_dst == 0) {
      CustomData_set_layer_active(&em->mesh->ldata, CD_MLOOPUV, layernum_dst);
    }
  }
  else {
    layernum_dst = CustomData_number_of_layers(&me->ldata, CD_MLOOPUV);
    if (layernum_dst >= MAX_MTFACE) {
      dune_reportf(reports, RPT_WARNING, "Cannot add more than %i UV maps", MAX_MTFACE);
      return -1;
    }

    if (me->mloopuv && do_init) {
      CustomData_add_layer_named(
          &me->ldata, CD_MLOOPUV, CD_DUPLICATE, me->mloopuv, me->totloop, name);
      is_init = true;
    }
    else {
      CustomData_add_layer_named(&me->ldata, CD_MLOOPUV, CD_DEFAULT, NULL, me->totloop, name);
    }

    if (active_set || layernum_dst == 0) {
      CustomData_set_layer_active(&me->ldata, CD_MLOOPUV, layernum_dst);
    }

    dune_mesh_update_customdata_ptrs(me, true);
  }

  /* don't overwrite our copied coords */
  if (!is_init && do_init) {
    ed_mesh_uv_loop_reset_ex(me, layernum_dst);
  }

  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return layernum_dst;
}

void ed_mesh_uv_texture_ensure(struct Mesh *me, const char *name)
{
  MEditMesh *em;
  int layernum_dst;

  if (me->edit_mesh) {
    em = me->edit_mesh;

    layernum_dst = CustomData_number_of_layers(&em->mesh->ldata, CD_MLOOPUV);
    if (layernum_dst == 0) {
      ed_mesh_uv_texture_add(me, name, true, true, NULL);
    }
  }
  else {
    layernum_dst = CustomData_number_of_layers(&me->ldata, CD_MLOOPUV);
    if (layernum_dst == 0) {
      ed_mesh_uv_texture_add(me, name, true, true, NULL);
    }
  }
}

bool ed_mesh_uv_texture_remove_index(Mesh *me, const int n)
{
  CustomData *ldata = GET_CD_DATA(me, ldata);
  CustomDataLayer *cdlu;
  int index;

  idx = CustomData_get_layer_idx_n(ldata, CD_MLOOPUV, n);
  cdlu = (index == -1) ? NULL : &ldata->layers[idx];

  if (!cdlu) {
    return false;
  }

  delete_customdata_layer(me, cdlu);

  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return true;
}
bool ED_mesh_uv_texture_remove_active(Mesh *me)
{
  /* texpoly/uv are assumed to be in sync */
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int n = CustomData_get_active_layer(ldata, CD_MLOOPUV);

  if (n != -1) {
    return ed_mesh_uv_texture_remove_idx(me, n);
  }
  return false;
}
bool ed_mesh_uv_texture_remove_named(Mesh *me, const char *name)
{
  /* texpoly/uv are assumed to be in sync */
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int n = CustomData_get_named_layer(ldata, CD_MLOOPUV, name);
  if (n != -1) {
    return ed_mesh_uv_texture_remove_idx(me, n);
  }
  return false;
}

int ed_mesh_color_add(
    Mesh *me, const char *name, const bool active_set, const bool do_init, ReportList *reports)
{
  /* NOTE: keep in sync with #ED_mesh_uv_texture_add. */

  MEditMesh *em;
  int layernum;

  if (me->edit_mesh) {
    em = me->edit_mesh;

    layernum = CustomData_number_of_layers(&em->mesh->ldata, CD_MLOOPCOL);
    if (layernum >= MAX_MCOL) {
      dune_reportf(reports, RPT_WARNING, "Cannot add more than %i vertex color layers", MAX_MCOL);
      return -1;
    }

    /* CD_MLOOPCOL */
    m_data_layer_add_named(em->mesh, &em->mesh->ldata, CD_MLOOPCOL, name);
    /* copy data from active vertex color layer */
    if (layernum && do_init) {
      const int layernum_dst = CustomData_get_active_layer(&em->bm->ldata, CD_MLOOPCOL);
      m_data_layer_copy(em->mesh, &em->mesh->ldata, CD_MLOOPCOL, layernum_dst, layernum);
    }
    if (active_set || layernum == 0) {
      CustomData_set_layer_active(&em->mesh->ldata, CD_MLOOPCOL, layernum);
    }
  }
  else {
    layernum = CustomData_number_of_layers(&me->ldata, CD_MLOOPCOL);
    if (layernum >= MAX_MCOL) {
      BKE_reportf(reports, RPT_WARNING, "Cannot add more than %i vertex color layers", MAX_MCOL);
      return -1;
    }

    if (me->mloopcol && do_init) {
      CustomData_add_layer_named(
          &me->ldata, CD_MLOOPCOL, CD_DUPLICATE, me->mloopcol, me->totloop, name);
    }
    else {
      CustomData_add_layer_named(&me->ldata, CD_MLOOPCOL, CD_DEFAULT, NULL, me->totloop, name);
    }

    if (active_set || layernum == 0) {
      CustomData_set_layer_active(&me->ldata, CD_MLOOPCOL, layernum);
    }

    dune_mesh_update_customdata_pointers(me, true);
  }

  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return layernum;
}

bool ed_mesh_color_ensure(struct Mesh *me, const char *name)
{
  lib_assert(me->edit_mesh == NULL);

  if (!me->mloopcol && me->totloop) {
    CustomData_add_layer_named(&me->ldata, CD_MLOOPCOL, CD_DEFAULT, NULL, me->totloop, name);
    BKE_mesh_update_customdata_pointers(me, true);
  }

  graph_id_tag_update(&me->id, 0);

  return (me->mloopcol != NULL);
}

bool ed_mesh_color_remove_idx(Mesh *me, const int n)
{
  CustomData *ldata = GET_CD_DATA(me, ldata);
  CustomDataLayer *cdl;
  int idx;

  idx = CustomData_get_layer_idx_n(ldata, CD_MLOOPCOL, n);
  cdl = (idx == -1) ? NULL : &ldata->layers[idx];

  if (!cdl) {
    return false;
  }

  delete_customdata_layer(me, cdl);
  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return true;
}
bool ed_mesh_color_remove_active(Mesh *me)
{
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int n = CustomData_get_active_layer(ldata, CD_MLOOPCOL);
  if (n != -1) {
    return ed_mesh_color_remove_idx(me, n);
  }
  return false;
}
bool ed_mesh_color_remove_named(Mesh *me, const char *name)
{
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int n = CustomData_get_named_layer(ldata, CD_MLOOPCOL, name);
  if (n != -1) {
    return ed_mesh_color_remove_index(me, n);
  }
  return false;
}

/*********************** General poll ************************/
static bool layers_poll(Cx *C)
{
  Object *ob = ed_object_cx(C);
  ID *data = (ob) ? ob->data : NULL;
  return (ob && !ID_IS_LINKED(ob) && ob->type == OB_MESH && data && !ID_IS_LINKED(data));
}

/*********************** Sculpt Vertex colors ops ************************/
static bool sculpt_vert_color_remove_poll(Cx *C)
{
  if (!layers_poll(C)) {
    return false;
  }

  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;
  CustomData *vdata = GET_CD_DATA(me, vdata);
  const int active = CustomData_get_active_layer(vdata, CD_PROP_COLOR);
  if (active != -1) {
    return true;
  }

  return false;
}

int ed_mesh_sculpt_color_add(
    Mesh *me, const char *name, const bool active_set, const bool do_init, ReportList *reports)
{
  /* NOTE: keep in sync with #ed_mesh_uv_texture_add. */
  MEditMesh *em;
  int layernum;

  if (me->edit_mesh) {
    em = me->edit_mesh;

    layernum = CustomData_number_of_layers(&em->mesh->vdata, CD_PROP_COLOR);
    if (layernum >= MAX_MCOL) {
      dune_reportf(
          reports, RPT_WARNING, "Cannot add more than %i sculpt vertex color layers", MAX_MCOL);
      return -1;
    }

    /* CD_PROP_COLOR */
    m_data_layer_add_named(em->mesh, &em->mesh->vdata, CD_PROP_COLOR, name);
    /* copy data from active vert color layer */
    if (layernum && do_init) {
      const int layernum_dst = CustomData_get_active_layer(&em->mesh->vdata, CD_PROP_COLOR);
      m_data_layer_copy(em->mesh, &em->mesh->vdata, CD_PROP_COLOR, layernum_dst, layernum);
    }
    if (active_set || layernum == 0) {
      CustomData_set_layer_active(&em->mesh->vdata, CD_PROP_COLOR, layernum);
    }
  }
  else {
    layernum = CustomData_number_of_layers(&me->vdata, CD_PROP_COLOR);
    if (layernum >= MAX_MCOL) {
      dune_reportf(
          reports, RPT_WARNING, "Cannot add more than %i sculpt vert color layers", MAX_MCOL);
      return -1;
    }

    if (CustomData_has_layer(&me->vdata, CD_PROP_COLOR) && do_init) {
      MPropCol *color_data = CustomData_get_layer(&me->vdata, CD_PROP_COLOR);
      CustomData_add_layer_named(
          &me->vdata, CD_PROP_COLOR, CD_DUPLICATE, color_data, me->totvert, name);
    }
    else {
      CustomData_add_layer_named(&me->vdata, CD_PROP_COLOR, CD_DEFAULT, NULL, me->totvert, name);
    }

    if (active_set || layernum == 0) {
      CustomData_set_layer_active(&me->vdata, CD_PROP_COLOR, layernum);
    }

    dune_mesh_update_customdata_ptrs(me, true);
  }

  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return layernum;
}

bool ed_mesh_sculpt_color_ensure(struct Mesh *me, const char *name)
{
  BLI_assert(me->edit_mesh == NULL);

  if (me->totvert && !CustomData_has_layer(&me->vdata, CD_PROP_COLOR)) {
    CustomData_add_layer_named(&me->vdata, CD_PROP_COLOR, CD_DEFAULT, NULL, me->totvert, name);
    dune_mesh_update_customdata_ptrs(me, true);
  }

  graph_id_tag_update(&me->id, 0);

  return (me->mloopcol != NULL);
}

bool ed_mesh_sculpt_color_remove_index(Mesh *me, const int n)
{
  CustomData *vdata = GET_CD_DATA(me, vdata);
  CustomDataLayer *cdl;
  int idx;

  idx = CustomData_get_layer_idx_n(vdata, CD_PROP_COLOR, n);
  cdl = (idx == -1) ? NULL : &vdata->layers[idx];

  if (!cdl) {
    return false;
  }

  delete_customdata_layer(me, cdl);
  graph_id_tag_update(&me->id, 0);
  wm_main_add_notifier(NC_GEOM | ND_DATA, me);

  return true;
}
bool ed_mesh_sculpt_color_remove_active(Mesh *me)
{
  CustomData *vdata = GET_CD_DATA(me, vdata);
  const int n = CustomData_get_active_layer(vdata, CD_PROP_COLOR);
  if (n != -1) {
    return ed_mesh_sculpt_color_remove_index(me, n);
  }
  return false;
}
bool ed_mesh_sculpt_color_remove_named(Mesh *me, const char *name)
{
  CustomData *vdata = GET_CD_DATA(me, vdata);
  const int n = CustomData_get_named_layer(vdata, CD_PROP_COLOR, name);
  if (n != -1) {
    return ed_mesh_sculpt_color_remove_index(me, n);
  }
  return false;
}

/* UV texture ops */
static bool uv_texture_remove_poll(Cx *C)
{
  if (!layers_poll(C)) {
    return false;
  }

  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int active = CustomData_get_active_layer(ldata, CD_MLOOPUV);
  if (active != -1) {
    return true;
  }

  return false;
}

static int mesh_uv_texture_add_exec(Cx *C, wmOp *op)
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (ed_mesh_uv_texture_add(me, NULL, true, true, op->reports) == -1) {
    return OP_CANCELLED;
  }

  if (ob->mode & OB_MODE_TEXTURE_PAINT) {
    Scene *scene = cx_data_scene(C);
    ed_paint_proj_mesh_data_check(scene, ob, NULL, NULL, NULL, NULL);
    wm_event_add_notifier(C, NC_SCENE | ND_TOOLSETTINGS, NULL);
  }

  return OP_FINISHED;
}

void mesh_ot_uv_texture_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add UV Map";
  ot->description = "Add UV map";
  ot->idname = "MESH_OT_uv_texture_add";

  /* api callbacks */
  ot->poll = layers_poll;
  ot->exec = mesh_uv_texture_add_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int mesh_uv_texture_remove_exec(Cx *C, wmOp *UNUSED(op))
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (!ed_mesh_uv_texture_remove_active(me)) {
    return OP_CANCELLED;
  }

  if (ob->mode & OB_MODE_TEXTURE_PAINT) {
    Scene *scene = cx_data_scene(C);
    ed_paint_proj_mesh_data_check(scene, ob, NULL, NULL, NULL, NULL);
    wm_event_add_notifier(C, NC_SCENE | ND_TOOLSETTINGS, NULL);
  }

  return OP_FINISHED;
}

void mesh_ot_uv_texture_remove(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Remove UV Map";
  ot->description = "Remove UV map";
  ot->idname = "MESH_OT_uv_texture_remove";

  /* api callbacks */
  ot->poll = uv_texture_remove_poll;
  ot->exec = mesh_uv_texture_remove_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* vertex color ops */
static bool vert_color_remove_poll(Cx *C)
{
  if (!layers_poll(C)) {
    return false;
  }

  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;
  CustomData *ldata = GET_CD_DATA(me, ldata);
  const int active = CustomData_get_active_layer(ldata, CD_MLOOPCOL);
  if (active != -1) {
    return true;
  }

  return false;
}

static int mesh_vert_color_add_exec(Cx *C, wmOp *op)
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (ed_mesh_color_add(me, NULL, true, true, op->reports) == -1) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void mesh_ot_vert_color_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Vertex Color";
  ot->description = "Add vertex color layer";
  ot->idname = "mesh_ot_vert_color_add";

  /* api callbacks */
  ot->poll = layers_poll;
  ot->exec = mesh_vert_color_add_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int mesh_vert_color_remove_exec(Cx *C, wmOp *UNUSED(op))
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (!ED_mesh_color_remove_active(me)) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void mesh_ot_vert_color_remove(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Remove Vertex Color";
  ot->description = "Remove vertex color layer";
  ot->idname = "mesh_ot_vert_color_remove";

  /* api callbacks */
  ot->exec = mesh_vert_color_remove_exec;
  ot->poll = vert_color_remove_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/*********************** Sculpt Vertex Color Operators ************************/
static int mesh_sculpt_vert_color_add_exec(Cx *C, wmOp *op)
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (ed_mesh_sculpt_color_add(me, NULL, true, true, op->reports) == -1) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void mesh_ot_sculpt_vert_color_add(wmOpType *ot)
{
  /* identifiers */
  ot->name = "Add Sculpt Vert Color";
  ot->description = "Add vert color layer";
  ot->idname = "mesh_ot_sculpt_vert_color_add";

  /* api callbacks */
  ot->poll = layers_poll;
  ot->exec = mesh_sculpt_vert_color_add_exec;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int mesh_sculpt_vert_color_remove_exec(Cx *C, wmOp *UNUSED(op))
{
  Object *ob = ed_object_cx(C);
  Mesh *me = ob->data;

  if (!ed_mesh_sculpt_color_remove_active(me)) {
    return OP_CANCELLED;
  }

  return OP_FINISHED;
}

void MESH_OT_sculpt_vertex_color_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Sculpt Vertex Color";
  ot->description = "Remove vertex color layer";
  ot->idname = "MESH_OT_sculpt_vertex_color_remove";

  /* api callbacks */
  ot->exec = mesh_sculpt_vertex_color_remove_exec;
  ot->poll = sculpt_vertex_color_remove_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* *** CustomData clear functions, we need an operator for each *** */

static int mesh_customdata_clear_exec__internal(bContext *C, char htype, int type)
{
  Mesh *me = ED_mesh_context(C);

  int tot;
  CustomData *data = mesh_customdata_get_type(me, htype, &tot);

  BLI_assert(CustomData_layertype_is_singleton(type) == true);

  if (CustomData_has_layer(data, type)) {
    if (me->edit_mesh) {
      BM_data_layer_free(me->edit_mesh->bm, data, type);
    }
    else {
      CustomData_free_layers(data, type, tot);
    }

    DEG_id_tag_update(&me->id, 0);
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, me);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

/* Clear Mask */
static bool mesh_customdata_mask_clear_poll(bContext *C)
{
  Object *ob = ED_object_context(C);
  if (ob && ob->type == OB_MESH) {
    Mesh *me = ob->data;

    /* special case - can't run this if we're in sculpt mode */
    if (ob->mode & OB_MODE_SCULPT) {
      return false;
    }

    if (!ID_IS_LINKED(me)) {
      CustomData *data = GET_CD_DATA(me, vdata);
      if (CustomData_has_layer(data, CD_PAINT_MASK)) {
        return true;
      }
      data = GET_CD_DATA(me, ldata);
      if (CustomData_has_layer(data, CD_GRID_PAINT_MASK)) {
        return true;
      }
    }
  }
  return false;
}
static int mesh_customdata_mask_clear_exec(bContext *C, wmOperator *UNUSED(op))
{
  int ret_a = mesh_customdata_clear_exec__internal(C, BM_VERT, CD_PAINT_MASK);
  int ret_b = mesh_customdata_clear_exec__internal(C, BM_LOOP, CD_GRID_PAINT_MASK);

  if (ret_a == OPERATOR_FINISHED || ret_b == OPERATOR_FINISHED) {
    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_customdata_mask_clear(wmOperatorType *ot)
{
  /* NOTE: no create_mask yet */

  /* identifiers */
  ot->name = "Clear Sculpt Mask Data";
  ot->idname = "MESH_OT_customdata_mask_clear";
  ot->description = "Clear vertex sculpt masking data from the mesh";

  /* api callbacks */
  ot->exec = mesh_customdata_mask_clear_exec;
  ot->poll = mesh_customdata_mask_clear_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/**
 * Clear Skin
 * \return -1 invalid state, 0 no skin, 1 has skin.
 */
static int mesh_customdata_skin_state(bContext *C)
{
  Object *ob = ED_object_context(C);

  if (ob && ob->type == OB_MESH) {
    Mesh *me = ob->data;
    if (!ID_IS_LINKED(me)) {
      CustomData *data = GET_CD_DATA(me, vdata);
      return CustomData_has_layer(data, CD_MVERT_SKIN);
    }
  }
  return -1;
}

static bool mesh_customdata_skin_add_poll(bContext *C)
{
  return (mesh_customdata_skin_state(C) == 0);
}

static int mesh_customdata_skin_add_exec(bContext *C, wmOperator *UNUSED(op))
{
  Object *ob = ED_object_context(C);
  Mesh *me = ob->data;

  BKE_mesh_ensure_skin_customdata(me);

  DEG_id_tag_update(&me->id, 0);
  WM_event_add_notifier(C, NC_GEOM | ND_DATA, me);

  return OPERATOR_FINISHED;
}

void MESH_OT_customdata_skin_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Skin Data";
  ot->idname = "MESH_OT_customdata_skin_add";
  ot->description = "Add a vertex skin layer";

  /* api callbacks */
  ot->exec = mesh_customdata_skin_add_exec;
  ot->poll = mesh_customdata_skin_add_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static bool mesh_customdata_skin_clear_poll(bContext *C)
{
  return (mesh_customdata_skin_state(C) == 1);
}

static int mesh_customdata_skin_clear_exec(bContext *C, wmOperator *UNUSED(op))
{
  return mesh_customdata_clear_exec__internal(C, BM_VERT, CD_MVERT_SKIN);
}

void MESH_OT_customdata_skin_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Skin Data";
  ot->idname = "MESH_OT_customdata_skin_clear";
  ot->description = "Clear vertex skin layer";

  /* api callbacks */
  ot->exec = mesh_customdata_skin_clear_exec;
  ot->poll = mesh_customdata_skin_clear_poll;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* Clear custom loop normals */
static int mesh_customdata_custom_splitnormals_add_exec(bContext *C, wmOperator *UNUSED(op))
{
  Mesh *me = ED_mesh_context(C);

  if (!BKE_mesh_has_custom_loop_normals(me)) {
    CustomData *data = GET_CD_DATA(me, ldata);

    if (me->edit_mesh) {
      /* Tag edges as sharp according to smooth threshold if needed,
       * to preserve autosmooth shading. */
      if (me->flag & ME_AUTOSMOOTH) {
        BM_edges_sharp_from_angle_set(me->edit_mesh->bm, me->smoothresh);
      }

      BM_data_layer_add(me->edit_mesh->bm, data, CD_CUSTOMLOOPNORMAL);
    }
    else {
      /* Tag edges as sharp according to smooth threshold if needed,
       * to preserve autosmooth shading. */
      if (me->flag & ME_AUTOSMOOTH) {
        BKE_edges_sharp_from_angle_set(me->mvert,
                                       me->totvert,
                                       me->medge,
                                       me->totedge,
                                       me->mloop,
                                       me->totloop,
                                       me->mpoly,
                                       BKE_mesh_poly_normals_ensure(me),
                                       me->totpoly,
                                       me->smoothresh);
      }

      CustomData_add_layer(data, CD_CUSTOMLOOPNORMAL, CD_DEFAULT, NULL, me->totloop);
    }

    DEG_id_tag_update(&me->id, 0);
    WM_event_add_notifier(C, NC_GEOM | ND_DATA, me);

    return OPERATOR_FINISHED;
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_customdata_custom_splitnormals_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Custom Split Normals Data";
  ot->idname = "MESH_OT_customdata_custom_splitnormals_add";
  ot->description = "Add a custom split normals layer, if none exists yet";

  /* api callbacks */
  ot->exec = mesh_customdata_custom_splitnormals_add_exec;
  ot->poll = ED_operator_editable_mesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

static int mesh_customdata_custom_splitnormals_clear_exec(bContext *C, wmOperator *UNUSED(op))
{
  Mesh *me = ED_mesh_context(C);

  if (BKE_mesh_has_custom_loop_normals(me)) {
    BMEditMesh *em = me->edit_mesh;
    if (em != NULL && em->bm->lnor_spacearr != NULL) {
      BKE_lnor_spacearr_clear(em->bm->lnor_spacearr);
    }
    return mesh_customdata_clear_exec__internal(C, BM_LOOP, CD_CUSTOMLOOPNORMAL);
  }
  return OPERATOR_CANCELLED;
}

void MESH_OT_customdata_custom_splitnormals_clear(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Clear Custom Split Normals Data";
  ot->idname = "MESH_OT_customdata_custom_splitnormals_clear";
  ot->description = "Remove the custom split normals layer, if it exists";

  /* api callbacks */
  ot->exec = mesh_customdata_custom_splitnormals_clear_exec;
  ot->poll = ED_operator_editable_mesh;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/************************** Add Geometry Layers *************************/

void ED_mesh_update(Mesh *mesh, bContext *C, bool calc_edges, bool calc_edges_loose)
{
  if (calc_edges || ((mesh->totpoly || mesh->totface) && mesh->totedge == 0)) {
    BKE_mesh_calc_edges(mesh, calc_edges, true);
  }

  if (calc_edges_loose && mesh->totedge) {
    BKE_mesh_calc_edges_loose(mesh);
  }

  /* Default state is not to have tessface's so make sure this is the case. */
  BKE_mesh_tessface_clear(mesh);

  BKE_mesh_calc_normals(mesh);

  DEG_id_tag_update(&mesh->id, 0);
  WM_event_add_notifier(C, NC_GEOM | ND_DATA, mesh);
}

static void mesh_add_verts(Mesh *mesh, int len)
{
  if (len == 0) {
    return;
  }

  int totvert = mesh->totvert + len;
  CustomData vdata;
  CustomData_copy(&mesh->vdata, &vdata, CD_MASK_MESH.vmask, CD_DEFAULT, totvert);
  CustomData_copy_data(&mesh->vdata, &vdata, 0, 0, mesh->totvert);

  if (!CustomData_has_layer(&vdata, CD_MVERT)) {
    CustomData_add_layer(&vdata, CD_MVERT, CD_CALLOC, NULL, totvert);
  }

  CustomData_free(&mesh->vdata, mesh->totvert);
  mesh->vdata = vdata;
  BKE_mesh_update_customdata_pointers(mesh, false);

  BKE_mesh_runtime_clear_cache(mesh);

  /* scan the input list and insert the new vertices */

  /* set default flags */
  MVert *mvert = &mesh->mvert[mesh->totvert];
  for (int i = 0; i < len; i++, mvert++) {
    mvert->flag |= SELECT;
  }

  /* set final vertex list size */
  mesh->totvert = totvert;
}

static void mesh_add_edges(Mesh *mesh, int len)
{
  CustomData edata;
  MEdge *medge;
  int i, totedge;

  if (len == 0) {
    return;
  }

  totedge = mesh->totedge + len;

  /* Update custom-data. */
  CustomData_copy(&mesh->edata, &edata, CD_MASK_MESH.emask, CD_DEFAULT, totedge);
  CustomData_copy_data(&mesh->edata, &edata, 0, 0, mesh->totedge);

  if (!CustomData_has_layer(&edata, CD_MEDGE)) {
    CustomData_add_layer(&edata, CD_MEDGE, CD_CALLOC, NULL, totedge);
  }

  CustomData_free(&mesh->edata, mesh->totedge);
  mesh->edata = edata;
  BKE_mesh_update_customdata_pointers(mesh, false); /* new edges don't change tessellation */

  BKE_mesh_runtime_clear_cache(mesh);

  /* set default flags */
  medge = &mesh->medge[mesh->totedge];
  for (i = 0; i < len; i++, medge++) {
    medge->flag = ME_EDGEDRAW | ME_EDGERENDER | SELECT;
  }

  mesh->totedge = totedge;
}

static void mesh_add_loops(Mesh *mesh, int len)
{
  CustomData ldata;
  int totloop;

  if (len == 0) {
    return;
  }

  totloop = mesh->totloop + len; /* new face count */

  /* update customdata */
  CustomData_copy(&mesh->ldata, &ldata, CD_MASK_MESH.lmask, CD_DEFAULT, totloop);
  CustomData_copy_data(&mesh->ldata, &ldata, 0, 0, mesh->totloop);

  if (!CustomData_has_layer(&ldata, CD_MLOOP)) {
    CustomData_add_layer(&ldata, CD_MLOOP, CD_CALLOC, NULL, totloop);
  }

  BKE_mesh_runtime_clear_cache(mesh);

  CustomData_free(&mesh->ldata, mesh->totloop);
  mesh->ldata = ldata;
  BKE_mesh_update_customdata_pointers(mesh, true);

  mesh->totloop = totloop;
}

static void mesh_add_polys(Mesh *mesh, int len)
{
  CustomData pdata;
  MPoly *mpoly;
  int i, totpoly;

  if (len == 0) {
    return;
  }

  totpoly = mesh->totpoly + len; /* new face count */

  /* update customdata */
  CustomData_copy(&mesh->pdata, &pdata, CD_MASK_MESH.pmask, CD_DEFAULT, totpoly);
  CustomData_copy_data(&mesh->pdata, &pdata, 0, 0, mesh->totpoly);

  if (!CustomData_has_layer(&pdata, CD_MPOLY)) {
    CustomData_add_layer(&pdata, CD_MPOLY, CD_CALLOC, NULL, totpoly);
  }

  CustomData_free(&mesh->pdata, mesh->totpoly);
  mesh->pdata = pdata;
  BKE_mesh_update_customdata_pointers(mesh, true);

  BKE_mesh_runtime_clear_cache(mesh);

  /* set default flags */
  mpoly = &mesh->mpoly[mesh->totpoly];
  for (i = 0; i < len; i++, mpoly++) {
    mpoly->flag = ME_FACE_SEL;
  }

  mesh->totpoly = totpoly;
}

/* -------------------------------------------------------------------- */
/** \name Add Geometry
 * \{ */

void ED_mesh_verts_add(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot add vertices in edit mode");
    return;
  }
  mesh_add_verts(mesh, count);
}

void ED_mesh_edges_add(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot add edges in edit mode");
    return;
  }
  mesh_add_edges(mesh, count);
}

void ED_mesh_loops_add(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot add loops in edit mode");
    return;
  }
  mesh_add_loops(mesh, count);
}

void ED_mesh_polys_add(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot add polygons in edit mode");
    return;
  }
  mesh_add_polys(mesh, count);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Remove Geometry
 * \{ */

static void mesh_remove_verts(Mesh *mesh, int len)
{
  if (len == 0) {
    return;
  }
  const int totvert = mesh->totvert - len;
  CustomData_free_elem(&mesh->vdata, totvert, len);
  mesh->totvert = totvert;
}

static void mesh_remove_edges(Mesh *mesh, int len)
{
  if (len == 0) {
    return;
  }
  const int totedge = mesh->totedge - len;
  CustomData_free_elem(&mesh->edata, totedge, len);
  mesh->totedge = totedge;
}

static void mesh_remove_loops(Mesh *mesh, int len)
{
  if (len == 0) {
    return;
  }
  const int totloop = mesh->totloop - len;
  CustomData_free_elem(&mesh->ldata, totloop, len);
  mesh->totloop = totloop;
}

static void mesh_remove_polys(Mesh *mesh, int len)
{
  if (len == 0) {
    return;
  }
  const int totpoly = mesh->totpoly - len;
  CustomData_free_elem(&mesh->pdata, totpoly, len);
  mesh->totpoly = totpoly;
}

void ED_mesh_verts_remove(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot remove vertices in edit mode");
    return;
  }
  if (count > mesh->totvert) {
    BKE_report(reports, RPT_ERROR, "Cannot remove more vertices than the mesh contains");
    return;
  }

  mesh_remove_verts(mesh, count);
}

void ED_mesh_edges_remove(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot remove edges in edit mode");
    return;
  }
  if (count > mesh->totedge) {
    BKE_report(reports, RPT_ERROR, "Cannot remove more edges than the mesh contains");
    return;
  }

  mesh_remove_edges(mesh, count);
}

void ED_mesh_loops_remove(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    BKE_report(reports, RPT_ERROR, "Cannot remove loops in edit mode");
    return;
  }
  if (count > mesh->totloop) {
    BKE_report(reports, RPT_ERROR, "Cannot remove more loops than the mesh contains");
    return;
  }

  mesh_remove_loops(mesh, count);
}

void ed_mesh_polys_remove(Mesh *mesh, ReportList *reports, int count)
{
  if (mesh->edit_mesh) {
    dune_report(reports, RPT_ERR, "Cannot remove polys in edit mode");
    return;
  }
  if (count > mesh->totpoly) {
    dune_report(reports, RPT_ERR, "Cannot remove more polys than the mesh contains");
    return;
  }

  mesh_remove_polys(mesh, count);
}

void ed_mesh_geometry_clear(Mesh *mesh)
{
  mesh_remove_verts(mesh, mesh->totvert);
  mesh_remove_edges(mesh, mesh->totedge);
  mesh_remove_loops(mesh, mesh->totloop);
  mesh_remove_polys(mesh, mesh->totpoly);
}

void ed_mesh_report_mirror_ex(wmOp *op, int totmirr, int totfail, char selmode)
{
  const char *elem_type;

  if (selmode & SCE_SEL_VERT) {
    elem_type = "vertices";
  }
  else if (selectmode & SCE_SEL_EDGE) {
    elem_type = "edges";
  }
  else {
    elem_type = "faces";
  }

  if (totfail) {
    dune_reportf(
        op->reports, RPT_WARNING, "%d %s mirrored, %d failed", totmirr, elem_type, totfail);
  }
  else {
    dune_reportf(op->reports, RPT_INFO, "%d %s mirrored", totmirr, elem_type);
  }
}

void ed_mesh_report_mirror(wmOp *op, int totmirr, int totfail)
{
  ed_mesh_report_mirror_ex(op, totmirr, totfail, SCE_SEL_VERT);
}

Mesh *ed_mesh_cx(struct Cx *C)
{
  Mesh *mesh = cx_data_ptr_get_type(C, "mesh", &API_Mesh).data;
  if (mesh != NULL) {
    return mesh;
  }

  Object *ob = ed_object_active_cx(C);
  if (ob == NULL) {
    return NULL;
  }

  ID *data = (ID *)ob->data;
  if (data == NULL || GS(data->name) != ID_ME) {
    return NULL;
  }

  return (Mesh *)data;
}
