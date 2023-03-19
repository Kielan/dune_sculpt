
/**
 * Manage GL vertex array IDs in a thread-safe way
 * Use these instead of glGenBuffers & its friends
 * - alloc must be called from a thread that is bound
 *   to the context that will be used for drawing with
 *   this VAO.
 * - free can be called from any thread
 */

/* TODO: Create cmake option. */
#if WITH_OPENGL
#  define WITH_OPENGL_BACKEND 1
#endif

#include "lib_assert.h"
#include "lib_utildefines.h"

#include "gpu_context.h"
#include "gpu_framebuffer.h"

#include "GHOST_C-api.h"

#include "gpu_backend.hh"
#include "gpu_batch_private.hh"
#include "gpu_context_private.hh"
#include "gpu_matrix_private.h"

#ifdef WITH_OPENGL_BACKEND
#  include "gl_backend.hh"
#  include "gl_context.hh"
#endif
#ifdef WITH_METAL_BACKEND
#  include "mtl_backend.hh"
#endif

#include <mutex>
#include <vector>

using namespace dune::gpu;

static thread_local Context *active_ctx = nullptr;

/* -------------------------------------------------------------------- */
/** gpu::Context methods **/

namespace dune::gpu {

Context::Context()
{
  thread_ = pthread_self();
  is_active_ = false;
  matrix_state = gpu_matrix_state_create();
}

Context::~Context()
{
  gpu_matrix_state_discard(matrix_state);
  delete state_manager;
  delete front_left;
  delete back_left;
  delete front_right;
  delete back_right;
  delete imm;
}

bool Context::is_active_on_thread()
{
  return (this == active_ctx) && pthread_equal(pthread_self(), thread_);
}

Context *Context::get()
{
  return active_ctx;
}

}  // namespace dune::gpu

/* -------------------------------------------------------------------- */

GPUContext *gpu_ctx_create(void *ghost_window)
{
  if (GPUBackend::get() == nullptr) {
    /* TODO: move where it make sense. */
    gpu_backend_init(GPU_BACKEND_OPENGL);
  }

  Context *ctx = GPUBackend::get()->context_alloc(ghost_window);

  gpu_context_active_set(wrap(ctx));
  return wrap(ctx);
}

void gpu_ctx_discard(GPUContext *ctx_)
{
  Context *ctx = unwrap(ctx_);
  delete ctx;
  active_ctx = nullptr;
}

void gpu_ctx_active_set(GPUContext *ctx_)
{
  Context *ctx = unwrap(ctx_);

  if (active_ctx) {
    active_ctx->deactivate();
  }

  active_ctx = ctx;

  if (ctx) {
    ctx->activate();
  }
}

GPUContext *gpu_ctx_active_get()
{
  return wrap(Context::get());
}

/* -------------------------------------------------------------------- */
/** Main context global mutex
 *
 * Used to avoid crash on some old drivers.
 **/

static std::mutex main_ctx_mutex;

void gpu_context_main_lock()
{
  main_context_mutex.lock();
}

void gpu_context_main_unlock()
{
  main_context_mutex.unlock();
}

/* -------------------------------------------------------------------- */
/** GPU Begin/end work blocks
 *
 * Used to explicitly define a per-frame block within which GPU work will happen.
 * Used for global autoreleasepool flushing in Metal
 **/

void gpu_render_begin()
{
  GPUBackend *backend = GPUBackend::get();
  lib_assert(backend);
  backend->render_begin();
}
void gpu_render_end()
{
  GPUBackend *backend = GPUBackend::get();
  lib_assert(backend);
  backend->render_end();
}
void gpu_render_step()
{
  GPUBackend *backend = GPUBackend::get();
  lib_assert(backend);
  backend->render_step();
}

/* -------------------------------------------------------------------- */
/** Backend selection */

static GPUBackend *g_backend;

bool gpu_backend_supported(eGPUBackendType type)
{
  switch (type) {
    case GPU_BACKEND_OPENGL:
#ifdef WITH_OPENGL_BACKEND
      return true;
#else
      return false;
#endif
    case GPU_BACKEND_METAL:
#ifdef WITH_METAL_BACKEND
      return MTLBackend::metal_is_supported();
#else
      return false;
#endif
    default:
      lib_assert(false && "No backend specified");
      return false;
  }
}

void gpu_backend_init(eGPUBackendType backend_type)
{
  lib_assert(g_backend == nullptr);
  lib_assert(gpu_backend_supported(backend_type));

  switch (backend_type) {
#ifdef WITH_OPENGL_BACKEND
    case GPU_BACKEND_OPENGL:
      g_backend = new GLBackend;
      break;
#endif
#ifdef WITH_METAL_BACKEND
    case GPU_BACKEND_METAL:
      g_backend = new MTLBackend;
      break;
#endif
    default:
      lib_assert(0);
      break;
  }
}

void gpu_backend_exit()
{
  /* TODO: assert no resource left. Currently UI textures are still not freed in their context
   * correctly. */
  delete g_backend;
  g_backend = nullptr;
}

eGPUBackendType gpu_backend_get_type()
{

#ifdef WITH_OPENGL_BACKEND
  if (g_backend && dynamic_cast<GLBackend *>(g_backend) != nullptr) {
    return GPU_BACKEND_OPENGL;
  }
#endif

#ifdef WITH_METAL_BACKEND
  if (g_backend && dynamic_cast<MTLBackend *>(g_backend) != nullptr) {
    return GPU_BACKEND_METAL;
  }
#endif

  return GPU_BACKEND_NONE;
}

GPUBackend *GPUBackend::get()
{
  return g_backend;
}
