/* These define have its origin at sgi, where all device defines were written down in device.h.
 * Dune copied the conventions quite some, and expanded it with internal new defines (ton) */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* WinEv.customdata type */
enum {
  EV_DATA_TIMER = 2,
  EV_DATA_DRAGDROP = 3,
  EV_DATA_NDOF_MOTION = 4,
  EV_DATA_XR = 5,
};

/* WinTabletData.active tablet active, matches GHOST_TTabletMode.
 * Typically access via `ev->tablet.active`. */
enum {
  EV_TABLET_NONE = 0,
  EV_TABLET_STYLUS = 1,
  EV_TABLET_ERASER = 2,
};

/* WinEv.type
 * Also used for WinKeyMapItem.type which is saved in key-map files,
 * do not change the values of existing values which can be used in key-maps. */
enum {
  /* non-ev, for example disabled timer */
  EV_NONE = 0x0000,

  /* Start of Input devices. */

  /* MOUSE: 0x000x, 0x001x */
  LEFTMOUSE = 0x0001,
  MIDDLEMOUSE = 0x0002,
  RIGHTMOUSE = 0x0003,
  MOUSEMOVE = 0x0004,
  /* Extra mouse buttons */
  BTN4MOUSE = 0x0007,
  BTN5MOUSE = 0x0008,
  /* More mouse buttons - can't use 9 and 10 here (wheel) */
  BTN6MOUSE = 0x0012,
  BTN7MOUSE = 0x0013,
  /* Extra track-pad gestures. */
  MOUSEPAN = 0x000e,
  MOUSEZOOM = 0x000f,
  MOUSEROTATE = 0x0010,
  MOUSESMARTZOOM = 0x0017,

  /* defaults from ghost */
  WHEELUPMOUSE = 0x000a,
  WHEELDOWNMOUSE = 0x000b,
  /* mapped with userdef */
  WHEELINMOUSE = 0x000c,
  WHEELOUTMOUSE = 0x000d,
  /* Successive MOUSEMOVE's are converted to this, so we can easily
   * ignore all but the most recent MOUSEMOVE (for better performance),
   * paint and drawing tools however will want to handle these. */
  INBETWEEN_MOUSEMOVE = 0x0011,

  /* IME ev, GHOST_kEventImeCompositionStart in ghost */
  WM_IME_COMPOSITE_START = 0x0014,
  /* IME ev, GHOST_kEventImeComposition in ghost */
  WM_IME_COMPOSITE_EVENT = 0x0015,
  /* IME ev, GHOST_kEventImeCompositionEnd in ghost */
  WM_IME_COMPOSITE_END = 0x0016,

  /* Tablet/Pen Specific Evs */
  TABLET_STYLUS = 0x001a,
  TABLET_ERASER = 0x001b,

/* Start of keyboard codes. */
/* Minimum keyboard val (inclusive). */
#define _EV_KEYBOARD_MIN 0x0020

  /* Standard keyboard.
   * - 0x0020 to 0x00ff [#_EV_KEYBOARD_MIN to #_EV_KEYBOARD_MAX] inclusive - for keys.
   * - 0x012c to 0x0143 [#EV_F1KEY to #EV_F24KEY] inclusive - for function keys. */

  EV_ZEROKEY = 0x0030,  /* '0' (48). */
  EV_ONEKEY = 0x0031,   /* '1' (49). */
  EV_TWOKEY = 0x0032,   /* '2' (50). */
  EV_THREEKEY = 0x0033, /* '3' (51). */
  EV_FOURKEY = 0x0034,  /* '4' (52). */
  EV_FIVEKEY = 0x0035,  /* '5' (53). */
  EV_SIXKEY = 0x0036,   /* '6' (54). */
  EV_SEVENKEY = 0x0037, /* '7' (55). */
  EV_EIGHTKEY = 0x0038, /* '8' (56). */
  EV_NINEKEY = 0x0039,  /* '9' (57). */

  EV_AKEY = 0x0061, /* 'a' (97). */
  EV_BKEY = 0x0062, /* 'b' (98). */
  EV_CKEY = 0x0063, /* 'c' (99). */
  EV_DKEY = 0x0064, /* 'd' (100). */
  EV_EKEY = 0x0065, /* 'e' (101). */
  EV_FKEY = 0x0066, /* 'f' (102). */
  EV_GKEY = 0x0067, /* 'g' (103). */
  EV_HKEY = 0x0068, /* 'h' (104). */
  EV_IKEY = 0x0069, /* 'i' (105). */
  EV_JKEY = 0x006a, /* 'j' (106). */
  EV_KKEY = 0x006b, /* 'k' (107). */
  EV_LKEY = 0x006c, /* 'l' (108). */
  EV_MKEY = 0x006d, /* 'm' (109). */
  EV_NKEY = 0x006e, /* 'n' (110). */
  EV_OKEY = 0x006f, /* 'o' (111). */
  EV_PKEY = 0x0070, /* 'p' (112). */
  EV_QKEY = 0x0071, /* 'q' (113). */
  EV_RKEY = 0x0072, /* 'r' (114). */
  EV_SKEY = 0x0073, /* 's' (115). */
  EV_TKEY = 0x0074, /* 't' (116). */
  EV_UKEY = 0x0075, /* 'u' (117). */
  EV_VKEY = 0x0076, /* 'v' (118). */
  EV_WKEY = 0x0077, /* 'w' (119). */
  EVT_XKEY = 0x0078, /* 'x' (120). */
  EV_YKEY = 0x0079, /* 'y' (121). */
  EV_ZKEY = 0x007a, /* 'z' (122). */

  EV_LEFTARROWKEY = 0x0089,  /* 137 */
  EV_DOWNARROWKEY = 0x008a,  /* 138 */
  EV_RIGHTARROWKEY = 0x008b, /* 139 */
  EV_UPARROWKEY = 0x008c,    /* 140 */

  EVT_PAD0 = 0x0096, /* 150 */
  EVT_PAD1 = 0x0097, /* 151 */
  EVT_PAD2 = 0x0098, /* 152 */
  EVT_PAD3 = 0x0099, /* 153 */
  EV_PAD4 = 0x009a, /* 154 */
  EV_PAD5 = 0x009b, /* 155 */
  EV_PAD6 = 0x009c, /* 156 */
  EV_PAD7 = 0x009d, /* 157 */
  EV_PAD8 = 0x009e, /* 158 */
  EV_PAD9 = 0x009f, /* 159 */
  /* Key-pad keys. */
  EV_PADASTERKEY = 0x00a0, /* 160 */
  EVT_PADSLASHKEY = 0x00a1, /* 161 */
  EVT_PADMINUS = 0x00a2,    /* 162 */
  EVT_PADENTER = 0x00a3,    /* 163 */
  EVT_PADPLUSKEY = 0x00a4,  /* 164 */

  EVT_PAUSEKEY = 0x00a5,    /* 165 */
  EVT_INSERTKEY = 0x00a6,   /* 166 */
  EVT_HOMEKEY = 0x00a7,     /* 167 */
  EVT_PAGEUPKEY = 0x00a8,   /* 168 */
  EV_PAGEDOWNKEY = 0x00a9, /* 169 */
  EV_ENDKEY = 0x00aa,      /* 170 */
  /* Note that 'PADPERIOD' is defined out-of-order. */
  EVT_UNKNOWNKEY = 0x00ab, /* 171 */
  EV_OSKEY = 0x00ac,      /* 172 */
  EV_GRLESSKEY = 0x00ad,  /* 173 */
  /* Media keys. */
  EV_MEDIAPLAY = 0x00ae,  /* 174 */
  EV_MEDIASTOP = 0x00af,  /* 175 */
  EV_MEDIAFIRST = 0x00b0, /* 176 */
  EV_MEDIALAST = 0x00b1,  /* 177 */
  /* Menu/App key. */
  EV_APPKEY = 0x00b2, /* 178 */

  EV_PADPERIOD = 0x00c7, /* 199 */

  EV_CAPSLOCKKEY = 0x00d3, /* 211 */

  /* Mod keys. */
  EV_LEFTCTRLKEY = 0x00d4,   /* 212 */
  EV_LEFTALTKEY = 0x00d5,    /* 213 */
  EV_RIGHTALTKEY = 0x00d6,   /* 214 */
  EV_RIGHTCTRLKEY = 0x00d7,  /* 215 */
  EV_RIGHTSHIFTKEY = 0x00d8, /* 216 */
  EV_LEFTSHIFTKEY = 0x00d9,  /* 217 */
  /* Special characters. */
  EVT_ESCKEY = 0x00da,          /* 218 */
  EVT_TABKEY = 0x00db,          /* 219 */
  EV_RETKEY = 0x00dc,          /* 220 */
  EV_SPACEKEY = 0x00dd,        /* 221 */
  EV_LINEFEEDKEY = 0x00de,     /* 222 */
  EVT_BACKSPACEKEY = 0x00df,    /* 223 */
  EVT_DELKEY = 0x00e0,          /* 224 */
  EV_SEMICOLONKEY = 0x00e1,    /* 225 */
  EVT_PERIODKEY = 0x00e2,       /* 226 */
  EVT_COMMAKEY = 0x00e3,        /* 227 */
  EVT_QUOTEKEY = 0x00e4,        /* 228 */
  EVT_ACCENTGRAVEKEY = 0x00e5,  /* 229 */
  EVT_MINUSKEY = 0x00e6,        /* 230 */
  EVT_PLUSKEY = 0x00e7,         /* 231 */
  EV_SLASHKEY = 0x00e8,        /* 232 */
  EV_BACKSLASHKEY = 0x00e9,    /* 233 */
  EV_EQUALKEY = 0x00ea,        /* 234 */
  EV_LEFTBRACKETKEY = 0x00eb,  /* 235 */
  EV_RIGHTBRACKETKEY = 0x00ec, /* 236 */

/* Maximum keyboard value (inclusive). */
#define _EVT_KEYBOARD_MAX 0x00ff /* 255 */

  /* WARNING: 0x010x are used for internal events
   * (but are still stored in the key-map). */

  EVT_F1KEY = 0x012c,  /* 300 */
  EVT_F2KEY = 0x012d,  /* 301 */
  EVT_F3KEY = 0x012e,  /* 302 */
  EVT_F4KEY = 0x012f,  /* 303 */
  EVT_F5KEY = 0x0130,  /* 304 */
  EVT_F6KEY = 0x0131,  /* 305 */
  EV_F7KEY = 0x0132,  /* 306 */
  EV_F8KEY = 0x0133,  /* 307 */
  EVT_F9KEY = 0x0134,  /* 308 */
  EVT_F10KEY = 0x0135, /* 309 */
  EV_F11KEY = 0x0136, /* 310 */
  EVT_F12KEY = 0x0137, /* 311 */
  EVT_F13KEY = 0x0138, /* 312 */
  EVT_F14KEY = 0x0139, /* 313 */
  EVT_F15KEY = 0x013a, /* 314 */
  EVT_F16KEY = 0x013b, /* 315 */
  EVT_F17KEY = 0x013c, /* 316 */
  EVT_F18KEY = 0x013d, /* 317 */
  EVT_F19KEY = 0x013e, /* 318 */
  EVT_F20KEY = 0x013f, /* 319 */
  EVT_F21KEY = 0x0140, /* 320 */
  EVT_F22KEY = 0x0141, /* 321 */
  EVT_F23KEY = 0x0142, /* 322 */
  EVT_F24KEY = 0x0143, /* 323 */

  /* End of keyboard codes. *** */
  /* NDOF (from "Space Navigator" & friends)
   * These must be kept in sync with `GHOST_NDOFManager.h`.
   * Ordering matters, exact vals do not. */
  NDOF_MOTION = 0x0190, /* 400 */

#define _NDOF_MIN NDOF_MOTION
#define _NDOF_BUTTON_MIN NDOF_BUTTON_MENU

  /* used internally, never sent */
  NDOF_BTN_NONE = NDOF_MOTION,
  /* these two are available from any 3Dconnexion device */

  NDOF_BTN_MENU = 0x0191, /* 401 */
  NDOF_BTN_FIT = 0x0192,  /* 402 */
  /* standard views */
  NDOF_BTN_TOP = 0x0193,    /* 403 */
  NDOF_BTN_BOTTOM = 0x0194, /* 404 */
  NDOF_BTN_LEFT = 0x0195,   /* 405 */
  NDOF_BTN_RIGHT = 0x0196,  /* 406 */
  NDOF_BTN_FRONT = 0x0197,  /* 407 */
  NDOF_BTN_BACK = 0x0198,   /* 408 */
  /* more views */
  NDOF_BTN_ISO1 = 0x0199, /* 409 */
  NDOF_BTN_ISO2 = 0x019a, /* 410 */
  /* 90 degree rotations */
  NDOF_BTN_ROLL_CW = 0x019b,  /* 411 */
  NDOF_BTN_ROLL_CCW = 0x019c, /* 412 */
  NDOF_BTN_SPIN_CW = 0x019d,  /* 413 */
  NDOF_BTN_SPIN_CCW = 0x019e, /* 414 */
  NDOF_BTN_TILT_CW = 0x019f,  /* 415 */
  NDOF_BTN_TILT_CCW = 0x01a0, /* 416 */
  /* device control */
  NDOF_BTN_ROTATE = 0x01a1,   /* 417 */
  NDOF_BTN_PANZOOM = 0x01a2,  /* 418 */
  NDOF_BTN_DOMINANT = 0x01a3, /* 419 */
  NDOF_BTN_PLUS = 0x01a4,     /* 420 */
  NDOF_BTN_MINUS = 0x01a5,    /* 421 */

/* Disabled as GHOST converts these to keyboard events
 * which use regular keyboard ev handling logic. */
#if 0
  /* keyboard emulation */
  NDOF_BTN_ESC = 0x01a6,   /* 422 */
  NDOF_BTN_ALT = 0x01a7,   /* 423 */
  NDOF_BTN_SHIFT = 0x01a8, /* 424 */
  NDOF_BTN_CTRL = 0x01a9,  /* 425 */
#endif

  /* general-purpose btns */
  NDOF_BTN_1 = 0x01aa,  /* 426 */
  NDOF_BTN_2 = 0x01ab,  /* 427 */
  NDOF_BTN_3 = 0x01ac,  /* 428 */
  NDOF_BTN_4 = 0x01ad,  /* 429 */
  NDOF_BTN_5 = 0x01ae,  /* 430 */
  NDOF_BTN_6 = 0x01af,  /* 431 */
  NDOF_BTN_7 = 0x01b0,  /* 432 */
  NDOF_BTN_8 = 0x01b1,  /* 433 */
  NDOF_BTN_9 = 0x01b2,  /* 434 */
  NDOF_BTN_10 = 0x01b3, /* 435 */
  /* more general-purpose buttons */
  NDOF_BTN_A = 0x01b4, /* 436 */
  NDOF_BTN_B = 0x01b5, /* 437 */
  NDOF_BTN_C = 0x01b6, /* 438 */

#define _NDOF_MAX NDOF_BTN_C
#define _NDOF_BTN_MAX NDOF_BTN_C

  /* End of Input devices. */
  /* Start of Dune internal evs. */
  /* Those are mixed inside keyboard 'area'! */
  /* System: 0x010x */
  INPUTCHANGE = 0x0103,   /* Input connected or disconnected, (259). */
  WINDEACTIVATE = 0x0104, /* Window is deactivated, focus lost, (260). */
  /* Timer: 0x011x */
  TIMER = 0x0110,         /* Timer event, passed on to all queues (272). */
  TIMER0 = 0x0111,        /* Timer event, slot for internal use (273). */
  TIMER1 = 0x0112,        /* Timer event, slot for internal use (274). */
  TIMER2 = 0x0113,        /* Timer event, slot for internal use (275). */
  TIMERJOBS = 0x0114,     /* Timer event, jobs system (276). */
  TIMERAUTOSAVE = 0x0115, /* Timer event, autosave (277). */
  TIMERREPORT = 0x0116,   /* Timer event, reports (278). */
  TIMERRGN = 0x0117,   /* Timer event, region slide in/out (279). */
  TIMERNOTIFIER = 0x0118, /* Timer event, notifier sender (280). */
  TIMERF = 0x011F,        /* Last timer (287). */

  /* Actionzones, tweak, gestures: 0x500x, 0x501x */
  /* Keep in sync with IS_EV_ACTIONZONE(...). */
  EV_ACTIONZONE_AREA = 0x5000,       /* 20480 */
  EV_ACTIONZONE_RGN = 0x5001,     /* 20481 */
  EV_ACTIONZONE_FULLSCREEN = 0x5011, /* 20497 */

  /* hese vals are saved in key-map files, do not change them but just add new ones. */
  /* 0x5011 is taken, see EVT_ACTIONZONE_FULLSCREEN */
  /* Misc Dune internals: 0x502x */
  EV_FILESEL = 0x5020, /* 20512 */
  EV_BTN_OPEN = 0x5021,   /* 20513 */
  EV_MODAL_MAP = 0x5022,  /* 20514 */
  EV_DROP = 0x5023,       /* 20515 */
  /* When val is 0, re-activate, when 1, don't re-activate the button under the cursor. */
  EV_BTN_CANCEL = 0x5024, /* 20516 */

  /* could become gizmo cb */
  EV_GIZMO_UPDATE = 0x5025, /* 20517 */

  /* XR events: 0x503x */
  EV_XR_ACTION = 0x5030, /* 20528 */
  /* End of Dune internal events. */
};

/* WinEv.type Helpers */
/* Test whether the ev is timer ev. */
#define ISTIMER(ev_type) ((ev_type) >= TIMER && (ev_type) <= TIMERF)

/* for event checks */
/* only used for KM_TXTINPUT, so assume that we want all user-inputtable ascii codes included */
/* Unused, see win_evmatch, see: T30479. */
// #define ISTXTINPUT(ev_type)  ((ev_type) >= ' ' && (ev_type) <= 255)
/* NOTE: an alternative could be to check `event->utf8_buf`. */

/* Test whether the ev is a key on the keyboard (including mod keys). */
#define ISKEYBOARD(ev_type) \
  (((ev_type) >= _EV_KEYBOARD_MIN && (ev_type) <= _EV_KEYBOARD_MAX) || \
   ((ev_type) >= EV_F1KEY && (ev_type) <= EV_F24KEY))

/* Test whether the ev is a key on the keyboard
 * or any other kind of btn that supports press & release
 * (use for click & click-drag detection).
 *
 * Mouse wheel evs are excluded from this macro, while they do generate press events it
 * doesn't make sense to have click & click-drag events for a mouse-wheel as it can't be held down. */
#define ISKEYBOARD_OR_BTN(ev_type) \
  (ISMOUSE_BTN(ev_type) || ISKEYBOARD(ev_type) || ISNDOF_BUTTON(event_type))

/* Test whether the ev is a mod key. */
#define ISKEYMOD(ev_type) \
  (((ev_type) >= EV_LEFTCTRLKEY && (ev_type) <= EV_LEFTSHIFTKEY) || \
   (ev_type) == EV_OSKEY)

/* Test whether the evis a mouse btn. */
#define ISMOUSE(ev_type) \
  (((ev_type) >= LEFTMOUSE && (ev_type) <= BTN7MOUSE) || (ev_type) == MOUSESMARTZOOM)
/* Test whether the ev is a mouse wheel. */
#define ISMOUSE_WHEEL(ev_type) ((ev_type) >= WHEELUPMOUSE && (ev_type) <= WHEELOUTMOUSE)
/* Test whether the ev is a mouse (track-pad) gesture. */
#define ISMOUSE_GESTURE(ev_type) ((ev_type) >= MOUSEPAN && (ev_type) <= MOUSEROTATE)
/* Test whether the event is a mouse button (excluding mouse-wheel). */
#define ISMOUSE_BTN(ev_type) \
  (ELEM(ev_type, \
        LEFTMOUSE, \
        MIDDLEMOUSE, \
        RIGHTMOUSE, \
        BTN4MOUSE, \
        BTN5MOUSE, \
        BTN6MOUSE, \
        BTN7MOUSE))

/* Test whether the ev is a NDOF event. */
#define ISNDOF(ev_type) ((ev_type) >= _NDOF_MIN && (ev_type) <= _NDOF_MAX)
#define ISNDOF_BTN(ev_type) \
  ((event_type) >= _NDOF_BTN_MIN && (ev_type) <= _NDOF_BTN_MAX)

#define IS_EV_ACTIONZONE(ev_type) \
  ELEM(ev_type, EV_ACTIONZONE_AREA, EV_ACTIONZONE_RGN, EV_ACTIONZONE_FULLSCREEN)

/* Test whether event type is acceptable as hotkey (excluding mods). */
#define ISHOTKEY(ev_type) \
  ((ISKEYBOARD(ev_type) || ISMOUSE_BTN(ev_type) || ISMOUSE_WHEEL(event_type) || \
    ISNDOF_BTN(ev_type)) && \
   (ISKEYMOD(ev_type) == false))

enum eEvTypeMask {
  /* ISKEYMOD */
  EV_TYPE_MASK_KEYBOARD_MOD = (1 << 0),
  /* ISKEYBOARD */
  EV_TYPE_MASK_KEYBOARD = (1 << 1),
  /* ISMOUSE_WHEEL */
  EV_TYPE_MASK_MOUSE_WHEEL = (1 << 2),
  /* ISMOUSE_BTN */
  EV_TYPE_MASK_MOUSE_GESTURE = (1 << 3),
  /* ISMOUSE_GESTURE */
  EV_TYPE_MASK_MOUSE_BTN = (1 << 4),
  /* ISMOUSE */
  EV_TYPE_MASK_MOUSE = (1 << 5),
  /* #ISNDOF */
  EV_TYPE_MASK_NDOF = (1 << 6),
  /* IS_EV_ACTIONZONE */
  EV_TYPE_MASK_ACTIONZONE = (1 << 7),
};
#define EV_TYPE_MASK_ALL \
  (EV_TYPE_MASK_KEYBOARD | EV_TYPE_MASK_MOUSE | EV_TYPE_MASK_NDOF | EV_TYPE_MASK_ACTIONZONE)

#define EV_TYPE_MASK_HOTKEY_INCLUDE \
  (EV_TYPE_MASK_KEYBOARD | EV_TYPE_MASK_MOUSE | EV_TYPE_MASK_NDOF)
#define EV_TYPE_MASK_HOTKEY_EXCLUDE EV_TYPE_MASK_KEYBOARD_MOD

bool win_ev_type_mask_test(int ev_type, enum eEvTypeMask mask);

/* WinEv.val Vals */
/* Gestures */

/* File sel */
enum {
  EV_FILESEL_FULL_OPEN = 1,
  EV_FILESEL_EX = 2,
  EV_FILESEL_CANCEL = 3,
  EV_FILESEL_EXTERNAL_CANCEL = 4,
};

/* Gesture
 * Used in WinEv.val
 * These vals are saved in keymap files.
 * do not change them but just add new ones. */
enum {
  GESTURE_MODAL_CANCEL = 1,
  GESTURE_MODAL_CONFIRM = 2,

  /* Uses 'desel' op prop. */
  GESTURE_MODAL_SEL = 3,
  GESTURE_MODAL_DESEL = 4,

  /* Circle sel: when no mouse btn is pressed */
  GESTURE_MODAL_NOP = 5,

  /* Circle sel: larger brush. */
  GESTURE_MODAL_CIRCLE_ADD = 6,
  /* Circle sel: smaller brush. */
  GESTURE_MODAL_CIRCLE_SUB = 7,

  /* Box sel/straight line, activate, use release to detect which button. */
  GESTURE_MODAL_BEGIN = 8,

  /* Uses 'zoom_out' op prop. */
  GESTURE_MODAL_IN = 9,
  GESTURE_MODAL_OUT = 10,

  /* circle sel: size brush (for trackpad ev). */
  GESTURE_MODAL_CIRCLE_SIZE = 11,

  /* Move sel area. */
  GESTURE_MODAL_MOVE = 12,

  /* Toggle to activate snapping (angle snapping for straight line). */
  GESTURE_MODAL_SNAP = 13,

  /* Toggle to activate flip (flip the active side of a straight line). */
  GESTURE_MODAL_FLIP = 14,
};

#ifdef __cplusplus
}
#endif
