/** \file
 * \ingroup bmesh
 *
 * BMesh operator access.
 */

#include "MEM_guardedalloc.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_memarena.h"
#include "BLI_mempool.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BLT_translation.h"

#include "bmesh.h"
#include "intern/bmesh_private.h"

/* forward declarations */
static void bmo_flag_layer_alloc(BMesh *bm);
static void bmo_flag_layer_free(BMesh *bm);
static void bmo_flag_layer_clear(BMesh *bm);
static int bmo_name_to_slotcode(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *identifier);
static int bmo_name_to_slotcode_check(BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                                      const char *identifier);

const int BMO_OPSLOT_TYPEINFO[BMO_OP_SLOT_TOTAL_TYPES] = {
    0,                /*  0: BMO_OP_SLOT_SENTINEL */
    sizeof(int),      /*  1: BMO_OP_SLOT_BOOL */
    sizeof(int),      /*  2: BMO_OP_SLOT_INT */
    sizeof(float),    /*  3: BMO_OP_SLOT_FLT */
    sizeof(void *),   /*  4: BMO_OP_SLOT_PNT */
    sizeof(void *),   /*  5: BMO_OP_SLOT_PNT */
    0,                /*  6: unused */
    0,                /*  7: unused */
    sizeof(float[3]), /*  8: BMO_OP_SLOT_VEC */
    sizeof(void *),   /*  9: BMO_OP_SLOT_ELEMENT_BUF */
    sizeof(void *),   /* 10: BMO_OP_SLOT_MAPPING */
};

/* Dummy slot so there is something to return when slot name lookup fails */
// static BMOpSlot BMOpEmptySlot = {0};

void BMO_op_flag_enable(BMesh *UNUSED(bm), BMOperator *op, const int op_flag)
{
  op->flag |= op_flag;
}

void BMO_op_flag_disable(BMesh *UNUSED(bm), BMOperator *op, const int op_flag)
{
  op->flag &= ~op_flag;
}

void BMO_push(BMesh *bm, BMOperator *UNUSED(op))
{
  bm->toolflag_index++;

  BLI_assert(bm->totflags > 0);

  /* add flag layer, if appropriate */
  if (bm->toolflag_index > 0) {
    bmo_flag_layer_alloc(bm);
  }
  else {
    bmo_flag_layer_clear(bm);
  }
}

void BMO_pop(BMesh *bm)
{
  if (bm->toolflag_index > 0) {
    bmo_flag_layer_free(bm);
  }

  bm->toolflag_index--;
}

/* use for both slot_types_in and slot_types_out */
static void bmo_op_slots_init(const BMOSlotType *slot_types, BMOpSlot *slot_args)
{
  BMOpSlot *slot;
  uint i;
  for (i = 0; slot_types[i].type; i++) {
    slot = &slot_args[i];
    slot->slot_name = slot_types[i].name;
    slot->slot_type = slot_types[i].type;
    slot->slot_subtype = slot_types[i].subtype;
    // slot->index = i;  // UNUSED

    switch (slot->slot_type) {
      case BMO_OP_SLOT_MAPPING:
        slot->data.ghash = BLI_ghash_ptr_new("bmesh slot map hash");
        break;
      case BMO_OP_SLOT_INT:
        if (ELEM(slot->slot_subtype.intg,
                 BMO_OP_SLOT_SUBTYPE_INT_ENUM,
                 BMO_OP_SLOT_SUBTYPE_INT_FLAG)) {
          slot->data.enum_data.flags = slot_types[i].enum_flags;
          /* Set the first value of the enum as the default value. */
          slot->data.i = slot->data.enum_data.flags[0].value;
        }
      default:
        break;
    }
  }
}

static void bmo_op_slots_free(const BMOSlotType *slot_types, BMOpSlot *slot_args)
{
  BMOpSlot *slot;
  uint i;
  for (i = 0; slot_types[i].type; i++) {
    slot = &slot_args[i];
    switch (slot->slot_type) {
      case BMO_OP_SLOT_MAPPING:
        BLI_ghash_free(slot->data.ghash, NULL, NULL);
        break;
      default:
        break;
    }
  }
}

void BMO_op_init(BMesh *bm, BMOperator *op, const int flag, const char *opname)
{
  int opcode = BMO_opcode_from_opname(opname);

#ifdef DEBUG
  BM_ELEM_INDEX_VALIDATE(bm, "pre bmo", opname);
#else
  (void)bm;
#endif

  if (opcode == -1) {
    opcode = 0; /* error!, already printed, have a better way to handle this? */
  }

  memset(op, 0, sizeof(BMOperator));
  op->type = opcode;
  op->type_flag = bmo_opdefines[opcode]->type_flag;
  op->flag = flag;

  /* initialize the operator slot types */
  bmo_op_slots_init(bmo_opdefines[opcode]->slot_types_in, op->slots_in);
  bmo_op_slots_init(bmo_opdefines[opcode]->slot_types_out, op->slots_out);

  /* callback */
  op->exec = bmo_opdefines[opcode]->exec;

  /* memarena, used for operator's slot buffers */
  op->arena = BLI_memarena_new(BLI_MEMARENA_STD_BUFSIZE, __func__);
  BLI_memarena_use_calloc(op->arena);
}

void BMO_op_exec(BMesh *bm, BMOperator *op)
{
  /* allocate tool flags on demand */
  BM_mesh_elem_toolflags_ensure(bm);

  BMO_push(bm, op);

  if (bm->toolflag_index == 1) {
    bmesh_edit_begin(bm, op->type_flag);
  }
  op->exec(bm, op);

  if (bm->toolflag_index == 1) {
    bmesh_edit_end(bm, op->type_flag);
  }

  BMO_pop(bm);
}

void BMO_op_finish(BMesh *bm, BMOperator *op)
{
  bmo_op_slots_free(bmo_opdefines[op->type]->slot_types_in, op->slots_in);
  bmo_op_slots_free(bmo_opdefines[op->type]->slot_types_out, op->slots_out);

  BLI_memarena_free(op->arena);

#ifdef DEBUG
  BM_ELEM_INDEX_VALIDATE(bm, "post bmo", bmo_opdefines[op->type]->opname);

  /* avoid accidental re-use */
  memset(op, 0xff, sizeof(*op));
#else
  (void)bm;
#endif
}

bool BMO_slot_exists(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *identifier)
{
  int slot_code = bmo_name_to_slotcode(slot_args, identifier);
  return (slot_code >= 0);
}

BMOpSlot *BMO_slot_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *identifier)
{
  int slot_code = bmo_name_to_slotcode_check(slot_args, identifier);

  if (UNLIKELY(slot_code < 0)) {
    // return &BMOpEmptySlot;
    BLI_assert(0);
    return NULL; /* better crash */
  }

  return &slot_args[slot_code];
}

void _bmo_slot_copy(BMOpSlot slot_args_src[BMO_OP_MAX_SLOTS],
                    const char *slot_name_src,
                    BMOpSlot slot_args_dst[BMO_OP_MAX_SLOTS],
                    const char *slot_name_dst,
                    struct MemArena *arena_dst)
{
  BMOpSlot *slot_src = BMO_slot_get(slot_args_src, slot_name_src);
  BMOpSlot *slot_dst = BMO_slot_get(slot_args_dst, slot_name_dst);

  if (slot_src == slot_dst) {
    return;
  }

  BLI_assert(slot_src->slot_type == slot_dst->slot_type);
  if (slot_src->slot_type != slot_dst->slot_type) {
    return;
  }

  if (slot_dst->slot_type == BMO_OP_SLOT_ELEMENT_BUF) {
    /* do buffer copy */
    slot_dst->data.buf = NULL;
    slot_dst->len = slot_src->len;
    if (slot_dst->len) {
      /* check dest has all flags enabled that the source has */
      const eBMOpSlotSubType_Elem src_elem_flag = (slot_src->slot_subtype.elem & BM_ALL_NOLOOP);
      const eBMOpSlotSubType_Elem dst_elem_flag = (slot_dst->slot_subtype.elem & BM_ALL_NOLOOP);

      if ((src_elem_flag | dst_elem_flag) == dst_elem_flag) {
        /* pass */
      }
      else {
        /* check types */
        const uint tot = slot_src->len;
        uint i;
        uint out = 0;
        BMElem **ele_src = (BMElem **)slot_src->data.buf;
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
        const int slot_alloc_size = BMO_OPSLOT_TYPEINFO[slot_dst->slot_type] * slot_dst->len;
        slot_dst->data.buf = BLI_memarena_alloc(arena_dst, slot_alloc_size);
        if (slot_src->len == slot_dst->len) {
          memcpy(slot_dst->data.buf, slot_src->data.buf, slot_alloc_size);
        }
        else {
          /* only copy compatible elements */
          const uint tot = slot_src->len;
          uint i;
          BMElem **ele_src = (BMElem **)slot_src->data.buf;
          BMElem **ele_dst = (BMElem **)slot_dst->data.buf;
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
  else if (slot_dst->slot_type == BMO_OP_SLOT_MAPPING) {
    GHashIterator gh_iter;
    GHASH_ITER (gh_iter, slot_src->data.ghash) {
      void *key = BLI_ghashIterator_getKey(&gh_iter);
      void *val = BLI_ghashIterator_getValue(&gh_iter);
      BLI_ghash_insert(slot_dst->data.ghash, key, val);
    }
  }
  else {
    slot_dst->data = slot_src->data;
  }
}

/*
 * BMESH OPSTACK SET XXX
 *
 * Sets the value of a slot depending on its type
 */

void BMO_slot_float_set(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, const float f)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_FLT);
  if (!(slot->slot_type == BMO_OP_SLOT_FLT)) {
    return;
  }

  slot->data.f = f;
}

void BMO_slot_int_set(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, const int i)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_INT);
  if (!(slot->slot_type == BMO_OP_SLOT_INT)) {
    return;
  }

  slot->data.i = i;
}

void BMO_slot_bool_set(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, const bool i)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_BOOL);
  if (!(slot->slot_type == BMO_OP_SLOT_BOOL)) {
    return;
  }

  slot->data.i = i;
}

void BMO_slot_mat_set(BMOperator *op,
                      BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                      const char *slot_name,
                      const float *mat,
                      int size)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAT);
  if (!(slot->slot_type == BMO_OP_SLOT_MAT)) {
    return;
  }

  slot->len = 4;
  slot->data.p = BLI_memarena_alloc(op->arena, sizeof(float[4][4]));

  if (size == 4) {
    copy_m4_m4(slot->data.p, (const float(*)[4])mat);
  }
  else if (size == 3) {
    copy_m4_m3(slot->data.p, (const float(*)[3])mat);
  }
  else {
    fprintf(stderr, "%s: invalid size argument %d (bmesh internal error)\n", __func__, size);

    zero_m4(slot->data.p);
  }
}

void BMO_slot_mat4_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                       const char *slot_name,
                       float r_mat[4][4])
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAT);
  if (!(slot->slot_type == BMO_OP_SLOT_MAT)) {
    return;
  }

  if (slot->data.p) {
    copy_m4_m4(r_mat, BMO_SLOT_AS_MATRIX(slot));
  }
  else {
    unit_m4(r_mat);
  }
}

void BMO_slot_mat3_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                       const char *slot_name,
                       float r_mat[3][3])
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAT);
  if (!(slot->slot_type == BMO_OP_SLOT_MAT)) {
    return;
  }

  if (slot->data.p) {
    copy_m3_m4(r_mat, BMO_SLOT_AS_MATRIX(slot));
  }
  else {
    unit_m3(r_mat);
  }
}

void BMO_slot_ptr_set(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, void *p)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_PTR);
  if (!(slot->slot_type == BMO_OP_SLOT_PTR)) {
    return;
  }

  slot->data.p = p;
}

void BMO_slot_vec_set(BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                      const char *slot_name,
                      const float vec[3])
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_VEC);
  if (!(slot->slot_type == BMO_OP_SLOT_VEC)) {
    return;
  }

  copy_v3_v3(slot->data.vec, vec);
}

float BMO_slot_float_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_FLT);
  if (!(slot->slot_type == BMO_OP_SLOT_FLT)) {
    return 0.0f;
  }

  return slot->data.f;
}

int BMO_slot_int_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_INT);
  if (!(slot->slot_type == BMO_OP_SLOT_INT)) {
    return 0;
  }

  return slot->data.i;
}

bool BMO_slot_bool_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_BOOL);
  if (!(slot->slot_type == BMO_OP_SLOT_BOOL)) {
    return 0;
  }

  return slot->data.i;
}

void *BMO_slot_as_arrayN(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, int *len)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  void **ret;

  /* could add support for mapping type */
  BLI_assert(slot->slot_type == BMO_OP_SLOT_ELEMENT_BUF);

  ret = MEM_mallocN(sizeof(void *) * slot->len, __func__);
  memcpy(ret, slot->data.buf, sizeof(void *) * slot->len);
  *len = slot->len;
  return ret;
}

void *BMO_slot_ptr_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_PTR);
  if (!(slot->slot_type == BMO_OP_SLOT_PTR)) {
    return NULL;
  }

  return slot->data.p;
}

void BMO_slot_vec_get(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name, float r_vec[3])
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_VEC);
  if (!(slot->slot_type == BMO_OP_SLOT_VEC)) {
    return;
  }

  copy_v3_v3(r_vec, slot->data.vec);
}

/*
 * BMO_COUNTFLAG
 *
 * Counts the number of elements of a certain type that have a
 * specific flag enabled (or disabled if test_for_enabled is false).
 */

static int bmo_mesh_flag_count(BMesh *bm,
                               const char htype,
                               const short oflag,
                               const bool test_for_enabled)
{
  int count_vert = 0, count_edge = 0, count_face = 0;

  if (htype & BM_VERT) {
    BMIter iter;
    BMVert *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_VERTS_OF_MESH) {
      if (BMO_vert_flag_test_bool(bm, ele, oflag) == test_for_enabled) {
        count_vert++;
      }
    }
  }
  if (htype & BM_EDGE) {
    BMIter iter;
    BMEdge *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_EDGES_OF_MESH) {
      if (BMO_edge_flag_test_bool(bm, ele, oflag) == test_for_enabled) {
        count_edge++;
      }
    }
  }
  if (htype & BM_FACE) {
    BMIter iter;
    BMFace *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_FACES_OF_MESH) {
      if (BMO_face_flag_test_bool(bm, ele, oflag) == test_for_enabled) {
        count_face++;
      }
    }
  }

  return (count_vert + count_edge + count_face);
}

int BMO_mesh_enabled_flag_count(BMesh *bm, const char htype, const short oflag)
{
  return bmo_mesh_flag_count(bm, htype, oflag, true);
}

int BMO_mesh_disabled_flag_count(BMesh *bm, const char htype, const short oflag)
{
  return bmo_mesh_flag_count(bm, htype, oflag, false);
}

void BMO_mesh_flag_disable_all(BMesh *bm,
                               BMOperator *UNUSED(op),
                               const char htype,
                               const short oflag)
{
  if (htype & BM_VERT) {
    BMIter iter;
    BMVert *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_VERTS_OF_MESH) {
      BMO_vert_flag_disable(bm, ele, oflag);
    }
  }
  if (htype & BM_EDGE) {
    BMIter iter;
    BMEdge *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_EDGES_OF_MESH) {
      BMO_edge_flag_disable(bm, ele, oflag);
    }
  }
  if (htype & BM_FACE) {
    BMIter iter;
    BMFace *ele;
    BM_ITER_MESH (ele, &iter, bm, BM_FACES_OF_MESH) {
      BMO_face_flag_disable(bm, ele, oflag);
    }
  }
}

void BMO_mesh_selected_remap(BMesh *bm,
                             BMOpSlot *slot_vert_map,
                             BMOpSlot *slot_edge_map,
                             BMOpSlot *slot_face_map,
                             const bool check_select)
{
  if (bm->selected.first) {
    BMEditSelection *ese, *ese_next;
    BMOpSlot *slot_elem_map;

    for (ese = bm->selected.first; ese; ese = ese_next) {
      ese_next = ese->next;

      switch (ese->htype) {
        case BM_VERT:
          slot_elem_map = slot_vert_map;
          break;
        case BM_EDGE:
          slot_elem_map = slot_edge_map;
          break;
        default:
          slot_elem_map = slot_face_map;
          break;
      }

      ese->ele = BMO_slot_map_elem_get(slot_elem_map, ese->ele);

      if (UNLIKELY((ese->ele == NULL) ||
                   (check_select && (BM_elem_flag_test(ese->ele, BM_ELEM_SELECT) == false)))) {
        BLI_remlink(&bm->selected, ese);
        MEM_freeN(ese);
      }
    }
  }

  if (bm->act_face) {
    BMFace *f = BMO_slot_map_elem_get(slot_face_map, bm->act_face);
    if (f) {
      bm->act_face = f;
    }
  }
}

int BMO_slot_buffer_len(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_ELEMENT_BUF);

  /* check if its actually a buffer */
  if (slot->slot_type != BMO_OP_SLOT_ELEMENT_BUF) {
    return 0;
  }

  return slot->len;
}

int BMO_slot_map_len(BMOpSlot slot_args[BMO_OP_MAX_SLOTS], const char *slot_name)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAPPING);
  return BLI_ghash_len(slot->data.ghash);
}

void BMO_slot_map_insert(BMOperator *op, BMOpSlot *slot, const void *element, const void *data)
{
  (void)op; /* Ignored in release builds. */

  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAPPING);
  BMO_ASSERT_SLOT_IN_OP(slot, op);

  BLI_ghash_insert(slot->data.ghash, (void *)element, (void *)data);
}

#if 0
void *bmo_slot_buffer_grow(BMesh *bm, BMOperator *op, int slot_code, int totadd)
{
  BMOpSlot *slot = &op->slots[slot_code];
  void *tmp;
  ssize_t allocsize;

  BLI_assert(slot->slottype == BMO_OP_SLOT_ELEMENT_BUF);

  /* check if its actually a buffer */
  if (slot->slottype != BMO_OP_SLOT_ELEMENT_BUF) {
    return NULL;
  }

  if (slot->flag & BMOS_DYNAMIC_ARRAY) {
    if (slot->len >= slot->size) {
      slot->size = (slot->size + 1 + totadd) * 2;

      allocsize = BMO_OPSLOT_TYPEINFO[bmo_opdefines[op->type]->slot_types[slot_code].type] *
                  slot->size;
      slot->data.buf = MEM_recallocN_id(slot->data.buf, allocsize, "opslot dynamic array");
    }

    slot->len += totadd;
  }
  else {
    slot->flag |= BMOS_DYNAMIC_ARRAY;
    slot->len += totadd;
    slot->size = slot->len + 2;

    allocsize = BMO_OPSLOT_TYPEINFO[bmo_opdefines[op->type]->slot_types[slot_code].type] *
                slot->len;

    tmp = slot->data.buf;
    slot->data.buf = MEM_callocN(allocsize, "opslot dynamic array");
    memcpy(slot->data.buf, tmp, allocsize);
  }

  return slot->data.buf;
}
#endif

void BMO_slot_map_to_flag(BMesh *bm,
                          BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                          const char *slot_name,
                          const char htype,
                          const short oflag)
{
  GHashIterator gh_iter;
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);
  BMElemF *ele_f;

  BLI_assert(slot->slot_type == BMO_OP_SLOT_MAPPING);

  GHASH_ITER (gh_iter, slot->data.ghash) {
    ele_f = BLI_ghashIterator_getKey(&gh_iter);
    if (ele_f->head.htype & htype) {
      BMO_elem_flag_enable(bm, ele_f, oflag);
    }
  }
}

void *BMO_slot_buffer_alloc(BMOperator *op,
                            BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                            const char *slot_name,
                            const int len)
{
  BMOpSlot *slot = BMO_slot_get(slot_args, slot_name);

  /* check if its actually a buffer */
  if (slot->slot_type != BMO_OP_SLOT_ELEMENT_BUF) {
    return NULL;
  }

  slot->len = len;
  if (len) {
    slot->data.buf = BLI_memarena_alloc(op->arena, BMO_OPSLOT_TYPEINFO[slot->slot_type] * len);
  }
  else {
    slot->data.buf = NULL;
  }

  return slot->data.buf;
}

void BMO_slot_buffer_from_all(BMesh *bm,
                              BMOperator *op,
                              BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                              const char *slot_name,
                              const char htype)
{
  BMOpSlot *output = BMO_slot_get(slot_args, slot_name);
  int totelement = 0, i = 0;

  BLI_assert(output->slot_type == BMO_OP_SLOT_ELEMENT_BUF);
  BLI_assert(((output->slot_subtype.elem & BM_ALL_NOLOOP) & htype) == htype);

  if (htype & BM_VERT) {
    totelement += bm->totvert;
  }
  if (htype & BM_EDGE) {
    totelement += bm->totedge;
  }
  if (htype & BM_FACE) {
    totelement += bm->totface;
  }

  if (totelement) {
    BMIter iter;
    BMHeader *ele;

    BMO_slot_buffer_alloc(op, slot_args, slot_name, totelement);

    /* TODO: collapse these loops into one. */

    if (htype & BM_VERT) {
      BM_ITER_MESH (ele, &iter, bm, BM_VERTS_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }

    if (htype & BM_EDGE) {
      BM_ITER_MESH (ele, &iter, bm, BM_EDGES_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }

    if (htype & BM_FACE) {
      BM_ITER_MESH (ele, &iter, bm, BM_FACES_OF_MESH) {
        output->data.buf[i] = ele;
        i++;
      }
    }
  }
}

/**
 * \brief BMO_HEADERFLAG_TO_SLOT
 *
 * Copies elements of a certain type, which have a certain header flag
 * enabled/disabled into a slot for an operator.
 */
static void bmo_slot_buffer_from_hflag(BMesh *bm,
                                       BMOperator *op,
                                       BMOpSlot slot_args[BMO_OP_MAX_SLOTS],
                                       const char *slot_name,
                                       const char htype,
                                       const char hflag,
                                       const bool test_for_enabled)
{
  BMOpSlot *output = BMO_slot_get(slot_args, slot_name);
  int totelement = 0, i = 0;
  const bool respecthide = ((op->flag & BMO_FLAG_RESPECT_HIDE) != 0) &&
                           ((hflag & BM_ELEM_HIDDEN) == 0);

  BLI_assert(output->slot_type == BMO_OP_SLOT_ELEMENT_BUF);
  BLI_assert(((output->slot_subtype.elem & BM_ALL_NOLOOP) & htype) == htype);
  BLI_assert((output->slot_subtype.elem & BMO_OP_SLOT_SUBTYPE_ELEM_IS_SINGLE) == 0);

  if (test_for_enabled) {
    totelement = BM_mesh_elem_hflag_count_enabled(bm, htype, hflag, respecthide);
  }
  else {
    totelement = BM_mesh_elem_hflag_count_disabled(bm, htype, hflag, respecthide);
  }

  if (totelement) {
    BMIter iter;
    BMElem *ele;

    BMO_slot_buffer_alloc(op, slot_args, slot_name, totelement);

    /* TODO: collapse these loops into one. */

    if (htype & BM_VERT) {
      BM_ITER_MESH (ele, &iter, bm, BM_VERTS_OF_MESH) {
        if ((!respecthide || !BM_elem_flag_test(ele, BM_ELEM_HIDDEN)) &&
            BM_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
          output->data.buf[i] = ele;
          i++;
        }
      }
    }

    if (htype & BM_EDGE) {
      BM_ITER_MESH (ele, &iter, bm, BM_EDGES_OF_MESH) {
        if ((!respecthide || !BM_elem_flag_test(ele, BM_ELEM_HIDDEN)) &&
            BM_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
          output->data.buf[i] = ele;
          i++;
        }
      }
    }

    if (htype & BM_FACE) {
      BM_ITER_MESH (ele, &iter, bm, BM_FACES_OF_MESH) {
        if ((!respecthide || !BM_elem_flag_test(ele, BM_ELEM_HIDDEN)) &&
            BM_elem_flag_test_bool(ele, hflag) == test_for_enabled) {
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
