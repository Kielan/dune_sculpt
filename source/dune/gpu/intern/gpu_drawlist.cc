/* Implementation of Multi Draw Indirect. */

#include "mem_guardedalloc.h"

#include "gpu_batch.h"
#include "gpu_drawlist.h"

#include "gpu_backend.hh"

#include "gpu_drawlist_private.hh"

using namespace dune::gpu;

GPUDrawList *gpu_draw_list_create(int list_length)
{
  DrawList *list_ptr = GPUBackend::get()->drawlist_alloc(list_length);
  return wrap(list_ptr);
}

void gpu_draw_list_discard(GPUDrawList *list)
{
  DrawList *list_ptr = unwrap(list);
  delete list_ptr;
}

void gpu_draw_list_append(GPUDrawList *list, GPUBatch *batch, int i_first, int i_count)
{
  DrawList *list_ptr = unwrap(list);
  list_ptr->append(batch, i_first, i_count);
}

void gpu_draw_list_submit(GPUDrawList *list)
{
  DrawList *list_ptr = unwrap(list);
  list_ptr->submit();
}
