#pragma once

struct GPUOffScreen;
struct GPUTexture;
struct GPUViewport;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WinDrwBuf {
  struct GPUOffScreen *offscreen;
  struct GPUViewport *viewport;
  bool stereo;
  int bound_view;
} WinDrwBuf;

struct ARgn;
struct ScrArea;
struct Cxt;
struct Win;

/* win_drw.c */
void win_drw_update(struct Cxt *C);
void win_drw_rgn_clear(struct Win *win, struct ARgn *rgn);
void win_drw_rgn_blend(struct ARgn *rgn, int view, bool blend);
void win_drw_rgn_test(struct Cxt *C, struct ScrArea *area, struct ARgn *rgn);

struct GPUTexture *win_drw_rgn_texture(struct ARgn *rgn, int view);

#ifdef __cplusplus
}
#endif
