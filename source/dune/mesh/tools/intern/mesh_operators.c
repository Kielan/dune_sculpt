/** Mesh operator access. **/

#include "mem_guardedalloc.h"

#include "lib_listbase.h"
#include "lib_math.h"
#include "lib_memarena.h"
#include "lib_mempool.h"
#include "lib_string.h"
#include "lib_utildefines.h"

#include "i18n.h"

#include "mesh.h"
#include "intern/mesh_private.h"

/* forward declarations */
static void mesh_op_flag_layer_alloc(Mesh *mesh);
static void mesh_op_flag_layer_free(Mesh *mesh);
static void mesh_op_flag_layer_clear(Mesh *mesh);
static int mesh_op_name_to_slotcode(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *identifier);
static int mesh_op_name_to_slotcode_check(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                      const char *identifier);

const int MESH_OPSLOT_TYPEINFO[MESH_OP_SLOT_TOTAL_TYPES] = {
    0,                /*  0: MESH_OP_SLOT_SENTINEL */
    sizeof(int),      /*  1: MESH_OP_SLOT_BOOL */
    sizeof(int),      /*  2: MESH_OP_SLOT_INT */
    sizeof(float),    /*  3: MESH_OP_SLOT_FLT */
    sizeof(void *),   /*  4: MESH_OP_SLOT_PNT */
    sizeof(void *),   /*  5: MESH_OP_SLOT_PNT */
    0,                /*  6: unused */
    0,                /*  7: unused */
    sizeof(float[3]), /*  8: MESH_OP_SLOT_VEC */
    sizeof(void *),   /*  9: MESH_OP_SLOT_ELEMENT_BUF */
    sizeof(void *),   /* 10: MESH_OP_SLOT_MAPPING */
};

/* Dummy slot so there is something to return when slot name lookup fails */
// static MeshOpSlot MeshOpEmptySlot = {0};

void mesh_op_flag_enable(Mesh *UNUSED(mesh), MeshOp *op, const int op_flag)
{
  op->flag |= op_flag;
}

void mesh_op_flag_disable(Mesh *UNUSED(mesh), MeshOp *op, const int op_flag)
{
  op->flag &= ~op_flag;
}

void mesh_op_push(Mesh *mesh, MeshOp *UNUSED(op))
{
  mesh->toolflag_index++;

  lib_assert(mesh->totflags > 0);

  /* add flag layer, if appropriate */
  if (mesh->toolflag_index > 0) {
    mesh_flag_layer_alloc(mesh);
  }
  else {
    mesh_flag_layer_clear(mesh);
  }
}

void mesh_pop(Mesh *mesh)
{
  if (mesh->toolflag_index > 0) {
    mesh_flag_layer_free(mesh);
  }

  mesh->toolflag_index--;
}

/* use for both slot_types_in and slot_types_out */
static void mesh_op_slots_init(const MeshSlotType *slot_types, MeshOpSlot *slot_args)
{
  MeshOpSlot *slot;
  uint i;
  for (i = 0; slot_types[i].type; i++) {
    slot = &slot_args[i];
    slot->slot_name = slot_types[i].name;
    slot->slot_type = slot_types[i].type;
    slot->slot_subtype = slot_types[i].subtype;
    // slot->index = i;  // UNUSED

    switch (slot->slot_type) {
      case MESH_OP_SLOT_MAPPING:
        slot->data.ghash = lib_ghash_ptr_new("mesh slot map hash");
        break;
      case MESH_OP_SLOT_INT:
        if (ELEM(slot->slot_subtype.intg,
                 MESH_OP_SLOT_SUBTYPE_INT_ENUM,
                 MESH_OP_SLOT_SUBTYPE_INT_FLAG)) {
          slot->data.enum_data.flags = slot_types[i].enum_flags;
          /* Set the first value of the enum as the default value. */
          slot->data.i = slot->data.enum_data.flags[0].value;
        }
      default:
        break;
    }
  }
}

static void mesh_op_slots_free(const MeshOpSlotType *slot_types, MeshOpSlot *slot_args)
{
  MeshOpSlot *slot;
  uint i;
  for (i = 0; slot_types[i].type; i++) {
    slot = &slot_args[i];
    switch (slot->slot_type) {
      case MESH_OP_SLOT_MAPPING:
        lib_ghash_free(slot->data.ghash, NULL, NULL);
        break;
      default:
        break;
    }
  }
}

void mesh_op_init(Mesh *mesh, MeshOp *op, const int flag, const char *opname)
{
  int opcode = mesh_opcode_from_opname(opname);

#ifdef DEBUG
  MESH_ELEM_INDEX_VALIDATE(mesh, "pre bmo", opname);
#else
  (void)bm;
#endif

  if (opcode == -1) {
    opcode = 0; /* error!, already printed, have a better way to handle this? */
  }

  memset(op, 0, sizeof(MeshOp));
  op->type = opcode;
  op->type_flag = mesh_opdefines[opcode]->type_flag;
  op->flag = flag;

  /* initialize the operator slot types */
  mesh_op_slots_init(mesh_opdefines[opcode]->slot_types_in, op->slots_in);
  mesh_op_slots_init(mesh_opdefines[opcode]->slot_types_out, op->slots_out);

  /* callback */
  op->exec = mesh_opdefines[opcode]->exec;

  /* memarena, used for operator's slot buffers */
  op->arena = lib_memarena_new(LIB_MEMARENA_STD_BUFSIZE, __func__);
  lib_memarena_use_calloc(op->arena);
}

void mesh_op_exec(Mesh *mesh, MeshOp *op)
{
  /* allocate tool flags on demand */
  mesh_elem_toolflags_ensure(mesh);

  mesh_push(mesh, op);

  if (mesh->toolflag_index == 1) {
    mesh_edit_begin(mesh, op->type_flag);
  }
  op->exec(mesh, op);

  if (mesh->toolflag_index == 1) {
    mesh_edit_end(mesh, op->type_flag);
  }

  mesh_pop(mesh);
}

void mesh_op_finish(Mesh *mesh, MeshOperator *op)
{
  mesh_op_slots_free(mesh_opdefines[op->type]->slot_types_in, op->slots_in);
  mesh_op_slots_free(mesh_opdefines[op->type]->slot_types_out, op->slots_out);

  lib_memarena_free(op->arena);

#ifdef DEBUG
  MESH_ELEM_INDEX_VALIDATE(mesh, "post meshop", mesh_opdefines[op->type]->opname);

  /* avoid accidental re-use */
  memset(op, 0xff, sizeof(*op));
#else
  (void)mesh;
#endif
}

bool mesh_slot_exists(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *identifier)
{
  int slot_code = mesh_name_to_slotcode(slot_args, identifier);
  return (slot_code >= 0);
}

MeshOpSlot *mesh_slot_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *identifier)
{
  int slot_code = mesh_name_to_slotcode_check(slot_args, identifier);

  if (UNLIKELY(slot_code < 0)) {
    // return &MeshOpEmptySlot;
    lib_assert(0);
    return NULL; /* better crash */
  }

  return &slot_args[slot_code];
}

void mesh_slot_copy(MeshOpSlot slot_args_src[MESH_OP_MAX_SLOTS],
                    const char *slot_name_src,
                    MeshOpSlot slot_args_dst[MESH_OP_MAX_SLOTS],
                    const char *slot_name_dst,
                    struct MemArena *arena_dst)
{
  MeshOpSlot *slot_src = mesh_slot_get(slot_args_src, slot_name_src);
  MeshOpSlot *slot_dst = mesh_slot_get(slot_args_dst, slot_name_dst);

  if (slot_src == slot_dst) {
    return;
  }

  lib_assert(slot_src->slot_type == slot_dst->slot_type);
  if (slot_src->slot_type != slot_dst->slot_type) {
    return;
  }

  if (slot_dst->slot_type == MESH_OP_SLOT_ELEMENT_BUF) {
    /* do buffer copy */
    slot_dst->data.buf = NULL;
    slot_dst->len = slot_src->len;
    if (slot_dst->len) {
      /* check dest has all flags enabled that the source has */
      const eMeshOpSlotSubTypeElem src_elem_flag = (slot_src->slot_subtype.elem & BM_ALL_NOLOOP);
      const eMeshOpSlotSubTypeElem dst_elem_flag = (slot_dst->slot_subtype.elem & BM_ALL_NOLOOP);

      if ((src_elem_flag | dst_elem_flag) == dst_elem_flag) {
        /* pass */
      }
      else {
        /* check types */
        const uint tot = slot_src->len;
        uint i;
        uint out = 0;
        MeshElem **ele_src = (MeshElem **)slot_src->data.buf;
        for (i = 0; i < tot; i++, ele_src++) {
          if ((*ele_src)->head.htype & dst_elem_flag) {
            out++;
          }
        }
        if (out != tot) {
          slot_dst->len = out;
        }
      }

      if (slot_dst->len) {
        const int slot_alloc_size = MESH_OPSLOT_TYPEINFO[slot_dst->slot_type] * slot_dst->len;
        slot_dst->data.buf = lib_memarena_alloc(arena_dst, slot_alloc_size);
        if (slot_src->len == slot_dst->len) {
          memcpy(slot_dst->data.buf, slot_src->data.buf, slot_alloc_size);
        }
        else {
          /* only copy compatible elements */
          const uint tot = slot_src->len;
          uint i;
          MeshElem **ele_src = (MeshElem **)slot_src->data.buf;
          MeshElem **ele_dst = (MeshElem **)slot_dst->data.buf;
          for (i = 0; i < tot; i++, ele_src++) {
            if ((*ele_src)->head.htype & dst_elem_flag) {
              *ele_dst = *ele_src;
              ele_dst++;
            }
          }
        }
      }
    }
  }
  else if (slot_dst->slot_type == MESH_OP_SLOT_MAPPING) {
    GHashIterator gh_iter;
    GHASH_ITER (gh_iter, slot_src->data.ghash) {
      void *key = lib_ghashIterator_getKey(&gh_iter);
      void *val = lib_ghashIterator_getValue(&gh_iter);
      lib_ghash_insert(slot_dst->data.ghash, key, val);
    }
  }
  else {
    slot_dst->data = slot_src->data;
  }
}

/*
 * MESH OPSTACK SET XXX
 *
 * Sets the value of a slot depending on its type
 */

void mesh_slot_float_set(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name, const float f)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_FLT);
  if (!(slot->slot_type == MESH_OP_SLOT_FLT)) {
    return;
  }

  slot->data.f = f;
}

void mesh_slot_int_set(MeshOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, const int i)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_INT);
  if (!(slot->slot_type == MESH_OP_SLOT_INT)) {
    return;
  }

  slot->data.i = i;
}

void mesh_slot_bool_set(MeshOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, const bool i)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_BOOL);
  if (!(slot->slot_type == MESH_OP_SLOT_BOOL)) {
    return;
  }

  slot->data.i = i;
}

void mesh_slot_mat_set(MeshOperator *op,
                       MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                       const char *slot_name,
                       const float *mat,
                       int size)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_MAT);
  if (!(slot->slot_type == MESH_OP_SLOT_MAT)) {
    return;
  }

  slot->len = 4;
  slot->data.p = lib_memarena_alloc(op->arena, sizeof(float[4][4]));

  if (size == 4) {
    copy_m4_m4(slot->data.p, (const float(*)[4])mat);
  }
  else if (size == 3) {
    copy_m4_m3(slot->data.p, (const float(*)[3])mat);
  }
  else {
    fprintf(stderr, "%s: invalid size argument %d (mesh internal error)\n", __func__, size);

    zero_m4(slot->data.p);
  }
}

void mesh_slot_mat4_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                        const char *slot_name,
                        float r_mat[4][4])
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_MAT);
  if (!(slot->slot_type == MESH_OP_SLOT_MAT)) {
    return;
  }

  if (slot->data.p) {
    copy_m4_m4(r_mat, MESH_SLOT_AS_MATRIX(slot));
  }
  else {
    unit_m4(r_mat);
  }
}

void mesh_slot_mat3_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                        const char *slot_name,
                        float r_mat[3][3])
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_MAT);
  if (!(slot->slot_type == MESH_OP_SLOT_MAT)) {
    return;
  }

  if (slot->data.p) {
    copy_m3_m4(r_mat, MESH_SLOT_AS_MATRIX(slot));
  }
  else {
    unit_m3(r_mat);
  }
}

void mesh_slot_ptr_set(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name, void *p)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_PTR);
  if (!(slot->slot_type == MESH_OP_SLOT_PTR)) {
    return;
  }

  slot->data.p = p;
}

void mesh_slot_vec_set(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                       const char *slot_name,
                       const float vec[3])
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_VEC);
  if (!(slot->slot_type == MESH_OP_SLOT_VEC)) {
    return;
  }

  copy_v3_v3(slot->data.vec, vec);
}

float mesh_slot_float_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_FLT);
  if (!(slot->slot_type == MESH_OP_SLOT_FLT)) {
    return 0.0f;
  }

  return slot->data.f;
}

int mesh_slot_int_get(MeshOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_INT);
  if (!(slot->slot_type == MESH_OP_SLOT_INT)) {
    return 0;
  }

  return slot->data.i;
}

bool mesh_slot_bool_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_BOOL);
  if (!(slot->slot_type == MESH_OP_SLOT_BOOL)) {
    return 0;
  }

  return slot->data.i;
}

void *mesh_slot_as_arrayN(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name, int *len)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  void **ret;

  /* could add support for mapping type */
  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);

  ret = mem_mallocn(sizeof(void *) * slot->len, __func__);
  memcpy(ret, slot->data.buf, sizeof(void *) * slot->len);
  *len = slot->len;
  return ret;
}

void *mesh_slot_ptr_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_PTR);
  if (!(slot->slot_type == MESH_OP_SLOT_PTR)) {
    return NULL;
  }

  return slot->data.p;
}

void mesh_slot_vec_get(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name, float r_vec[3])
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_VEC);
  if (!(slot->slot_type == MESH_OP_SLOT_VEC)) {
    return;
  }

  copy_v3_v3(r_vec, slot->data.vec);
}

/*
 * MESH_COUNTFLAG
 *
 * Counts the number of elements of a certain type that have a
 * specific flag enabled (or disabled if test_for_enabled is false).
 */

static int mesh_flag_count(Mesh *mesh,
                           const char htype,
                           const short oflag,
                           const bool test_for_enabled)
{
  int count_vert = 0, count_edge = 0, count_face = 0;

  if (htype & MESH_VERT) {
    MeshIter iter;
    MeshVert *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
      if (mesh_vert_flag_test_bool(mesh, ele, oflag) == test_for_enabled) {
        count_vert++;
      }
    }
  }
  if (htype & MESH_EDGE) {
    MeshIter iter;
    MeshEdge *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      if (mesh_edge_flag_test_bool(mesh, ele, oflag) == test_for_enabled) {
        count_edge++;
      }
    }
  }
  if (htype & MESH_FACE) {
    MeshIter iter;
    MeshFace *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
      if (mesh_face_flag_test_bool(mesh, ele, oflag) == test_for_enabled) {
        count_face++;
      }
    }
  }

  return (count_vert + count_edge + count_face);
}

int mesh_enabled_flag_count(Mesh *mesh, const char htype, const short oflag)
{
  return mesh_flag_count(mesh, htype, oflag, true);
}

int mesh_disabled_flag_count(Mesh *mesh, const char htype, const short oflag)
{
  return mesh_flag_count(mesh, htype, oflag, false);
}

void mesh_flag_disable_all(Mesh *mesh,
                           MeshOp *UNUSED(op),
                           const char htype,
                           const short oflag)
{
  if (htype & MESH_VERT) {
    MeshIter iter;
    MeshVert *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
      mesh_vert_flag_disable(mesh, ele, oflag);
    }
  }
  if (htype & MESH_EDGE) {
    MeshIter iter;
    MeshEdge *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
      mesh_edge_flag_disable(mesh, ele, oflag);
    }
  }
  if (htype & MESH_FACE) {
    MeshIter iter;
    MeshFace *ele;
    MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
      mesh_face_flag_disable(mesh, ele, oflag);
    }
  }
}

void mesh_selected_remap(Mesh *mesh,
                         MeshOpSlot *slot_vert_map,
                         MeshOpSlot *slot_edge_map,
                         MeshOpSlot *slot_face_map,
                         const bool check_select)
{
  if (mesh->selected.first) {
    MeshEditSelection *ese, *ese_next;
    MeshOpSlot *slot_elem_map;

    for (ese = mesh->selected.first; ese; ese = ese_next) {
      ese_next = ese->next;

      switch (ese->htype) {
        case MESH_VERT:
          slot_elem_map = slot_vert_map;
          break;
        case MESH_EDGE:
          slot_elem_map = slot_edge_map;
          break;
        default:
          slot_elem_map = slot_face_map;
          break;
      }

      ese->ele = mesh_slot_map_elem_get(slot_elem_map, ese->ele);

      if (UNLIKELY((ese->ele == NULL) ||
                   (check_select && (mesh_elem_flag_test(ese->ele, MESH_ELEM_SELECT) == false)))) {
        lib_remlink(&mesh->selected, ese);
        mem_freen(ese);
      }
    }
  }

  if (mesh->act_face) {
    MeshFace *f = mesh_slot_map_elem_get(slot_face_map, mesh->act_face);
    if (f) {
      mesh->act_face = f;
    }
  }
}

int mesh_slot_buffer_len(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);

  /* check if its actually a buffer */
  if (slot->slot_type != MESH_OP_SLOT_ELEMENT_BUF) {
    return 0;
  }

  return slot->len;
}

int mesh_slot_map_len(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  lib_assert(slot->slot_type == MESH_OP_SLOT_MAPPING);
  return lib_ghash_len(slot->data.ghash);
}

void mesh_slot_map_insert(MeshOp *op, MeshOpSlot *slot, const void *element, const void *data)
{
  (void)op; /* Ignored in release builds. */

  lib_assert(slot->slot_type == MESH_OP_SLOT_MAPPING);
  MESH_ASSERT_SLOT_IN_OP(slot, op);

  lib_ghash_insert(slot->data.ghash, (void *)element, (void *)data);
}

#if 0
void *mesh_slot_buffer_grow(Mesh *mesh, MeshOp *op, int slot_code, int totadd)
{
  MeshOpSlot *slot = &op->slots[slot_code];
  void *tmp;
  ssize_t allocsize;

  lib_assert(slot->slottype == MESH_OP_SLOT_ELEMENT_BUF);

  /* check if its actually a buffer */
  if (slot->slottype != MESH_OP_SLOT_ELEMENT_BUF) {
    return NULL;
  }

  if (slot->flag & MESH_DYNAMIC_ARRAY) {
    if (slot->len >= slot->size) {
      slot->size = (slot->size + 1 + totadd) * 2;

      allocsize = MESH_OPSLOT_TYPEINFO[mesh_opdefines[op->type]->slot_types[slot_code].type] *
                  slot->size;
      slot->data.buf = mem_recallocn_id(slot->data.buf, allocsize, "opslot dynamic array");
    }

    slot->len += totadd;
  }
  else {
    slot->flag |= MESH_OP_SLOT_DYNAMIC_ARRAY;
    slot->len += totadd;
    slot->size = slot->len + 2;

    allocsize = MESH_OP_SLOT_TYPEINFO[mesh_opdefines[op->type]->slot_types[slot_code].type] *
                slot->len;

    tmp = slot->data.buf;
    slot->data.buf = mem_callocn(allocsize, "opslot dynamic array");
    memcpy(slot->data.buf, tmp, allocsize);
  }

  return slot->data.buf;
}
#endif

void mesh_slot_map_to_flag(Mesh *mesh,
                           MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                           const char *slot_name,
                           const char htype,
                           const short oflag)
{
  GHashIterator gh_iter;
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  MeshElemF *ele_f;

  lib_assert(slot->slot_type == MESH_OP_SLOT_MAPPING);

  GHASH_ITER (gh_iter, slot->data.ghash) {
    ele_f = lib_ghashIterator_getKey(&gh_iter);
    if (ele_f->head.htype & htype) {
      mesh_elem_flag_enable(mesh, ele_f, oflag);
    }
  }
}

void *mesh_slot_buffer_alloc(MeshOp *op,
                             MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                             const char *slot_name,
                             const int len)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);

  /* check if its actually a buffer */
  if (slot->slot_type != MESH_OP_SLOT_ELEMENT_BUF) {
    return NULL;
  }

  slot->len = len;
  if (len) {
    slot->data.buf = lib_memarena_alloc(op->arena, BMO_OPSLOT_TYPEINFO[slot->slot_type] * len);
  }
  else {
    slot->data.buf = NULL;
  }

  return slot->data.buf;
}

void mesh_slot_buffer_from_all(Mesh *mesh,
                               MeshOp *op,
                               MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                               const char *slot_name,
                               const char htype)
{
  MeshOpSlot *output = mesh_slot_get(slot_args, slot_name);
  int totelement = 0, i = 0;

  lib_assert(output->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((output->slot_subtype.elem & BM_ALL_NOLOOP) & htype) == htype);

  if (htype & MESH_VERT) {
    totelement += mesh->totvert;
  }
  if (htype & MESH_EDGE) {
    totelement += mesh->totedge;
  }
  if (htype & MESH_FACE) {
    totelement += mesh->totface;
  }

  if (totelement) {
    MeshIter iter;
    MeshHeader *ele;

    mesh_slot_buffer_alloc(op, slot_args, slot_name, totelement);

    /* TODO: collapse these loops into one. */

    if (htype & MESH_VERT) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }

    if (htype & MESH_EDGE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }

    if (htype & MESH_FACE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }
  }
}

/**
 * MESH_HEADERFLAG_TO_SLOT
 *
 * Copies elements of a certain type, which have a certain header flag
 * enabled/disabled into a slot for an operator.
 */
static void mesh_op_slot_buffer_from_hflag(Mesh *mesh,
                                       MeshOp *op,
                                       MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                       const char *slot_name,
                                       const char htype,
                                       const char hflag,
                                       const bool test_for_enabled)
{
  MeshOpSlot *output = mesh_slot_get(slot_args, slot_name);
  int totelement = 0, i = 0;
  const bool respecthide = ((op->flag & MESH_FLAG_RESPECT_HIDE) != 0) &&
                           ((hflag & MESH_ELEM_HIDDEN) == 0);

  lib_assert(output->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((output->slot_subtype.elem & MESH_ALL_NOLOOP) & htype) == htype);
  lib_assert((output->slot_subtype.elem & MESH_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE) == 0);

  if (test_for_enabled) {
    totelement = mesh_elem_hflag_count_enabled(mesh, htype, hflag, respecthide);
  }
  else {
    totelement = mesh_elem_hflag_count_disabled(bm, htype, hflag, respecthide);
  }

  if (totelement) {
    MeshIter iter;
    MeshElem *ele;

    mesh_slot_buffer_alloc(op, slot_args, slot_name, totelement);

    /* TODO: collapse these loops into one. */

    if (htype & MESH_VERT) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
        if ((!respecthide || !mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) &&
            mesh_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
          output->data.buf[i] = ele;
          i++;
        }
      }
    }

    if (htype & MESH_EDGE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
        if ((!respecthide || !mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) &&
            mesh_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
          output->data.buf[i] = ele;
          i++;
        }
      }
    }

    if (htype & MESH_FACE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
        if ((!respecthide || !mesh_elem_flag_test(ele, MESH_ELEM_HIDDEN)) &&
            mesh_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
          output->data.buf[i] = ele;
          i++;
        }
      }
    }
  }
  else {
    output->len = 0;
  }
}
void mesh_slot_buffer_from_enabled_hflag(Mesh *mesh,
                                        MeshOperator *op,
                                        MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                        const char *slot_name,
                                        const char htype,
                                        const char hflag)
{
  mesh_slot_buffer_from_hflag(mesh, op, slot_args, slot_name, htype, hflag, true);
}

void mesh_slot_buffer_from_disabled_hflag(Mesh *mesh,
                                         MeshOp *op,
                                         MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                         const char *slot_name,
                                         const char htype,
                                         const char hflag)
{
  mesh_slot_buffer_from_hflag(mesh, op, slot_args, slot_name, htype, hflag, false);
}

void mesh_slot_buffer_from_single(MeshOp *op, MeshOpSlot *slot, BMHeader *ele)
{
  MESH_ASSERT_SLOT_IN_OP(slot, op);
  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(slot->slot_subtype.elem & MESH_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE);
  lib_assert(ELEM(slot->len, 0, 1));

  lib_assert(slot->slot_subtype.elem & ele->htype);

  slot->data.buf = lib_memarena_alloc(op->arena, sizeof(void *) * 4); /* XXX, why 'x4' ? */
  slot->len = 1;
  *slot->data.buf = ele;
}

void mesh_op_slot_buffer_from_array(MeshOp *op,
                                    MeshOpSlot *slot,
                                    MeshHeader **ele_buffer,
                                    int ele_buffer_len)
{
  MESH_ASSERT_SLOT_IN_OP(slot, op);
  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(ELEM(slot->len, 0, ele_buffer_len));

  if (slot->data.buf == NULL) {
    slot->data.buf = lib_memarena_alloc(op->arena, sizeof(*slot->data.buf) * ele_buffer_len);
  }

  slot->len = ele_buffer_len;
  memcpy(slot->data.buf, ele_buffer, ele_buffer_len * sizeof(*slot->data.buf));
}

void *mesh_slot_buffer_get_single(MeshOpSlot *slot)
{
  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(slot->slot_subtype.elem & MESH_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE);
  lib_assert(ELEM(slot->len, 0, 1));

  return slot->len ? (MeshHeader *)slot->data.buf[0] : NULL;
}

void mesh_slot_buffer_append(MeshOpSlot slot_args_dst[MESH_OP_MAX_SLOTS],
                             const char *slot_name_dst,
                             MeshOpSlot slot_args_src[MESH_OP_MAX_SLOTS],
                             const char *slot_name_src,
                             struct MemArena *arena_dst)
{
  MeshOpSlot *slot_dst = mesh_slot_get(slot_args_dst, slot_name_dst);
  MeshOpSlot *slot_src = mesh_slot_get(slot_args_src, slot_name_src);

  lib_assert(slot_dst->slot_type == MESH_OP_SLOT_ELEMENT_BUF &&
             slot_src->slot_type == MESH_OP_SLOT_ELEMENT_BUF);

  if (slot_dst->len == 0) {
    /* output slot is empty, copy rather than append */
    mesh_slot_copy(slot_args_src, slot_name_src, slot_args_dst, slot_name_dst, arena_dst);
  }
  else if (slot_src->len != 0) {
    int elem_size = MESH_OPSLOT_TYPEINFO[slot_dst->slot_type];
    int alloc_size = elem_size * (slot_dst->len + slot_src->len);
    /* allocate new buffer */
    void *buf = lib_memarena_alloc(arena_dst, alloc_size);

    /* copy slot data */
    memcpy(buf, slot_dst->data.buf, elem_size * slot_dst->len);
    memcpy(
        ((char *)buf) + elem_size * slot_dst->len, slot_src->data.buf, elem_size * slot_src->len);

    slot_dst->data.buf = buf;
    slot_dst->len += slot_src->len;
  }
}

/**
 * MESH_FLAG_TO_SLOT
 *
 * Copies elements of a certain type, which have a certain flag set
 * into an output slot for an operator.
 */
static void mesh_slot_buffer_from_flag(Mesh *mesh,
                                       MeshOp *op,
                                       MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                       const char *slot_name,
                                       const char htype,
                                       const short oflag,
                                       const bool test_for_enabled)
{
  MeshOpSlot *slot = mesh_op_slot_get(slot_args, slot_name);
  int totelement, i = 0;

  lib_assert(op->slots_in == slot_args || op->slots_out == slot_args);

  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((slot->slot_subtype.elem & MESH_ALL_NOLOOP) & htype) == htype);
  lib_assert((slot->slot_subtype.elem & MESH_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE) == 0);

  if (test_for_enabled) {
    totelement = mesh_op_mesh_enabled_flag_count(bm, htype, oflag);
  }
  else {
    totelement = mesh_op_mesh_disabled_flag_count(bm, htype, oflag);
  }

  if (totelement) {
    MeshIter iter;
    MeshHeader *ele;
    MeshHeader **ele_array;

    mesh_op_slot_buffer_alloc(op, slot_args, slot_name, totelement);

    ele_array = (MeshHeader **)slot->data.buf;

    /* TODO: collapse these loops into one. */

    if (htype & MESH_VERT) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_VERTS_OF_MESH) {
        if (mesh_op_vert_flag_test_bool(bm, (MeshVert *)ele, oflag) == test_for_enabled) {
          ele_array[i] = ele;
          i++;
        }
      }
    }

    if (htype & MESH_EDGE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_EDGES_OF_MESH) {
        if (mesh_edge_flag_test_bool(mesh, (MeshEdge *)ele, oflag) == test_for_enabled) {
          ele_array[i] = ele;
          i++;
        }
      }
    }

    if (htype & MESH_FACE) {
      MESH_ITER_MESH (ele, &iter, mesh, MESH_FACES_OF_MESH) {
        if (mesh_face_flag_test_bool(mesh, (MeshFace *)ele, oflag) == test_for_enabled) {
          ele_array[i] = ele;
          i++;
        }
      }
    }
  }
  else {
    slot->len = 0;
  }
}

void mesh_slot_buffer_from_enabled_flag(Mesh *mesh,
                                        MeshOp *op,
                                        MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                        const char *slot_name,
                                        const char htype,
                                        const short oflag)
{
  mesh_slot_buffer_from_flag(mesh, op, slot_args, slot_name, htype, oflag, true);
}

void mesh_op_slot_buffer_from_disabled_flag(Mesh *mesh,
                                            MeshOp *op,
                                            MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                            const char *slot_name,
                                            const char htype,
                                            const short oflag)
{
  mesh_slot_buffer_from_flag(mesh, op, slot_args, slot_name, htype, oflag, false);
}

void mesh_op_slot_buffer_hflag_enable(Mesh *mesh,
                                      MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                      const char *slot_name,
                                      const char htype,
                                      const char hflag,
                                      const bool do_flush)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  MeshElem **data = (MeshElem **)slot->data.buf;
  int i;
  const bool do_flush_select = (do_flush && (hflag & MESH_ELEM_SELECT));
  const bool do_flush_hide = (do_flush && (hflag & MESH_ELEM_HIDDEN));

  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((slot->slot_subtype.elem & MESH_ALL_NOLOOP) & htype) == htype);
  lib_assert((slot->slot_subtype.elem & MESH_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE) == 0);

  for (i = 0; i < slot->len; i++, data++) {
    if (!(htype & (*data)->head.htype)) {
      continue;
    }

    if (do_flush_select) {
      mesh_elem_select_set(mesh, *data, true);
    }

    if (do_flush_hide) {
      mesh_elem_hide_set(mesh, *data, false);
    }

    mesh_elem_flag_enable(*data, hflag);
  }
}

void mesh_slot_buffer_hflag_disable(Mesh *mesh,
                                    MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                    const char *slot_name,
                                    const char htype,
                                    const char hflag,
                                    const bool do_flush)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  MeshElem **data = (MeshElem **)slot->data.buf;
  int i;
  const bool do_flush_select = (do_flush && (hflag & MESH_ELEM_SELECT));
  const bool do_flush_hide = (do_flush && (hflag & MESH_ELEM_HIDDEN));

  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((slot->slot_subtype.elem & MESH_ALL_NOLOOP) & htype) == htype);

  for (i = 0; i < slot->len; i++, data++) {
    if (!(htype & (*data)->head.htype)) {
      continue;
    }

    if (do_flush_select) {
      mesh_elem_select_set(mesh, *data, false);
    }

    if (do_flush_hide) {
      mesh_elem_hide_set(mesh, *data, false);
    }

    mesh_elem_flag_disable(*data, hflag);
  }
}

void mesh_op_slot_buffer_flag_enable(Mesh *mesh,
                                 MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                 const char *slot_name,
                                 const char htype,
                                 const short oflag)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);
  MeshHeader **data = slot->data.p;
  int i;

  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((slot->slot_subtype.elem & MESH_ALL_NOLOOP) & htype) == htype);

  for (i = 0; i < slot->len; i++) {
    if (!(htype & data[i]->htype)) {
      continue;
    }

    mesh_op_elem_flag_enable(mesh, (MeshElemF *)data[i], oflag);
  }
}

void mesh_slot_buffer_flag_disable(Mesh *mesh,
                                   MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                                   const char *slot_name,
                                   const char htype,
                                   const short oflag)
{
  MeshOpSlot *slot = mesh_op_slot_get(slot_args, slot_name);
  MeshHeader **data = (MeshHeader **)slot->data.buf;
  int i;

  lib_assert(slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF);
  lib_assert(((slot->slot_subtype.elem & BM_ALL_NOLOOP) & htype) == htype);

  for (i = 0; i < slot->len; i++) {
    if (!(htype & data[i]->htype)) {
      continue;
    }

    mesh_elem_flag_disable(mesh, (MeshElemF *)data[i], oflag);
  }
}

/**
 * ALLOC/FREE FLAG LAYER
 *
 * Used by operator stack to free/allocate
 * private flag data. This is allocated
 * using a mempool so the allocation/frees
 * should be quite fast.
 *
 * MESH_TODO:
 * Investigate not freeing flag layers until
 * all operators have been executed. This would
 * save a lot of realloc potentially.
 */
static void mesh_flag_layer_alloc(Mesh *mesh)
{
  /* set the index values since we are looping over all data anyway,
   * may save time later on */

  lib_mempool *voldpool = mesh->vtoolflagpool; /* old flag pool */
  lib_mempool *eoldpool = mesh->etoolflagpool; /* old flag pool */
  lib_mempool *foldpool = mesh->ftoolflagpool; /* old flag pool */

  /* store memcpy size for reuse */
  const size_t old_totflags_size = (mesh->totflags * sizeof(MeshFlagLayer));

  mesh->totflags++;

  mesh->vtoolflagpool = lib_mempool_create(
      sizeof(MeshFlagLayer) * mesh->totflags, mesh->totvert, 512, LIB_MEMPOOL_NOP);
  mesh->etoolflagpool = lib_mempool_create(
      sizeof(MeshFlagLayer) * mesh->totflags, mesh->totedge, 512, LIB_MEMPOOL_NOP);
  mesh->ftoolflagpool = lib_mempool_create(
      sizeof(MeshFlagLayer) * mesh->totflags, mesh->totface, 512, LIB_MEMPOOL_NOP);

  /* now go through and memcpy all the flags. Loops don't get a flag layer at this time. */
  MeshIter iter;
  int i;

  MeshVert_OFlag *v_oflag;
  lib_mempool *newpool = bm->vtoolflagpool;
  MESH_ITER_MESH_INDEX (v_oflag, &iter, mesh, MESH_VERTS_OF_MESH, i) {
    void *oldflags = v_oflag->oflags;
    v_oflag->oflags = lib_mempool_calloc(newpool);
    memcpy(v_oflag->oflags, oldflags, old_totflags_size);
    mesh_elem_index_set(&v_oflag->base, i); /* set_inline */
    MESH_ELEM_API_FLAG_CLEAR((MeshElemF *)v_oflag);
  }

  MeshEdge_OFlag *e_oflag;
  newpool = mesh->etoolflagpool;
  MESH_ITER_MESH_INDEX (e_oflag, &iter, mesh, MESH_EDGES_OF_MESH, i) {
    void *oldflags = e_oflag->oflags;
    e_oflag->oflags = lib_mempool_calloc(newpool);
    memcpy(e_oflag->oflags, oldflags, old_totflags_size);
    mesh_elem_index_set(&e_oflag->base, i); /* set_inline */
    MESH_ELEM_API_FLAG_CLEAR((MeshElemF *)e_oflag);
  }

  MeshFace_OFlag *f_oflag;
  newpool = mesh->ftoolflagpool;
  MESH_ITER_MESH_INDEX (f_oflag, &iter, mesh, MESH_FACES_OF_MESH, i) {
    void *oldflags = f_oflag->oflags;
    f_oflag->oflags = lib_mempool_calloc(newpool);
    memcpy(f_oflag->oflags, oldflags, old_totflags_size);
    mesh_elem_index_set(&f_oflag->base, i); /* set_inline */
    mesh_ELEM_API_FLAG_CLEAR((MeshElemF *)f_oflag);
  }

  lib_mempool_destroy(voldpool);
  lib_mempool_destroy(eoldpool);
  lib_mempool_destroy(foldpool);

  mesh->elem_index_dirty &= ~(MESH_VERT | MESH_EDGE | MESH_FACE);
}

static void mesh_flag_layer_free(Mesh *mesh)
{
  /* set the index values since we are looping over all data anyway,
   * may save time later on */

  lib_mempool *voldpool = mesh->vtoolflagpool;
  lib_mempool *eoldpool = mesh->etoolflagpool;
  lib_mempool *foldpool = mesh->ftoolflagpool;

  /* store memcpy size for reuse */
  const size_t new_totflags_size = ((mesh->totflags - 1) * sizeof(BMFlagLayer));

  /* de-increment the totflags first. */
  mesh->totflags--;

  mesh->vtoolflagpool = lib_mempool_create(new_totflags_size, bm->totvert, 512, BLI_MEMPOOL_NOP);
  mesh->etoolflagpool = lib_mempool_create(new_totflags_size, bm->totedge, 512, BLI_MEMPOOL_NOP);
  mesh->ftoolflagpool = lib_mempool_create(new_totflags_size, bm->totface, 512, BLI_MEMPOOL_NOP);

  /* now go through and memcpy all the flag */
  MeshIter iter;
  int i;

  MeshVert_OFlag *v_oflag;
  lib_mempool *newpool = mesh->vtoolflagpool;
  MESH_ITER_MESH_INDEX (v_oflag, &iter, mesh, MESH_VERTS_OF_MESH, i) {
    void *oldflags = v_oflag->oflags;
    v_oflag->oflags = lib_mempool_alloc(newpool);
    memcpy(v_oflag->oflags, oldflags, new_totflags_size);
    mesh_elem_index_set(&v_oflag->base, i); /* set_inline */
    MESH_ELEM_API_FLAG_CLEAR((MeshElemF *)v_oflag);
  }

  MeshEdge_OFlag *e_oflag;
  newpool = mesh->etoolflagpool;
  MESH_ITER_MESH_INDEX (e_oflag, &iter, bm, BM_EDGES_OF_MESH, i) {
    void *oldflags = e_oflag->oflags;
    e_oflag->oflags = lib_mempool_alloc(newpool);
    memcpy(e_oflag->oflags, oldflags, new_totflags_size);
    mesh_elem_index_set(&e_oflag->base, i); /* set_inline */
    MESH_ELEM_API_FLAG_CLEAR((MeshElemF *)e_oflag);
  }

  MeshFace_OFlag *f_oflag;
  newpool = mesh->ftoolflagpool;
  MESH_ITER_MESH_INDEX (f_oflag, &iter, mesh, MESH_FACES_OF_MESH, i) {
    void *oldflags = f_oflag->oflags;
    f_oflag->oflags = lib_mempool_alloc(newpool);
    memcpy(f_oflag->oflags, oldflags, new_totflags_size);
    mesh_elem_index_set(&f_oflag->base, i); /* set_inline */
    MESH_ELEM_API_FLAG_CLEAR((MeshElemF *)f_oflag);
  }

  lib_mempool_destroy(voldpool);
  lib_mempool_destroy(eoldpool);
  lib_mempool_destroy(foldpool);

  mesh->elem_index_dirty &= ~(MESH_VERT | MESH_EDGE | MESH_FACE);
}

static void bmo_flag_layer_clear(Mesh *mesh)
{
  /* set the index values since we are looping over all data anyway,
   * may save time later on */
  const MeshFlagLayer zero_flag = {0};

  const int totflags_offset = bm->totflags - 1;

  /* now go through and memcpy all the flag */
  {
    MeshIter iter;
    MeshVert_OFlag *ele;
    int i;
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_VERTS_OF_MESH, i) {
      ele->oflags[totflags_offset] = zero_flag;
      mesh_elem_index_set(&ele->base, i); /* set_inline */
    }
  }
  {
    MeshIter iter;
    MeshEdge_OFlag *ele;
    int i;
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_EDGES_OF_MESH, i) {
      ele->oflags[totflags_offset] = zero_flag;
      mesh_elem_index_set(&ele->base, i); /* set_inline */
    }
  }
  {
    MeshIter iter;
    MeshFace_OFlag *ele;
    int i;
    MESH_ITER_MESH_INDEX (ele, &iter, mesh, MESH_FACES_OF_MESH, i) {
      ele->oflags[totflags_offset] = zero_flag;
      mesh_elem_index_set(&ele->base, i); /* set_inline */
    }
  }

  mesh->elem_index_dirty &= ~(MESH_VERT | MESH_EDGE | MESH_FACE);
}

void *mesh_slot_buffer_get_first(MeshOpSlot slot_args[MESH_OP_MAX_SLOTS], const char *slot_name)
{
  MeshOpSlot *slot = mesh_op_slot_get(slot_args, slot_name);

  if (slot->slot_type != MESH_OP_SLOT_ELEMENT_BUF) {
    return NULL;
  }

  return slot->data.buf ? *slot->data.buf : NULL;
}

void *mesh_iter_new(MeshOpIter *iter,
                    MeshOpSlot slot_args[MESH_OP_MAX_SLOTS],
                    const char *slot_name,
                    const char restrictmask)
{
  MeshOpSlot *slot = mesh_slot_get(slot_args, slot_name);

  memset(iter, 0, sizeof(MeshIter));

  iter->slot = slot;
  iter->cur = 0;
  iter->restrictmask = restrictmask;

  if (iter->slot->slot_type == MESH_OP_SLOT_MAPPING) {
    lib_ghashIterator_init(&iter->giter, slot->data.ghash);
  }
  else if (iter->slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF) {
    lib_assert(restrictmask & slot->slot_subtype.elem);
  }
  else {
    lib_assert(0);
  }

  return mesh_iter_step(iter);
}

void *mesh_iter_step(MeshIter *iter)
{
  MeshOpSlot *slot = iter->slot;
  if (slot->slot_type == MESH_OP_SLOT_ELEMENT_BUF) {
    MeshHeader *ele;

    if (iter->cur >= slot->len) {
      return NULL;
    }

    ele = slot->data.buf[iter->cur++];
    while (!(iter->restrictmask & ele->htype)) {
      if (iter->cur >= slot->len) {
        return NULL;
      }

      ele = slot->data.buf[iter->cur++];
      lib_assert((ele == NULL) || (slot->slot_subtype.elem & ele->htype));
    }

    lib_assert((ele == NULL) || (slot->slot_subtype.elem & ele->htype));

    return ele;
  }
  if (slot->slot_type == MESH_OP_SLOT_MAPPING) {
    void *ret;

    if (lib_ghashIterator_done(&iter->giter) == false) {
      ret = lib_ghashIterator_getKey(&iter->giter);
      iter->val = lib_ghashIterator_getValue_p(&iter->giter);

      lib_ghashIterator_step(&iter->giter);
    }
    else {
      ret = NULL;
      iter->val = NULL;
    }

    return ret;
  }
  lib_assert(0);

  return NULL;
}

/* used for iterating over mappings */

void **mesh_op_iter_map_value_p(MeshOpIter *iter)
{
  return iter->val;
}

void *mesh_op_iter_map_value_ptr(MeshOpIter *iter)
{
  lib_assert(ELEM(iter->slot->slot_subtype.map,
                  MESG_OP_SLOT_SUBTYPE_MAP_ELEM,
                  MESH_OP_SLOT_SUBTYPE_MAP_INTERNAL));
  return iter->val ? *iter->val : NULL;
}

float mesh_op_iter_map_value_float(MeshOpIter *iter)
{
  lib_assert(iter->slot->slot_subtype.map == MESH_OP_SLOT_SUBTYPE_MAP_FLT);
  return **((float **)iter->val);
}

int mesh_iter_map_value_int(MeshOpIter *iter)
{
  lib_assert(iter->slot->slot_subtype.map == BMO_OP_SLOT_SUBTYPE_MAP_INT);
  return **((int **)iter->val);
}

bool mesh_iter_map_value_bool(MeshOpIter *iter)
{
  lib_assert(iter->slot->slot_subtype.map == BMO_OP_SLOT_SUBTYPE_MAP_BOOL);
  return **((bool **)iter->val);
}

/* error system */
typedef struct MeshOpError {
  struct MeshOpError *next, *prev;
  MeshOp *op;
  const char *msg;
  eMeshOpErrorLevel level;
} MeshOpError;

void mesh_op_error_clear(Mesh *mesh)
{
  while (mesh_op_error_pop(mesh, NULL, NULL, NULL)) {
    /* pass */
  }
}

void BMO_error_raise(BMesh *bm, BMOperator *owner, eBMOpErrorLevel level, const char *msg)
{
  BMOpError *err = MEM_callocN(sizeof(BMOpError), "bmop_error");

  err->msg = msg;
  err->op = owner;
  err->level = level;

  BLI_addhead(&bm->errorstack, err);
}

bool BMO_error_occurred_at_level(BMesh *bm, eBMOpErrorLevel level)
{
  for (const BMOpError *err = bm->errorstack.first; err; err = err->next) {
    if (err->level == level) {
      return true;
    }
  }
  return false;
}

bool BMO_error_get(BMesh *bm, const char **r_msg, BMOperator **r_op, eBMOpErrorLevel *r_level)
{
  BMOpError *err = bm->errorstack.first;
  if (err == NULL) {
    return false;
  }

  if (r_msg) {
    *r_msg = err->msg;
  }
  if (r_op) {
    *r_op = err->op;
  }
  if (r_level) {
    *r_level = err->level;
  }

  return true;
}

bool BMO_error_get_at_level(BMesh *bm,
                            eBMOpErrorLevel level,
                            const char **r_msg,
                            BMOperator **r_op)
{
  for (BMOpError *err = bm->errorstack.first; err; err = err->next) {
    if (err->level >= level) {
      if (r_msg) {
        *r_msg = err->msg;
      }
      if (r_op) {
        *r_op = err->op;
      }
      return true;
    }
  }

  return false;
}

bool BMO_error_pop(BMesh *bm, const char **r_msg, BMOperator **r_op, eBMOpErrorLevel *r_level)
{
  bool result = BMO_error_get(bm, r_msg, r_op, r_level);

  if (result) {
    BMOpError *err = bm->errorstack.first;

    BLI_remlink(&bm->errorstack, bm->errorstack.first);
    MEM_freeN(err);
  }

  return result;
}

#define NEXT_CHAR(fmt) ((fmt)[0] != 0 ? (fmt)[1] : 0)

static int bmo_name_to_slotcode(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *identifier)
{
  int i = 0;

  while (slot_args->slot_name) {
    if (STREQLEN(identifier, slot_args->slot_name, MAX_SLOTNAME)) {
      return i;
    }
    slot_args++;
    i++;
  }

  return -1;
}

static int bmo_name_to_slotcode_check(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *identifier)
{
  int i = bmo_name_to_slotcode(slot_args, identifier);
  if (i < 0) {
    fprintf(stderr,
            "%s: ! could not find bmesh slot for name %s! (bmesh internal error)\n",
            __func__,
            identifier);
  }

  return i;
}

int BMO_opcode_from_opname(const char *opname)
{

  const uint tot = bmo_opdefines_total;
  uint i;
  for (i = 0; i < tot; i++) {
    if (STREQ(bmo_opdefines[i]->opname, opname)) {
      return i;
    }
  }
  return -1;
}

static int BMO_opcode_from_opname_check(const char *opname)
{
  int i = BMO_opcode_from_opname(opname);
  if (i == -1) {
    fprintf(stderr,
            "%s: could not find bmesh slot for name %s! (bmesh internal error)\n",
            __func__,
            opname);
  }
  return i;
}

bool BMO_op_vinitf(BMesh *bm, BMOperator *op, const int flag, const char *_fmt, va_list vlist)
{
  //  BMOpDefine *def;
  char *opname, *ofmt, *fmt;
  char slot_name[64] = {0};
  int i, type;
  bool noslot, state;

  /* basic useful info to help find where bmop formatting strings fail */
  const char *err_reason = "Unknown";
  int lineno = -1;

#define GOTO_ERROR(reason) \
  { \
    err_reason = reason; \
    lineno = __LINE__; \
    goto error; \
  } \
  (void)0

  /* we muck around in here, so dup it */
  fmt = ofmt = BLI_strdup(_fmt);

  /* find operator name */
  i = strcspn(fmt, " ");

  opname = fmt;
  noslot = (opname[i] == '\0');
  opname[i] = '\0';

  fmt += i + (noslot ? 0 : 1);

  i = BMO_opcode_from_opname_check(opname);

  if (i == -1) {
    MEM_freeN(ofmt);
    BLI_assert(0);
    return false;
  }

  BMO_op_init(bm, op, flag, opname);
  //  def = bmo_opdefines[i];

  i = 0;
  state = true; /* false: not inside slot_code name, true: inside slot_code name */

  while (*fmt) {
    if (state) {
      /* Jump past leading white-space. */
      i = strspn(fmt, " ");
      fmt += i;

      /* Ignore trailing white-space. */
      if (!fmt[i]) {
        break;
      }

      /* find end of slot name, only "slot=%f", can be used */
      i = strcspn(fmt, "=");
      if (!fmt[i]) {
        GOTO_ERROR("could not match end of slot name");
      }

      fmt[i] = 0;

      if (bmo_name_to_slotcode_check(op->slots_in, fmt) < 0) {
        GOTO_ERROR("name to slot code check failed");
      }

      BLI_strncpy(slot_name, fmt, sizeof(slot_name));

      state = false;
      fmt += i;
    }
    else {
      switch (*fmt) {
        case ' ':
        case '=':
        case '%':
          break;
        case 'm': {
          int size;
          const char c = NEXT_CHAR(fmt);
          fmt++;

          if (c == '3') {
            size = 3;
          }
          else if (c == '4') {
            size = 4;
          }
          else {
            GOTO_ERROR("matrix size was not 3 or 4");
          }

          BMO_slot_mat_set(op, op->slots_in, slot_name, va_arg(vlist, void *), size);
          state = true;
          break;
        }
        case 'v': {
          BMO_slot_vec_set(op->slots_in, slot_name, va_arg(vlist, float *));
          state = true;
          break;
        }
        case 'e': {
          BMOpSlot *slot = BMO_slot_get(op->slots_in, slot_name);

          if (NEXT_CHAR(fmt) == 'b') {
            BMHeader **ele_buffer = va_arg(vlist, void *);
            int ele_buffer_len = va_arg(vlist, int);

            BMO_slot_buffer_from_array(op, slot, ele_buffer, ele_buffer_len);
            fmt++;
          }
          else {
            /* single vert/edge/face */
            BMHeader *ele = va_arg(vlist, void *);

            BMO_slot_buffer_from_single(op, slot, ele);
          }

          state = true;
          break;
        }
        case 's':
        case 'S': {
          BMOperator *op_other = va_arg(vlist, void *);
          const char *slot_name_other = va_arg(vlist, char *);

          if (*fmt == 's') {
            BLI_assert(bmo_name_to_slotcode_check(op_other->slots_in, slot_name_other) != -1);
            BMO_slot_copy(op_other, slots_in, slot_name_other, op, slots_in, slot_name);
          }
          else {
            BLI_assert(bmo_name_to_slotcode_check(op_other->slots_out, slot_name_other) != -1);
            BMO_slot_copy(op_other, slots_out, slot_name_other, op, slots_in, slot_name);
          }
          state = true;
          break;
        }
        case 'i':
          BMO_slot_int_set(op->slots_in, slot_name, va_arg(vlist, int));
          state = true;
          break;
        case 'b':
          BMO_slot_bool_set(op->slots_in, slot_name, va_arg(vlist, int));
          state = true;
          break;
        case 'p':
          BMO_slot_ptr_set(op->slots_in, slot_name, va_arg(vlist, void *));
          state = true;
          break;
        case 'f':
        case 'F':
        case 'h':
        case 'H':
        case 'a':
          type = *fmt;

          if (NEXT_CHAR(fmt) == ' ' || NEXT_CHAR(fmt) == '\0') {
            BMO_slot_float_set(op->slots_in, slot_name, va_arg(vlist, double));
          }
          else {
            char htype = 0;

            while (1) {
              char htype_set;
              const char c = NEXT_CHAR(fmt);
              if (c == 'f') {
                htype_set = BM_FACE;
              }
              else if (c == 'e') {
                htype_set = BM_EDGE;
              }
              else if (c == 'v') {
                htype_set = BM_VERT;
              }
              else {
                break;
              }

              if (UNLIKELY(htype & htype_set)) {
                GOTO_ERROR("htype duplicated");
              }

              htype |= htype_set;
              fmt++;
            }

            if (type == 'h') {
              BMO_slot_buffer_from_enabled_hflag(
                  bm, op, op->slots_in, slot_name, htype, va_arg(vlist, int));
            }
            else if (type == 'H') {
              BMO_slot_buffer_from_disabled_hflag(
                  bm, op, op->slots_in, slot_name, htype, va_arg(vlist, int));
            }
            else if (type == 'a') {
              if ((op->flag & BMO_FLAG_RESPECT_HIDE) == 0) {
                BMO_slot_buffer_from_all(bm, op, op->slots_in, slot_name, htype);
              }
              else {
                BMO_slot_buffer_from_disabled_hflag(
                    bm, op, op->slots_in, slot_name, htype, BM_ELEM_HIDDEN);
              }
            }
            else if (type == 'f') {
              BMO_slot_buffer_from_enabled_flag(
                  bm, op, op->slots_in, slot_name, htype, va_arg(vlist, int));
            }
            else if (type == 'F') {
              BMO_slot_buffer_from_disabled_flag(
                  bm, op, op->slots_in, slot_name, htype, va_arg(vlist, int));
            }
          }

          state = true;
          break;
        default:
          fprintf(stderr,
                  "%s: unrecognized bmop format char: '%c', %d in '%s'\n",
                  __func__,
                  *fmt,
                  (int)(fmt - ofmt),
                  ofmt);
          break;
      }
    }
    fmt++;
  }

  MEM_freeN(ofmt);
  return true;
error:

  /* TODO: explain exactly what is failing (not urgent). */
  fprintf(stderr, "%s: error parsing formatting string\n", __func__);

  fprintf(stderr, "string: '%s', position %d\n", _fmt, (int)(fmt - ofmt));
  fprintf(stderr, "         ");
  {
    int pos = (int)(fmt - ofmt);
    for (i = 0; i < pos; i++) {
      fprintf(stderr, " ");
    }
    fprintf(stderr, "^\n");
  }

  fprintf(stderr, "source code:  %s:%d\n", __FILE__, lineno);

  fprintf(stderr, "reason: %s\n", err_reason);

  MEM_freeN(ofmt);

  BMO_op_finish(bm, op);
  return false;

#undef GOTO_ERROR
}

bool BMO_op_initf(BMesh *bm, BMOperator *op, const int flag, const char *fmt, ...)
{
  va_list list;

  va_start(list, fmt);
  if (!BMO_op_vinitf(bm, op, flag, fmt, list)) {
    printf("%s: failed\n", __func__);
    va_end(list);
    return false;
  }
  va_end(list);

  return true;
}

bool BMO_op_callf(BMesh *bm, const int flag, const char *fmt, ...)
{
  va_list list;
  BMOperator op;

  va_start(list, fmt);
  if (!BMO_op_vinitf(bm, &op, flag, fmt, list)) {
    printf("%s: failed, format is:\n    \"%s\"\n", __func__, fmt);
    va_end(list);
    return false;
  }

  BMO_op_exec(bm, &op);
  BMO_op_finish(bm, &op);

  va_end(list);
  return true;
}
