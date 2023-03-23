
#include "draw_render.h"

#include "types_meta.h"

#include "dune_object.h"

#include "dsgraph_query.h"

#include "ed_mball.h"

#include "overlay_private.h"

void overlay_metaball_cache_init(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;
  OverlayPrivateData *pd = vedata->stl->pd;

  OverlayInstanceFormats *formats = overlay_shader_instance_formats_get();

#define BUF_INSTANCE DRW_shgroup_call_buffer_instance

  for (int i = 0; i < 2; i++) {
    DrawState infront_state = (draw_state_is_select() && (i == 1)) ? DRAW_STATE_IN_FRONT_SELECT : 0;
    DrawState state = DRAW_STATE_WRITE_COLOR | DRAW_STATE_WRITE_DEPTH | DRAW_STATE_DEPTH_LESS_EQUAL;
    DRAW_PASS_CREATE(psl->metaball_ps[i], state | pd->clipping_state | infront_state);

    /* Reuse armature shader as it's perfect to outline ellipsoids. */
    struct GPUVertFormat *format = formats->instance_bone;
    struct GPUShader *sh = overlay_shader_armature_sphere(true);
    DrawShadingGroup *grp = draw_shgroup_create(sh, psl->metaball_ps[i]);
    draw_shgroup_uniform_block(grp, "globalsBlock", G_draw.block_ubo);
    pd->mball.handle[i] = BUF_INSTANCE(grp, format, draw_cache_bone_point_wire_outline_get());
  }
}

static void metaball_instance_data_set(
    BoneInstanceData *data, Object *ob, const float *pos, const float radius, const float color[4])
{
  /* Bone point radius is 0.05. Compensate for that. */
  mul_v3_v3fl(data->mat[0], ob->obmat[0], radius / 0.05f);
  mul_v3_v3fl(data->mat[1], ob->obmat[1], radius / 0.05f);
  mul_v3_v3fl(data->mat[2], ob->obmat[2], radius / 0.05f);
  mul_v3_m4v3(data->mat[3], ob->obmat, pos);
  /* WATCH: Reminder, alpha is wire-size. */
  overlay_bone_instance_data_set_color(data, color);
}

void overlay_edit_metaball_cache_populate(OverlayData *vedata, Object *ob)
{
  const bool do_in_front = (ob->dtx & OB_DRAW_IN_FRONT) != 0;
  const bool is_select = draw_state_is_select();
  OverlayPrivateData *pd = vedata->stl->pd;
  MetaBall *mb = ob->data;

  const float *color;
  const float *col_radius = G_draw.block.colorMballRadius;
  const float *col_radius_select = G_draw.block.colorMballRadiusSelect;
  const float *col_stiffness = G_draw.block.colorMballStiffness;
  const float *col_stiffness_select = G_draw.block.colorMballStiffnessSelect;

  int select_id = 0;
  if (is_select) {
    select_id = ob->runtime.select_id;
  }

  LISTBASE_FOREACH (MetaElem *, ml, mb->editelems) {
    const bool is_selected = (ml->flag & SELECT) != 0;
    const bool is_scale_radius = (ml->flag & MB_SCALE_RAD) != 0;
    float stiffness_radius = ml->rad * atanf(ml->s) / (float)M_PI_2;
    BoneInstanceData instdata;

    if (is_select) {
      draw_select_load_id(select_id | MBALLSEL_RADIUS);
    }
    color = (is_selected && is_scale_radius) ? col_radius_select : col_radius;
    metaball_instance_data_set(&instdata, ob, &ml->x, ml->rad, color);
    draw_buffer_add_entry_struct(pd->mball.handle[do_in_front], &instdata);

    if (is_select) {
      draw_select_load_id(select_id | MBALLSEL_STIFF);
    }
    color = (is_selected && !is_scale_radius) ? col_stiffness_select : col_stiffness;
    metaball_instance_data_set(&instdata, ob, &ml->x, stiffness_radius, color);
    draw_buffer_add_entry_struct(pd->mball.handle[do_in_front], &instdata);

    select_id += 0x10000;
  }

  /* Needed so object centers and geometry are not detected as meta-elements. */
  if (is_select) {
    draw_select_load_id(-1);
  }
}

void overlay_metaball_cache_populate(OverlayData *vedata, Object *ob)
{
  const bool do_in_front = (ob->dtx & OB_DRAW_IN_FRONT) != 0;
  OverlayPrivateData *pd = vedata->stl->pd;
  MetaBall *mb = ob->data;
  const DrawCtxState *draw_ctx = draw_ctx_state_get();

  float *color;
  draw_object_wire_theme_get(ob, draw_ctx->view_layer, &color);

  LISTBASE_FOREACH (MetaElem *, ml, &mb->elems) {
    /* Draw radius only. */
    BoneInstanceData instdata;
    metaball_instance_data_set(&instdata, ob, &ml->x, ml->rad, color);
    draw_buffer_add_entry_struct(pd->mball.handle[do_in_front], &instdata);
  }
}

void overlay_metaball_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  draw_draw_pass(psl->metaball_ps[0]);
}

void oerkay_metaball_in_front_draw(OverlayData *vedata)
{
  OverlayPassList *psl = vedata->psl;

  draw_draw_pass(psl->metaball_ps[1]);
}
