#pragma once

struct ListBase;
struct Render;
struct RenderData;
struct RenderLayer;
struct RenderResult;

#ifdef __cplusplus
extern "C" {
#endif

struct RenderLayer *render_get_active_layer(struct Render *re, struct RenderResult *rr);
/**
 * Update some variables that can be animated, and otherwise wouldn't be due to
 * #RenderData getting copied once at the start of animation render.
 */
void render_update_anim_renderdata(struct Render *re,
                                   struct RenderData *rd,
                                   struct ListBase *render_layers);
void render_copy_renderdata(struct RenderData *to, struct RenderData *from);

#ifdef __cplusplus
}
#endif
