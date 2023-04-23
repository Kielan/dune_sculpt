#pragma once

#include "lib_bitmap.h"
#include "types_listBase.h"
#include "api_types.h"

struct ARegionType;
struct Id;
struct SpaceProps;
struct Tex;
struct Ctx;
struct CtxDataResult;
struct Node;
struct NodeSocket;
struct NodeTree;
struct wmOpType;

struct SpacePropsRuntime {
  /** For filtering properties displayed in the space. */
  char search_string[UI_MAX_NAME_STR];
  /**
   * Bitfield (in the same order as the tabs) for whether each tab has properties
   * that match the search filter. Only valid when search_string is set.
   */
  lib_bitmap *tab_search_results;
};

/* context data */

typedef struct BtnsCtxPath {
  ApiPtr ptr[8];
  int len;
  int flag;
  int collection_ctx;
} BtnsCtxPath;

typedef struct BtnsTextureUser {
  struct BtnsTextureUser *next, *prev;

  struct Id *id;

  ApiPtr ptr;
  PropertyRNA *prop;

  struct bNodeTree *ntree;
  struct bNode *node;
  struct bNodeSocket *socket;

  const char *category;
  int icon;
  const char *name;

  int index;
} ButsTextureUser;

typedef struct ButsContextTexture {
  ListBase users;

  struct Tex *texture;

  struct ButsTextureUser *user;
  int index;
} ButsContextTexture;

/* internal exports only */

/* btns_ctx.c */

void btns_ctx_compute(const struct Ctx *C, struct SpaceProps *sbtns);
int btns_ctxt(const struct Ctx *C,
              const char *member,
              struct CtxDataResult *result);
void buttons_context_register(struct ARegionType *art);
struct ID *btns_context_id_path(const struct Ctx *C);

extern const char *buttons_context_dir[]; /* doc access */

/* buttons_texture.c */

void buttons_texture_context_compute(const struct bContext *C, struct SpaceProperties *sbuts);

/* btns_ops.c */

void btns_ot_start_filter(struct wmOpType *ot);
void btns_ot_clear_filter(struct wmOpType *ot);
void btns_ot_toggle_pin(struct wmOpType *ot);
void btns_it_file_browse(struct wmOpType *ot);
/**
 * Second operator, only difference from btns_ot_file_browse is MESH_FILESEL_DIRECTORY.
 */
void btns_ot_dir_browse(struct wmOpType *ot);
void btns_ot_ctx_menu(struct wmOpType *ot);
