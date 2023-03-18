
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct GPUMatrixState *gpu_matrix_state_create(void);
void gpu_matrix_state_discard(struct GPUMatrixState *state);

#ifdef __cplusplus
}
#endif
