#pragma once

struct DuneHead;
struct DuneThumbnail;
struct Collection;
struct FileData;
struct LinkNode;
struct ListBase;
struct Main;
struct MemFile;
struct Object;
struct ReportList;
struct Scene;
struct UserDef;
struct View3D;
struct ViewLayer;
struct WorkSpace;
struct bScreen;
struct wmWindowManager;

typedef struct DuneHandle DuneHandle;

typedef struct WorkspaceConfigFileData {
  struct Main *main; /* has to be freed when done reading file data */

  struct ListBase workspaces;
} WorkspaceConfigFileData;

/* -------------------------------------------------------------------- */
/** LOADER Read File API
 *
 * #BLO_write_file for file writing.
 */

typedef enum eLoaderFileType {
  LOADERFILETYPE_DUNE = 1,
  /* DUNEFILETYPE_PUB = 2, */     /* UNUSED */
  /* DUNEFILETYPE_RUNTIME = 3, */ /* UNUSED */
} eDuneFileType;

#pragma once

typedef struct LoaderFileData {
  struct Main *main;
  struct UserDef *user;

  int fileflags;
  int globalf;
  char filepath[1024]; /* 1024 = FILE_MAX */

  struct bScreen *curscreen; /* TODO: think this isn't needed anymore? */
  struct Scene *curscene;
  struct ViewLayer *cur_view_layer; /* layer to activate in workspaces when reading without UI */

  eDuneFileType type;
} DuneFileData;

struct DuneFileReadParams {
  uint skip_flags : 3; /* #eLOADERReadSkip */
  uint is_startup : 1;

  /** Whether we are reading the memfile for an undo or a redo. */
  int undo_direction; /* #eUndoStepDir */
};

typedef struct DuneFileReadReport {
  /* General reports handling. */
  struct ReportList *reports;

  /* Timing information. */
  struct {
    double whole;
    double libraries;
    double lib_overrides;
    double lib_overrides_resync;
    double lib_overrides_recursive_resync;
  } duration;
