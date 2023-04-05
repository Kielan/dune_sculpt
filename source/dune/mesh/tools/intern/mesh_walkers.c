/** Mesh Walker API. **/

#include <stdlib.h>
#include <string.h> /* for memcpy */

#include "lib_listbase.h"
#include "lib_utildefines.h"

#include "mesh.h"

#include "mesh_walkers_private.h"

/**
 * NOTE: Details on design.
 *
 * Original design: walkers directly emulation recursive functions.
 * functions save their state onto a MeshWalker.worklist, and also add new states
 * to implement recursive or looping behavior.
 * Generally only one state push per call with a specific state is desired.
 *
 * basic design pattern: the walker step function goes through its
 * list of possible choices for recursion, and recurses (by pushing a new state)
 * using the first non-visited one.  This choice is the flagged as visited using the #GHash.
 * Each step may push multiple new states onto the #BMWalker.worklist at once.
 *
 * - Walkers use tool flags, not header flags.
 * - Walkers now use #GHash for storing visited elements,
 *   rather than stealing flags. #GHash can be rewritten
 *   to be faster if necessary, in the far future :) .
 * - tools should ALWAYS have necessary error handling
 *   for if walkers fail.
 */

void *mesh_walker_begin(MeshWalker *walker, void *start)
{
  lib_assert(((MeshHeader *)start)->htype & walker->begin_htype);

  walker->begin(walker, start);

  return mesh_walker_current_state(walker) ? walker->step(walker) : NULL;
}

void mesh_walker_init(MeshWalker *walker,
              Mesh *bm,
              int type,
              short mask_vert,
              short mask_edge,
              short mask_face,
              BMeshWalkerFlag flag,
              int layer)
{
  memset(walker, 0, sizeof(MeshWalker));

  walker->layer = layer;
  walker->flag = flag;
  walker->mesh = mesh;

  walker->mask_vert = mask_vert;
  walker->mask_edge = mask_edge;
  walker->mask_face = mask_face;

  walker->visit_set = lib_gset_ptr_new("mesh walkers");
  walker->visit_set_alt = lib_gset_ptr_new("bmesh walkers sec");

  if (UNLIKELY(type >= MESH_WALKER_MAXWALKERS || type < 0)) {
    fprintf(stderr,
            "%s: Invalid walker type in BMW_init; type: %d, "
            "searchmask: (v:%d, e:%d, f:%d), flag: %u, layer: %d\n",
            __func__,
            type,
            mask_vert,
            mask_edge,
            mask_face,
            flag,
            layer);
    lib_assert(0);
    return;
  }

  if (type != MESH_WALKER_CUSTOM) {
    walker->begin_htype = mesh_walker_types[type]->begin_htype;
    walker->begin = mesh_walker_types[type]->begin;
    walker->yield = mesh_walker_types[type]->yield;
    walker->step = mesh_walker_types[type]->step;
    walker->structsize = mesh_walker_types[type]->structsize;
    walker->order = mesh_walker_types[type]->order;
    walker->valid_mask = mesh_walker_types[type]->valid_mask;

    /* safety checks */
    /* if this raises an error either the caller is wrong or
     * 'mesh_walker_types' needs updating */
    lib_assert(mask_vert == 0 || (walker->valid_mask & BM_VERT));
    lib_assert(mask_edge == 0 || (walker->valid_mask & BM_EDGE));
    lib_assert(mask_face == 0 || (walker->valid_mask & BM_FACE));
  }

  walker->worklist = lib_mempool_create(walker->structsize, 0, 128, BLI_MEMPOOL_NOP);
  lib_listbase_clear(&walker->states);
}

void mesh_walker_end(MeshWalker *walker)
{
  lib_mempool_destroy(walker->worklist);
  lib_gset_free(walker->visit_set, NULL);
  lib_gset_free(walker->visit_set_alt, NULL);
}

void *BMW_step(BMWalker *walker)
{
  BMHeader *head;

  head = BMW_walk(walker);

  return head;
}

int BMW_current_depth(BMWalker *walker)
{
  return walker->depth;
}

void *BMW_walk(BMWalker *walker)
{
  void *current = NULL;

  while (BMW_current_state(walker)) {
    current = walker->step(walker);
    if (current) {
      return current;
    }
  }
  return NULL;
}

void *mesh_walker_current_state(MeshWalker *walker)
{
  MeshWalkerGeneric *currentstate = walker->states.first;
  if (currentstate) {
    /* Automatic update of depth. For most walkers that
     * follow the standard "Step" pattern of:
     * - read current state
     * - remove current state
     * - push new states
     * - return walk result from just-removed current state
     * this simple automatic update should keep track of depth
     * just fine. Walkers that deviate from that pattern may
     * need to manually update the depth if they care about
     * keeping it correct. */
    walker->depth = currentstate->depth + 1;
  }
  return currentstate;
}

void mesh_walker_state_remove(MeshWalker *walker)
{
  void *oldstate;
  oldstate = mesh_walker_current_state(walker);
  lib_remlink(&walker->states, oldstate);
  lib_mempool_free(walker->worklist, oldstate);
}

void *mesh_walker_state_add(MeshWalker *walker)
{
  MeshWalkerGeneric *newstate;
  newstate = lib_mempool_alloc(walker->worklist);
  newstate->depth = walker->depth;
  switch (walker->order) {
    case MESH_WALKER_DEPTH_FIRST:
      lib_addhead(&walker->states, newstate);
      break;
    case MESH_WALKER_BREADTH_FIRST:
      lib_addtail(&walker->states, newstate);
      break;
    default:
      lib_assert(0);
      break;
  }
  return newstate;
}

void mesh_walker_reset(MeshWalker *walker)
{
  while (mesh_walker_current_state(walker)) {
    mesh_walker_state_remove(walker);
  }
  walker->depth = 0;
  lib_gset_clear(walker->visit_set, NULL);
  lib_gset_clear(walker->visit_set_alt, NULL);
}
