#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct DerivedMesh;
struct IMBuf;
struct ImBuf;
struct MeshLoopUV;
struct Mesh;

/**
 * Generate a margin around the textures uv islands by copying pixels from the adjacent polygon.
 *
 * param ibuf: the texture image.
 * param mask: pixels with a mask value of 1 are not written to.
 * param margin: the size of the margin in pixels.
 * param me: the mesh to use the polygons of.
 * param mloopuv: the uv data to use.
 */
void render_generate_texturemargin_adjacentfaces(
    struct ImBuf *ibuf, char *mask, const int margin, struct Mesh const *me, char const *uv_layer);

void render_generate_texturemargin_adjacentfaces_dm(struct ImBuf *ibuf,
                                                    char *mask,
                                                    const int margin,
                                                    struct DerivedMesh *dm);

#ifdef __cplusplus
}
#endif
