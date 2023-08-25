#pragma once

#include "lib_bitmap.h"
#include "types_list.h"
#include "api_types.h"

struct ARegionType;
struct Id;
struct SpaceProps;
struct Tex;
struct Cxt;
struct CxtDataResult;
struct Node;
struct NodeSocket;
struct NodeTree;
struct wmOpType;

struct SpacePropsRuntime {
  /* For filtering props displayed in the space. */
  char search_string[UI_MAX_NAME_STR];
  /* Bitfield (in the same order as the tabs) for whether each tab has properties
   * that match the search filter. Only valid when search_string is set. */
  lib_bitmap *tab_search_results;
};

/* context data */
typedef struct BtnsCxtPath {
  ApiPtr ptr[8];
  int len;
  int flag;
  int collection_ctx;
} BtnsCxtPath;

typedef struct BtnsTextureUser {
  struct BtnsTextureUser *next, *prev;

  struct Id *id;

  ApiPtr ptr;
  ApiProp *prop;

  struct NodeTree *ntree;
  struct Node *node;
  struct NodeSocket *socket;

  const char *category;
  int icon;
  const char *name;

  int index;
} BtnsTextureUser;

typedef struct BtnsCxtTexture {
  List users;
  struct Tex *texture;
  struct BtnsTextureUser *user;
  int index;
} BtnsCxtTexture;

/* internal exports only */
/* btns_ctx.c */
void btns_cxt_compute(const struct Cxt *C, struct SpaceProps *sbtns);
int btns_cxt(const struct Cxt *C,
              const char *member,
              struct CxtDataResult *result);
void btns_cxt_register(struct ARegionType *art);
struct Id *btns_cxt_id_path(const struct Cxt *C);

extern const char *btns_cxt_dir[]; /* doc access */

/* buttons_texture.c */
void btns_texture_cxt_compute(const struct Ctx *C, struct SpaceProps *sbtns);

/* btns_ops.c */
void btns_ot_start_filter(struct wmOpType *ot);
void btns_ot_clear_filter(struct wmOpType *ot);
void btns_ot_toggle_pin(struct wmOpType *ot);
void btns_ot_file_browse(struct wmOpType *ot);
/* Second op, only difference from btns_ot_file_browse is MESH_FILESEL_DIRECTORY. */
void btns_ot_dir_browse(struct wmOpType *ot);
void btns_ot_cxt_menu(struct wmOpType *ot);
