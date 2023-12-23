#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Settings for off-screen rendering. */
typedef enum eV3DOffscreenDrwFlag {
  V3D_OFSDRW_NONE = (0),
  V3D_OFSDRW_SHOW_ANNOTATION = (1 << 0),
  V3D_OFSDRW_OVERRIDE_SCENE_SETTINGS = (1 << 1),
  V3D_OFSDRW_SHOW_GRIDFLOOR = (1 << 2),
  V3D_OFSDRW_SHOW_SEL = (1 << 3),
  V3D_OFSDRW_XR_SHOW_CTRLRS = (1 << 4),
  V3D_OFSDRW_XR_SHOW_CUSTOM_OVERLAYS = (1 << 5),
} eV3DOffscreenDrwFlag;

/* View3DShading.light */
typedef enum eV3DShadingLightingMode {
  V3D_LIGHTING_FLAT = 0,
  V3D_LIGHTING_STUDIO = 1,
  V3D_LIGHTING_MATCAP = 2,
} eV3DShadingLightingMode;

/* View3DShading.color_type, View3DShading.wire_color_type */
typedef enum eV3DShadingColorType {
  V3D_SHADING_MATERIAL_COLOR = 0,
  V3D_SHADING_RANDOM_COLOR = 1,
  V3D_SHADING_SINGLE_COLOR = 2,
  V3D_SHADING_TEXTURE_COLOR = 3,
  V3D_SHADING_OB_COLOR = 4,
  V3D_SHADING_VERT_COLOR = 5,
} eV3DShadingColorType;

/** #View3DShading.background_type */
typedef enum eV3DShadingBackgroundType {
  V3D_SHADING_BACKGROUND_THEME = 0,
  V3D_SHADING_BACKGROUND_WORLD = 1,
  V3D_SHADING_BACKGROUND_VIEWPORT = 2,
} eV3DShadingBackgroundType;

#ifdef __cplusplus
}
#endif
