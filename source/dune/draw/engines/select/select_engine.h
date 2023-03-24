#pragma once

/* select_engine.c */
extern DrawEngineType draw_engine_select_type;
extern RenderEngineType draw_engine_viewport_select_type;

#ifdef WITH_DRAW_DEBUG
/* select_debug_engine.c */
extern DrawEngineType draw_engine_debug_select_type;
#endif

struct SelectIdCtx *draw_select_engine_context_get(void);

struct GPUFrameBuffer *draw_engine_select_framebuffer_get(void);
struct GPUTexture *draw_engine_select_texture_get(void);
