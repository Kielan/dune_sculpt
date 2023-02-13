#pragma once

struct dContext;

#ifdef __cplusplus
extern "C" {
#endif

bool CURVES_SCULPT_mode_poll(struct dContext *C);
bool CURVES_SCULPT_mode_poll_view3d(struct dContext *C);

#ifdef __cplusplus
}
#endif
