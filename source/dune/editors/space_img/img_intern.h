#pragma once

/* internal exports only */
struct ARgn;
struct ARgnType;
struct SpaceImg;
struct Cxt;
struct NodeTree;
struct WinOpType;

#ifdef __cplusplus
extern "C" {
#endif

/* `space_img.cc` */
extern const char *img_cxt_dir[]; /* doc access */

/* `img_drw.cc` */
void drw_img_main_helpers(const struct Cxt *C, struct ARgn *rgn);
void drw_img_cache(const struct Cxt *C, struct ARgn *rgn);
void drw_img_sample_line(struct SpaceImg *simg);

/* `img_ops.cc` */
bool space_img_main_rgn_poll(struct Cxt *C);
bool space_img_view_center_cursor_poll(struct bContext *C);

void IMG_OT_view_all(struct wmOpType *ot);
void IMG_OT_view_pan(struct wmOpType *ot);
void IMG_OT_view_selected(struct wmOpType *ot);
void IMG_OT_view_center_cursor(struct wmOpType *ot);
void IMG_OT_view_cursor_center(struct wmOpType *ot);
void IMG_OT_view_zoom(struct wmOpType *ot);
void IMG_OT_view_zoom_in(struct wmOpType *ot);
void IMG_OT_view_zoom_out(struct wmOpType *ot);
void IMG_OT_view_zoom_ratio(struct wmOpType *ot);
void IMH_OT_view_zoom_border(struct wmOpType *ot);
#ifdef WITH_INPUT_NDOF
void IMG_OT_view_ndof(struct wmOperatorType *ot);
#endif

void IMG_OT_new(struct wmOperatorType *ot);
/**
 * Called by other space types too.
 */
void IMG_OT_open(struct wmOperatorType *ot);
void IMG_OT_file_browse(struct wmOperatorType *ot);
/**
 * Called by other space types too.
 */
void IMG_OT_match_movie_length(struct wmOperatorType *ot);
void IMH_OT_replace(struct wmOperatorType *ot);
void IMAGE_OT_reload(struct wmOperatorType *ot);
void IMAGE_OT_save(struct wmOperatorType *ot);
void IMAGE_OT_save_as(struct wmOperatorType *ot);
void IMAGE_OT_save_sequence(struct wmOperatorType *ot);
void IMAGE_OT_save_all_modified(struct wmOperatorType *ot);
void IMAGE_OT_pack(struct wmOperatorType *ot);
void IMAGE_OT_unpack(struct wmOperatorType *ot);
void IMAGE_OT_clipboard_copy(struct wmOperatorType *ot);
void IMG_OT_clipboard_paste(struct wmOperatorType *ot);

void IMG_OT_flip(struct wmOperatorType *ot);
void IMG_OT_invert(struct wmOperatorType *ot);
void IMG_OT_resize(struct wmOperatorType *ot);

void IMG_OT_cycle_render_slot(struct wmOperatorType *ot);
void IMG_OT_clear_render_slot(struct wmOperatorType *ot);
void IMG_OT_add_render_slot(struct wmOperatorType *ot);
void IMG_OT_remove_render_slot(struct wmOperatorType *ot);

void IMG_OT_sample(struct wmOperatorType *ot);
void IMG_OT_sample_line(struct wmOperatorType *ot);
void IMG_OT_curves_point_set(struct wmOperatorType *ot);

void IMG_OT_change_frame(struct wmOperatorType *ot);

void IMG_OT_read_viewlayers(struct wmOperatorType *ot);
void IMG_OT_render_border(struct wmOperatorType *ot);
void IMG_OT_clear_render_border(struct wmOperatorType *ot);

void IMG_OT_tile_add(struct wmOperatorType *ot);
void IMAGE_OT_tile_remove(struct wmOperatorType *ot);
void IMAGE_OT_tile_fill(struct wmOperatorType *ot);

/* image_panels.c */

/**
 * Gets active viewer user.
 */
struct ImageUser *ntree_get_active_iuser(struct bNodeTree *ntree);
void image_buttons_register(struct ARegionType *art);

#ifdef __cplusplus
}
#endif
