#pragma once

/* temporary, until AssetHandle is designed properly and queries can return a pointer to it. */
#include "types_asset.h"

#include "types_list.h"
#include "types_object_enums.h"
#include "api_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct Base;
struct CacheFile;
struct Collection;
struct Graph;
struct EditBone;
struct Id;
struct Image;
struct LayerCollection;
struct List;
struct Main;
struct Object;
struct ApiPtr;
struct RegionView3D;
struct RenderEngineType;
struct ReportList;
struct Scene;
struct ScrArea;
struct SpaceClip;
struct SpaceImage;
struct SpaceLink;
struct SpaceText;
struct ApiStruct;
struct Text;
struct ToolSettings;
struct View3D;
struct ViewLayer;
struct PenDataframe;
struct PenDatalayer;
struct PenData;
struct PoseChannel;
struct Screen;
struct Window;
struct WindowManager;

/* Structs */
struct Cxt;
typedef struct Cxt Cxt;

struct CxtDataResult;
typedef struct CxtDataResult CxtDataResult;

/* Result of cxt lookups.
 * The specific values are important, and used implicitly in cxt_data_get(). Some fns also
 * still accept/return `int` instead, to ensure that the compiler uses the correct storage size
 * when mixing C/C++ code. */
typedef enum eCxtResult {
  /* The cxt member was found, and its data is available. */
  CXT_RESULT_OK = 1,

  /* The context member was not found. */
  CXT_RESULT_MEMBER_NOT_FOUND = 0,

  /* The context member was found, but its data is not available.
   * For example, "active_bone" is a valid context member, but has not data in Object mode. */
  CXT_RESULT_NO_DATA = -1,
} eCxtResult;

/* Function mapping a context member name to its value. */
typedef int /*eCxtResult*/ (*CxtDataCb)(const Cxt *C,
                                        const char *member,
                                        CxtDataResult *result);

typedef struct CxtStoreEntry {
  struct CxtStoreEntry *next, *prev;

  char name[128];
  ApiPtr ptr;
} CxtStoreEntry;

typedef struct CxtStore {
  struct CxtStore *next, *prev;

  List entries;
  bool used;
} CxtStore;

/* for the context's rna mode enum
 * keep aligned with data_mode_strings in cxt.c */
typedef enum eCxtObjectMode {
  CXT_MODE_EDIT_MESH = 0,
  CXT_MODE_EDIT_CURVE,
  CXT_MODE_EDIT_SURFACE,
  CXT_MODE_EDIT_TEXT,
  CXT_MODE_EDIT_ARMATURE,
  CXT_MODE_EDIT_METABALL,
  CXT_MODE_EDIT_LATTICE,
  CXT_MODE_EDIT_CURVES,
  CXT_MODE_POSE,
  CXT_MODE_SCULPT,
  CXT_MODE_PAINT_WEIGHT,
  CXT_MODE_PAINT_VERTEX,
  CXT_MODE_PAINT_TEXTURE,
  CXT_MODE_PARTICLE,
  CXT_MODE_OBJECT,
  CXT_MODE_PAINT_PEN,
  CXT_MODE_EDIT_PEN,
  CXT_MODE_SCULPT_PEN,
  CXT_MODE_WEIGHT_PEN,
  CXT_MODE_VERTEX_PEN,
  CXT_MODE_SCULPT_CURVES,
} eCxtObjectMode;
#define CXT_MODE_NUM (CXT_MODE_SCULPT_CURVES + 1)

/* Context */
Cxt *cxt_create(void);
void cxt_free(Cxt *C);

Cxt *cxt_copy(const Cxt *C);

/* Stored Context */
CxtStore *cxt_store_add(List *cxts, const char *name, const PointerRNA *ptr);
CxtStore *cxt_store_add_all(List *cxts, CxtStore *context);
CxtStore *cxt_store_get(Cxt *C);
void cxt_store_set(Cxt *C, CxtStore *store);
CxtStore *cxt_store_copy(CxtStore *store);
void cxt_store_free(CxtStore *store);
void cxt_store_free_list(List *cxts);

/* Window Manager Cxt */
struct WindowManager *cxt_wm_manager(const Cxt *C);
struct Window *cxt_wm_window(const Cxt *C);
struct WorkSpace *cxt_wm_workspace(const Cxt *C);
struct Screen *cxt_wm_screen(const Cxt *C);
struct ScrArea *cxt_wm_area(const Cxt *C);
struct SpaceLink *cxt_wm_space_data(const Cxt *C);
struct ARegion *cxt_wm_region(const Cxt *C);
void *cxt_wm_region_data(const Cxt *C);
struct ARegion *cxt_wm_menu(const Cxt *C);
struct wmGizmoGroup *cxt_wm_gizmo_group(const Cxt *C);
struct wmMsgBus *cxt_wm_message_bus(const Cxt *C);
struct ReportList *cxt_wm_reports(const Cxt *C);

struct View3D *cxt_wm_view3d(const Cxt *C);
struct RegionView3D *cxt_wm_region_view3d(const Cxt *C);
struct SpaceText *cxt_wm_space_text(const Cxt *C);
struct SpaceImage *cxt_wm_space_image(const Cxt *C);
struct SpaceConsole *cxt_wm_space_console(const Cxt *C);
struct SpaceProperties *cxt_wm_space_properties(const Cxt *C);
struct SpaceFile *cxt_wm_space_file(const Cxt *C);
struct SpaceSeq *cxt_wm_space_seq(const Cxt *C);
struct SpaceOutliner *cxt_wm_space_outliner(const Cxt *C);
struct SpaceNla *cxt_wm_space_nla(const Cxt *C);
struct SpaceNode *cxt_wm_space_node(const Cxt *C);
struct SpaceGraph *cxt_wm_space_graph(const Cxt *C);
struct SpaceAction *cxt_wm_space_action(const Cxt *C);
struct SpaceInfo *cxt_wm_space_info(const Cxt *C);
struct SpaceUserPref *cxt_wm_space_userpref(const Cxt *C);
struct SpaceClip *cxt_wm_space_clip(const Cxt *C);
struct SpaceTopBar *cxt_wm_space_topbar(const Cxt *C);
struct SpaceSpreadsheet *cxt_wm_space_spreadsheet(const Cxt *C);

void cxt_wm_manager_set(Cxt *C, struct wmWindowManager *wm);
void cxt_wm_window_set(Cxt *C, struct Window *win);
void cxt_wm_screen_set(Cxt *C, struct Screen *screen); /* to be removed */
void cxt_wm_area_set(Cxt *C, struct ScrArea *area);
void cxt_wm_region_set(Cxt *C, struct ARegion *region);
void cxt_wm_menu_set(Cxt *C, struct ARegion *menu);
void cxt_wm_gizmo_group_set(Cxt *C, struct wmGizmoGroup *gzgroup);

/* Values to create the message that describes the reason poll failed.
 * This must be called in the same cxt as the poll fn that created it */
struct CxtPollMsgDyn_Params {
  /** The result is allocated . */
  char *(*get_fn)(Cxt *C, void *user_data);
  /** Optionally free the user-data. */
  void (*free_fn)(Cxt *C, void *user_data);
  void *user_data;
};

const char *cxt_wm_op_poll_msg_get(struct Cxt *C, bool *r_free);
void cxt_wm_op_poll_msg_set(struct Cxt *C, const char *msg);
void cxt_wm_op_poll_msg_set_dynamic(Cxt *C,
                                    const struct CxtPollMsgDyn_Params *params);
void cxt_wm_op_poll_msg_clear(struct Cxt *C);

/* Data Contex
 * - list consist of CollectionPtrLink items and must be
 *   freed with lib_freelistn!
 * - the dir list consists of LinkData items */

/* data type, needed so we can tell between a NULL ptr and an empty list */
enum {
  CTX_DATA_TYPE_PTR = 0,
  CTX_DATA_TYPE_COLLECTION,
};

ApiPtr cxt_data_ptr_get(const Cxt *C, const char *member);
ApiPtr cxt_data_ptr_get_type(const Cxt *C, const char *member, ApiStruct *type);
ApiPtr cxt_data_ptr_get_type_silent(const Cxt *C,
                                    const char *member,
                                    ApiStruct *type);
List cxt_data_collection_get(const Cxt *C, const char *member);
/* param C: Cxt.
 * param use_store: Use 'C->wm.store'.
 * param use_api: Use Include the props from 'ApiCxt'.
 * param use_all: Don't skip values (currently only "scene"). */
List cxt_data_dir_get_ex(const Cxt *C, bool use_store, bool use_api, bool use_all);
List cxt_data_dir_get(const Cxt *C);
int /*eCxtResult*/ cxt_data_get(
    const Cxt *C, const char *member, ApiPtr *r_ptr, List *r_list, short *r_type);

void cxt_data_id_ptr_set(CxtDataResult *result, struct Id *id);
void cxt_data_ptr_set_ptr(CxtDataResult *result, const ApiPtr *ptr);
void cxt_data_ptr_set(CxtDataResult *result, struct Id *id, ApiStruct *type, void *data);

void cxt_data_id_list_add(CxtDataResult *result, struct Id *id);
void cxt_data_list_add_ptr(CxtDataResult *result, const ApiPtr *ptr);
void cxt_data_list_add(CxtDataResult *result, struct Id *id, ApiStruct *type, void *data);

void cxt_data_dir_set(CxtDataResult *result, const char **dir);

void cxt_data_type_set(struct CxtDataResult *result, short type);
short cxt_data_type_get(struct CxtDataResult *result);

bool cxt_data_equals(const char *member, const char *str);
bool cxt_data_dir(const char *member);

#define CXT_DATA_BEGIN(C, Type, instance, member) \
  { \
    List cxt_data_list; \
    CollectionPtrLink *ctx_link; \
    cxt_data_##member(C, &cxt_data_list); \
    for (cxt_link = (CollectionPtrLink *)cxt_data_list.first; cxt_link; \
         cxt_link = ctx_link->next) { \
      Type instance = (Type)cxt_link->ptr.data;

#define CXT_DATA_END \
  } \
  lib_freelistn(&cxt_data_list); \
  } \
  (void)0

#define CXT_DATA_BEGIN_WITH_ID(C, Type, instance, member, Type_id, instance_id) \
  CXT_DATA_BEGIN (C, Type, instance, member) \
    Type_id instance_id = (Type_id)cxt_link->ptr.owner_id;

int cxt_data_list_count(const Cxt *C, int (*fn)(const Cxt *, List *));

#define CXT_DATA_COUNT(C, member) ctx_data_list_count(C, cxt_data_##member)

/* Data Cxt Members */
struct Main *cxt_data_main(const Cxt *C);
struct Scene *cxt_data_scene(const Cxt *C);
/* This is tricky. Sometimes the user overrides the render_layer
 * but not the scene_collection. In this case what to do?
 * If the scene_collection is linked to the ViewLayer we use it.
 * Otherwise we fallback to the active one of the ViewLayer. */
struct LayerCollection *cxt_data_layer_collection(const Cxt *C);
struct Collection *cxt_data_collection(const Cxt *C);
struct ViewLayer *cxt_data_view_layer(const Cxt *C);
struct RenderEngineType *cxt_data_engine_type(const Cxt *C);
struct ToolSettings *cxt_data_tool_settings(const Cxt *C);

const char *cxt_data_mode_string(const Cxt *C);
enum eCxtObjectMode cxt_data_mode_enum_ex(const struct Object *obedit,
                                          const struct Object *ob,
                                          eObjectMode object_mode);
enum eCxtObjectMode cxt_data_mode_enum(const Cxt *C);

void cxt_data_main_set(Cxt *C, struct Main *main);
void cxt_data_scene_set(Cxt *C, struct Scene *scene);

/* Only Outliner currently! */
int cxt_data_selected_ids(const Cxt *C, List *list);

int cxt_data_selected_editable_objects(const Cxt *C, List *list);
int cxt_data_selected_editable_bases(const Cxt *C, List *list);

int cxt_data_editable_objects(const Cxt *C, List *list);
int cxt_data_editable_bases(const Cxt *C, List *list);

int cxt_data_selected_objects(const Cxt *C, List *list);
int cxt_data_selected_bases(const Cxt *C, List *list);

int cxt_data_visible_objects(const Cxt *C, List *list);
int cxt_data_visible_bases(const Cxt *C, List *list);

int cxt_data_selectable_objects(const Cxt *C, List *list);
int cxt_data_selectable_bases(const Cxt *C, List *list);

struct Object *cxt_data_active_object(const Cxt *C);
struct Base *cxt_data_active_base(const Cxt *C);
struct Object *cxt_data_edit_object(const Cxt *C);

struct Image *cxt_data_edit_image(const Cxt *C);

struct Text *cxt_data_edit_text(const Cxt *C);
struct MovieClip *cxt_data_edit_movieclip(const Cxt *C);
struct Mask *cxt_data_edit_mask(const Cxt *C);

struct CacheFile *cxt_data_edit_cachefile(const Cxt *C);

int cxt_data_selected_nodes(const Cxt *C, List *list);

struct EditBone *cxt_data_active_bone(const Cxt *C);
int cxt_data_selected_bones(const Cxt *C, List *list);
int cxt_data_selected_editable_bones(const Cxt *C, List *list);
int cxt_data_visible_bones(const Cxt *C, List *list);
int cxt_data_editable_bones(const Cxt *C, List *list);

struct PoseChannel *cxt_data_active_pose_bone(const Cxt *C);
int cxt_data_selected_pose_bones(const Cxt *C, List *list);
int cxt_data_selected_pose_bones_from_active_object(const Cxt *C, List *list);
int cxt_data_visible_pose_bones(const Cxt *C, List *list);

struct PenData *cxt_data_gpencil_data(const Cxt *C);
struct PenDatalayer *cxt_data_active_pen_layer(const Cxt *C);
struct PenDataframe *cxt_data_active_pen_frame(const Cxt *C);
int cxt_data_visible_pen_layers(const Cxt *C, List *list);
int cxt_data_editable_pen_layers(const Cxt *C, List *list);
int cxt_data_editable_pen_strokes(const Cxt *C, List *list);

const struct AssetLibRef *cxt_wm_asset_lib_ref(const Cxt *C);
struct AssetHandle cxt_wm_asset_handle(const Cxt *C, bool *r_is_valid);

bool cxt_wm_interface_locked(const Cxt *C);

/* Gets ptr to the graph.
 * If it doesn't exist yet, it will be allocated.
 * The result graph is NOT guaranteed to be up-to-date neither from relation nor from
 * eval data points of view.
 * Can not be used if access to a fully evaluated data-block is needed. */
struct Graph *cxt_data_graph_ptr(const Cxt *C);

/* Get graph which is expected to be fully eval.
 * In the release builds it is the same as cxt_data_graph_ptr(). In the debug builds extra
 * sanity checks are done. Additionally, this provides more semantic meaning to what is exactly
 * expected to happen. */
struct Graph *cxt_data_expect_eval_graph(const Cxt *C);

/* Gets fully updated and eval graph.
 * All the relations and evaluated objects are guaranteed to be up to date.
 * Will be expensive if there are relations or objects tagged for update.
 * If there are pending updates graph hooks will be invoked. */
struct Graph *cxt_data_ensure_eval_graph(const Cxt *C);

/* Will Return NULL if graph is not allocated yet.
 * Only used by handful of ops which are run on file load. */
struct Graph *cxt_data_graph_on_load(const Cxt *C);

#ifdef __cplusplus
}
#endif
