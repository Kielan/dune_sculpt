/** Function declarations for filter.c **/

#pragma once

struct ImBuf;

void imbuf_filterx(struct ImBuf *ibuf);

void imbuf_premultiply_rect(unsigned int *rect, char planes, int w, int h);
void imbuf_premultiply_rect_float(float *rect_float, int channels, int w, int h);

void imbuf_unpremultiply_rect(unsigned int *rect, char planes, int w, int h);
void imbuf_unpremultiply_rect_float(float *rect_float, int channels, int w, int h);

/** Result in ibuf2, scaling should be done correctly. */
void imbuf_onehalf_no_alloc(struct ImBuf *ibuf2, struct ImBuf *ibuf1);
