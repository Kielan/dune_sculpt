#include <stdlib.h>

#include "LIB_utildefines.h"
#include "IMB_imbuf.h"

struct ColorSpace;
struct ImBuf;

void IMB_freeImBuf(struct ImBuf *UNUSED(ibuf))
{
}
void IMB_colormanagement_display_to_scene_linear_v3(float UNUSED(pixel[3]),
                                                    struct ColorManagedDisplay *UNUSED(display))
{
}

bool IMB_colormanagement_space_is_scene_linear(struct ColorSpace *colorspace)
{
  return false;
}

bool IMB_colormanagement_space_is_data(struct ColorSpace *colorspace)
{
  return false;
}

void KERNEL_material_defaults_free_gpu(void)
{
}

/* Variables. */
int G;

/* Functions which aren't called. */
void *KERNEL_image_free_buffers = NULL;
void *KERNEL_image_get_tile = NULL;
void *KERNEL_image_get_tile_from_iuser = NULL;
void *KERNEL_tempdir_session = NULL;
void *DRW_deferred_shader_remove = NULL;
void *datatoc_common_view_lib_glsl = NULL;
void *ntreeFreeLocalTree = NULL;
void *ntreeGPUMaterialNodes = NULL;
void *ntreeLocalize = NULL;
