/* dust/source/dust/windowmanager/intern/wm_window.c */

/* the global to talk to ghost */
static GHOST_SystemHandle g_system = NULL;

/* Direct OpenGL Context Management */

void *WM_opengl_context_create(void)
{
  /* On Windows there is a problem creating contexts that share lists
   * from one context that is current in another thread.
   * So we should call this function only on the main thread.
   */
  LIB_assert(LIB_thread_is_main());
  LIB_assert(GPU_framebuffer_active_get() == GPU_framebuffer_back_get());

  GHOST_GLSettings glSettings = {0};
  if (G.debug & G_DEBUG_GPU) {
    glSettings.flags |= GHOST_glDebugContext;
  }
  return GHOST_CreateOpenGLContext(g_system, glSettings);
}

void WM_opengl_context_activate(void *context)
{
  LIB_assert(GPU_framebuffer_active_get() == GPU_framebuffer_back_get());
  GHOST_ActivateOpenGLContext((GHOST_ContextHandle)context);
}

/* Ghost Init/Exit */

/**
 * #bContext can be null in background mode because we don't
 * need to event handling.
 */
void wm_ghost_init(bContext *C)
{
  if (!g_system) {
    GHOST_EventConsumerHandle consumer;

    if (C != NULL) {
      consumer = GHOST_CreateEventConsumer(ghost_event_proc, C);
    }

    g_system = GHOST_CreateSystem();
    GHOST_SystemInitDebug(g_system, G.debug & G_DEBUG_GHOST);

    if (C != NULL) {
      GHOST_AddEventConsumer(g_system, consumer);
    }

    if (wm_init_state.native_pixels) {
      GHOST_UseNativePixels();
    }

    GHOST_UseWindowFocus(wm_init_state.window_focus);
  }
}

void wm_ghost_exit(void)
{
  if (g_system) {
    GHOST_DisposeSystem(g_system);
  }
  g_system = NULL;
}
