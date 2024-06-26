#pragma once

struct Main;
struct duneContext;
struct duneContextDataResult;

/* internal exports only */

typedef enum eScreenDir {
  /** This can mean unset, unknown or invalid. */
  SCREEN_DIR_NONE = -1,
  /** West/Left. */
  SCREEN_DIR_W = 0,
  /** North/Up. */
  SCREEN_DIR_N = 1,
  /** East/Right. */
  SCREEN_DIR_E = 2,
  /** South/Down. */
  SCREEN_DIR_S = 3,
} eScreenDir;

#define SCREEN_DIR_IS_VERTICAL(dir) (ELEM(dir, SCREEN_DIR_N, SCREEN_DIR_S))
#define SCREEN_DIR_IS_HORIZONTAL(dir) (ELEM(dir, SCREEN_DIR_W, SCREEN_DIR_E))

typedef enum eScreenAxis {
  /** Horizontal. */
  SCREEN_AXIS_H = 'h',
  /** Vertical. */
  SCREEN_AXIS_V = 'v',
} eScreenAxis;

#define AZONESPOTW UI_HEADER_OFFSET         /* width of corner AZone - max */
#define AZONESPOTH (0.6f * U.widget_unit)   /* height of corner AZone */
#define AZONEFADEIN (5.0f * U.widget_unit)  /* when AZone is totally visible */
#define AZONEFADEOUT (6.5f * U.widget_unit) /* when we start seeing the AZone */

/* Edges must be within these to allow joining. */
#define AREAJOINTOLERANCEX (AREAMINX * U.dpi_fac)
#define AREAJOINTOLERANCEY (HEADERY * U.dpi_fac)

/** Expanded interaction influence of area borders. **/
#define BORDERPADDING ((2.0f * U.dpi_fac) + U.pixelsize)

/* area.c */

/** swap spaces for fullscreen to keep all allocated data area vertices were set **/
void ED_area_data_copy(ScrArea *area_dst, ScrArea *area_src, bool do_free);
void ED_area_data_swap(ScrArea *area_dst, ScrArea *area_src);
/* for quick toggle, can skip fades */
void region_toggle_hidden(struct duneContext *C, ARegion *region, bool do_fade);

/* screen_draw.c */

/**
 * Visual indication of the two areas involved in a proposed join.
 *
 * param sa1: Area from which the resultant originates.
 * param sa2: Target area that will be replaced.
 */
void screen_draw_join_highlight(struct ScrArea *sa1, struct ScrArea *sa2);
void screen_draw_split_preview(struct ScrArea *area, eScreenAxis dir_axis, float fac);

/* screen_edit.c */

/** Empty screen, with 1 dummy area without space-data. Uses window size. */
duneScreen *screen_add(struct Main *duneMain, const char *name, const rcti *rect);
void screen_data_copy(duneScreen *to, duneScreen *from);

/** Prepare a newly created screen for initializing it as active screen. **/
void screen_new_activate_prepare(const wmWindow *win, duneScreen *screen_new);
void screen_change_update(struct duneContext *C, wmWindow *win, duneScreen *screen);
/**
 * return the screen to activate.
 * warning The returned screen may not always equal \a screen_new!
 */
void screen_change_prepare(duneScreen *screen_old,
                           duneScreen *screen_new,
                           struct Main *duneMain,
                           struct duneContext *C,
                           wmWindow *win);
ScrArea *area_split(const wmWindow *win,
                    duneScreen *screen,
                    ScrArea *area,
                    eScreenAxis dir_axis,
                    float fac,
                    bool merge);
/**
 * Join any two neighboring areas. Might involve complex changes.
 */
int screen_area_join(struct duneContext *C, duneScreen *screen, ScrArea *sa1, ScrArea *sa2);
/**
 * with `sa_a` as center, `sa_b` is located at: 0=W, 1=N, 2=E, 3=S
 * -1 = not valid check.
 * used with join operator.
 */
eScreenDir area_getorientation(ScrArea *sa_a, ScrArea *sa_b);
/**
 * Get alignment offset of adjacent areas. 'dir' value is like area_getorientation().
 */
void area_getoffsets(ScrArea *sa_a, ScrArea *sa_b, eScreenDir dir, int *r_offset1, int *r_offset2);
/**
 * Close a screen area, allowing most-aligned neighbor to take its place.
 */
bool screen_area_close(struct duneContext *C, duneScreen *screen, ScrArea *area);
void screen_area_spacelink_add(struct Scene *scene, ScrArea *area, eSpace_Type space_type);
struct AZone *ED_area_actionzone_find_xy(ScrArea *area, const int xy[2]);

/* screen_geometry.c */
int screen_geom_area_height(const ScrArea *area);
int screen_geom_area_width(const ScrArea *area);
ScrVert *screen_geom_vertex_add_ex(ScrAreaMap *area_map, short x, short y);
ScrVert *screen_geom_vertex_add(duneScreen *screen, short x, short y);
ScrEdge *screen_geom_edge_add_ex(ScrAreaMap *area_map, ScrVert *v1, ScrVert *v2);
ScrEdge *screen_geom_edge_add(duneScreen *screen, ScrVert *v1, ScrVert *v2);
bool screen_geom_edge_is_horizontal(ScrEdge *se);
/**
 * param bounds_rect: Either window or screen bounds.
 * Used to exclude edges along window/screen edges.
 */
ScrEdge *screen_geom_area_map_find_active_scredge(const struct ScrAreaMap *area_map,
                                                  const rcti *bounds_rect,
                                                  int mx,
                                                  int my);
/** Need win size to make sure not to include edges along screen edge. **/
ScrEdge *screen_geom_find_active_scredge(const wmWindow *win,
                                         const duneScreen *screen,
                                         int mx,
                                         int my);
/**
 * brief Main screen-layout calculation function.
 *
 * * Scale areas nicely on window size and DPI changes.
 * * Ensure areas have a minimum height.
 * * Correctly set global areas to their fixed height.
 */
void screen_geom_vertices_scale(const wmWindow *win, duneScreen *screen);
/**
 * return 0 if no split is possible, otherwise the screen-coordinate at which to split.
 */
short screen_geom_find_area_split_point(const ScrArea *area,
                                        const rcti *window_rect,
                                        eScreenAxis dir_axis,
                                        float fac);
/** Select all edges that are directly or indirectly connected to a edge. **/
void screen_geom_select_connected_edge(const wmWindow *win, ScrEdge *edge);

/* screen_context.c */

/** Entry point for the screen context. */
int ed_screen_context(const struct duneContext *C,
                      const char *member,
                      struct duneContextDataResult *result);

extern const char *screen_context_dir[]; /* doc access */

/* screendump.c */
void SCREEN_OT_screenshot(struct wmOperatorType *ot);
void SCREEN_OT_screenshot_area(struct wmOperatorType *ot);

/* workspace_layout_edit.c */
bool workspace_layout_set_poll(const struct WorkSpaceLayout *layout);
