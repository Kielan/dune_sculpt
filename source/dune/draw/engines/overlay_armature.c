#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "types_armature.h"
#include "types_constraint.h"
#include "types_mesh.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_view3d.h"

#include "draw_render.h"

#include "lib_math.h"
#include "lib_utildefines.h"

#include "dune_action.h"
#include "dune_armature.h"
#include "dune_deform.h"
#include "dune_modifier.h"
#include "dune_object.h"

#include "dgraph_query.h"

#include "ed_armature.h"
#include "ed_view3d.h"

#include "ui_resources.h"

#include "draw_common.h"
#include "draw_manager_text.h"

#include "overlay_private.h"

#include "draw_cache_impl.h"

#define BONE_VAR(eBone, pchan, var) ((eBone) ? (eBone->var) : (pchan->var))
#define BONE_FLAG(eBone, pchan) ((eBone) ? (eBone->flag) : (pchan->bone->flag))

#define PT_DEFAULT_RAD 0.05f /* radius of the point batch. */

typedef struct ArmatureDrawContext {
  /* Current armature object */
  Object *ob;
  /* bArmature *arm; */ /* TODO */

  union {
    struct {
      DRWCallBuffer *outline;
      DRWCallBuffer *solid;
      DRWCallBuffer *wire;
    };
    struct {
      DRWCallBuffer *envelope_outline;
      DRWCallBuffer *envelope_solid;
      DRWCallBuffer *envelope_distance;
    };
    struct {
      DRWCallBuffer *stick;
    };
  };

  DRWCallBuffer *dof_lines;
  DRWCallBuffer *dof_sphere;
  DRWCallBuffer *point_solid;
  DRWCallBuffer *point_outline;
  DRWShadingGroup *custom_solid;
  DRWShadingGroup *custom_outline;
  DRWShadingGroup *custom_wire;
  GHash *custom_shapes_ghash;

  OVERLAY_ExtraCallBuffers *extras;

  /* not a theme, this is an override */
  const float *const_color;
  float const_wire;

  bool do_relations;
  bool transparent;
  bool show_relations;

  const ThemeWireColor *bcolor; /* pchan color */
} ArmatureDrawContext;

bool overay_armature_is_pose_mode(Object *ob, const DRWContextState *draw_ctx)
{
  Object *active_ob = draw_ctx->obact;

  /* Pose armature is handled by pose mode engine. */
  if (((ob == active_ob) || (ob->mode & OB_MODE_POSE)) &&
      ((draw_ctx->object_mode & OB_MODE_POSE) != 0)) {
    return true;
  }

  /* Armature parent is also handled by pose mode engine. */
  if ((active_ob != NULL) && (draw_ctx->object_mode & OB_MODE_ALL_WEIGHT_PAINT)) {
    if (ob == draw_ctx->object_pose) {
      return true;
    }
  }

  return false;
}

void overlay_armature_cache_init(overlay_Data *vedata)
{
  overlay_PassList *psl = vedata->psl;
  overlay_PrivateData *pd = vedata->stl->pd;

  const DrawCtxState *draw_ctx = draw_ctx_state_get();
  const bool is_select_mode = draw_state_is_select();
  pd->armature.transparent = (draw_ctx->v3d->shading.type == OB_WIRE) ||
                             XRAY_FLAG_ENABLED(draw_ctx->v3d);
  pd->armature.show_relations = ((draw_ctx->v3d->flag & V3D_HIDE_HELPLINES) == 0) &&
                                !is_select_mode;
  pd->armature.do_pose_xray = (pd->overlay.flag & V3D_OVERLAY_BONE_SELECT) != 0;
  pd->armature.do_pose_fade_geom = pd->armature.do_pose_xray &&
                                   ((draw_ctx->object_mode & OB_MODE_WEIGHT_PAINT) == 0) &&
                                   draw_ctx->object_pose != NULL;

  const float wire_alpha = pd->overlay.bone_wire_alpha;
  const bool use_wire_alpha = (wire_alpha < 1.0f);

  DRWState state;

  if (pd->armature.do_pose_fade_geom) {
    state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_EQUAL | DRW_STATE_BLEND_ALPHA;
    DRW_PASS_CREATE(psl->armature_bone_select_ps, state | pd->clipping_state);

    float alpha = pd->overlay.xray_alpha_bone;
    struct GPUShader *sh = overlay_shader_uniform_color();
    DrawShadingGroup *grp;

    pd->armature_bone_select_act_grp = grp = DRW_shgroup_create(sh, psl->armature_bone_select_ps);
    draw_shgroup_uniform_vec4_copy(grp, "color", (float[4]){0.0f, 0.0f, 0.0f, alpha});

    pd->armature_bone_select_grp = grp = DRW_shgroup_create(sh, psl->armature_bone_select_ps);
    draw_shgroup_uniform_vec4_copy(grp, "color", (float[4]){0.0f, 0.0f, 0.0f, pow(alpha, 4)});
  }

  for (int i = 0; i < 2; i++) {
    struct GPUShader *sh;
    struct GPUVertFormat *format;
    DrawShadingGroup *grp = NULL;

    OVERLAY_InstanceFormats *formats = OVERLAY_shader_instance_formats_get();
    OVERLAY_ArmatureCallBuffers *cb = &pd->armature_call_buffers[i];

    cb->solid.custom_shapes_ghash = lib_ghash_ptr_new(__func__);
    cb->transp.custom_shapes_ghash = lib_ghash_ptr_new(__func__);

    DrawPass **p_armature_ps = &psl->armature_ps[i];
    DrawState infront_state = (DRW_state_is_select() && (i == 1)) ? DRW_STATE_IN_FRONT_SELECT : 0;
    state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_WRITE_DEPTH;
    DRW_PASS_CREATE(*p_armature_ps, state | pd->clipping_state | infront_state);
    DrawPass *armature_ps = *p_armature_ps;

    DrawPass **p_armature_trans_ps = &psl->armature_transp_ps[i];
    state = DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_BLEND_ADD;
    DRW_PASS_CREATE(*p_armature_trans_ps, state | pd->clipping_state);
    DrawPass *armature_transp_ps = *p_armature_trans_ps;

#define BUF_INSTANCE DRW_shgroup_call_buffer_instance
#define BUF_LINE(grp, format) DRW_shgroup_call_buffer(grp, format, GPU_PRIM_LINES)
#define BUF_POINT(grp, format) DRW_shgroup_call_buffer(grp, format, GPU_PRIM_POINTS)

    {
      format = formats->instance_bone;

      sh = overlay_shader_armature_sphere(false);
      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.point_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_point_get());

      grp = draw_shgroup_create(sh, armature_ps);
      draw_shgroup_state_disable(grp, DRW_STATE_WRITE_DEPTH);
      draw_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
      draw_shgroup_uniform_float_copy(grp, "alpha", wire_alpha * 0.4f);
      cb->transp.point_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_point_get());

      sh = overlay_shader_armature_shape(false);
      grp = draw_shgroup_create(sh, armature_ps);
      draw_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.custom_fill = grp;
      cb->solid.box_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_box_get());
      cb->solid.octa_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_octahedral_get());

      grp = draw_shgroup_create(sh, armature_ps);
      draw_shgroup_state_disable(grp, DRW_STATE_WRITE_DEPTH);
      draw_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
      draw_shgroup_uniform_float_copy(grp, "alpha", wire_alpha * 0.6f);
      cb->transp.custom_fill = grp;
      cb->transp.box_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_box_get());
      cb->transp.octa_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_octahedral_get());

      sh = OVERLAY_shader_armature_sphere(true);
      grp = DRW_shgroup_create(sh, armature_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.point_outline = BUF_INSTANCE(grp, format, DRW_cache_bone_point_wire_outline_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_ps);
        draw_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        draw_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.point_outline = BUF_INSTANCE(
            grp, format, DRW_cache_bone_point_wire_outline_get());
      }
      else {
        cb->transp.point_outline = cb->solid.point_outline;
      }

      sh = OVERLAY_shader_armature_shape(true);
      cb->solid.custom_outline = grp = DRW_shgroup_create(sh, armature_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.box_outline = BUF_INSTANCE(grp, format, DRW_cache_bone_box_wire_get());
      cb->solid.octa_outline = BUF_INSTANCE(grp, format, DRW_cache_bone_octahedral_wire_get());

      if (use_wire_alpha) {
        cb->transp.custom_outline = grp = DRW_shgroup_create(sh, armature_ps);
        draw_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        draw_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.box_outline = BUF_INSTANCE(grp, format, DRW_cache_bone_box_wire_get());
        cb->transp.octa_outline = BUF_INSTANCE(grp, format, DRW_cache_bone_octahedral_wire_get());
      }
      else {
        cb->transp.custom_outline = cb->solid.custom_outline;
        cb->transp.box_outline = cb->solid.box_outline;
        cb->transp.octa_outline = cb->solid.octa_outline;
      }

      sh = overlay_shader_armature_shape_wire();
      cb->solid.custom_wire = grp = DRW_shgroup_create(sh, armature_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", 1.0f);

      if (use_wire_alpha) {
        cb->transp.custom_wire = grp = DRW_shgroup_create(sh, armature_ps);
        draw_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        draw_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
      }
      else {
        cb->transp.custom_wire = cb->solid.custom_wire;
      }
    }
    {
      format = formats->instance_extra;

      sh = overlay_shader_armature_degrees_of_freedom_wire();
      grp = DRW_shgroup_create(sh, armature_ps);
      draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      draw_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.dof_lines = BUF_INSTANCE(grp, format, DRW_cache_bone_dof_lines_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_ps);
        DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.dof_lines = BUF_INSTANCE(grp, format, DRW_cache_bone_dof_lines_get());
      }
      else {
        cb->transp.dof_lines = cb->solid.dof_lines;
      }

      sh = OVERLAY_shader_armature_degrees_of_freedom_solid();
      grp = DRW_shgroup_create(sh, armature_transp_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.dof_sphere = BUF_INSTANCE(grp, format, DRW_cache_bone_dof_sphere_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_transp_ps);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.dof_sphere = BUF_INSTANCE(grp, format, DRW_cache_bone_dof_sphere_get());
      }
      else {
        cb->transp.dof_sphere = cb->solid.dof_sphere;
      }
    }
    {
      format = formats->instance_bone_stick;

      sh = OVERLAY_shader_armature_stick();
      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.stick = BUF_INSTANCE(grp, format, DRW_cache_bone_stick_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_ps);
        DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.stick = BUF_INSTANCE(grp, format, DRW_cache_bone_stick_get());
      }
      else {
        cb->transp.stick = cb->solid.stick;
      }
    }
    {
      format = formats->instance_bone_envelope;

      sh = OVERLAY_shader_armature_envelope(false);
      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_state_enable(grp, DRW_STATE_CULL_BACK);
      DRW_shgroup_uniform_bool_copy(grp, "isDistance", false);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.envelope_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_envelope_solid_get());

      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_state_disable(grp, DRW_STATE_WRITE_DEPTH);
      DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA | DRW_STATE_CULL_BACK);
      DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha * 0.6f);
      cb->transp.envelope_fill = BUF_INSTANCE(grp, format, DRW_cache_bone_envelope_solid_get());

      format = formats->instance_bone_envelope_outline;

      sh = OVERLAY_shader_armature_envelope(true);
      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.envelope_outline = BUF_INSTANCE(
          grp, format, DRW_cache_bone_envelope_outline_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_ps);
        DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.envelope_outline = BUF_INSTANCE(
            grp, format, DRW_cache_bone_envelope_outline_get());
      }
      else {
        cb->transp.envelope_outline = cb->solid.envelope_outline;
      }

      format = formats->instance_bone_envelope_distance;

      sh = OVERLAY_shader_armature_envelope(false);
      grp = DRW_shgroup_create(sh, armature_transp_ps);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      DRW_shgroup_uniform_bool_copy(grp, "isDistance", true);
      DRW_shgroup_state_enable(grp, DRW_STATE_CULL_FRONT);
      cb->solid.envelope_distance = BUF_INSTANCE(grp, format, DRW_cache_bone_envelope_solid_get());

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_transp_ps);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        DRW_shgroup_uniform_bool_copy(grp, "isDistance", true);
        DRW_shgroup_state_enable(grp, DRW_STATE_CULL_FRONT);
        cb->transp.envelope_distance = BUF_INSTANCE(
            grp, format, DRW_cache_bone_envelope_solid_get());
      }
      else {
        cb->transp.envelope_distance = cb->solid.envelope_distance;
      }
    }
    {
      format = formats->pos_color;

      sh = OVERLAY_shader_armature_wire();
      grp = DRW_shgroup_create(sh, armature_ps);
      DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
      DRW_shgroup_uniform_float_copy(grp, "alpha", 1.0f);
      cb->solid.wire = BUF_LINE(grp, format);

      if (use_wire_alpha) {
        grp = DRW_shgroup_create(sh, armature_ps);
        DRW_shgroup_state_enable(grp, DRW_STATE_BLEND_ALPHA);
        DRW_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
        DRW_shgroup_uniform_float_copy(grp, "alpha", wire_alpha);
        cb->transp.wire = BUF_LINE(grp, format);
      }
      else {
        cb->transp.wire = cb->solid.wire;
      }
    }
  }
}

/* -------------------------------------------------------------------- */
/** Shader Groups (draw_shgroup) **/

static void bone_instance_data_set_angle_minmax(BoneInstanceData *data,
                                                const float aminx,
                                                const float aminz,
                                                const float amaxx,
                                                const float amaxz)
{
  data->amin_a = aminx;
  data->amin_b = aminz;
  data->amax_a = amaxx;
  data->amax_b = amaxz;
}

/* Encode 2 units float with byte precision into a float. */
static float encode_2f_to_float(float a, float b)
{
  CLAMP(a, 0.0f, 1.0f);
  CLAMP(b, 0.0f, 2.0f); /* Can go up to 2. Needed for wire size. */
  return (float)((int)(a * 255) | ((int)(b * 255) << 8));
}

void OVERLAY_bone_instance_data_set_color_hint(BoneInstanceData *data, const float hint_color[4])
{
  /* Encoded color into 2 floats to be able to use the obmat to color the custom bones. */
  data->color_hint_a = encode_2f_to_float(hint_color[0], hint_color[1]);
  data->color_hint_b = encode_2f_to_float(hint_color[2], hint_color[3]);
}

void OVERLAY_bone_instance_data_set_color(BoneInstanceData *data, const float bone_color[4])
{
  /* Encoded color into 2 floats to be able to use the obmat to color the custom bones. */
  data->color_a = encode_2f_to_float(bone_color[0], bone_color[1]);
  data->color_b = encode_2f_to_float(bone_color[2], bone_color[3]);
}

/* Octahedral */
static void drw_shgroup_bone_octahedral(ArmatureDrawContext *ctx,
                                        const float (*bone_mat)[4],
                                        const float bone_color[4],
                                        const float hint_color[4],
                                        const float outline_color[4])
{
  BoneInstanceData inst_data;
  mul_m4_m4m4(inst_data.mat, ctx->ob->obmat, bone_mat);
  if (ctx->solid) {
    OVERLAY_bone_instance_data_set_color(&inst_data, bone_color);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_color);
    DRW_buffer_add_entry_struct(ctx->solid, &inst_data);
  }
  if (outline_color[3] > 0.0f) {
    OVERLAY_bone_instance_data_set_color(&inst_data, outline_color);
    DRW_buffer_add_entry_struct(ctx->outline, &inst_data);
  }
}

/* Box / B-Bone */
static void drw_shgroup_bone_box(ArmatureDrawContext *ctx,
                                 const float (*bone_mat)[4],
                                 const float bone_color[4],
                                 const float hint_color[4],
                                 const float outline_color[4])
{
  BoneInstanceData inst_data;
  mul_m4_m4m4(inst_data.mat, ctx->ob->obmat, bone_mat);
  if (ctx->solid) {
    OVERLAY_bone_instance_data_set_color(&inst_data, bone_color);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_color);
    DRW_buffer_add_entry_struct(ctx->solid, &inst_data);
  }
  if (outline_color[3] > 0.0f) {
    OVERLAY_bone_instance_data_set_color(&inst_data, outline_color);
    DRW_buffer_add_entry_struct(ctx->outline, &inst_data);
  }
}

/* Wire */
static void drw_shgroup_bone_wire(ArmatureDrawContext *ctx,
                                  const float (*bone_mat)[4],
                                  const float color[4])
{
  float head[3], tail[3];
  mul_v3_m4v3(head, ctx->ob->obmat, bone_mat[3]);
  add_v3_v3v3(tail, bone_mat[3], bone_mat[1]);
  mul_m4_v3(ctx->ob->obmat, tail);

  DRW_buffer_add_entry(ctx->wire, head, color);
  DRW_buffer_add_entry(ctx->wire, tail, color);
}

/* Stick */
static void drw_shgroup_bone_stick(ArmatureDrawContext *ctx,
                                   const float (*bone_mat)[4],
                                   const float col_wire[4],
                                   const float col_bone[4],
                                   const float col_head[4],
                                   const float col_tail[4])
{
  float head[3], tail[3];
  mul_v3_m4v3(head, ctx->ob->obmat, bone_mat[3]);
  add_v3_v3v3(tail, bone_mat[3], bone_mat[1]);
  mul_m4_v3(ctx->ob->obmat, tail);

  DRW_buffer_add_entry(ctx->stick, head, tail, col_wire, col_bone, col_head, col_tail);
}

/* Envelope */
static void drw_shgroup_bone_envelope_distance(ArmatureDrawContext *ctx,
                                               const float (*bone_mat)[4],
                                               const float *radius_head,
                                               const float *radius_tail,
                                               const float *distance)
{
  if (ctx->envelope_distance) {
    float head_sph[4] = {0.0f, 0.0f, 0.0f, 1.0f}, tail_sph[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    float xaxis[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    /* Still less operation than m4 multiplication. */
    mul_m4_v4(bone_mat, head_sph);
    mul_m4_v4(bone_mat, tail_sph);
    mul_m4_v4(bone_mat, xaxis);
    mul_m4_v4(ctx->ob->obmat, head_sph);
    mul_m4_v4(ctx->ob->obmat, tail_sph);
    mul_m4_v4(ctx->ob->obmat, xaxis);
    sub_v3_v3(xaxis, head_sph);
    float obscale = mat4_to_scale(ctx->ob->obmat);
    head_sph[3] = *radius_head * obscale;
    head_sph[3] += *distance * obscale;
    tail_sph[3] = *radius_tail * obscale;
    tail_sph[3] += *distance * obscale;
    DRW_buffer_add_entry(ctx->envelope_distance, head_sph, tail_sph, xaxis);
  }
}

static void drw_shgroup_bone_envelope(ArmatureDrawContext *ctx,
                                      const float (*bone_mat)[4],
                                      const float bone_col[4],
                                      const float hint_col[4],
                                      const float outline_col[4],
                                      const float *radius_head,
                                      const float *radius_tail)
{
  float head_sph[4] = {0.0f, 0.0f, 0.0f, 1.0f}, tail_sph[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  float xaxis[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  /* Still less operation than m4 multiplication. */
  mul_m4_v4(bone_mat, head_sph);
  mul_m4_v4(bone_mat, tail_sph);
  mul_m4_v4(bone_mat, xaxis);
  mul_m4_v4(ctx->ob->obmat, head_sph);
  mul_m4_v4(ctx->ob->obmat, tail_sph);
  mul_m4_v4(ctx->ob->obmat, xaxis);
  float obscale = mat4_to_scale(ctx->ob->obmat);
  head_sph[3] = *radius_head * obscale;
  tail_sph[3] = *radius_tail * obscale;

  if (head_sph[3] < 0.0f || tail_sph[3] < 0.0f) {
    BoneInstanceData inst_data;
    if (head_sph[3] < 0.0f) {
      /* Draw Tail only */
      scale_m4_fl(inst_data.mat, tail_sph[3] / PT_DEFAULT_RAD);
      copy_v3_v3(inst_data.mat[3], tail_sph);
    }
    else {
      /* Draw Head only */
      scale_m4_fl(inst_data.mat, head_sph[3] / PT_DEFAULT_RAD);
      copy_v3_v3(inst_data.mat[3], head_sph);
    }

    if (ctx->point_solid) {
      OVERLAY_bone_instance_data_set_color(&inst_data, bone_col);
      OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_col);
      DRW_buffer_add_entry_struct(ctx->point_solid, &inst_data);
    }
    if (outline_col[3] > 0.0f) {
      OVERLAY_bone_instance_data_set_color(&inst_data, outline_col);
      DRW_buffer_add_entry_struct(ctx->point_outline, &inst_data);
    }
  }
  else {
    /* Draw Body */
    float tmp_sph[4];
    float len = len_v3v3(tail_sph, head_sph);
    float fac_head = (len - head_sph[3]) / len;
    float fac_tail = (len - tail_sph[3]) / len;
    /* Small epsilon to avoid problem with float precision in shader. */
    if (len > (tail_sph[3] + head_sph[3]) + 1e-8f) {
      copy_v4_v4(tmp_sph, head_sph);
      interp_v4_v4v4(head_sph, tail_sph, head_sph, fac_head);
      interp_v4_v4v4(tail_sph, tmp_sph, tail_sph, fac_tail);
      if (ctx->envelope_solid) {
        DRW_buffer_add_entry(ctx->envelope_solid, head_sph, tail_sph, bone_col, hint_col, xaxis);
      }
      if (outline_col[3] > 0.0f) {
        DRW_buffer_add_entry(ctx->envelope_outline, head_sph, tail_sph, outline_col, xaxis);
      }
    }
    else {
      /* Distance between endpoints is too small for a capsule. Draw a Sphere instead. */
      float fac = max_ff(fac_head, 1.0f - fac_tail);
      interp_v4_v4v4(tmp_sph, tail_sph, head_sph, clamp_f(fac, 0.0f, 1.0f));

      BoneInstanceData inst_data;
      scale_m4_fl(inst_data.mat, tmp_sph[3] / PT_DEFAULT_RAD);
      copy_v3_v3(inst_data.mat[3], tmp_sph);
      if (ctx->point_solid) {
        OVERLAY_bone_instance_data_set_color(&inst_data, bone_col);
        OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_col);
        DRW_buffer_add_entry_struct(ctx->point_solid, &inst_data);
      }
      if (outline_col[3] > 0.0f) {
        OVERLAY_bone_instance_data_set_color(&inst_data, outline_col);
        DRW_buffer_add_entry_struct(ctx->point_outline, &inst_data);
      }
    }
  }
}

/* Custom (geometry) */

extern void drw_batch_cache_validate(Object *custom);
extern void drw_batch_cache_generate_requested_delayed(Object *custom);

BLI_INLINE DRWCallBuffer *custom_bone_instance_shgroup(ArmatureDrawContext *ctx,
                                                       DRWShadingGroup *grp,
                                                       struct GPUBatch *custom_geom)
{
  DRWCallBuffer *buf = BLI_ghash_lookup(ctx->custom_shapes_ghash, custom_geom);
  if (buf == NULL) {
    OVERLAY_InstanceFormats *formats = OVERLAY_shader_instance_formats_get();
    buf = DRW_shgroup_call_buffer_instance(grp, formats->instance_bone, custom_geom);
    BLI_ghash_insert(ctx->custom_shapes_ghash, custom_geom, buf);
  }
  return buf;
}

static void drw_shgroup_bone_custom_solid(ArmatureDrawContext *ctx,
                                          const float (*bone_mat)[4],
                                          const float bone_color[4],
                                          const float hint_color[4],
                                          const float outline_color[4],
                                          Object *custom)
{
  /* The custom object is not an evaluated object, so its object->data field hasn't been replaced
   * by #data_eval. This is bad since it gives preference to an object's evaluated mesh over any
   * other data type, but supporting all evaluated geometry components would require a much larger
   * refactor of this area. */
  Mesh *mesh = BKE_object_get_evaluated_mesh_no_subsurf(custom);
  if (mesh == NULL) {
    return;
  }

  /* TODO(fclem): arg... less than ideal but we never iter on this object
   * to assure batch cache is valid. */
  DRW_mesh_batch_cache_validate(custom, mesh);

  struct GPUBatch *surf = DRW_mesh_batch_cache_get_surface(mesh);
  struct GPUBatch *edges = DRW_mesh_batch_cache_get_edge_detection(mesh, NULL);
  struct GPUBatch *ledges = DRW_mesh_batch_cache_get_loose_edges(mesh);
  BoneInstanceData inst_data;
  DRWCallBuffer *buf;

  if (surf || edges || ledges) {
    mul_m4_m4m4(inst_data.mat, ctx->ob->obmat, bone_mat);
  }

  if (surf && ctx->custom_solid) {
    buf = custom_bone_instance_shgroup(ctx, ctx->custom_solid, surf);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_color);
    OVERLAY_bone_instance_data_set_color(&inst_data, bone_color);
    DRW_buffer_add_entry_struct(buf, inst_data.mat);
  }

  if (edges && ctx->custom_outline) {
    buf = custom_bone_instance_shgroup(ctx, ctx->custom_outline, edges);
    OVERLAY_bone_instance_data_set_color(&inst_data, outline_color);
    DRW_buffer_add_entry_struct(buf, inst_data.mat);
  }

  if (ledges) {
    buf = custom_bone_instance_shgroup(ctx, ctx->custom_wire, ledges);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, outline_color);
    OVERLAY_bone_instance_data_set_color(&inst_data, outline_color);
    DRW_buffer_add_entry_struct(buf, inst_data.mat);
  }

  /* TODO(fclem): needs to be moved elsewhere. */
  drw_batch_cache_generate_requested_delayed(custom);
}

static void drw_shgroup_bone_custom_wire(ArmatureDrawContext *ctx,
                                         const float (*bone_mat)[4],
                                         const float color[4],
                                         Object *custom)
{
  /* See comments in #drw_shgroup_bone_custom_solid. */
  Mesh *mesh = BKE_object_get_evaluated_mesh_no_subsurf(custom);
  if (mesh == NULL) {
    return;
  }
  /* TODO(fclem): arg... less than ideal but we never iter on this object
   * to assure batch cache is valid. */
  DRW_mesh_batch_cache_validate(custom, mesh);

  struct GPUBatch *geom = DRW_mesh_batch_cache_get_all_edges(mesh);
  if (geom) {
    DRWCallBuffer *buf = custom_bone_instance_shgroup(ctx, ctx->custom_wire, geom);
    BoneInstanceData inst_data;
    mul_m4_m4m4(inst_data.mat, ctx->ob->obmat, bone_mat);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, color);
    OVERLAY_bone_instance_data_set_color(&inst_data, color);
    DRW_buffer_add_entry_struct(buf, inst_data.mat);
  }

  /* TODO(fclem): needs to be moved elsewhere. */
  drw_batch_cache_generate_requested_delayed(custom);
}

static void drw_shgroup_bone_custom_empty(ArmatureDrawContext *ctx,
                                          const float (*bone_mat)[4],
                                          const float color[4],
                                          Object *custom)
{
  const float final_color[4] = {color[0], color[1], color[2], 1.0f};
  float mat[4][4];
  mul_m4_m4m4(mat, ctx->ob->obmat, bone_mat);

  switch (custom->empty_drawtype) {
    case OB_PLAINAXES:
    case OB_SINGLE_ARROW:
    case OB_CUBE:
    case OB_CIRCLE:
    case OB_EMPTY_SPHERE:
    case OB_EMPTY_CONE:
    case OB_ARROWS:
      OVERLAY_empty_shape(
          ctx->extras, mat, custom->empty_drawsize, custom->empty_drawtype, final_color);
      break;
    case OB_EMPTY_IMAGE:
      break;
  }
}

/* Head and tail sphere */
static void drw_shgroup_bone_point(ArmatureDrawContext *ctx,
                                   const float (*bone_mat)[4],
                                   const float bone_color[4],
                                   const float hint_color[4],
                                   const float outline_color[4])
{
  BoneInstanceData inst_data;
  mul_m4_m4m4(inst_data.mat, ctx->ob->obmat, bone_mat);
  if (ctx->point_solid) {
    OVERLAY_bone_instance_data_set_color(&inst_data, bone_color);
    OVERLAY_bone_instance_data_set_color_hint(&inst_data, hint_color);
    DRW_buffer_add_entry_struct(ctx->point_solid, &inst_data);
  }
  if (outline_color[3] > 0.0f) {
    OVERLAY_bone_instance_data_set_color(&inst_data, outline_color);
    DRW_buffer_add_entry_struct(ctx->point_outline, &inst_data);
  }
}

/* Axes */
static void drw_shgroup_bone_axes(ArmatureDrawContext *ctx,
                                  const float (*bone_mat)[4],
                                  const float color[4])
{
  float mat[4][4];
  mul_m4_m4m4(mat, ctx->ob->obmat, bone_mat);
  /* Move to bone tail. */
  add_v3_v3(mat[3], mat[1]);
  OVERLAY_empty_shape(ctx->extras, mat, 0.25f, OB_ARROWS, color);
}

/* Relationship lines */
static void drw_shgroup_bone_relationship_lines_ex(ArmatureDrawContext *ctx,
                                                   const float start[3],
                                                   const float end[3],
                                                   const float color[4])
{
  float s[3], e[3];
  mul_v3_m4v3(s, ctx->ob->obmat, start);
  mul_v3_m4v3(e, ctx->ob->obmat, end);
  /* reverse order to have less stipple overlap */
  OVERLAY_extra_line_dashed(ctx->extras, s, e, color);
}

static void drw_shgroup_bone_relationship_lines(ArmatureDrawContext *ctx,
                                                const float start[3],
                                                const float end[3])
{
  drw_shgroup_bone_relationship_lines_ex(ctx, start, end, G_draw.block.colorWire);
}

static void drw_shgroup_bone_ik_lines(ArmatureDrawContext *ctx,
                                      const float start[3],
                                      const float end[3])
{
  drw_shgroup_bone_relationship_lines_ex(ctx, start, end, G_draw.block.colorBoneIKLine);
}

static void drw_shgroup_bone_ik_no_target_lines(ArmatureDrawContext *ctx,
                                                const float start[3],
                                                const float end[3])
{
  drw_shgroup_bone_relationship_lines_ex(ctx, start, end, G_draw.block.colorBoneIKLineNoTarget);
}

static void drw_shgroup_bone_ik_spline_lines(ArmatureDrawContext *ctx,
                                             const float start[3],
                                             const float end[3])
{
  drw_shgroup_bone_relationship_lines_ex(ctx, start, end, G_draw.block.colorBoneIKLineSpline);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Drawing Theme Helpers
 *
 * \note this section is duplicate of code in 'drawarmature.c'.
 *
 * \{ */

/* values of colCode for set_pchan_color */
enum {
  PCHAN_COLOR_NORMAL = 0, /* normal drawing */
  PCHAN_COLOR_SOLID,      /* specific case where "solid" color is needed */
  PCHAN_COLOR_CONSTS,     /* "constraint" colors (which may/may-not be suppressed) */
};

/* This function sets the color-set for coloring a certain bone */
static void set_pchan_colorset(ArmatureDrawContext *ctx, Object *ob, bPoseChannel *pchan)
{
  bPose *pose = (ob) ? ob->pose : NULL;
  bArmature *arm = (ob) ? ob->data : NULL;
  bActionGroup *grp = NULL;
  short color_index = 0;

  /* sanity check */
  if (ELEM(NULL, ob, arm, pose, pchan)) {
    ctx->bcolor = NULL;
    return;
  }

  /* only try to set custom color if enabled for armature */
  if (arm->flag & ARM_COL_CUSTOM) {
    /* currently, a bone can only use a custom color set if its group (if it has one),
     * has been set to use one
     */
    if (pchan->agrp_index) {
      grp = (bActionGroup *)BLI_findlink(&pose->agroups, (pchan->agrp_index - 1));
      if (grp) {
        color_index = grp->customCol;
      }
    }
  }

  /* bcolor is a pointer to the color set to use. If NULL, then the default
   * color set (based on the theme colors for 3d-view) is used.
   */
  if (color_index > 0) {
    bTheme *btheme = UI_GetTheme();
    ctx->bcolor = &btheme->tarm[(color_index - 1)];
  }
  else if (color_index == -1) {
    /* use the group's own custom color set (grp is always != NULL here) */
    ctx->bcolor = &grp->cs;
  }
  else {
    ctx->bcolor = NULL;
  }
}

/* This function is for brightening/darkening a given color (like UI_GetThemeColorShade3ubv()) */
static void cp_shade_color3ub(uchar cp[3], const int offset)
{
  int r, g, b;

  r = offset + (int)cp[0];
  CLAMP(r, 0, 255);
  g = offset + (int)cp[1];
  CLAMP(g, 0, 255);
  b = offset + (int)cp[2];
  CLAMP(b, 0, 255);

  cp[0] = r;
  cp[1] = g;
  cp[2] = b;
}

/* This function sets the gl-color for coloring a certain bone (based on bcolor) */
static bool set_pchan_color(const ArmatureDrawContext *ctx,
                            short colCode,
                            const int boneflag,
                            const short constflag,
                            float r_color[4])
{
  float *fcolor = r_color;
  const ThemeWireColor *bcolor = ctx->bcolor;

  switch (colCode) {
    case PCHAN_COLOR_NORMAL: {
      if (bcolor) {
        uchar cp[4] = {255};
        if (boneflag & BONE_DRAW_ACTIVE) {
          copy_v3_v3_uchar(cp, bcolor->active);
          if (!(boneflag & BONE_SELECTED)) {
            cp_shade_color3ub(cp, -80);
          }
        }
        else if (boneflag & BONE_SELECTED) {
          copy_v3_v3_uchar(cp, bcolor->select);
        }
        else {
          /* a bit darker than solid */
          copy_v3_v3_uchar(cp, bcolor->solid);
          cp_shade_color3ub(cp, -50);
        }
        rgb_uchar_to_float(fcolor, cp);
        /* Meh, hardcoded srgb transform here. */
        srgb_to_linearrgb_v4(fcolor, fcolor);
      }
      else {
        if ((boneflag & BONE_DRAW_ACTIVE) && (boneflag & BONE_SELECTED)) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseActive);
        }
        else if (boneflag & BONE_DRAW_ACTIVE) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseActiveUnsel);
        }
        else if (boneflag & BONE_SELECTED) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePose);
        }
        else {
          copy_v4_v4(fcolor, G_draw.block.colorWire);
        }
      }
      return true;
    }
    case PCHAN_COLOR_SOLID: {
      if (bcolor) {
        rgb_uchar_to_float(fcolor, (uchar *)bcolor->solid);
        fcolor[3] = 1.0f;
        /* Meh, hardcoded srgb transform here. */
        srgb_to_linearrgb_v4(fcolor, fcolor);
      }
      else {
        copy_v4_v4(fcolor, G_draw.block.colorBoneSolid);
      }
      return true;
    }
    case PCHAN_COLOR_CONSTS: {
      if ((bcolor == NULL) || (bcolor->flag & TH_WIRECOLOR_CONSTCOLS)) {
        if (constflag & PCHAN_HAS_TARGET) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseTarget);
        }
        else if (constflag & PCHAN_HAS_IK) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseIK);
        }
        else if (constflag & PCHAN_HAS_SPLINEIK) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseSplineIK);
        }
        else if (constflag & PCHAN_HAS_CONST) {
          copy_v4_v4(fcolor, G_draw.block.colorBonePoseConstraint);
        }
        else {
          return false;
        }
        return true;
      }
      return false;
    }
  }

  return false;
}
