#pragma once

#include "types_id.h"
#include "types_color.h" /* for color management */
#include "types_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct GPUTexture;
struct MovieCache;
struct PackedFile;
struct RenderResult;
struct Scene;
struct anim;

/* ImgUser is in Texture, in Nodes, Background Img, Img Win, .... */
/* should be used in conjunction with an Id * to Img. */
typedef struct ImgUser {
  /* To retrieve render result. */
  struct Scene *scene;

  /* Movies, seqs: current to display. */
  int framenr;
  /* Total amount of frames to use. */
  int frames;
  /* Offset w/in movie, start frame in global time. */
  int offset, sfra;
  /* Cyclic flag. */
  char cycl;

  /* Multiview current eye - for internal use of drawing routines. */
  char multiview_eye;
  short pass;

  int tile;

  /* List indices, for menu browsing or retrieve buffer. */
  short multi_index, view, layer;
  short flag;
} ImgUser;

typedef struct ImgAnim {
  struct ImgAnim *next, *prev;
  struct anim *anim;
} ImgAnim;

typedef struct ImgView {
  struct ImgView *next, *prev;
  /* MAX_NAME. */
  char name[64];
  /* 1024 = FILE_MAX. */
  char filepath[1024];
} ImgView;

typedef struct ImgPackedFile {
  struct ImgPackedFile *next, *prev;
  struct PackedFile *packedfile;
  /* 1024 = FILE_MAX. */
  char filepath[1024];
} ImgPackedFile;

typedef struct RenderSlot {
  struct RenderSlot *next, *prev;
  /* 64 = MAX_NAME. */
  char name[64];
  struct RenderResult *render;
} RenderSlot;

typedef struct ImgTileRuntimeTextureSlot {
  int tilearray_layer;
  int _pad;
  int tilearray_offset[2];
  int tilearray_size[2];
} ImgTileRuntimeTextureSlot;

typedef struct ImgTileRuntime {
  /* Data per `eImgTextureResolution`.
   * Should match `IMG_TEXTURE_RESOLUTION_LEN` */
  ImgTileRuntimeTextureSlot slots[2];
} ImgTileRuntime;

typedef struct ImgTile {
  struct ImgTile *next, *prev;

  struct ImgTileRuntime runtime;

  char _pad[4];
  int tile_number;
  char label[64];
} ImgTile;

/* iuser->flag */
#define IMG_ANIM_ALWAYS (1 << 0)
/* #define IMA_UNUSED_1         (1 << 1) */
/* #define IMA_UNUSED_2         (1 << 2) */
#define IMG_NEED_FRAME_RECALC (1 << 3)
#define IMG_SHOW_STEREO (1 << 4)
/* Do not limit the resolution by the limit texture size option in the user preferences.
 * Images in the image editor or used as a backdrop are always shown using the maximum
 * possible resolution. */
#define IMG_SHOW_MAX_RESOLUTION (1 << 5)

/* Used to get the correct gpu texture from an Image datablock. */
typedef enum eGPUTextureTarget {
  TEXTARGET_2D = 0,
  TEXTARGET_2D_ARRAY,
  TEXTARGET_TILE_MAPPING,
  TEXTARGET_COUNT,
} eGPUTextureTarget;

/* Resolution variations that can be cached for an image. */
typedef enum eImgTextureResolution {
  IMG_TEXTURE_RESOLUTION_FULL = 0,
  IMG_TEXTURE_RESOLUTION_LIMITED,

  /* Not an option, but holds the number of options defined for this struct. */
  IMG_TEXTURE_RESOLUTION_LEN
} eImgTextureResolution;

/* Defined in dune_img.h. */
struct PartialUpdateRegister;
struct PartialUpdateUser;

typedef struct ImgRuntime {
  /* Mutex used to guarantee thread-safe access to the cached ImBuf of the corresponding image ID. */
  void *cache_mutex;

  /* Register containing partial updates. */
  struct PartialUpdateRegister *partial_update_register;
  /* Partial update user for GPUTextures stored inside the Image. */
  struct PartialUpdateUser *partial_update_user;

} ImgRuntime;

typedef struct Img {
  Id id;

  /* File path, 1024 = FILE_MAX. */
  char filepath[1024];

  /* Not written in file. */
  struct MovieCache *cache;
  /* Not written in file 3 = TEXTARGET_COUNT, 2 = stereo eyes, 2 = IMA_TEXTURE_RESOLUTION_LEN. */
  struct GPUTexture *gputexture[3][2][2];

  /* sources from: */
  List anims;
  struct RenderResult *rr;

  List renderslots;
  short render_slot, last_render_slot;

  int flag;
  short src, type;
  int lastframe;

  /* GPU texture flag. */
  int gpuframenr;
  short gpuflag;
  short gpu_pass;
  short gpu_layer;
  short gpu_view;
  char _pad2[4];

  /* Deprecated. */
  struct PackedFile *packedfile TYPES_DEPRECATED;
  struct List packedfiles;
  struct PreviewImg *preview;

  int lastused;

  /* for generated imgs */
  int gen_x, gen_y;
  char gen_type, gen_flag;
  short gen_depth;
  float gen_color[4];

  /* display aspect - for UV editing imgs resized for faster openGL display */
  float aspx, aspy;

  /* color management */
  ColorManagedColorspaceSettings colorspace_settings;
  char alpha_mode;

  char _pad;

  /* Multiview */
  /* For viewer node stereoscopy. */
  char eye;
  char views_format;

  /* ImgTile list for UDIMs. */
  int active_tile_index;
  List tiles;

  /* ImgView. */
  List views;
  struct Stereo3dFormat *stereo3d_format;

  ImgRuntime runtime;
} Img;

/* IMG */
/* Img.flag */
enum {
  IMG_HIGH_BITDEPTH = (1 << 0),
  IMG_FLAG_UNUSED_1 = (1 << 1), /* cleared */
#ifdef TYPES_DEPRECATED_ALLOW
  IMG_DO_PREMUL = (1 << 2),
#endif
  IMG_FLAG_UNUSED_4 = (1 << 4), /* cleared */
  IMG_NOCOLLECT = (1 << 5),
  IMG_FLAG_UNUSED_6 = (1 << 6), /* cleared */
  IMG_OLD_PREMUL = (1 << 7),
  IMG_FLAG_UNUSED_8 = (1 << 8), /* cleared */
  IMG_USED_FOR_RENDER = (1 << 9),
  /* For img user, but these flags are mixed. */
  IMG_USER_FRAME_IN_RANGE = (1 << 10),
  IMG_VIEW_AS_RENDER = (1 << 11),
  IMG_FLAG_UNUSED_12 = (1 << 12), /* cleared */
  IMG_DEINTERLACE = (1 << 13),
  IMG_USE_VIEWS = (1 << 14),
  IMG_FLAG_UNUSED_15 = (1 << 15), /* cleared */
  IMG_FLAG_UNUSED_16 = (1 << 16), /* cleared */
};

/* Img.gpuflag */
enum {
  /* All mipmap levels in OpenGL texture set? */
  IMG_GPU_MIPMAP_COMPLETE = (1 << 0),
  /* Reuse the max resolution textures as they fit in the limited scale. */
  IMG_GPU_REUSE_MAX_RESOLUTION = (1 << 1),
  /* Has any limited scale textures been alloc.
   * Adds additional checks to reuse max resolution imgs when they fit inside limited scale. */
  IMG_GPU_HAS_LIMITED_SCALE_TEXTURES = (1 << 2),
};

/* Img.src, where the img comes from */
typedef enum eImgSrc {
  /* IMG_SRC_CHECK = 0, */ /* UNUSED */
  IMG_SRC_FILE = 1,
  IMG_SRC_SEQ = 2,
  IMA_SRC_MOVIE = 3,
  IMA_SRC_GENERATED = 4,
  IMA_SRC_VIEWER = 5,
  IMA_SRC_TILED = 6,
} eImgSrc;

/* Img.type, how to handle or generate the image */
typedef enum eImgType {
  IMG_TYPE_IMG = 0,
  IMG_TYPE_MULTILAYER = 1,
  /* generated */
  IMG_TYPE_UV_TEST = 2,
  /* viewers */
  IMG_TYPE_R_RESULT = 4,
  IMG_TYPE_COMPOSITE = 5,
} eImgType;

/* Img.gen_type */
enum {
  IMG_GENTYPE_BLANK = 0,
  IMG_GENTYPE_GRID = 1,
  IMG_GENTYPE_GRID_COLOR = 2,
};

/* render */
#define IMG_MAX_RENDER_TXT (1 << 9)

/* Img.gen_flag */
enum {
  IMG_GEN_FLOAT = 1,
};

/* Img.alpha_mode */
enum {
  IMG_ALPHA_STRAIGHT = 0,
  IMG_ALPHA_PREMUL = 1,
  IMG_ALPHA_CHANNEL_PACKED = 2,
  IMG_ALPHA_IGNORE = 3,
};

#ifdef __cplusplus
}
#endif
