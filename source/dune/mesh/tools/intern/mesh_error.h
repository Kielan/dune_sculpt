#pragma once

#include "mesh_op_api.h"

/*----------- mesh op error system ----------*/

/** More can be added as needed. **/
typedef enum eMeshOpErrorLevel {
  /**
   * Use when the operation could not succeed,
   * typically from input that isn't sufficient for completing the operation.
   */
  MO_ERROR_CANCEL = 0,
  /**
   * Use this when one or more operations could not succeed,
   * when the resulting mesh can be used (since some operations succeeded or no change was made).
   * This is used by default.
   */
  MO_ERROR_WARN = 1,
  /**
   * The mesh resulting from this operation should not be used (where possible).
   * It should not be left in a corrupt state either.
   *
   * See MeshBackup type & function calls.
   */
  MO_ERROR_FATAL = 2,
} eMeshOpErrorLevel;

/**
 * Pushes an error onto the mesh error stack.
 * if msg is null, then the default message for the `errcode` is used.
 */
void mesh_error_raise(Mesh *mesh, MeshOp *owner, eMeshOpErrorLevel level, const char *msg)
    ATTR_NONNULL(1, 2, 4);

/**
 * Gets the topmost error from the stack.
 * returns error code or 0 if no error.
 */
bool mesh_error_get(Mesh *mesh, const char **r_msg, MeshOp **r_op, eMeshOpErrorLevel *r_level);
bool mesh_error_get_at_level(Mesh *meeh,
                             eMeshOpErrorLevel level,
                             const char **r_msg,
                             MeshOp **r_op);
bool mesh_op_error_occurred_at_level(Mesh *mesh, eMeshOpErrorLevel level);

/* Same as mesh_op_error_get, only pops the error off the stack as well. */
bool mesh_op_error_pop(Mesh *mesh, const char **r_msg, MeshOp **r_op, eMeshOpErrorLevel *r_level);
void mesh_op_error_clear(Mesh *mesh);

/* This is meant for handling errors, like self-intersection test failures.
 * it's dangerous to handle errors in general though, so disabled for now. */

/* Catches an error raised by the op pointed to by catchop. */
/* Not yet implemented. */
// int mesh_error_catch_op(Mesh *mesh, MeshOp *catchop, char **msg);

#define MESH_ELEM_INDEX_VALIDATE(_mesh, _msg_a, _msg_b) \
  mesh_elem_index_validate(_mesh, __FILE__ ":" STRINGIFY(__LINE__), __func__, _msg_a, _msg_b)

/* MESH_ASSERT */
#ifdef WITH_ASSERT_ABORT
#  define _MESH_DUMMY_ABORT abort
#else
#  define _MESH_DUMMY_ABORT() (void)0
#endif

/**
 * This is meant to be higher level than lib_assert(),
 * its enabled even when in Release mode.
 */
#define MESH_ASSERT(a) \
  (void)((!(a)) ? ((fprintf(stderr, \
                            "MESH_ASSERT failed: %s, %s(), %d at \'%s\'\n", \
                            __FILE__, \
                            __func__, \
                            __LINE__, \
                            STRINGIFY(a)), \
                    _MESH_DUMMY_ABORT(), \
                    NULL)) : \
                  NULL)
