/**
 * Used by ED_undo.h, internal implementation.
 */

#include <stdio.h>
#include <string.h>

#include "CLG_log.h"

#include "LIB_listbase.h"
#include "LIB_string.h"
#include "LIB_sys_types.h"
#include "LIB_utildefines.h"

#include "TRANSLATION_translation.h"

#include "structs_listBase.h"
#include "structs_windowmanager_types.h"

#include "KERNEL_context.h"
#include "KERNEL_global.h"
#include "KERNEL_lib_override.h"
#include "KERNEL_main.h"
#include "KERNEL_undo_system.h"

#include "MEM_guardedalloc.h"

#define undo_stack _wm_undo_stack_disallow /* pass in as a variable always. */

/** Odd requirement of Blender that we always keep a memfile undo in the stack. */
#define WITH_GLOBAL_UNDO_KEEP_ONE

/** Make sure all ID's created at the point we add an undo step that uses ID's. */
#define WITH_GLOBAL_UNDO_ENSURE_UPDATED

/**
 * Make sure we don't apply edits on top of a newer memfile state, see: T56163.
 * Keep an eye on this, could solve differently.
 */
#define WITH_GLOBAL_UNDO_CORRECT_ORDER

/** We only need this locally. */
static CLG_LogRef LOG = {"kernel.undosys"};

/* -------------------------------------------------------------------- */
/** Undo Types */

const UndoType *KERNEL_UNDOSYS_TYPE_IMAGE = NULL;
const UndoType *KERNEL_UNDOSYS_TYPE_MEMFILE = NULL;
const UndoType *KERNEL_UNDOSYS_TYPE_PAINTCURVE = NULL;
const UndoType *KERNEL_UNDOSYS_TYPE_PARTICLE = NULL;
const UndoType *KERNEL_UNDOSYS_TYPE_SCULPT = NULL;
const UndoType *KERNEL_UNDOSYS_TYPE_TEXT = NULL;

static ListBase g_undo_types = {NULL, NULL};

static const UndoType *KERNEL_undosys_type_from_context(duneContext *C)
{
  LISTBASE_FOREACH (const UndoType *, ut, &g_undo_types) {
    /* No poll means we don't check context. */
    if (ut->poll && ut->poll(C)) {
      return ut;
    }
  }
  return NULL;
}

/* -------------------------------------------------------------------- */
/** Internal Nested Undo Checks
 *
 * Make sure we're not running undo operations from 'step_encode', 'step_decode' callbacks.
 * bugs caused by this situation aren't _that_ hard to spot but aren't always so obvious.
 * Best we have a check which shows the problem immediately.
 **/

#define WITH_NESTED_UNDO_CHECK

#ifdef WITH_NESTED_UNDO_CHECK
static bool g_undo_callback_running = false;
#  define UNDO_NESTED_ASSERT(state) BLI_assert(g_undo_callback_running == state)
#  define UNDO_NESTED_CHECK_BEGIN \
    { \
      UNDO_NESTED_ASSERT(false); \
      g_undo_callback_running = true; \
    } \
    ((void)0)
#  define UNDO_NESTED_CHECK_END \
    { \
      UNDO_NESTED_ASSERT(true); \
      g_undo_callback_running = false; \
    } \
    ((void)0)
#else
#  define UNDO_NESTED_ASSERT(state) ((void)0)
#  define UNDO_NESTED_CHECK_BEGIN ((void)0)
#  define UNDO_NESTED_CHECK_END ((void)0)
#endif

/* -------------------------------------------------------------------- */
/** Internal Callback Wrappers
 *
 * #UndoRefID is simply a way to avoid in-lining name copy and lookups,
 * since it's easy to forget a single case when done inline (crashing in some cases).
 **/

static void undosys_id_ref_store(void *UNUSED(user_data), UndoRefID *id_ref)
{
  LIB_assert(id_ref->name[0] == '\0');
  if (id_ref->ptr) {
    LIB_strncpy(id_ref->name, id_ref->ptr->name, sizeof(id_ref->name));
    /* Not needed, just prevents stale data access. */
    id_ref->ptr = NULL;
  }
}

static void undosys_id_ref_resolve(void *user_data, UndoRefID *id_ref)
{
  /* NOTE: we could optimize this,
   * for now it's not too bad since it only runs when we access undo! */
  Main *dunemain = user_data;
  ListBase *lb = which_libbase(dunemain, GS(id_ref->name));
  LISTBASE_FOREACH (ID *, id, lb) {
    if (STREQ(id_ref->name, id->name) && !ID_IS_LINKED(id)) {
      id_ref->ptr = id;
      break;
    }
  }
}

static bool undosys_step_encode(bContext *C, Main *bmain, UndoStack *ustack, UndoStep *us)
{
  CLOG_INFO(&LOG, 2, "addr=%p, name='%s', type='%s'", us, us->name, us->type->name);
  UNDO_NESTED_CHECK_BEGIN;
  bool ok = us->type->step_encode(C, bmain, us);
  UNDO_NESTED_CHECK_END;
  if (ok) {
    if (us->type->step_foreach_ID_ref != NULL) {
      /* Don't use from context yet because sometimes context is fake and
       * not all members are filled in. */
      us->type->step_foreach_ID_ref(us, undosys_id_ref_store, bmain);
    }

#ifdef WITH_GLOBAL_UNDO_CORRECT_ORDER
    if (us->type == BKE_UNDOSYS_TYPE_MEMFILE) {
      ustack->step_active_memfile = us;
    }
#endif
  }
  if (ok == false) {
    CLOG_INFO(&LOG, 2, "encode callback didn't create undo step");
  }
  return ok;
}

static void undosys_step_decode(bContext *C,
                                Main *bmain,
                                UndoStack *ustack,
                                UndoStep *us,
                                const eUndoStepDir dir,
                                bool is_final)
{
  CLOG_INFO(&LOG, 2, "addr=%p, name='%s', type='%s'", us, us->name, us->type->name);

  if (us->type->step_foreach_ID_ref) {
#ifdef WITH_GLOBAL_UNDO_CORRECT_ORDER
    if (us->type != KERNEL_UNDOSYS_TYPE_MEMFILE) {
      for (UndoStep *us_iter = us->prev; us_iter; us_iter = us_iter->prev) {
        if (us_iter->type == BKE_UNDOSYS_TYPE_MEMFILE) {
          if (us_iter == ustack->step_active_memfile) {
            /* Common case, we're already using the last memfile state. */
          }
          else {
            /* Load the previous memfile state so any ID's referenced in this
             * undo step will be correctly resolved, see: T56163. */
            undosys_step_decode(C, bmain, ustack, us_iter, dir, false);
            /* May have been freed on memfile read. */
            dunemain = G_MAIN;
          }
          break;
        }
      }
    }
#endif
    /* Don't use from context yet because sometimes context is fake and
     * not all members are filled in. */
    us->type->step_foreach_ID_ref(us, undosys_id_ref_resolve, bmain);
  }

  UNDO_NESTED_CHECK_BEGIN;
  us->type->step_decode(C, dunemain, us, dir, is_final);
  UNDO_NESTED_CHECK_END;

#ifdef WITH_GLOBAL_UNDO_CORRECT_ORDER
  if (us->type == BKE_UNDOSYS_TYPE_MEMFILE) {
    ustack->step_active_memfile = us;
  }
#endif
}

static void undosys_step_free_and_unlink(UndoStack *ustack, UndoStep *us)
{
  CLOG_INFO(&LOG, 2, "addr=%p, name='%s', type='%s'", us, us->name, us->type->name);
  UNDO_NESTED_CHECK_BEGIN;
  us->type->step_free(us);
  UNDO_NESTED_CHECK_END;

  LIB_remlink(&ustack->steps, us);
  MEM_freeN(us);

#ifdef WITH_GLOBAL_UNDO_CORRECT_ORDER
  if (ustack->step_active_memfile == us) {
    ustack->step_active_memfile = NULL;
  }
#endif
}

/* -------------------------------------------------------------------- */
/** Undo Stack **/

#ifndef NDEBUG
static void undosys_stack_validate(UndoStack *ustack, bool expect_non_empty)
{
  if (ustack->step_active != NULL) {
    LIB_assert(!LIB_listbase_is_empty(&ustack->steps));
    LIB_assert(LIB_findindex(&ustack->steps, ustack->step_active) != -1);
  }
  if (expect_non_empty) {
    LIB_assert(!LIB_listbase_is_empty(&ustack->steps));
  }
}
#else
static void undosys_stack_validate(UndoStack *UNUSED(ustack), bool UNUSED(expect_non_empty))
{
}
#endif

UndoStack *KERNEL_undosys_stack_create(void)
{
  UndoStack *ustack = MEM_callocN(sizeof(UndoStack), __func__);
  return ustack;
}

void KERNEL_undosys_stack_destroy(UndoStack *ustack)
{
  KERNEL_undosys_stack_clear(ustack);
  MEM_freeN(ustack);
}

void KERNEL_undosys_stack_clear(UndoStack *ustack)
{
  UNDO_NESTED_ASSERT(false);
  CLOG_INFO(&LOG, 1, "steps=%d", LIB_listbase_count(&ustack->steps));
  for (UndoStep *us = ustack->steps.last, *us_prev; us; us = us_prev) {
    us_prev = us->prev;
    undosys_step_free_and_unlink(ustack, us);
  }
  LIB_listbase_clear(&ustack->steps);
  ustack->step_active = NULL;
}

void KERNEL_undosys_stack_clear_active(UndoStack *ustack)
{
  /* Remove active and all following undo-steps. */
  UndoStep *us = ustack->step_active;

  if (us) {
    ustack->step_active = us->prev;
    bool is_not_empty = ustack->step_active != NULL;

    while (ustack->steps.last != ustack->step_active) {
      UndoStep *us_iter = ustack->steps.last;
      undosys_step_free_and_unlink(ustack, us_iter);
      undosys_stack_validate(ustack, is_not_empty);
    }
  }
}
