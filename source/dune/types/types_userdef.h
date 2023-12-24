#pragma once

#include "types_list.h"
#include "types_texture.h" /* ColorBand */
#include "types_userdef_enums.h"

/* Themes; defines in `BIF_resource.h`. */
struct ColorBand;

/* style definitions */
#define MAX_STYLE_NAME 64

/* Default offered by Dune.
 * uiFont.uifont_id */
typedef enum eUIFontId {
  UIFONT_DEFAULT = 0,
  /* UIFONT_BITMAP   = 1 */ /* UNUSED */
  /* free slots */
  UIFONT_CUSTOM1 = 2,
  /* UIFONT_CUSTOM2 = 3, */ /* UNUSED */
} eUIFontId;

/* Default fonts to load/init.
 * First font is the default (index 0), others optional. */
typedef struct uiFont {
  struct uiFont *next, *prev;
  /* 1024 = FILE_MAX. */
  char filepath[1024];
  /* From dlfont lib. */
  short font_id;
  /* Own id (eUIFontId). */
  short uifont_id;
} uiFont;

/* This state defines appearance of txt. */
typedef struct uiFontStyle {
  /* Saved in file, 0 is default. */
  short uifont_id;
  char _pad1[2];
  /* Actual size deps on 'global' dpi. */
  float points;
  /* Style hint. */
  short italic, bold;
  /* Val is amnt of pixels blur. */
  short shadow;
  /* Shadow offset in pixels. */
  short shadx, shady;
  char _pad0[2];
  /* Total alpha. */
  float shadowalpha;
  /* 1 val, typically white or black anyway. */
  float shadowcolor;
  char _pad2[4];
} uiFontStyle;

/* this is fed to the layout engine and widget code */
typedef struct uiStyle {
  struct uiStyle *next, *prev;

  /* MAX_STYLE_NAME. */
  char name[64];

  uiFontStyle pnltitle;
  uiFontStyle grouplabel;
  uiFontStyle widgetlabel;
  uiFontStyle widget;

  float pnlzoom;

  /* In chars. */
  short minlabelchars;
  /* In mainlabelchars. */
  short minwidgetchars;

  short columnspace;
  short templatespace;
  short boxspace;
  short btnspacex;
  short btnspacey;
  short pnlspace;
  short pnlouter;

  char _pad0[2];
} uiStyle;

typedef struct uiWidgetColors {
  unsigned char outline[4];
  unsigned char inner[4];
  unsigned char inner_sel[4];
  unsigned char item[4];
  unsigned char txt[4];
  unsigned char txt_sel[4];
  unsigned char shaded;
  char _pad0[7];
  short shadetop, shadedown;
  float roundness;
} uiWidgetColors;

typedef struct uiWidgetStateColors {
  unsigned char inner_anim[4];
  unsigned char inner_anim_sel[4];
  unsigned char inner_key[4];
  unsigned char inner_key_sel[4];
  unsigned char inner_driven[4];
  unsigned char inner_driven_sel[4];
  unsigned char inner_overridden[4];
  unsigned char inner_overridden_sel[4];
  unsigned char inner_changed[4];
  unsigned char inner_changed_sel[4];
  float blend;
  char _pad0[4];
} uiWidgetStateColors;

typedef struct uiPnlColors {
  unsigned char header[4];
  unsigned char back[4];
  unsigned char sub_back[4];
  char _pad0[4];
} uiPnlColors;

typedef struct ThemeUI {
  /* Interface Elements (btns, menus, icons) */
  uiWidgetColors wcol_regular, wcol_tool, wcol_toolbar_item, wcol_txt;
  uiWidgetColors wcol_radio, wcol_option, wcol_toggle;
  uiWidgetColors wcol_num, wcol_numslider, wcol_tab;
  uiWidgetColors wcol_menu, wcol_pulldown, wcol_menu_back, wcol_menu_item, wcol_tooltip;
  uiWidgetColors wcol_box, wcol_scroll, wcol_progress, wcol_list_item, wcol_pie_menu;

  uiWidgetStateColors wcol_state;

  unsigned char widget_emboss[4];

  /* fac: 0 - 1 for blend factor, width in pixels */
  float menu_shadow_fac;
  short menu_shadow_width;

  unsigned char editor_outline[4];

  /* Transparent Grid */
  unsigned char transparent_checker_primary[4], transparent_checker_secondary[4];
  unsigned char transparent_checker_size;
  char _pad1[1];

  float icon_alpha;
  float icon_saturation;
  unsigned char widget_txt_cursor[4];

  /* Axis Colors */
  unsigned char xaxis[4], yaxis[4], zaxis[4];

  /* Gizmo Colors. */
  unsigned char gizmo_hi[4];
  unsigned char gizmo_primary[4];
  unsigned char gizmo_secondary[4];
  unsigned char gizmo_view_align[4];
  unsigned char gizmo_a[4];
  unsigned char gizmo_b[4];

  /* Icon Colors. */
  /* Scene items. */
  unsigned char icon_scene[4];
  /* Collection items. */
  unsigned char icon_collection[4];
  /* Ob items. */
  unsigned char icon_ob[4];
  /* Ob data items. */
  unsigned char icon_ob_data[4];
  /* Mod and constraint items. */
  unsigned char icon_modifier[4];
  /* Shading related items. */
  unsigned char icon_shading[4];
  /* File folders. */
  unsigned char icon_folder[4];
  /* Intensity of the border icons. >0 will render an border around themed
   * icons. */
  float icon_border_intensity;
  float pnl_roundness;
  char _pad2[4];

} ThemeUI;

/* try to put them all in one, if needed a special struct can be created as well
 * for example later on, when we introduce wire colors for ob types or so... */
typedef struct ThemeSpace {
  /* main win colors */
  unsigned char back[4];
  unsigned char back_grad[4];

  char background_type;
  char _pad0[3];

  /* Pnl title. */
  unsigned char title[4];
  unsigned char txt[4];
  unsigned char txt_hi[4];

  /* header colors */
  /* Rgn background. */
  unsigned char header[4];
  /* Unused. */
  unsigned char header_title[4];
  unsigned char header_txt[4];
  unsigned char header_txt_hi[4];

  /* rgn tabs */
  unsigned char tab_active[4];
  unsigned char tab_inactive[4];
  unsigned char tab_back[4];
  unsigned char tab_outline[4];

  /* btn/tool rgns */
  /* Rgn background. */
  unsigned char btn[4];
  /* Pnl title. */
  unsigned char btn_title[4];
  unsigned char btn_txt[4];
  unsigned char btn_txt_hi[4];

  /* listview rgns */
  /* Rgn background. */
  unsigned char list[4];
  /* Pnl title. */
  unsigned char list_title[4];
  unsigned char list_txt[4];
  unsigned char list_txt_hi[4];

  /* nav bar rgns */
  /* Rgn background. */
  unsigned char nav_bar[4];
  /* Rgn background. */
  unsigned char ex_btns[4];

  /* Cannot use name 'pnls' bc of Types mapping old files. */
  uiPanelColors pnlcolors;

  unsigned char shade1[4];
  unsigned char shade2[4];

  unsigned char hilite[4];
  unsigned char grid[4];

  unsigned char view_overlay[4];

  unsigned char wire[4], wire_edit[4], sel[4];
  unsigned char lamp[4], speaker[4], empty[4], camera[4];
  unsigned char active[4], group[4], group_active[4], transform[4];
  unsigned char vert[4], vert_sel[4], vert_active[4], vert_bevel[4],
      vert_unref[4];
  unsigned char edge[4], edge_sel[4];
  unsigned char edge_seam[4], edge_sharp[4], edge_facesel[4], edge_crease[4], edge_bevel[4];
  /* Solid faces. */
  unsigned char face[4], face_sel[4], face_back[4], face_front[4];
  /* Sel color. */
  unsigned char face_dot[4];
  unsigned char extra_edge_len[4], extra_edge_angle[4], extra_face_angle[4], extra_face_area[4];
  unsigned char normal[4];
  unsigned char vert_normal[4];
  unsigned char loop_normal[4];
  unsigned char bone_solid[4], bone_pose[4], bone_pose_active[4], bone_locked_weight[4];
  unsigned char strip[4], strip_select[4];
  unsigned char cframe[4];
  unsigned char time_keyframe[4], time_gp_keyframe[4];
  unsigned char freestyle_edge_mark[4], freestyle_face_mark[4];
  unsigned char time_scrub_background[4];
  unsigned char time_marker_line[4], time_marker_line_selected[4];

  unsigned char nurb_uline[4], nurb_vline[4];
  unsigned char act_spline[4], nurb_sel_uline[4], nurb_sel_vline[4], lastsel_point[4];

  unsigned char handle_free[4], handle_auto[4], handle_vect[4], handle_align[4],
      handle_auto_clamped[4];
  unsigned char handle_sel_free[4], handle_sel_auto[4], handle_sel_vect[4], handle_sel_align[4],
      handle_sel_auto_clamped[4];

  /* Dopesheet. */
  unsigned char ds_channel[4], ds_subchannel[4], ds_ipoline[4];
  /* Keytypes. */
  unsigned char keytype_keyframe[4], keytype_extreme[4], keytype_breakdown[4], keytype_jitter[4],
      keytype_movehold[4];
  /* Keytypes. */
  unsigned char keytype_keyframe_select[4], keytype_extreme_select[4], keytype_breakdown_sel[4],
      keytype_jitter_select[4], keytype_movehold_select[4];
  unsigned char keyborder[4], keyborder_select[4];
  char _pad4[3];

  unsigned char console_output[4], console_input[4], console_info[4], console_error[4];
  unsigned char console_cursor[4], console_select[4];

  unsigned char vert_size, outline_width, obcenter_dia, facedot_size;
  unsigned char noodle_curving;
  unsigned char grid_levels;
  char _pad5[3];
  float dash_alpha;

  /* syntax for txtwin and nodes */
  unsigned char syntaxl[4], syntaxs[4]; /* in nodespace used for backdrop matte */
  unsigned char syntaxb[4], syntaxn[4]; /* in nodespace used for color input */
  unsigned char syntaxv[4], syntaxc[4]; /* in nodespace used for converter group */
  unsigned char syntaxd[4], syntaxr[4]; /* in nodespace used for distort */

  unsigned char line_numbers[4];
  char _pad6[3];

  unsigned char nodeclass_output[4], nodeclass_filter[4];
  unsigned char nodeclass_vector[4], nodeclass_texture[4];
  unsigned char nodeclass_shader[4], nodeclass_script[4];
  unsigned char nodeclass_pattern[4], nodeclass_layout[4];
  unsigned char nodeclass_geometry[4], nodeclass_attribute[4];

  /* For Seq editor. */
  unsigned char movie[4], movieclip[4], mask[4], image[4], scene[4], audio[4];
  unsigned char effect[4], transition[4], meta[4], text_strip[4], color_strip[4];
  unsigned char active_strip[4], sel_strip[4];

  /* Dopesheet: scale factor for size of keyframes (i.e. height of channels). */
  char _pad7[1];
  float keyframe_scale_fac;

  unsigned char editmesh_active[4];

  unsigned char handle_vert[4];
  unsigned char handle_vert_sel[4];

  unsigned char handle_vert_size;

  unsigned char clipping_border_3d[4];

  unsigned char marker_outline[4], marker[4], act_marker[4], sel_marker[4], dis_marker[4],
      lock_marker[4];
  unsigned char bundle_solid[4];
  unsigned char path_before[4], path_after[4];
  unsigned char path_keyframe_before[4], path_keyframe_after[4];
  unsigned char camera_path[4];
  unsigned char _pad1[6];

  unsigned char pen_vertex_size;
  unsigned char pen_vertex[4], pen_vert_se[4];

  unsigned char preview_back[4];
  unsigned char preview_stitch_face[4];
  unsigned char preview_stitch_edge[4];
  unsigned char preview_stitch_vert[4];
  unsigned char preview_stitch_stitchable[4];
  unsigned char preview_stitch_unstitchable[4];
  unsigned char preview_stitch_active[4];

  /** Two uses, for uvs w mod applied on mesh and uvs during painting. */
  unsigned char uv_shadow[4];

  /* Search filter match, used for prop search and in the outliner. */
  unsigned char match[4];
  /* Outliner - sel item. */
  unsigned char sel_highlight[4];
  /* Outliner - sel object. */
  unsigned char sel_ob[4];
  /* Outliner - active ob. */
  unsigned char active_ob[4];
  /* Outliner - edited ob. */
  unsigned char edited_ob[4];
  /* Outliner - row color diff. */
  unsigned char row_alternate[4];

  /* Skin mod root color. */
  unsigned char skin_root[4];

  /* NLA */
  /* Active Action + Summary Channel. */
  unsigned char anim_active[4];
  /* Active Action = NULL. */
  unsigned char anim_non_active[4];
  /* Preview range overlay. */
  unsigned char anim_preview_range[4];

  /* NLA 'Tweaking' action/strip. */
  unsigned char nla_tweaking[4];
  /* NLA - warning color for dup instances of tweaking strip. */
  unsigned char nla_tweakdupli[4];

  /* NLA "Track" */
  unsigned char nla_track[4];
  /* NLA "Transition" strips. */
  unsigned char nla_transition[4], nla_transition_sel[4];
  /* NLA "Meta" strips. */
  unsigned char nla_meta[4], nla_meta_sel[4];
  /* NLA "Sound" strips. */
  unsigned char nla_sound[4], nla_sound_sel[4];

  /* info */
  unsigned char info_sel[4], info_sel_txt[4];
  unsigned char info_error[4], info_error_txt[4];
  unsigned char info_warning[4], info_warning_txt[4];
  unsigned char info_info[4], info_info_txt[4];
  unsigned char info_debug[4], info_debug_txt[4];
  unsigned char info_prop[4], info_prop_txt[4];
  unsigned char info_op[4], info_op_txt[4];

  unsigned char paint_curve_pivot[4];
  unsigned char paint_curve_handle[4];

  unsigned char metadatabg[4];
  unsigned char metadatatxt[4];

} ThemeSpace;

/* Viewport Background Gradient Types. */
typedef enum eBackgroundGradientTypes {
  TH_BACKGROUND_SINGLE_COLOR = 0,
  TH_BACKGROUND_GRADIENT_LINEAR = 1,
  TH_BACKGROUND_GRADIENT_RADIAL = 2,
} eBackgroundGradientTypes;

/* Set of colors for use as a custom color set for Objects/Bones wire drawing. */
typedef struct ThemeWireColor {
  unsigned char solid[4];
  unsigned char sel[4];
  unsigned char active[4];
  /* eWireColor_Flags. */
  short flag;
  char _pad0[2];
} ThemeWireColor;

/* ThemeWireColor.flag */
typedef enum eWireColorFlags {
  TH_WIRECOLOR_CONSTCOLS = (1 << 0),
  /* TH_WIRECOLOR_TXTCOLS = (1 << 1), */ /* UNUSED */
} eWireColor_Flags;

typedef struct ThemeCollectionColor {
  unsigned char color[4];
} ThemeCollectionColor;

typedef struct ThemeStripColor {
  unsigned char color[4];
} ThemeStripColor;

/* Currently only a single theme is ever used at once.
 * Diff theme presets are stored as external files now. */
typedef struct DuneTheme {
  struct DuneTheme *next, *prev;
  char name[32];
  ThemeUI tui;
  /* Individual Spacetypes:
   * Ensure UI_THEMESPACE_END is updated when adding. */
  ThemeSpace space_props;
  ThemeSpace space_view3d;
  ThemeSpace space_file;
  ThemeSpace space_graph;
  ThemeSpace space_info;
  ThemeSpace space_action;
  ThemeSpace space_nla;
  ThemeSpace space_seq;
  ThemeSpace space_img;
  ThemeSpace space_txt;
  ThemeSpace space_outliner;
  ThemeSpace space_node;
  ThemeSpace space_prefs;
  ThemeSpace space_console;
  ThemeSpace space_clip;
  ThemeSpace space_topbar;
  ThemeSpace space_statusbar;
  ThemeSpace space_spreadsheet;

  /* 20 sets of bone colors for this theme */
  ThemeWireColor tarm[20];
  // ThemeWireColor toob[20];

  /* See COLLECTION_COLOR_TOT for the num of collection colors. */
  ThemeCollectionColor collection_color[8];

  /* See SEQ_COLOR_TOT for the total num of strip colors. */
  ThemeStripColor strip_color[9];

  int active_theme_area;
} DuneTheme;

#define UI_THEMESPACE_START(dunetheme) \
  (CHECK_TYPE_INLINE(dunetheme, duneTheme *), &((dunetheme)->space_properties))
#define UI_THEMESPACE_END(dunetheme) \
  (CHECK_TYPE_INLINE(dunetheme, duneTheme *), (&((dunetheme)->space_spreadsheet) + 1))

typedef struct DuneAddon {
  struct DuneAddon *next, *prev;
  char module[64];
  /* User-Defined Props on this add-on (for storing preferences). */
  IdProp *prop;
} DuneAddon;

typedef struct DunePathCompare {
  struct DunePathCompare *next, *prev;
  /* FILE_MAXDIR. */
  char path[768];
  char flag;
  char _pad0[7];
} DunePathCompare;

typedef struct DuneUserMenu {
  struct DuneUserMenu *next, *prev;
  char space_type;
  char _pad0[7];
  char cxt[64];
  /* duneUserMenuItem */
  List items;
} DuneUserMenu;

/* May be part of DuneUserMenu or other list. */
typedef struct DuneUserMenuItem {
  struct DuneUserMenuItem *next, *prev;
  char ui_name[64];
  char type;
  char _pad0[7];
} DuneUserMenuItem;

typedef struct DuneUserMenuItemOp {
  DuneUserMenuItem item;
  char op_idname[64];
  struct IdProp *prop;
  char opcxt; /* WinOpCallCtx */
  char _pad0[7];
} DuneUserMenuItemOp;

typedef struct DuneUserMenuItemMenu {
  DuneUserMenuItem item;
  char mt_idname[64];
} DuneUserMenuItemMenu;

typedef struct DuneUserMenuItemProp {
  DuneUserMenuItem item;
  char cxt_data_path[256];
  char prop_id[64];
  int prop_index;
  char _pad0[4];
} DuneUserMenuItemProp;

enum {
  USER_MENU_TYPE_SEP = 1,
  USER_MENU_TYPE_O = 2,
  USER_MENU_TYPE_MENU = 3,
  USER_MENU_TYPE_PROP = 4,
};

typedef struct DuneUserAssetLib {
  struct DuneUserAssetLib *next, *prev;

  char name[64];   /* MAX_NAME */
  char path[1024]; /* FILE_MAX */
} DuneUserAssetLib;

typedef struct SolidLight {
  int flag;
  float smooth;
  char _pad0[8];
  float col[4], spec[4], vec[4];
} SolidLight;

typedef struct WalkNav {
  /* Speed factor for look around. */
  float mouse_speed;
  float walk_speed;
  float walk_speed_factor;
  float view_height;
  float jump_height;
  /* Duration to use for teleporting. */
  float teleport_time;
  short flag;
  char _pad0[6];
} WalkNav;

typedef struct UserDefRuntime {
  /* Mark as changed so the prefs are saved on exit. */
  char is_dirty;
  char _pad0[7];
} UserDefRuntime;

/* Store UI data here instead of the space
 * since the space is typically a win which is freed. */
typedef struct UserDefSpaceData {
  char section_active;
  /* eUserPrefSpaceDataFlag UI opts. */
  char flag;
  char _pad0[6];
} UserDefSpaceData;

/* Storage for UI data that to keep it even after the win was closed. (Similar to
 * UserDefSpaceData.) */
typedef struct UserDefFileSpaceData {
  int display_type;   /* FileSelParams.display */
  int thumbnail_size; /* FileSelParams.thumbnail_size */
  int sort_type;      /* FileSelParams.sort */
  int details_flags;  /* FileSelectParams.details_flags */
  int flag;           /* FileSelParams.flag */
  int _pad0;
  uint64_t filter_id; /* FileSelParams.filter_id */

  /* Info used when creating the file browser in a tmp win. */
  int tmp_win_sizex;
  int tmp_win_sizey;
} UserDefFileSpaceData;

typedef struct UserDefExperimental {
  /* Debug optss, always available. */
  char use_undo_legacy;
  char no_override_auto_resync;
  char use_cycles_debug;
  char show_asset_debug_info;
  char no_asset_indexing;
  char SANITIZE_AFTER_HERE;
  /* The following opts are automatically sanitized (set to 0)
   * when the release cycle is not alpha. */
  char use_new_curves_type;
  char use_new_point_cloud_type;
  char use_full_frame_compositor;
  char use_sculpt_vert_colors;
  char use_sculpt_tools_tilt;
  char use_extended_asset_browser;
  char use_override_templates;
  char use_named_attribute_nodes;
  char enable_eevee_next;
  char _pad[1];
  /* `types` does not allow empty structs. */
} UserDefExperimental;

#define USER_EXPERIMENTAL_TEST(userdef, member) \
  (((userdef)->flag & USER_DEVELOPER_UI) && ((userdef)->experimental).member)

typedef struct UserDef {
  /* UserDef has separate do-version handling, and can be read from other files. */
  int versionfile, subversionfile;
  /* eUserPrefFlag. */
  int flag;
  /* eDupIdFlags. */
  unsigned int dupflag;
  /* eUserPrefPrefFlag prefs for the preferences. */
  char pref_flag;
  char savetime;
  char mouse_emulate_3_btn_mod;
  char _pad4[1];
  /* FILE_MAXDIR length. */
  char tmpdir[768];
  char fontdir[768];
  /* FILE_MAX length. */
  char rndrdir[1024];
  /* EXR cache path */
  /* 768 = FILE_MAXDIR. */
  char rndr_cachedir[768];
  char textudir[768];
  /* Optional user location for scripts.
   *
   * This supports the same layout as Dune's scripts directory `release/scripts`.
   *
   * Unlike most paths, changing this is not fully supported at run-time,
   * requiring a restart to properly take effect. Supporting this would cause complications as
   * the script path can contain `startup`, `addons` & `modules` etc. properly unwinding the
   * Python environment to the state it _would_ have been in gets complicated.
   *
   * Altho this is partially supported as the `sys.path` is refreshed when loading prefs.
   * This is done to support PREFS_OT_copy_prev which is available to the user when they
   * launch with a new version of Dune. In this case setting the script path on top of
   * factory settings will work wo problems. */
  char pythondir[768];
  char sounddir[768];
  char langdir[768];
  /* 1024 = FILE_MAX. */
  char img_editor[1024];
  /* 1024 = FILE_MAX. */
  char anim_player[1024];
  int anim_player_preset;

  /* Min spacing between grid-lines in View2D grids. */
  short v2d_min_gridsize;
  /* eTimecodeStyles, style of time-code display. */
  short timecode_style;
  short versions;
  short dbl_click_time;
  char _pad0[3];
  char mini_axis_type;
  /* eUserprefUIFlag. */
  int uiflag;
  /* eUserprefUIFlag2. */
  char uiflag2;
  char gpu_flag;
  char _pad8[6];
  /* Experimental flag for app-templates to make changes to behavior
   * which are outside the scope of typical prefs. */
  char app_flag;
  char viewzoom;
  short language;
  int mixbufsize;
  int audiodevice;
  int audiorate;
  int audioformat;
  int audiochannels;
  /* Setting for UI scale (fractional), before screen DPI has been applied. */
  float ui_scale;
  /* Setting for UI line width. */
  int ui_line_width;
  /* Runtime, full DPI divided by `pixelsize`. */
  int dpi;
  /* Runtime, multiplier to scale UI elements based on DPI (fractional). */
  float dpi_fac;
  /* Runtime, `1.0 / dpi_fac` */
  float inv_dpi_fac;
  /* Runtime, calc from line-width and point-size based on DPI (rounded to int). */
  float pixelsize;
  /* Deprecated, for forward compatibility. */
  int virtual_pixel;
  /* Console scroll-back limit. */
  int scrollback;
  /* Node insert offset (aka auto-offset) margin, but might be useful for later stuff as well. */
  char node_margin;
  char _pad2[1];
  /* eUserprefTranslationFlags. */
  short transopts;
  short menuthreshold1, menuthreshold2;
  /* Startup application template. */
  char app_template[64];
  struct List themes;
  struct List uifonts;
  struct List uistyles;
  struct List user_keymaps;
  /* WinKeyConfigPref. */
  struct List user_keyconfig_prefs;
  struct List addons;
  struct List autoex_paths;
  /* duneUserMenu. */
  struct List user_menus;
  /* duneUserAssetLib */
  struct List asset_libs;

  char keyconfigstr[64];

  short undosteps;
  char _pad1[2];
  int undomem;
  float gpu_viewport_quality TYPES_DEPRECATED;
  short pen_manhattandist, pen_euclideandist, pen_eraser;
  /* ePenUserdefSettings. */
  short pen_settings;
  char _pad13[4];
  struct SolidLight light_param[4];
  float light_ambient[3];
  char gizmo_flag;
  /* Generic gizmo size. */
  char gizmo_size;
  /* Nav gizmo size. */
  char gizmo_size_nav_v3d;
  char _pad3[5];
  short edit_studio_light;
  short lookdev_sphere_size;
  short vbotimeout, vbocollectrate;
  short textimeout, texcollectrate;
  int memcachelimit;
  /* Unused. */
  int prefetchframes;
  /* Ctrl the rotation step of the view when PAD2, PAD4, PAD6&PAD8 is use. */
  float pad_rot_angle;
  char _pad12[4];
  /* Rotating view icon size. */
  short rvisize;
  /* Rotating view icon brightness. */
  short rvibright;
  /* Max num of recently used files to remember. */
  short recent_files;
  /* Milliseconds to spend spinning the view. */
  short smooth_viewtx;
  short glreslimit;
  /* eColorPickerTypes. */
  short color_picker_type;
  /* Curve smoothing type for newly added F-Curves. */
  char auto_smoothing_new;
  /* Interpolation mode for newly added F-Curves. */
  char ipo_new;
  /** Handle types for newly added keyframes. */
  char keyhandles_new;
  char _pad11[4];
  /* eZoomFrameMode. */
  char view_frame_type;
  /* Number of keyframes to zoom around current frame. */
  int view_frame_keyframes;
  /* Seconds to zoom around current frame. */
  float view_frame_seconds;
  char _pad7[6];
  /* Private, defaults to 20 for 72 DPI setting. */
  short widget_unit;
  short anisotropic_filter;
  /*Tablet API to use (Wins only). */
  short tablet_api;
  /* Raw tablet pressure that maps to 100%. */
  float pressure_threshold_max;
  /* Curve non-linearity param. */
  float pressure_softness;
  /* Overall sensitivity of 3D mouse. */
  float ndof_sensitivity;
  float ndof_orbit_sensitivity;
  /* Dead-zone of 3D mouse. */
  float ndof_deadzone;
  /* eNdof_Flag, flags for 3D mouse. */
  int ndof_flag;
  /* eMultiSampleType, amount of samples for OpenGL FSA, if zero no FSA. */
  short ogl_multisamples;
  /* eImgDrwMethod, Method to be used to drw the imgs
   * (AUTO, GLSL, Textures or DrwPixels) */
  short img_drw_method;
  float glalphaclip;
  /* eAutokey_Mode, autokeying mode. */
  short autokey_mode;
  /* Flags for autokeying. */
  short autokey_flag;
  /* Flags for anim. */
  short anim_flag;
  /* Options for txt rendering. */
  char txt_render;
  char nav_mode;
  /** Turn-table rotation amount per-pixel in radians. Scaled with DPI. */
  float view_rotate_sensitivity_turntable;
  /** Track-ball rotation scale. */
  float view_rotate_sensitivity_trackball;
  /** From texture.h. */
  struct ColorBand coba_weight;
  float sculpt_paint_overlay_col[3];
  /** Default color for newly created Grease Pencil layers. */
  float pen_new_layer_col[4];
  /** Drag pixels (scaled by DPI). */
  char drag_threshold_mouse;
  char drag_threshold_tablet;
  char drag_threshold;
  char move_threshold;
  char font_path_ui[1024];
  char font_path_ui_mono[1024];
  /** Legacy, for backwards compatibility only. */
  int compute_device_type;
  /** Opacity of inactive F-Curves in F-Curve Editor. */
  float fcu_inactive_alpha;
  /* If keeping a pie menu spawn button pressed after this time,
   * it turns into a drag/release pie menu. */
  short pie_tap_timeout;
  /* Direction in the pie menu will always be calculated from the
   * initial position within this time limit.   */
  short pie_initial_timeout;
  short pie_animation_timeout;
  short pie_menu_confirm;
  /** Pie menu radius. */
  short pie_menu_radius;
  /** Pie menu distance from center before a direction is set. */
  short pie_menu_threshold;

  short opensubdiv_compute_type;
  short _pad6;

  char factor_display_type;

  char viewport_aa;

  char render_display_type;      /* eUserpref_RenderDisplayType */
  char filebrowser_display_type; /* eUserpref_TempSpaceDisplayType */

  char seq_disk_cache_dir[1024];
  int seq_disk_cache_compression; /* eUserpref_DiskCacheCompression */
  int seq_disk_cache_size_limit;
  short seq_disk_cache_flag;
  short seq_proxy_setup; /* eUserpref_SeqProxySetup */

  float collection_instance_empty_size;
  char text_flag;
  char _pad10[1];

  char file_preview_type; /* eUserpref_File_Preview_Type */
  char statusbar_flag;    /* eUserpref_StatusBar_Flag */

  struct WalkNav walk_nav;

  /** The UI for the user preferences. */
  UserDefSpaceData space_data;
  UserDefFileSpaceData file_space_data;

  UserDefExperimental experimental;

  /** Runtime data (keep last). */
  UserDefRuntime runtime;
} UserDef;

/** From dune_kernel `dune.c`. */
extern UserDef U;

/* ***************** USERDEF ****************** */

/* Toggles for unfinished 2.8 UserPref design. */
//#define WITH_USERDEF_WORKSPACES

/** UserDefSpaceData.section_active (UI active_section) */
typedef enum eUserPrefSection {
  USER_SECTION_INTERFACE = 0,
  USER_SECTION_EDITING = 1,
  USER_SECTION_SAVE_LOAD = 2,
  USER_SECTION_SYSTEM = 3,
  USER_SECTION_THEME = 4,
  USER_SECTION_INPUT = 5,
  USER_SECTION_ADDONS = 6,
  USER_SECTION_LIGHT = 7,
  USER_SECTION_KEYMAP = 8,
#ifdef WITH_USERDEF_WORKSPACES
  USER_SECTION_WORKSPACE_CONFIG = 9,
  USER_SECTION_WORKSPACE_ADDONS = 10,
  USER_SECTION_WORKSPACE_KEYMAPS = 11,
#endif
  USER_SECTION_VIEWPORT = 12,
  USER_SECTION_ANIMATION = 13,
  USER_SECTION_NAVIGATION = 14,
  USER_SECTION_FILE_PATHS = 15,
  USER_SECTION_EXPERIMENTAL = 16,
} eUserPrefSection;

/** UserDefSpaceData.flag (State of the user preferences UI). */
typedef enum eUserPrefSpaceDataFlag {
  /** Hide/expand key-map preferences. */
  USER_SPACEDATA_INPUT_HIDE_UI_KEYCONFIG = (1 << 0),
  USER_SPACEDATA_ADDONS_SHOW_ONLY_ENABLED = (1 << 1),
} eUserPrefSpaceDataFlag;

/** UserDef.flag */
typedef enum eUserPrefFlag {
  USER_AUTOSAVE = (1 << 0),
  USER_FLAG_NUMINPUT_ADVANCED = (1 << 1),
  USER_FLAG_UNUSED_2 = (1 << 2), /* cleared */
  USER_FLAG_UNUSED_3 = (1 << 3), /* cleared */
  USER_FLAG_UNUSED_4 = (1 << 4), /* cleared */
  USER_TRACKBALL = (1 << 5),
  USER_FLAG_UNUSED_6 = (1 << 6), /* cleared */
  USER_FLAG_UNUSED_7 = (1 << 7), /* cleared */
  USER_MAT_ON_OB = (1 << 8),
  USER_FLAG_UNUSED_9 = (1 << 9), /* cleared */
  USER_DEVELOPER_UI = (1 << 10),
  USER_TOOLTIPS = (1 << 11),
  USER_TWOBUTTONMOUSE = (1 << 12),
  USER_NONUMPAD = (1 << 13),
  USER_ADD_CURSORALIGNED = (1 << 14),
  USER_FILECOMPRESS = (1 << 15),
  USER_FLAG_UNUSED_5 = (1 << 16), /* dirty */
  USER_CUSTOM_RANGE = (1 << 17),
  USER_ADD_EDITMODE = (1 << 18),
  USER_ADD_VIEWALIGNED = (1 << 19),
  USER_RELPATHS = (1 << 20),
  USER_RELEASECONFIRM = (1 << 21),
  USER_SCRIPT_AUTOEXEC_DISABLE = (1 << 22),
  USER_FILENOUI = (1 << 23),
  USER_NONEGFRAMES = (1 << 24),
  USER_TXT_TABSTOSPACES_DISABLE = (1 << 25),
  USER_TOOLTIPS_PYTHON = (1 << 26),
  USER_FLAG_UNUSED_27 = (1 << 27), /* dirty */
} eUserPrefFlag;

/** UserDef.file_preview_type */
typedef enum eUserprefFilePreviewType {
  USER_FILE_PREVIEW_NONE = 0,
  USER_FILE_PREVIEW_AUTO,
  USER_FILE_PREVIEW_SCREENSHOT,
  USER_FILE_PREVIEW_CAMERA,
} eUserprefFilePreviewType;

typedef enum eUserPrefPrefFlag {
  USER_PREF_FLAG_SAVE = (1 << 0),
} eUserPrefPrefFlag;

/** dunePathCompare.flag */
typedef enum ePathCompareFlag {
  USER_PATHCMP_GLOB = (1 << 0),
} ePathCompareFlag;

/* Helper macro for checking frame clamping */
#define FRAMENUMBER_MIN_CLAMP(cfra) \
  { \
    if ((U.flag & USER_NONEGFRAMES) && (cfra < 0)) { \
      cfra = 0; \
    } \
  } \
  (void)0

/** UserDef.viewzoom */
typedef enum eViewZoomStyle {
  /** Update zoom continuously with a timer while dragging the cursor. */
  USER_ZOOM_CONTINUE = 0,
  /** Map changes in distance from the view center to zoom. */
  USER_ZOOM_SCALE = 1,
  /** Map horizontal/vertical motion to zoom. */
  USER_ZOOM_DOLLY = 2,
} eViewZoomStyle;

/** UserDef.navigation_mode */
typedef enum eViewNavMethod {
  VIEW_NAVIGATION_WALK = 0,
  VIEW_NAVIGATION_FLY = 1,
} eViewNavigationMethod;

/** UserDef.uiflag */
typedef enum eUserprefMiniAxisType {
  USER_MINI_AXIS_TYPE_GIZMO = 0,
  USER_MINI_AXIS_TYPE_MINIMAL = 1,
  USER_MINI_AXIS_TYPE_NONE = 2,
} eUserprefMiniAxisType;

/** UserDef.flag */
typedef enum eWalkNavFlag {
  USER_WALK_GRAVITY = (1 << 0),
  USER_WALK_MOUSE_REVERSE = (1 << 1),
} eWalkNavigationFlag;

/** UserDef.uiflag */
typedef enum eUserprefUIFlag {
  USER_UIFLAG_UNUSED_0 = (1 << 0), /* cleared */
  USER_UIFLAG_UNUSED_1 = (1 << 1), /* cleared */
  USER_WHEELZOOMDIR = (1 << 2),
  USER_FILTERFILEEXTS = (1 << 3),
  USER_DRAWVIEWINFO = (1 << 4),
  USER_PLAINMENUS = (1 << 5),
  USER_LOCK_CURSOR_ADJUST = (1 << 6),
  USER_HEADER_BOTTOM = (1 << 7),
  /** Otherwise use header alignment from the file. */
  USER_HEADER_FROM_PREF = (1 << 8),
  USER_MENUOPENAUTO = (1 << 9),
  USER_DEPTH_CURSOR = (1 << 10),
  USER_AUTOPERSP = (1 << 11),
  USER_UIFLAG_UNUSED_12 = (1 << 12), /* cleared */
  USER_GLOBALUNDO = (1 << 13),
  USER_ORBIT_SELECTION = (1 << 14),
  USER_DEPTH_NAVIGATE = (1 << 15),
  USER_HIDE_DOT = (1 << 16),
  USER_SHOW_GIZMO_NAVIGATE = (1 << 17),
  USER_SHOW_VIEWPORTNAME = (1 << 18),
  USER_UIFLAG_UNUSED_3 = (1 << 19), /* Cleared. */
  USER_ZOOM_TO_MOUSEPOS = (1 << 20),
  USER_SHOW_FPS = (1 << 21),
  USER_UIFLAG_UNUSED_22 = (1 << 22), /* cleared */
  USER_MENUFIXEDORDER = (1 << 23),
  USER_CONTINUOUS_MOUSE = (1 << 24),
  USER_ZOOM_INVERT = (1 << 25),
  USER_ZOOM_HORIZ = (1 << 26), /* for CONTINUE and DOLLY zoom */
  USER_SPLASH_DISABLE = (1 << 27),
  USER_HIDE_RECENT = (1 << 28),
#ifdef TYPES_DEPRECATED_ALLOW
  /* Deprecated: We're just trying if there's much desire for this feature,
   * or if we can make it go for good. Should be cleared if so - Julian, Oct. 2019. */
  USER_SHOW_THUMBNAILS = (1 << 29),
#endif
  USER_SAVE_PROMPT = (1 << 30),
  USER_HIDE_SYSTEM_BOOKMARKS = (1u << 31),
} eUserprefUIFlag;

/** UserDef.uiflag2
 * note don't add new flags here, use 'uiflag' which has flags free. */
typedef enum eUserprefUIFlag2 {
  USER_UIFLAG2_UNUSED_0 = (1 << 0), /* cleared */
  USER_REGION_OVERLAP = (1 << 1),
  USER_UIFLAG2_UNUSED_2 = (1 << 2),
  USER_UIFLAG2_UNUSED_3 = (1 << 3), /* dirty */
} eUserprefUIFlag2;

/** UserDef.gpuflag */
typedef enum eUserprefGPUFlag {
  USER_GPU_FLAG_NO_DEPT_PICK = (1 << 0),
  USER_GPU_FLAG_NO_EDIT_MODE_SMOOTH_WIRE = (1 << 1),
  USER_GPU_FLAG_OVERLAY_SMOOTH_WIRE = (1 << 2),
  USER_GPU_FLAG_SUBDIVISION_EVAL = (1 << 3),
} eUserprefGPUFlag;

/** UserDef.tablet_api */
typedef enum eUserprefTableAPI {
  USER_TABLET_AUTOMATIC = 0,
  USER_TABLET_NATIVE = 1,
  USER_TABLET_WINTAB = 2,
} eUserprefTabletAPI;

/** UserDef.app_flag */
typedef enum eUserprefAPPFlag {
  USER_APP_LOCK_CORNER_SPLIT = (1 << 0),
  USER_APP_HIDE_REGION_TOGGLE = (1 << 1),
  USER_APP_LOCK_EDGE_RESIZE = (1 << 2),
} eUserprefAPPFlag;

/** UserDef.statusbar_flag */
typedef enum eUserprefStatusBarFlag {
  STATUSBAR_SHOW_MEMORY = (1 << 0),
  STATUSBAR_SHOW_VRAM = (1 << 1),
  STATUSBAR_SHOW_STATS = (1 << 2),
  STATUSBAR_SHOW_VERSION = (1 << 3),
} eUserprefStatusBarFlag;

/** Auto-Keying mode.
 * UserDef.autokey_mode */
typedef enum eAutokeyMode {
  /* AUTOKEY_ON is a bitflag */
  AUTOKEY_ON = 1,

  /* AUTOKEY_ON + 2**n...  (i.e. AUTOKEY_MODE_NORMAL = AUTOKEY_ON + 2)
   * to preserve setting, even when auto-key turned off. */
  AUTOKEY_MODE_NORMAL = 3,
  AUTOKEY_MODE_EDITKEYS = 5,
} eAutokeyMode;

/** Zoom to frame mode.
 * UserDef.view_frame_type */
typedef enum eZoomFrame_Mode {
  ZOOM_FRAME_MODE_KEEP_RANGE = 0,
  ZOOM_FRAME_MODE_SECONDS = 1,
  ZOOM_FRAME_MODE_KEYFRAMES = 2,
} eZoomFrame_Mode;

/* Auto-Keying flag
 * UserDef.autokey_flag (not strictly used when autokeying only -
 * is also used when keyframing these days).
 * eAutokey_Flag is used with a macro, search for lines like IS_AUTOKEY_FLAG(INSERTAVAIL) */
typedef enum eAutokeyFlag {
  AUTOKEY_FLAG_INSERTAVAIL = (1 << 0),
  AUTOKEY_FLAG_INSERTNEEDED = (1 << 1),
  AUTOKEY_FLAG_AUTOMATKEY = (1 << 2),
  AUTOKEY_FLAG_XYZ2RGB = (1 << 3),

  /* toolsettings->autokey_flag */
  AUTOKEY_FLAG_ONLYKEYINGSET = (1 << 6),
  AUTOKEY_FLAG_NOWARNING = (1 << 7),
  AUTOKEY_FLAG_CYCLEAWARE = (1 << 8),
  ANIMRECORD_FLAG_WITHNLA = (1 << 10),
} eAutokeyFlag;

/** Animation flags
 * UserDef.anim_flag, used for animation flags that aren't covered by more specific flags
 * (like eAutokey_Flag). */
typedef enum eUserprefAnimFlags {
  USER_ANIM_SHOW_CHANNEL_GROUP_COLORS = (1 << 0),
} eUserprefAnimFlags;

/** UserDef.transopts */
typedef enum eUserprefTranslationFlags {
  USER_TR_TOOLTIPS = (1 << 0),
  USER_TR_IFACE = (1 << 1),
  USER_TR_UNUSED_2 = (1 << 2),            /* cleared */
  USER_TR_UNUSED_3 = (1 << 3),            /* cleared */
  USER_TR_UNUSED_4 = (1 << 4),            /* cleared */
  USER_DOTRANSLATE_DEPRECATED = (1 << 5), /* Deprecated in 2.83. */
  USER_TR_UNUSED_6 = (1 << 6),            /* cleared */
  USER_TR_UNUSED_7 = (1 << 7),            /* cleared */
  USER_TR_NEWDATANAME = (1 << 8),
} eUserprefTranslationFlags;

/** Text Editor options
 * UserDef.text_flag */
typedef enum eTextEditFlags {
  USER_TEXT_EDIT_AUTO_CLOSE = (1 << 0),
} eTextEditFlags;

/** Text draw options
 * UserDef.text_render */
typedef enum eTextDrawOptions {
  USER_TEXT_DISABLE_AA = (1 << 0),
  USER_TEXT_HINTING_NONE = (1 << 1),
  USER_TEXT_HINTING_SLIGHT = (1 << 2),
  USER_TEXT_HINTING_FULL = (1 << 3),
} eTextDrawOptions;

/* Pen Settings.
 * UserDef.pen_settings */
typedef enum ePenUserdefSettings {
  PEN_PAINT_UNUSED_0 = (1 << 0),
} ePenUserdefSettings;

enum {
  USER_GIZMO_DRAW = (1 << 0),
};

/* Color Picker Types.
 * UserDef.color_picker_type */
typedef enum eColorPickerTypes {
  USER_CP_CIRCLE_HSV = 0,
  USER_CP_SQUARE_SV = 1,
  USER_CP_SQUARE_HS = 2,
  USER_CP_SQUARE_HV = 3,
  USER_CP_CIRCLE_HSL = 4,
} eColorPicker_Types;

/** Time-code display styles.
 * UserDef.timecode_style */
typedef enum eTimecodeStyles {
  /* As little info as is necessary to show relevant info with '+' to denote the frames
   * i.e. HH:MM:SS+FF, MM:SS+FF, SS+FF, or MM:SS. */
  USER_TIMECODE_MINIMAL = 0,
  /** Reduced SMPTE - (HH:)MM:SS:FF */
  USER_TIMECODE_SMPTE_MSF = 1,
  /** Full SMPTE - HH:MM:SS:FF */
  USER_TIMECODE_SMPTE_FULL = 2,
  /** Milliseconds for sub-frames - HH:MM:SS.sss. */
  USER_TIMECODE_MILLISECONDS = 3,
  /** Seconds only. */
  USER_TIMECODE_SECONDS_ONLY = 4,
  /* Private (not exposed as generic choices) options.
   * milliseconds for sub-frames, SubRip format- HH:MM:SS,sss. */
  USER_TIMECODE_SUBRIP = 100,
} eTimecodeStyles;

/** UserDef.ndof_flag (3D mouse options) */
typedef enum eNdofFlag {
  NDOF_SHOW_GUIDE = (1 << 0),
  NDOF_FLY_HELICOPTER = (1 << 1),
  NDOF_LOCK_HORIZON = (1 << 2),

  /* The following might not need to be saved between sessions,
   * but they do need to live somewhere accessible. */
  NDOF_SHOULD_PAN = (1 << 3),
  NDOF_SHOULD_ZOOM = (1 << 4),
  NDOF_SHOULD_ROTATE = (1 << 5),

  /* Orbit nav modes. */

  NDOF_MODE_ORBIT = (1 << 6),

  /* actually... users probably don't care about what the mode
   * is called, just that it feels right */
  /* zoom is up/down if this flag is set (otherwise forward/backward) */
  NDOF_PAN_YZ_SWAP_AXIS = (1 << 7),
  NDOF_ZOOM_INVERT = (1 << 8),
  NDOF_ROTX_INVERT_AXIS = (1 << 9),
  NDOF_ROTY_INVERT_AXIS = (1 << 10),
  NDOF_ROTZ_INVERT_AXIS = (1 << 11),
  NDOF_PANX_INVERT_AXIS = (1 << 12),
  NDOF_PANY_INVERT_AXIS = (1 << 13),
  NDOF_PANZ_INVERT_AXIS = (1 << 14),
  NDOF_TURNTABLE = (1 << 15),
  NDOF_CAMERA_PAN_ZOOM = (1 << 16),
} eNdofFlag;

#define NDOF_PIXELS_PER_SECOND 600.0f

/** UserDef.ogl_multisamples */
typedef enum eMultiSample_Type {
  USER_MULTISAMPLE_NONE = 0,
  USER_MULTISAMPLE_2 = 2,
  USER_MULTISAMPLE_4 = 4,
  USER_MULTISAMPLE_8 = 8,
  USER_MULTISAMPLE_16 = 16,
} eMultiSample_Type;

/** UserDef.image_draw_method */
typedef enum eImageDrawMethod {
  IMAGE_DRAW_METHOD_AUTO = 0,
  IMAGE_DRAW_METHOD_GLSL = 1,
  IMAGE_DRAW_METHOD_2DTEXTURE = 2,
} eImageDrawMethod;

/** UserDef.virtual_pixel */
typedef enum eUserprefVirtualPixel {
  VIRTUAL_PIXEL_NATIVE = 0,
  VIRTUAL_PIXEL_DOUBLE = 1,
} eUserprefVirtualPixel;

typedef enum eOpensubdivComputeType {
  USER_OPENSUBDIV_COMPUTE_NONE = 0,
  USER_OPENSUBDIV_COMPUTE_CPU = 1,
  USER_OPENSUBDIV_COMPUTE_OPENMP = 2,
  USER_OPENSUBDIV_COMPUTE_OPENCL = 3,
  USER_OPENSUBDIV_COMPUTE_CUDA = 4,
  USER_OPENSUBDIV_COMPUTE_GLSL_TRANSFORM_FEEDBACK = 5,
  USER_OPENSUBDIV_COMPUTE_GLSL_COMPUTE = 6,
} eOpensubdivComputeType;

/** UserDef.factor_display_type */
typedef enum eUserprefFactorDisplay {
  USER_FACTOR_AS_FACTOR = 0,
  USER_FACTOR_AS_PERCENTAGE = 1,
} eUserprefFactorDisplay;

typedef enum eUserprefRenderDisplayType {
  USER_RENDER_DISPLAY_NONE = 0,
  USER_RENDER_DISPLAY_SCREEN = 1,
  USER_RENDER_DISPLAY_AREA = 2,
  USER_RENDER_DISPLAY_WINDOW = 3
} eUserprefRenderDisplayType;

typedef enum eUserprefTempSpaceDisplayType {
  USER_TEMP_SPACE_DISPLAY_FULLSCREEN = 0,
  USER_TEMP_SPACE_DISPLAY_WINDOW = 1,
} eUserprefTempSpaceDisplayType;

typedef enum eUserprefEmulateMMBMod {
  USER_EMU_MMB_MOD_ALT = 0,
  USER_EMU_MMB_MOD_OSKEY = 1,
} eUserprefEmulateMMBMod;

typedef enum eUserprefDiskCacheCompression {
  USER_SEQ_DISK_CACHE_COMPRESSION_NONE = 0,
  USER_SEQ_DISK_CACHE_COMPRESSION_LOW = 1,
  USER_SEQ_DISK_CACHE_COMPRESSION_HIGH = 2,
} eUserprefDiskCacheCompression;

typedef enum eUserprefSeqProxySetup {
  USER_SEQ_PROXY_SETUP_MANUAL = 0,
  USER_SEQ_PROXY_SETUP_AUTOMATIC = 1,
} eUserprefSeqProxySetup;

/* Locale Ids. Auto will try to get local from OS. Our default is English though. */
/** UserDef.lang */
enum {
  ULANG_AUTO = 0,
  ULANG_ENGLISH = 1,
};
