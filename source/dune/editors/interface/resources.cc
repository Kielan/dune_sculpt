
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "mem_guardedalloc.h"

#include "types_screen..h"
#include "types_space.h"
#include "types_userdef.h"

#include "lib_dunelib.h"
#include "lib_math_vector.h"
#include "lib_utildefines.h"

#include "dune_addon.h"
#include "dune_appdir.h"
#include "dune_main.h"
#include "dune_mesh_runtime.hh"

#include "loader_readfile.h" /* for UserDef version patching. */

#include "font_api.h"

#include "ed_screen.hh"

#include "ui.hh"
#include "ui_icons.hh"

#include "gpu_framebuffer.h"
#include "ui_intern.hh"

/* be sure to keep 'ThemeState' in sync */
static ThemeState g_theme_state = {
    nullptr,
    SPACE_VIEW3D,
    RGN_TYPE_WIN,
};

void ui_resources_init()
{
  ui_icons_init();
}

void ui_resources_free()
{
  ui_icons_free();
}

/*    THEMES */

const uchar *ui_ThemeGetColorPtr(Theme *theme, int spacetype, int colorid)
{
  ThemeSpace *ts = nullptr;
  static uchar error[4] = {240, 0, 240, 255};
  static uchar alert[4] = {240, 60, 60, 255};
  static uchar header_active[4] = {0, 0, 0, 255};
  static uchar back[4] = {0, 0, 0, 255};
  static uchar setting = 0;
  const uchar *cp = error;

  /* ensure we're not getting a color after running dune_userdef_free */
  lib_assert(lib_findindex(&U.themes, g_theme_state.theme) != -1);
  lib_assert(colorid != TH_UNDEFINED);

  if (theme) {

    /* first check for ui buttons theme */
    if (colorid < TH_THEMEUI) {

      switch (colorid) {

        case TH_REDALERT:
          cp = alert;
          break;
      }
    }
    else {

      switch (spacetype) {
        case SPACE_PROPS:
          ts = &theme->space_props;
          break;
        case SPACE_VIEW3D:
          ts = &theme->space_view3d;
          break;
        case SPACE_GRAPH:
          ts = &theme->space_graph;
          break;
        case SPACE_FILE:
          ts = &theme->space_file;
          break;
        case SPACE_NLA:
          ts = &theme->space_nla;
          break;
        case SPACE_ACTION:
          ts = &theme->space_action;
          break;
        case SPACE_SEQ:
          ts = &theme->space_seq;
          break;
        case SPACE_IMG:
          ts = &theme->space_img;
          break;
        case SPACE_TXT:
          ts = &theme->space_txt;
          break;
        case SPACE_OUTLINER:
          ts = &theme->space_outliner;
          break;
        case SPACE_INFO:
          ts = &theme->space_info;
          break;
        case SPACE_USERPREF:
          ts = &theme->space_prefs;
          break;
        case SPACE_CONSOLE:
          ts = &theme->space_console;
          break;
        case SPACE_NODE:
          ts = &theme->space_node;
          break;
        case SPACE_CLIP:
          ts = &theme->space_clip;
          break;
        case SPACE_TOPBAR:
          ts = &theme->space_topbar;
          break;
        case SPACE_STATUSBAR:
          ts = &theme->space_statusbar;
          break;
        case SPACE_SPREADSHEET:
          ts = &theme->space_spreadsheet;
          break;
        default:
          ts = &theme->space_view3d;
          break;
      }

      switch (colorid) {
        case TH_BACK:
          if (ELEM(g_theme_state.rgnid, RGN_TYPE_WIN, RGN_TYPE_PREVIEW)) {
            cp = ts->back;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_CHANNELS) {
            cp = ts->list;
          }
          else if (ELEM(g_theme_state.rgnid, RGN_TYPE_HEADER, RGN_TYPE_FOOTER)) {
            cp = ts->header;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_NAV_BAR) {
            cp = ts->nav_bar;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_EX) {
            cp = ts->ex_btns;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_ASSET_SHELF) {
            cp = ts->asset_shelf.back;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_ASSET_SHELF_HEADER) {
            cp = ts->asset_shelf.header_back;
          }
          else {
            cp = ts->btn;
          }

          copy_v4_v4_uchar(back, cp);
          if (!ed_rgn_is_overlap(spacetype, g_theme_state.rgnid)) {
            back[3] = 255;
          }
          cp = back;
          break;
        case TH_BACK_GRAD:
          cp = ts->back_grad;
          break;

        case TH_BACKGROUND_TYPE:
          cp = &setting;
          setting = ts->background_type;
          break;
        case TH_TXT:
          if (g_theme_state.rgnid == RGN_TYPE_WIN) {
            cp = ts->txt;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_CHANNELS) {
            cp = ts->list_txt;
          }
          else if (ELEM(g_theme_state.rgnid,
                        RGN_TYPE_HEADER,
                        RGN_TYPE_FOOTER,
                        RGN_TYPE_ASSET_SHELF_HEADER))
          {
            cp = ts->header_txt;
          }
          else {
            cp = ts->btn_txt;
          }
          break;
        case TH_TXT_HI:
          if (g_theme_state.rgnid == RGN_TYPE_WIN) {
            cp = ts->txt_hi;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_CHANNELS) {
            cp = ts->list_text_hi;
          }
          else if (ELEM(g_theme_state.rgnid,
                        RGN_TYPE_HEADER,
                        RGN_TYPE_FOOTER,
                        RGN_TYPE_ASSET_SHELF_HEADER))
          {
            cp = ts->header_txt_hi;
          }
          else {
            cp = ts->btn_txt_hi;
          }
          break;
        case TH_TITLE:
          if (g_theme_state.rgnid == RGN_TYPE_WIN) {
            cp = ts->title;
          }
          else if (g_theme_state.rgnid == RGN_TYPE_CHANNELS) {
            cp = ts->list_title;
          }
          else if (ELEM(g_theme_state.rgnid,
                        RGN_TYPE_HEADER,
                        RGN_TYPE_FOOTER,
                        RGN_TYPE_ASSET_SHELF_HEADER))
          {
            cp = ts->header_title;
          }
          else {
            cp = ts->btn_title;
          }
          break;

        case TH_HEADER:
          cp = ts->header;
          break;

        case TH_HEADER_ACTIVE: {
          cp = ts->header;
          const int factor = 5;
          /* Lighten the header color when editor is active. */
          header_active[0] = cp[0] > 245 ? cp[0] - factor : cp[0] + factor;
          header_active[1] = cp[1] > 245 ? cp[1] - factor : cp[1] + factor;
          header_active[2] = cp[2] > 245 ? cp[2] - factor : cp[2] + factor;
          header_active[3] = cp[3];
          cp = header_active;
          break;
        }
        case TH_HEADER_TXT:
          cp = ts->header_txt;
          break;
        case TH_HEADER_TXT_HI:
          cp = ts->header_txt_hi;
          break;

        case TH_PNL_HEADER:
          cp = ts->pnlcolors.header;
          break;
        case TH_PNL_BACK:
          cp = ts->pnlcolors.back;
          break;
        case TH_PNL_SUB_BACK:
          cp = ts->pnlcolors.sub_back;
          break;

        case TH_BTNBACK:
          cp = ts->btn;
          break;
        case TH_BTNBACK_TXT:
          cp = ts->btn_txt;
          break;
        case TH_BTNBACK_TXT_HI:
          cp = ts->btn_txt_hi;
          break;

        case TH_TAB_ACTIVE:
          cp = ts->tab_active;
          break;
        case TH_TAB_INACTIVE:
          cp = ts->tab_inactive;
          break;
        case TH_TAB_BACK:
          cp = ts->tab_back;
          break;
        case TH_TAB_OUTLINE:
          cp = ts->tab_outline;
          break;

        case TH_SHADE1:
          cp = ts->shade1;
          break;
        case TH_SHADE2:
          cp = ts->shade2;
          break;
        case TH_HILITE:
          cp = ts->hilite;
          break;

        case TH_GRID:
          cp = ts->grid;
          break;
        case TH_TIME_SCRUB_BACKGROUND:
          cp = ts->time_scrub_background;
          break;
        case TH_TIME_MARKER_LINE:
          cp = ts->time_marker_line;
          break;
        case TH_TIME_MARKER_LINE_SELECTED:
          cp = ts->time_marker_line_selected;
          break;
        case TH_VIEW_OVERLAY:
          cp = ts->view_overlay;
          break;
        case TH_WIRE:
          cp = ts->wire;
          break;
        case TH_WIRE_INNER:
          cp = ts->syntaxr;
          break;
        case TH_WIRE_EDIT:
          cp = ts->wire_edit;
          break;
        case TH_LIGHT:
          cp = ts->lamp;
          break;
        case TH_SPEAKER:
          cp = ts->speaker;
          break;
        case TH_CAMERA:
          cp = ts->camera;
          break;
        case TH_EMPTY:
          cp = ts->empty;
          break;
        case TH_SELECT:
          cp = ts->select;
          break;
        case TH_ACTIVE:
          cp = ts->active;
          break;
        case TH_GROUP:
          cp = ts->group;
          break;
        case TH_GROUP_ACTIVE:
          cp = ts->group_active;
          break;
        case TH_TRANSFORM:
          cp = ts->transform;
          break;
        case TH_VERTEX:
          cp = ts->vertex;
          break;
        case TH_VERTEX_SEL:
          cp = ts->vertex_sel;
          break;
        case TH_VERTEX_ACTIVE:
          cp = ts->vertex_active;
          break;
        case TH_VERTEX_BEVEL:
          cp = ts->vertex_bevel;
          break;
        case TH_VERTEX_UNREFERENCED:
          cp = ts->vertex_unreferenced;
          break;
        case TH_VERTEX_SIZE:
          cp = &ts->vertex_size;
          break;
        case TH_OUTLINE_WIDTH:
          cp = &ts->outline_width;
          break;
        case TH_OBCENTER_DIA:
          cp = &ts->obcenter_dia;
          break;
        case TH_EDGE:
          cp = ts->edge;
          break;
        case TH_EDGE_WIDTH:
          cp = &ts->edge_width;
          break;
        case TH_EDGE_SEL:
          cp = ts->edge_sel;
          break;
        case TH_EDGE_MODE_SEL:
          cp = ts->edge_mode_sel;
          break;
        case TH_EDGE_SEAM:
          cp = ts->edge_seam;
          break;
        case TH_EDGE_SHARP:
          cp = ts->edge_sharp;
          break;
        case TH_EDGE_CREASE:
          cp = ts->edge_crease;
          break;
        case TH_EDGE_BEVEL:
          cp = ts->edge_bevel;
          break;
        case TH_EDITMESH_ACTIVE:
          cp = ts->editmesh_active;
          break;
        case TH_EDGE_FACESEL:
          cp = ts->edge_facesel;
          break;
        case TH_FACE:
          cp = ts->face;
          break;
        case TH_FACE_SEL:
          cp = ts->face_sel;
          break;
        case TH_FACE_MODE_SEL:
          cp = ts->face_mode_sel;
          break;
        case TH_FACE_RETOPOLOGY:
          cp = ts->face_retopology;
          break;
        case TH_FACE_BACK:
          cp = ts->face_back;
          break;
        case TH_FACE_FRONT:
          cp = ts->face_front;
          break;
        case TH_FACE_DOT:
          cp = ts->face_dot;
          break;
        case TH_FACEDOT_SIZE:
          cp = &ts->facedot_size;
          break;
        case TH_DRAWEXTRA_EDGELEN:
          cp = ts->extra_edge_len;
          break;
        case TH_DRAWEXTRA_EDGEANG:
          cp = ts->extra_edge_angle;
          break;
        case TH_DRAWEXTRA_FACEAREA:
          cp = ts->extra_face_area;
          break;
        case TH_DRAWEXTRA_FACEANG:
          cp = ts->extra_face_angle;
          break;
        case TH_NORMAL:
          cp = ts->normal;
          break;
        case TH_VNORMAL:
          cp = ts->vertex_normal;
          break;
        case TH_LNORMAL:
          cp = ts->loop_normal;
          break;
        case TH_BONE_SOLID:
          cp = ts->bone_solid;
          break;
        case TH_BONE_POSE:
          cp = ts->bone_pose;
          break;
        case TH_BONE_POSE_ACTIVE:
          cp = ts->bone_pose_active;
          break;
        case TH_BONE_LOCKED_WEIGHT:
          cp = ts->bone_locked_weight;
          break;
        case TH_STRIP:
          cp = ts->strip;
          break;
        case TH_STRIP_SELECT:
          cp = ts->strip_select;
          break;
        case TH_KEYTYPE_KEYFRAME:
          cp = ts->keytype_keyframe;
          break;
        case TH_KEYTYPE_KEYFRAME_SELECT:
          cp = ts->keytype_keyframe_select;
          break;
        case TH_KEYTYPE_EXTREME:
          cp = ts->keytype_extreme;
          break;
        case TH_KEYTYPE_EXTREME_SELECT:
          cp = ts->keytype_extreme_select;
          break;
        case TH_KEYTYPE_BREAKDOWN:
          cp = ts->keytype_breakdown;
          break;
        case TH_KEYTYPE_BREAKDOWN_SELECT:
          cp = ts->keytype_breakdown_select;
          break;
        case TH_KEYTYPE_JITTER:
          cp = ts->keytype_jitter;
          break;
        case TH_KEYTYPE_JITTER_SELECT:
          cp = ts->keytype_jitter_select;
          break;
        case TH_KEYTYPE_MOVEHOLD:
          cp = ts->keytype_movehold;
          break;
        case TH_KEYTYPE_MOVEHOLD_SELECT:
          cp = ts->keytype_movehold_select;
          break;
        case TH_KEYBORDER:
          cp = ts->keyborder;
          break;
        case TH_KEYBORDER_SELECT:
          cp = ts->keyborder_select;
          break;
        case TH_CFRAME:
          cp = ts->cframe;
          break;
        case TH_TIME_KEYFRAME:
          cp = ts->time_keyframe;
          break;
        case TH_TIME_GP_KEYFRAME:
          cp = ts->time_gp_keyframe;
          break;
        case TH_NURB_ULINE:
          cp = ts->nurb_uline;
          break;
        case TH_NURB_VLINE:
          cp = ts->nurb_vline;
          break;
        case TH_NURB_SEL_ULINE:
          cp = ts->nurb_sel_uline;
          break;
        case TH_NURB_SEL_VLINE:
          cp = ts->nurb_sel_vline;
          break;
        case TH_ACTIVE_SPLINE:
          cp = ts->act_spline;
          break;
        case TH_ACTIVE_VERT:
          cp = ts->lastsel_point;
          break;
        case TH_HANDLE_FREE:
          cp = ts->handle_free;
          break;
        case TH_HANDLE_AUTO:
          cp = ts->handle_auto;
          break;
        case TH_HANDLE_AUTOCLAMP:
          cp = ts->handle_auto_clamped;
          break;
        case TH_HANDLE_VECT:
          cp = ts->handle_vect;
          break;
        case TH_HANDLE_ALIGN:
          cp = ts->handle_align;
          break;
        case TH_HANDLE_SEL_FREE:
          cp = ts->handle_sel_free;
          break;
        case TH_HANDLE_SEL_AUTO:
          cp = ts->handle_sel_auto;
          break;
        case TH_HANDLE_SEL_AUTOCLAMP:
          cp = ts->handle_sel_auto_clamped;
          break;
        case TH_HANDLE_SEL_VECT:
          cp = ts->handle_sel_vect;
          break;
        case TH_HANDLE_SEL_ALIGN:
          cp = ts->handle_sel_align;
          break;
        case TH_FREESTYLE_EDGE_MARK:
          cp = ts->freestyle_edge_mark;
          break;
        case TH_FREESTYLE_FACE_MARK:
          cp = ts->freestyle_face_mark;
          break;

        case TH_SYNTAX_B:
          cp = ts->syntaxb;
          break;
        case TH_SYNTAX_V:
          cp = ts->syntaxv;
          break;
        case TH_SYNTAX_C:
          cp = ts->syntaxc;
          break;
        case TH_SYNTAX_L:
          cp = ts->syntaxl;
          break;
        case TH_SYNTAX_D:
          cp = ts->syntaxd;
          break;
        case TH_SYNTAX_R:
          cp = ts->syntaxr;
          break;
        case TH_SYNTAX_N:
          cp = ts->syntaxn;
          break;
        case TH_SYNTAX_S:
          cp = ts->syntaxs;
          break;
        case TH_LINENUMBERS:
          cp = ts->line_numbers;
          break;

        case TH_NODE:
          cp = ts->syntaxl;
          break;
        case TH_NODE_INPUT:
          cp = ts->syntaxn;
          break;
        case TH_NODE_OUTPUT:
          cp = ts->nodeclass_output;
          break;
        case TH_NODE_COLOR:
          cp = ts->syntaxb;
          break;
        case TH_NODE_FILTER:
          cp = ts->nodeclass_filter;
          break;
        case TH_NODE_VECTOR:
          cp = ts->nodeclass_vector;
          break;
        case TH_NODE_TEXTURE:
          cp = ts->nodeclass_texture;
          break;
        case TH_NODE_PATTERN:
          cp = ts->nodeclass_pattern;
          break;
        case TH_NODE_SCRIPT:
          cp = ts->nodeclass_script;
          break;
        case TH_NODE_LAYOUT:
          cp = ts->nodeclass_layout;
          break;
        case TH_NODE_GEOMETRY:
          cp = ts->nodeclass_geometry;
          break;
        case TH_NODE_ATTRIBUTE:
          cp = ts->nodeclass_attribute;
          break;
        case TH_NODE_SHADER:
          cp = ts->nodeclass_shader;
          break;
        case TH_NODE_CONVERTER:
          cp = ts->syntaxv;
          break;
        case TH_NODE_GROUP:
          cp = ts->syntaxc;
          break;
        case TH_NODE_INTERFACE:
          cp = ts->console_output;
          break;
        case TH_NODE_FRAME:
          cp = ts->movie;
          break;
        case TH_NODE_MATTE:
          cp = ts->syntaxs;
          break;
        case TH_NODE_DISTORT:
          cp = ts->syntaxd;
          break;
        case TH_NODE_CURVING:
          cp = &ts->noodle_curving;
          break;
        case TH_NODE_GRID_LEVELS:
          cp = &ts->grid_levels;
          break;
        case TH_NODE_ZONE_SIMULATION:
          cp = ts->node_zone_simulation;
          break;
        case TH_NODE_ZONE_REPEAT:
          cp = ts->node_zone_repeat;
          break;
        case TH_SIMULATED_FRAMES:
          cp = ts->simulated_frames;
          break;

        case TH_SEQ_MOVIE:
          cp = ts->movie;
          break;
        case TH_SEQ_MOVIECLIP:
          cp = ts->movieclip;
          break;
        case TH_SEQ_MASK:
          cp = ts->mask;
          break;
        case TH_SEQ_IMAGE:
          cp = ts->image;
          break;
        case TH_SEQ_SCENE:
          cp = ts->scene;
          break;
        case TH_SEQ_AUDIO:
          cp = ts->audio;
          break;
        case TH_SEQ_EFFECT:
          cp = ts->effect;
          break;
        case TH_SEQ_TRANSITION:
          cp = ts->transition;
          break;
        case TH_SEQ_META:
          cp = ts->meta;
          break;
        case TH_SEQ_TEXT:
          cp = ts->text_strip;
          break;
        case TH_SEQ_PREVIEW:
          cp = ts->preview_back;
          break;
        case TH_SEQ_COLOR:
          cp = ts->color_strip;
          break;
        case TH_SEQ_ACTIVE:
          cp = ts->active_strip;
          break;
        case TH_SEQ_SELECTED:
          cp = ts->selected_strip;
          break;

        case TH_CONSOLE_OUTPUT:
          cp = ts->console_output;
          break;
        case TH_CONSOLE_INPUT:
          cp = ts->console_input;
          break;
        case TH_CONSOLE_INFO:
          cp = ts->console_info;
          break;
        case TH_CONSOLE_ERROR:
          cp = ts->console_error;
          break;
        case TH_CONSOLE_CURSOR:
          cp = ts->console_cursor;
          break;
        case TH_CONSOLE_SELECT:
          cp = ts->console_select;
          break;

        case TH_HANDLE_VERTEX:
          cp = ts->handle_vertex;
          break;
        case TH_HANDLE_VERTEX_SELECT:
          cp = ts->handle_vertex_select;
          break;
        case TH_HANDLE_VERTEX_SIZE:
          cp = &ts->handle_vertex_size;
          break;

        case TH_GP_VERTEX:
          cp = ts->gp_vertex;
          break;
        case TH_GP_VERTEX_SEL:
          cp = ts->gp_vertex_select;
          break;
        case TH_GP_VERTEX_SIZE:
          cp = &ts->gp_vertex_size;
          break;

        case TH_DOPESHEET_CHANNELOB:
          cp = ts->ds_channel;
          break;
        case TH_DOPESHEET_CHANNELSUBOB:
          cp = ts->ds_subchannel;
          break;
        case TH_DOPESHEET_IPOLINE:
          cp = ts->ds_ipoline;
          break;

        case TH_PREVIEW_BACK:
          cp = ts->preview_back;
          break;

        case TH_STITCH_PREVIEW_FACE:
          cp = ts->preview_stitch_face;
          break;

        case TH_STITCH_PREVIEW_EDGE:
          cp = ts->preview_stitch_edge;
          break;

        case TH_STITCH_PREVIEW_VERT:
          cp = ts->preview_stitch_vert;
          break;

        case TH_STITCH_PREVIEW_STITCHABLE:
          cp = ts->preview_stitch_stitchable;
          break;

        case TH_STITCH_PREVIEW_UNSTITCHABLE:
          cp = ts->preview_stitch_unstitchable;
          break;
        case TH_STITCH_PREVIEW_ACTIVE:
          cp = ts->preview_stitch_active;
          break;

        case TH_PAINT_CURVE_HANDLE:
          cp = ts->paint_curve_handle;
          break;
        case TH_PAINT_CURVE_PIVOT:
          cp = ts->paint_curve_pivot;
          break;

        case TH_METADATA_BG:
          cp = ts->metadatabg;
          break;
        case TH_METADATA_TXT:
          cp = ts->metadatatxt;
          break;

        case TH_UV_SHADOW:
          cp = ts->uv_shadow;
          break;

        case TH_MARKER_OUTLINE:
          cp = ts->marker_outline;
          break;
        case TH_MARKER:
          cp = ts->marker;
          break;
        case TH_ACT_MARKER:
          cp = ts->act_marker;
          break;
        case TH_SEL_MARKER:
          cp = ts->sel_marker;
          break;
        case TH_BUNDLE_SOLID:
          cp = ts->bundle_solid;
          break;
        case TH_DIS_MARKER:
          cp = ts->dis_marker;
          break;
        case TH_PATH_BEFORE:
          cp = ts->path_before;
          break;
        case TH_PATH_AFTER:
          cp = ts->path_after;
          break;
        case TH_PATH_KEYFRAME_BEFORE:
          cp = ts->path_keyframe_before;
          break;
        case TH_PATH_KEYFRAME_AFTER:
          cp = ts->path_keyframe_after;
          break;
        case TH_CAMERA_PATH:
          cp = ts->camera_path;
          break;
        case TH_CAMERA_PASSEPARTOUT:
          cp = ts->camera_passepartout;
          break;
        case TH_LOCK_MARKER:
          cp = ts->lock_marker;
          break;

        case TH_MATCH:
          cp = ts->match;
          break;

        case TH_SELECTED_HIGHLIGHT:
          cp = ts->selected_highlight;
          break;

        case TH_SEL_ACTIVE:
          cp = ts->active;
          break;

        case TH_SELECTED_OB:
          cp = ts->selected_ob;
          break;

        case TH_ACTIVE_OB:
          cp = ts->active_ob;
          break;

        case TH_EDITED_OB:
          cp = ts->edited_ob;
          break;

        case TH_ROW_ALTERNATE:
          cp = ts->row_alternate;
          break;

        case TH_SKIN_ROOT:
          cp = ts->skin_root;
          break;

        case TH_ANIM_ACTIVE:
          cp = ts->anim_active;
          break;
        case TH_ANIM_INACTIVE:
          cp = ts->anim_non_active;
          break;
        case TH_ANIM_PREVIEW_RANGE:
          cp = ts->anim_preview_range;
          break;

        case TH_NLA_TWEAK:
          cp = ts->nla_tweaking;
          break;
        case TH_NLA_TWEAK_DUPLI:
          cp = ts->nla_tweakdupli;
          break;

        case TH_NLA_TRACK:
          cp = ts->nla_track;
          break;
        case TH_NLA_TRANSITION:
          cp = ts->nla_transition;
          break;
        case TH_NLA_TRANSITION_SEL:
          cp = ts->nla_transition_sel;
          break;
        case TH_NLA_META:
          cp = ts->nla_meta;
          break;
        case TH_NLA_META_SEL:
          cp = ts->nla_meta_sel;
          break;
        case TH_NLA_SOUND:
          cp = ts->nla_sound;
          break;
        case TH_NLA_SOUND_SEL:
          cp = ts->nla_sound_sel;
          break;

        case TH_WIDGET_EMBOSS:
          cp = theme->tui.widget_emboss;
          break;

        case TH_EDITOR_OUTLINE:
          cp = theme->tui.editor_outline;
          break;
        case TH_WIDGET_TXT_CURSOR:
          cp = theme->tui.widget_txt_cursor;
          break;
        case TH_WIDGET_TXT_SEL:
          cp = theme->tui.wcol_txt.item;
          break;
        case TH_WIDGET_TXT_HIGHLIGHT:
          cp = theme->tui.wcol_txt.txt_sel;
          break;

        case TH_TRANSPARENT_CHECKER_PRIMARY:
          cp = theme->tui.transparent_checker_primary;
          break;
        case TH_TRANSPARENT_CHECKER_SECONDARY:
          cp = theme->tui.transparent_checker_secondary;
          break;
        case TH_TRANSPARENT_CHECKER_SIZE:
          cp = &theme->tui.transparent_checker_size;
          break;

        case TH_AXIS_X:
          cp = theme->tui.xaxis;
          break;
        case TH_AXIS_Y:
          cp = theme->tui.yaxis;
          break;
        case TH_AXIS_Z:
          cp = theme->tui.zaxis;
          break;

        case TH_GIZMO_HI:
          cp = theme->tui.gizmo_hi;
          break;
        case TH_GIZMO_PRIMARY:
          cp = theme->tui.gizmo_primary;
          break;
        case TH_GIZMO_SECONDARY:
          cp = theme->tui.gizmo_secondary;
          break;
        case TH_GIZMO_VIEW_ALIGN:
          cp = theme->tui.gizmo_view_align;
          break;
        case TH_GIZMO_A:
          cp = theme->tui.gizmo_a;
          break;
        case TH_GIZMO_B:
          cp = theme->tui.gizmo_b;
          break;

        case TH_ICON_SCENE:
          cp = theme->tui.icon_scene;
          break;
        case TH_ICON_COLLECTION:
          cp = theme->tui.icon_collection;
          break;
        case TH_ICON_OB:
          cp = theme->tui.icon_ob;
          break;
        case TH_ICON_OB_DATA:
          cp = theme->tui.icon_ob_data;
          break;
        case TH_ICON_MOD:
          cp = theme->tui.icon_mod;
          break;
        case TH_ICON_SHADING:
          cp = theme->tui.icon_shading;
          break;
        case TH_ICON_FOLDER:
          cp = theme->tui.icon_folder;
          break;
        case TH_ICON_FUND: {
          /* Development fund icon color is not part of theme. */
          static const uchar red[4] = {204, 48, 72, 255};
          cp = red;
          break;
        }

        case TH_SCROLL_TXT:
          cp = theme->tui.wcol_scroll.txt;
          break;

        case TH_INFO_SELECTED:
          cp = ts->info_selected;
          break;
        case TH_INFO_SELECTED_TXT:
          cp = ts->info_selected_txt;
          break;
        case TH_INFO_ERR:
          cp = ts->info_err;
          break;
        case TH_INFO_ERR_TXT:
          cp = ts->info_err_txt;
          break;
        case TH_INFO_WARNING:
          cp = ts->info_warning;
          break;
        case TH_INFO_WARNING_TXT:
          cp = ts->info_warning_txt;
          break;
        case TH_INFO_INFO:
          cp = ts->info_info;
          break;
        case TH_INFO_INFO_TXT:
          cp = ts->info_info_txt;
          break;
        case TH_INFO_DEBUG:
          cp = ts->info_debug;
          break;
        case TH_INFO_DEBUG_TXT:
          cp = ts->info_debug_txt;
          break;
        case TH_INFO_PROP:
          cp = ts->info_prop;
          break;
        case TH_INFO_PROP_TXT:
          cp = ts->info_prop_txt;
          break;
        case TH_INFO_OP:
          cp = ts->info_op;
          break;
        case TH_INFO_OP_TXT:
          cp = ts->info_op_txt;
          break;
        case TH_V3D_CLIPPING_BORDER:
          cp = ts->clipping_border_3d;
          break;
      }
    }
  }

  return (const uchar *)cp;
}

void ui_theme_init_default()
{
  /* we search for the theme with name Default */
  Theme *theme = static_cast<Theme *>(
      lib_findstring(&U.themes, "Default", offsetof(Theme, name)));
  if (theme == nullptr) {
    theme = mem_cnew<Theme>(__func__);
    lib_addtail(&U.themes, theme);
  }

  ui_SetTheme(0, 0); /* make sure the global used in this file is set */

  const int active_theme_area = theme->active_theme_area;
  memcpy(theme, &U_theme_default, sizeof(*theme));
  theme->active_theme_area = active_theme_area;
}

void ui_style_init_default()
{
  lib_freelist(&U.uistyles);
  /* gets automatically re-allocated */
  uiStyleInit();
}

void ui_SetTheme(int spacetype, int rgnid)
{
  if (spacetype) {
    /* later on, a local theme can be found too */
    g_theme_state.theme = static_cast<Theme *>(U.themes.first);
    g_theme_state.spacetype = spacetype;
    g_theme_state.rgnid = rgnid;
  }
  else if (rgnid) {
    /* popups */
    g_theme_state.theme = static_cast<Theme *>(U.themes.first);
    g_theme_state.spacetype = SPACE_PROPS;
    g_theme_state.rgnid = rgnid;
  }
  else {
    /* for safety, when theme was deleted */
    g_theme_state.theme = static_cast<Theme *>(U.themes.first);
    g_theme_state.spacetype = SPACE_VIEW3D;
    g_theme_state.rgnid = RGN_TYPE_WIN;
  }
}

Theme *ui_GetTheme()
{
  return static_cast<Theme *>(U.themes.first);
}

void ui_Theme_Store(ThemeState *theme_state)
{
  *theme_state = g_theme_state;
}
void ui_Theme_Restore(ThemeState *theme_state)
{
  g_theme_state = *theme_state;
}

void ui_GetThemeColorShadeAlpha4ubv(int colorid, int coloffset, int alphaoffset, uchar col[4])
{
  int r, g, b, a;
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  r = coloffset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = coloffset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = coloffset + int(cp[2]);
  CLAMP(b, 0, 255);
  a = alphaoffset + int(cp[3]);
  CLAMP(a, 0, 255);

  col[0] = r;
  col[1] = g;
  col[2] = b;
  col[3] = a;
}

void ui_GetThemeColorBlend3ubv(int colorid1, int colorid2, float fac, uchar col[3])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);

  CLAMP(fac, 0.0f, 1.0f);
  col[0] = floorf((1.0f - fac) * cp1[0] + fac * cp2[0]);
  col[1] = floorf((1.0f - fac) * cp1[1] + fac * cp2[1]);
  col[2] = floorf((1.0f - fac) * cp1[2] + fac * cp2[2]);
}

void ui_GetThemeColorBlend3f(int colorid1, int colorid2, float fac, float r_col[3])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);

  CLAMP(fac, 0.0f, 1.0f);
  r_col[0] = ((1.0f - fac) * cp1[0] + fac * cp2[0]) / 255.0f;
  r_col[1] = ((1.0f - fac) * cp1[1] + fac * cp2[1]) / 255.0f;
  r_col[2] = ((1.0f - fac) * cp1[2] + fac * cp2[2]) / 255.0f;
}

void ui_GetThemeColorBlend4f(int colorid1, int colorid2, float fac, float r_col[4])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);

  CLAMP(fac, 0.0f, 1.0f);
  r_col[0] = ((1.0f - fac) * cp1[0] + fac * cp2[0]) / 255.0f;
  r_col[1] = ((1.0f - fac) * cp1[1] + fac * cp2[1]) / 255.0f;
  r_col[2] = ((1.0f - fac) * cp1[2] + fac * cp2[2]) / 255.0f;
  r_col[3] = ((1.0f - fac) * cp1[3] + fac * cp2[3]) / 255.0f;
}

void ui_FontThemeColor(int fontid, int colorid)
{
  uchar color[4];
  ui_GetThemeColor4ubv(colorid, color);
  font_color4ubv(fontid, color);
}

float ui_GetThemeValf(int colorid)
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  return float(cp[0]);
}

int ui_GetThemeVal(int colorid)
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  return int(cp[0]);
}

float ui_GetThemeValTypef(int colorid, int spacetype)
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  return float(cp[0]);
}

int ui_GetThemeValType(int colorid, int spacetype)
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  return int(cp[0]);
}

void ui_GetThemeColor3fv(int colorid, float col[3])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  col[0] = float(cp[0]) / 255.0f;
  col[1] = float(cp[1]) / 255.0f;
  col[2] = float(cp[2]) / 255.0f;
}

void ui_GetThemeColor4fv(int colorid, float col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  col[0] = float(cp[0]) / 255.0f;
  col[1] = float(cp[1]) / 255.0f;
  col[2] = float(cp[2]) / 255.0f;
  col[3] = float(cp[3]) / 255.0f;
}

void ui_GetThemeColorType4fv(int colorid, int spacetype, float col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  col[0] = float(cp[0]) / 255.0f;
  col[1] = float(cp[1]) / 255.0f;
  col[2] = float(cp[2]) / 255.0f;
  col[3] = float(cp[3]) / 255.0f;
}

void ui_GetThemeColorShade3fv(int colorid, int offset, float col[3])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  int r, g, b;

  r = offset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = offset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = offset + int(cp[2]);
  CLAMP(b, 0, 255);

  col[0] = float(r) / 255.0f;
  col[1] = float(g) / 255.0f;
  col[2] = float(b) / 255.0f;
}

void ui_GetThemeColorShade3ubv(int colorid, int offset, uchar col[3])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  int r, g, b;

  r = offset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = offset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = offset + int(cp[2]);
  CLAMP(b, 0, 255);

  col[0] = r;
  col[1] = g;
  col[2] = b;
}

void ui_GetThemeColorBlendShade3ubv(
    int colorid1, int colorid2, float fac, int offset, uchar col[3])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);

  CLAMP(fac, 0.0f, 1.0f);

  float blend[3];
  blend[0] = (offset + floorf((1.0f - fac) * cp1[0] + fac * cp2[0])) / 255.0f;
  blend[1] = (offset + floorf((1.0f - fac) * cp1[1] + fac * cp2[1])) / 255.0f;
  blend[2] = (offset + floorf((1.0f - fac) * cp1[2] + fac * cp2[2])) / 255.0f;

  unit_float_to_uchar_clamp_v3(col, blend);
}

void ui_GetThemeColorShade4ubv(int colorid, int offset, uchar col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  int r, g, b;

  r = offset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = offset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = offset + int(cp[2]);
  CLAMP(b, 0, 255);

  col[0] = r;
  col[1] = g;
  col[2] = b;
  col[3] = cp[3];
}

void ui_GetThemeColorShadeAlpha4fv(int colorid, int coloffset, int alphaoffset, float col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  int r, g, b, a;

  r = coloffset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = coloffset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = coloffset + int(cp[2]);
  CLAMP(b, 0, 255);
  a = alphaoffset + int(cp[3]);
  CLAMP(a, 0, 255);

  col[0] = float(r) / 255.0f;
  col[1] = float(g) / 255.0f;
  col[2] = float(b) / 255.0f;
  col[3] = float(a) / 255.0f;
}

void ui_GetThemeColorBlendShade3fv(int colorid1, int colorid2, float fac, int offset, float col[3])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);
  int r, g, b;

  CLAMP(fac, 0.0f, 1.0f);

  r = offset + floorf((1.0f - fac) * cp1[0] + fac * cp2[0]);
  CLAMP(r, 0, 255);
  g = offset + floorf((1.0f - fac) * cp1[1] + fac * cp2[1]);
  CLAMP(g, 0, 255);
  b = offset + floorf((1.0f - fac) * cp1[2] + fac * cp2[2]);
  CLAMP(b, 0, 255);

  col[0] = float(r) / 255.0f;
  col[1] = float(g) / 255.0f;
  col[2] = float(b) / 255.0f;
}

void ui_GetThemeColorBlendShade4fv(int colorid1, int colorid2, float fac, int offset, float col[4])
{
  const uchar *cp1 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid1);
  const uchar *cp2 = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid2);
  int r, g, b, a;

  CLAMP(fac, 0.0f, 1.0f);

  r = offset + floorf((1.0f - fac) * cp1[0] + fac * cp2[0]);
  CLAMP(r, 0, 255);
  g = offset + floorf((1.0f - fac) * cp1[1] + fac * cp2[1]);
  CLAMP(g, 0, 255);
  b = offset + floorf((1.0f - fac) * cp1[2] + fac * cp2[2]);
  CLAMP(b, 0, 255);

  a = floorf((1.0f - fac) * cp1[3] + fac * cp2[3]); /* No shading offset. */
  CLAMP(a, 0, 255);

  col[0] = float(r) / 255.0f;
  col[1] = float(g) / 255.0f;
  col[2] = float(b) / 255.0f;
  col[3] = float(a) / 255.0f;
}

void ui_GetThemeColor3ubv(int colorid, uchar col[3])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  col[0] = cp[0];
  col[1] = cp[1];
  col[2] = cp[2];
}

void ui_GetThemeColorShade4fv(int colorid, int offset, float col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  int r, g, b, a;

  r = offset + int(cp[0]);
  CLAMP(r, 0, 255);
  g = offset + int(cp[1]);
  CLAMP(g, 0, 255);
  b = offset + int(cp[2]);
  CLAMP(b, 0, 255);

  a = int(cp[3]); /* no shading offset... */
  CLAMP(a, 0, 255);

  col[0] = float(r) / 255.0f;
  col[1] = float(g) / 255.0f;
  col[2] = float(b) / 255.0f;
  col[3] = float(a) / 255.0f;
}

void ui_GetThemeColor4ubv(int colorid, uchar col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  col[0] = cp[0];
  col[1] = cp[1];
  col[2] = cp[2];
  col[3] = cp[3];
}

void ui_GetThemeColorType3fv(int colorid, int spacetype, float col[3])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  col[0] = float(cp[0]) / 255.0f;
  col[1] = float(cp[1]) / 255.0f;
  col[2] = float(cp[2]) / 255.0f;
}

void ui_GetThemeColorType3ubv(int colorid, int spacetype, uchar col[3])
{
  const uchar *cp = UI_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  col[0] = cp[0];
  col[1] = cp[1];
  col[2] = cp[2];
}

void ui_GetThemeColorType4ubv(int colorid, int spacetype, uchar col[4])
{
  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, spacetype, colorid);
  col[0] = cp[0];
  col[1] = cp[1];
  col[2] = cp[2];
  col[3] = cp[3];
}

bool ui_GetIconThemeColor4ubv(int colorid, uchar col[4])
{
  if (colorid == 0) {
    return false;
  }
  if (colorid == TH_ICON_FUND) {
    /* Always color development fund icon. */
  }
  else if (!((g_theme_state.spacetype == SPACE_OUTLINER &&
              g_theme_state.rgnid == RGN_TYPE_WIN) ||
             (g_theme_state.spacetype == SPACE_PROPS &&
              g_theme_state.rgnid == RGN_TYPE_NAV_BAR) ||
             (g_theme_state.spacetype == SPACE_FILE && g_theme_state.regionid == RGN_TYPE_WIN)))
  {
    /* Only colored icons in specific places, overall UI is intended
     * to stay monochrome and out of the way except a few places where it
     * is important to communicate different data types. */
    return false;
  }

  const uchar *cp = ui_ThemeGetColorPtr(g_theme_state.theme, g_theme_state.spacetype, colorid);
  col[0] = cp[0];
  col[1] = cp[1];
  col[2] = cp[2];
  col[3] = cp[3];

  return true;
}

void ui_GetColorPtrShade3ubv(const uchar cp[3], uchar col[3], int offset)
{
  int r, g, b;

  r = offset + int(cp[0]);
  g = offset + int(cp[1]);
  b = offset + int(cp[2]);

  CLAMP(r, 0, 255);
  CLAMP(g, 0, 255);
  CLAMP(b, 0, 255);

  col[0] = r;
  col[1] = g;
  col[2] = b;
}

void ui_GetColorPtrBlendShade3ubv(
    const uchar cp1[3], const uchar cp2[3], uchar col[3], float fac, int offset)
{
  int r, g, b;

  CLAMP(fac, 0.0f, 1.0f);
  r = offset + floor((1.0f - fac) * cp1[0] + fac * cp2[0]);
  g = offset + floor((1.0f - fac) * cp1[1] + fac * cp2[1]);
  b = offset + floor((1.0f - fac) * cp1[2] + fac * cp2[2]);

  CLAMP(r, 0, 255);
  CLAMP(g, 0, 255);
  CLAMP(b, 0, 255);

  col[0] = r;
  col[1] = g;
  col[2] = b;
}

void ui_ThemeClearColor(int colorid)
{
  float col[3];

  ui_GetThemeColor3fv(colorid, col);
  gpu_clear_color(col[0], col[1], col[2], 1.0f);
}

int ui_ThemeMenuShadowWidth()
{
  Theme *theme = ui_GetTheme();
  return int(theme->tui.menu_shadow_width * UI_SCALE_FAC);
}

void ui_make_axis_color(const uchar src_col[3], uchar dst_col[3], const char axis)
{
  uchar col[3];

  switch (axis) {
    case 'X':
      ui_GetThemeColor3ubv(TH_AXIS_X, col);
      ui_GetColorPtrBlendShade3ubv(src_col, col, dst_col, 0.5f, -10);
      break;
    case 'Y':
      ui_GetThemeColor3ubv(TH_AXIS_Y, col);
      ui_GetColorPtrBlendShade3ubv(src_col, col, dst_col, 0.5f, -10);
      break;
    case 'Z':
      ui_GetThemeColor3ubv(TH_AXIS_Z, col);
      ui_GetColorPtrBlendShade3ubv(src_col, col, dst_col, 0.5f, -10);
      break;
    default:
      lib_assert(0);
      break;
  }
}
