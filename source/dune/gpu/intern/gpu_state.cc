#ifndef GPU_STANDALONE
#  include "types_userdef.h"
#  define PIXELSIZE (U.pixelsize)
#else
#  define PIXELSIZE (1.0f)
#endif

#include "lib_math_vector.h"
#include "lib_utildefines.h"

#include "dune_global.h"

#include "gpu_state.h"

#include "gpu_cxt_private.hh"

#include "gpu_state_private.hh"

using namespace dune::gpu;

#define SET_STATE(_prefix, _state, _value) \
  do { \
    StateManager *stack = Cxt::get()->state_manager; \
    auto &state_object = stack->_prefix##state; \
    state_object._state = (_value); \
  } while (0)

#define SET_IMMUTABLE_STATE(_state, _value) SET_STATE(, _state, _value)
#define SET_MUTABLE_STATE(_state, _value) SET_STATE(mutable_, _state, _value)

/* Immutable state Setters */
void gpu_blend(eGPUBlend blend)
{
  SET_IMMUTABLE_STATE(blend, blend);
}

void gpu_face_culling(eGPUFaceCullTest culling)
{
  SET_IMMUTABLE_STATE(culling_test, culling);
}

void gpu_front_facing(bool invert)
{
  SET_IMMUTABLE_STATE(invert_facing, invert);
}

void gpu_provoking_vertex(eGPUProvokingVertex vert)
{
  SET_IMMUTABLE_STATE(provoking_vert, vert);
}

void gpu_depth_test(eGPUDepthTest test)
{
  SET_IMMUTABLE_STATE(depth_test, test);
}

void gpu_stencil_test(eGPUStencilTest test)
{
  SET_IMMUTABLE_STATE(stencil_test, test);
}

void gpu_line_smooth(bool enable)
{
  SET_IMMUTABLE_STATE(line_smooth, enable);
}

void gpu_polygon_smooth(bool enable)
{
  SET_IMMUTABLE_STATE(polygon_smooth, enable);
}

void gpu_logic_op_xor_set(bool enable)
{
  SET_IMMUTABLE_STATE(logic_op_xor, enable);
}

void gpu_write_mask(eGPUWriteMask mask)
{
  SET_IMMUTABLE_STATE(write_mask, mask);
}

void gpu_color_mask(bool r, bool g, bool b, bool a)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->state;
  uint32_t write_mask = state.write_mask;
  SET_FLAG_FROM_TEST(write_mask, r, (uint32_t)GPU_WRITE_RED);
  SET_FLAG_FROM_TEST(write_mask, g, (uint32_t)GPU_WRITE_GREEN);
  SET_FLAG_FROM_TEST(write_mask, b, (uint32_t)GPU_WRITE_BLUE);
  SET_FLAG_FROM_TEST(write_mask, a, (uint32_t)GPU_WRITE_ALPHA);
  state.write_mask = write_mask;
}

void gpu_depth_mask(bool depth)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->state;
  uint32_t write_mask = state.write_mask;
  SET_FLAG_FROM_TEST(write_mask, depth, (uint32_t)GPU_WRITE_DEPTH);
  state.write_mask = write_mask;
}

void gpu_shadow_offset(bool enable)
{
  SET_IMMUTABLE_STATE(shadow_bias, enable);
}

void gpu_clip_distances(int distances_enabled)
{
  SET_IMMUTABLE_STATE(clip_distances, distances_enabled);
}

void gpu_state_set(eGPUWriteMask write_mask,
                   eGPUBlend blend,
                   eGPUFaceCullTest culling_test,
                   eGPUDepthTest depth_test,
                   eGPUStencilTest stencil_test,
                   eGPUStencilOp stencil_op,
                   eGPUProvokingVertex provoking_vert)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->state;
  state.write_mask = (uint32_t)write_mask;
  state.blend = (uint32_t)blend;
  state.culling_test = (uint32_t)culling_test;
  state.depth_test = (uint32_t)depth_test;
  state.stencil_test = (uint32_t)stencil_test;
  state.stencil_op = (uint32_t)stencil_op;
  state.provoking_vert = (uint32_t)provoking_vert;
}

/* Mutable State Setters */
void gpu_depth_range(float near, float far)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->mutable_state;
  copy_v2_fl2(state.depth_range, near, far);
}

void gpu_line_width(float width)
{
  width = max_ff(1.0f, width * PIXELSIZE);
  SET_MUTABLE_STATE(line_width, width);
}

void gpu_point_size(float size)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->mutable_state;
  /* Keep the sign of point_size since it represents the enable state. */
  state.point_size = size * ((state.point_size > 0.0) ? 1.0f : -1.0f);
}

void gpu_program_point_size(bool enable)
{
  StateManager *stack = Cxt::get()->state_manager;
  auto &state = stack->mutable_state;
  /* Set point size sign negative to disable. */
  state.point_size = fabsf(state.point_size) * (enable ? 1 : -1);
}

void gpu_scissor_test(bool enable)
{
  Cxt::get()->active_fb->scissor_test_set(enable);
}

void gpu_scissor(int x, int y, int width, int height)
{
  int scissor_rect[4] = {x, y, width, height};
  Cxt::get()->active_fb->scissor_set(scissor_rect);
}

void gpu_viewport(int x, int y, int width, int height)
{
  int viewport_rect[4] = {x, y, width, height};
  Cxt::get()->active_fb->viewport_set(viewport_rect);
}

void gpu_stencil_ref_set(uint ref)
{
  SET_MUTABLE_STATE(stencil_ref, (uint8_t)ref);
}

void gpu_stencil_write_mask_set(uint write_mask)
{
  SET_MUTABLE_STATE(stencil_write_mask, (uint8_t)write_mask);
}

void gpu_stencil_compare_mask_set(uint compare_mask)
{
  SET_MUTABLE_STATE(stencil_compare_mask, (uint8_t)compare_mask);
}

/* State Getters */
eGPUBlend gpu_blend_get()
{
  GPUState &state = Cxt::get()->state_manager->state;
  return (eGPUBlend)state.blend;
}

eGPUWriteMask gpu_write_mask_get()
{
  GPUState &state = Cxt::get()->state_manager->state;
  return (eGPUWriteMask)state.write_mask;
}

uint gpu_stencil_mask_get()
{
  const GPUStateMutable &state = Cxt::get()->state_manager->mutable_state;
  return state.stencil_write_mask;
}

eGPUDepthTest gpu_depth_test_get()
{
  GPUState &state = Cxt::get()->state_manager->state;
  return (eGPUDepthTest)state.depth_test;
}

eGPUStencilTest gpu_stencil_test_get()
{
  GPUState &state = Cxt::get()->state_manager->state;
  return (eGPUStencilTest)state.stencil_test;
}

float gpu_line_width_get()
{
  const GPUStateMutable &state = Cxt::get()->state_manager->mutable_state;
  return state.line_width;
}

void gpu_scissor_get(int coords[4])
{
  Cxt::get()->active_fb->scissor_get(coords);
}

void gpu_viewport_size_get_f(float coords[4])
{
  int viewport[4];
  Cxt::get()->active_fb->viewport_get(viewport);
  for (int i = 0; i < 4; i++) {
    coords[i] = viewport[i];
  }
}

void gpu_viewport_size_get_i(int coords[4])
{
  Cxt::get()->active_fb->viewport_get(coords);
}

bool gpu_depth_mask_get()
{
  const GPUState &state = Cxt::get()->state_manager->state;
  return (state.write_mask & GPU_WRITE_DEPTH) != 0;
}

bool gpu_mipmap_enabled()
{
  /* TODO: this used to be a userdef option. */
  return true;
}

/* Cxt Utils **/

void gpu_flush()
{
  Cxt::get()->flush();
}

void gpu_finish()
{
  Cxt::get()->finish();
}

void gpu_apply_state()
{
  Cxt::get()->state_manager->apply_state();
}

/* BGL workaround
 * bgl makes direct GL calls that makes our state tracking out of date.
 * This flag make it so that the pyGPU calls will not override the state set by
 * bgl functions. */

void gpu_bgl_start()
{
  Cxt *cxt = Cxt::get();
  if (!(cxt && cxt->state_manager)) {
    return;
  }
  StateManager &state_manager = *(Cxt::get()->state_manager);
  if (state_manager.use_bgl == false) {
    /* Expected by many addons (see T80169, T81289).
     * This will reset the blend function. */
    gpu_blend(GPU_BLEND_NONE);

    /* Equivalent of setting the depth func `glDepthFunc(GL_LEQUAL)`
     * Needed since Python scripts may enable depth test.
     * Without this block the depth test function is undefined. */
    {
      eGPUDepthTest depth_test_real = GPU_depth_test_get();
      eGPUDepthTest depth_test_temp = GPU_DEPTH_LESS_EQUAL;
      if (depth_test_real != depth_test_temp) {
        gpu_depth_test(depth_test_temp);
        state_manager.apply_state();
        gpu_depth_test(depth_test_real);
      }
    }

    state_manager.apply_state();
    state_manager.use_bgl = true;
  }
}

void gpu_bgl_end()
{
  Cxt *cxt = Cxt::get();
  if (!(cxt && cxt->state_manager)) {
    return;
  }
  StateManager &state_manager = *cxt->state_manager;
  if (state_manager.use_bgl == true) {
    state_manager.use_bgl = false;
    /* Resync state tracking. */
    state_manager.force_state();
  }
}

bool gpu_bgl_get()
{
  return Cxt::get()->state_manager->use_bgl;
}

/* Synchronization Utils */
void gpu_memory_barrier(eGPUBarrier barrier)
{
  Cxt::get()->state_manager->issue_barrier(barrier);
}

/* Default State */
StateManager::StateManager()
{
  /* Set default state. */
  state.write_mask = GPU_WRITE_COLOR;
  state.blend = GPU_BLEND_NONE;
  state.culling_test = GPU_CULL_NONE;
  state.depth_test = GPU_DEPTH_NONE;
  state.stencil_test = GPU_STENCIL_NONE;
  state.stencil_op = GPU_STENCIL_OP_NONE;
  state.provoking_vert = GPU_VERTEX_LAST;
  state.logic_op_xor = false;
  state.invert_facing = false;
  state.shadow_bias = false;
  state.clip_distances = 0;
  state.polygon_smooth = false;
  state.line_smooth = false;

  mutable_state.depth_range[0] = 0.0f;
  mutable_state.depth_range[1] = 1.0f;
  mutable_state.point_size = -1.0f; /* Negative is not using point size. */
  mutable_state.line_width = 1.0f;
  mutable_state.stencil_write_mask = 0x00;
  mutable_state.stencil_compare_mask = 0x00;
  mutable_state.stencil_reference = 0x00;
}
