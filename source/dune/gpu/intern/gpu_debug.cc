/* Debug features of OpenGL. **/

#include "dune_global.h"
#include "lib_string.h"
#include "gpu_cxt_private.hh"
#include "gpu_debug.h"

using namespace dune;
using namespace dune::gpu;

void gpu_debug_group_begin(const char *name)
{
  if (!(G.debug & G_DEBUG_GPU)) {
    return;
  }
  Cxt *cxt = Cxt::get();
  DebugStack &stack = cxt->debug_stack;
  stack.append(StringRef(name));
  ctx->debug_group_begin(name, stack.size());
}

void gpu_debug_group_end()
{
  if (!(G.debug & G_DEBUG_GPU)) {
    return;
  }
  Context *ctx = Context::get();
  ctx->debug_stack.pop_last();
  ctx->debug_group_end();
}

void gpu_debug_get_groups_names(int name_buf_len, char *r_name_buf)
{
  Cxt *cxt = Cxt::get();
  if (ctx == nullptr) {
    return;
  }
  DebugStack &stack = cxt->debug_stack;
  if (stack.size() == 0) {
    r_name_buf[0] = '\0';
    return;
  }
  size_t sz = 0;
  for (StringRef &name : stack) {
    sz += lib_snprintf_rlen(r_name_buf + sz, name_buf_len - sz, "%s > ", name.data());
  }
  r_name_buf[sz - 3] = '\0';
}

bool gpu_debug_group_match(const char *ref)
{
  /* Otherwise there will be no names. */
  lib_assert(G.debug & G_DEBUG_GPU);
  Cxt *cxt = Cxt::get();
  if (cxt == nullptr) {
    return false;
  }
  const DebugStack &stack = cxt->debug_stack;
  for (const StringRef &name : stack) {
    if (name == ref) {
      return true;
    }
  }
  return false;
}
