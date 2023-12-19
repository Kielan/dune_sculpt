/* dust/source/dust/winmngr/intern/win.c */
/* the global to talk to ghost */
static GHOST_SystemHandle g_system = NULL;

/* Direct OpenGL Cxt Management */
void *win_opengl_cxt_create(void)
{
  /* On Wins there is a problem creating cxts that share lists
   * from one cxt that is current in another thread.
   * So we should call this fn only on the main thread. */
  lib_assert(lib_thread_is_main());
  lib_assert(gpu_framebuf_active_get() == gpu_framebuf_back_get());

  GHOST_GLSettings glSettings = {0};
  if (G.debug & G_DEBUG_GPU) {
    glSettings.flags |= GHOST_glDebugContext;
  }
  return GHOST_CreateOpenGLCxt(g_system, glSettings);
}

void win_opengl_cxt_activate(void *cxt)
{
  lib_assert(gpu_framebuf_active_get() == gpu_framebuf_back_get());
  GHOST_ActivateOpenGLCxt((GHOST_CxtHandle)cxt);
}

/* Ghost Init/Exit */
/* Cxt can be null in background mode bc we don't
 * need to ev handling. */
void win_ghost_init(Cxt *C)
{
  if (!g_system) {
    GHOST_EvConsumerHandle consumer;

    if (C != NULL) {
      consumer = GHOST_CreateEvConsumer(ghost_ev_proc, C);
    }

    g_system = GHOST_CreateSystem();
    GHOST_SystemInitDebug(g_system, G.debug & G_DEBUG_GHOST);

    if (C != NULL) {
      GHOST_AddEvConsumer(g_system, consumer);
    }

    if (win_init_state.native_pixels) {
      GHOST_UseNativePixels();
    }

    GHOST_UseWindowFocus(win_init_state.win_focus);
  }
}

void win_ghost_exit(void)
{
  if (g_system) {
    GHOST_DisposeSystem(g_system);
  }
  g_system = NULL;
}
