/* GPU vertex format */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct GPUVertFormat;

void VertexFormat_pack(struct GPUVertFormat *format);
uint padding(uint offset, uint alignment);
uint vertex_buffer_size(const struct GPUVertFormat *format, uint vertex_len);

#ifdef __cplusplus
}
#endif
