#include <stdlib.h>

#include "types_screen.h"
#include "types_space.h"
#include "types_userdef.h"
#include "types_windowmanager.h"

#include "lib_utildefines.h"

#include "lang.h"

#include "dune_keyconfig.h"
#include "dune_screen.h"
#include "dune_workspace.h"

#include "api_access.h"
#include "api_define.h"
#include "api_enum_types.h"

#include "api_internal.h"

#include "wm_api.h"
#include "wm_types.h"

#ifdef api_RUNTIME

static const EnumPropItem event_mouse_type_items[] = {
    {LEFTMOUSE, "LEFTMOUSE", 0, "Left", ""},
    {MIDDLEMOUSE, "MIDDLEMOUSE", 0, "Middle", ""},
    {RIGHTMOUSE, "RIGHTMOUSE", 0, "Right", ""},
    {BUTTON4MOUSE, "BUTTON4MOUSE", 0, "Button4", ""},
    {BUTTON5MOUSE, "BUTTON5MOUSE", 0, "Button5", ""},
    {BUTTON6MOUSE, "BUTTON6MOUSE", 0, "Button6", ""},
    {BUTTON7MOUSE, "BUTTON7MOUSE", 0, "Button7", ""},
    API_ENUM_ITEM_SEPR,
    {TABLET_STYLUS, "PEN", 0, "Pen", ""},
    {TABLET_ERASER, "ERASER", 0, "Eraser", ""},
    API_ENUM_ITEM_SEPR,
    {MOUSEMOVE, "MOUSEMOVE", 0, "Move", ""},
    {MOUSEPAN, "TRACKPADPAN", 0, "Mouse/Trackpad Pan", ""},
    {MOUSEZOOM, "TRACKPADZOOM", 0, "Mouse/Trackpad Zoom", ""},
    {MOUSEROTATE, "MOUSEROTATE", 0, "Mouse/Trackpad Rotate", ""},
    {MOUSESMARTZOOM, "MOUSESMARTZOOM", 0, "Mouse/Trackpad Smart Zoom", ""},
    API_ENUM_ITEM_SEPR,
    {WHEELUPMOUSE, "WHEELUPMOUSE", 0, "Wheel Up", ""},
    {WHEELDOWNMOUSE, "WHEELDOWNMOUSE", 0, "Wheel Down", ""},
    {WHEELINMOUSE, "WHEELINMOUSE", 0, "Wheel In", ""},
    {WHEELOUTMOUSE, "WHEELOUTMOUSE", 0, "Wheel Out", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem event_timer_type_items[] = {
    {TIMER, "TIMER", 0, "Timer", ""},
    {TIMER0, "TIMER0", 0, "Timer 0", ""},
    {TIMER1, "TIMER1", 0, "Timer 1", ""},
    {TIMER2, "TIMER2", 0, "Timer 2", ""},
    {TIMERJOBS, "TIMER_JOBS", 0, "Timer Jobs", ""},
    {TIMERAUTOSAVE, "TIMER_AUTOSAVE", 0, "Timer Autosave", ""},
    {TIMERREPORT, "TIMER_REPORT", 0, "Timer Report", ""},
    {TIMERREGION, "TIMERREGION", 0, "Timer Region", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem event_textinput_type_items[] = {
    {KM_TEXTINPUT, "TEXTINPUT", 0, "Text Input", ""},
    {0, NULL, 0, NULL, NULL},
};

static const EnumPropItem event_ndof_type_items[] = {
    {NDOF_MOTION, "NDOF_MOTION", 0, "Motion", ""},
    /* buttons on all 3dconnexion devices */
    {NDOF_BUTTON_MENU, "NDOF_BUTTON_MENU", 0, "Menu", ""},
    {NDOF_BUTTON_FIT, "NDOF_BUTTON_FIT", 0, "Fit", ""},
    /* view buttons */
    {NDOF_BUTTON_TOP, "NDOF_BUTTON_TOP", 0, "Top", ""},
    {NDOF_BUTTON_BOTTOM, "NDOF_BUTTON_BOTTOM", 0, "Bottom", ""},
    {NDOF_BUTTON_LEFT, "NDOF_BUTTON_LEFT", 0, "Left", ""},
    {NDOF_BUTTON_RIGHT, "NDOF_BUTTON_RIGHT", 0, "Right", ""},
    {NDOF_BUTTON_FRONT, "NDOF_BUTTON_FRONT", 0, "Front", ""},
    {NDOF_BUTTON_BACK, "NDOF_BUTTON_BACK", 0, "Back", ""},
    /* more views */
    {NDOF_BUTTON_ISO1, "NDOF_BUTTON_ISO1", 0, "Isometric 1", ""},
    {NDOF_BUTTON_ISO2, "NDOF_BUTTON_ISO2", 0, "Isometric 2", ""},
    /* 90 degree rotations */
    {NDOF_BUTTON_ROLL_CW, "NDOF_BUTTON_ROLL_CW", 0, "Roll CW", ""},
    {NDOF_BUTTON_ROLL_CCW, "NDOF_BUTTON_ROLL_CCW", 0, "Roll CCW", ""},
    {NDOF_BUTTON_SPIN_CW, "NDOF_BUTTON_SPIN_CW", 0, "Spin CW", ""},
    {NDOF_BUTTON_SPIN_CCW, "NDOF_BUTTON_SPIN_CCW", 0, "Spin CCW", ""},
    {NDOF_BUTTON_TILT_CW, "NDOF_BUTTON_TILT_CW", 0, "Tilt CW", ""},
    {NDOF_BUTTON_TILT_CCW, "NDOF_BUTTON_TILT_CCW", 0, "Tilt CCW", ""},
    /* device control */
    {NDOF_BUTTON_ROTATE, "NDOF_BUTTON_ROTATE", 0, "Rotate", ""},
    {NDOF_BUTTON_PANZOOM, "NDOF_BUTTON_PANZOOM", 0, "Pan/Zoom", ""},
    {NDOF_BUTTON_DOMINANT, "NDOF_BUTTON_DOMINANT", 0, "Dominant", ""},
    {NDOF_BUTTON_PLUS, "NDOF_BUTTON_PLUS", 0, "Plus", ""},
    {NDOF_BUTTON_MINUS, "NDOF_BUTTON_MINUS", 0, "Minus", ""},
    /* View buttons. */
    {NDOF_BUTTON_V1, "NDOF_BUTTON_V1", 0, "View 1", ""},
    {NDOF_BUTTON_V2, "NDOF_BUTTON_V2", 0, "View 2", ""},
    {NDOF_BUTTON_V3, "NDOF_BUTTON_V3", 0, "View 3", ""},
    /* general-purpose buttons */
    {NDOF_BUTTON_1, "NDOF_BUTTON_1", 0, "Button 1", ""},
    {NDOF_BUTTON_2, "NDOF_BUTTON_2", 0, "Button 2", ""},
    {NDOF_BUTTON_3, "NDOF_BUTTON_3", 0, "Button 3", ""},
    {NDOF_BUTTON_4, "NDOF_BUTTON_4", 0, "Button 4", ""},
    {NDOF_BUTTON_5, "NDOF_BUTTON_5", 0, "Button 5", ""},
    {NDOF_BUTTON_6, "NDOF_BUTTON_6", 0, "Button 6", ""},
    {NDOF_BUTTON_7, "NDOF_BUTTON_7", 0, "Button 7", ""},
    {NDOF_BUTTON_8, "NDOF_BUTTON_8", 0, "Button 8", ""},
    {NDOF_BUTTON_9, "NDOF_BUTTON_9", 0, "Button 9", ""},
    {NDOF_BUTTON_10, "NDOF_BUTTON_10", 0, "Button 10", ""},
    {NDOF_BUTTON_A, "NDOF_BUTTON_A", 0, "Button A", ""},
    {NDOF_BUTTON_B, "NDOF_BUTTON_B", 0, "Button B", ""},
    {NDOF_BUTTON_C, "NDOF_BUTTON_C", 0, "Button C", ""},
#  if 0 /* Never used (converted to keyboard events by GHOST). */
    /* keyboard emulation */
    {NDOF_BUTTON_ESC, "NDOF_BUTTON_ESC", 0, "Esc"},
    {NDOF_BUTTON_ENTER, "NDOF_BUTTON_ENTER", 0, "Enter"},
    {NDOF_BUTTON_DELETE, "NDOF_BUTTON_DELETE", 0, "Delete"},
    {NDOF_BUTTON_TAB, "NDOF_BUTTON_TAB", 0, "Tab"},
    {NDOF_BUTTON_SPACE, "NDOF_BUTTON_SPACE", 0, "Space"},
    {NDOF_BUTTON_ALT, "NDOF_BUTTON_ALT", 0, "Alt"},
    {NDOF_BUTTON_SHIFT, "NDOF_BUTTON_SHIFT", 0, "Shift"},
    {NDOF_BUTTON_CTRL, "NDOF_BUTTON_CTRL", 0, "Ctrl"},
#  endif
    {0, NULL, 0, NULL, NULL},
};
#endif /* API_RUNTIME */

/**
 * Job types for use in the `bpy.app.is_job_running(job_type)` call.
 *
 * This is a subset of the `WM_JOB_TYPE_...` anonymous enum defined in `WM_api.h`. It is
 * intentionally kept as a subset, such that by default how jobs are handled is kept as an
 * "internal implementation detail" of Blender, rather than a public, reliable part of the API.
 *
 * This array can be expanded on a case-by-case basis, when there is a clear and testable use case.
 */
const EnumPropItem api_enum_wm_job_type_items[] = {
    {WM_JOB_TYPE_RENDER, "RENDER", 0, "Regular rendering", ""},
    {WM_JOB_TYPE_RENDER_PREVIEW, "RENDER_PREVIEW", 0, "Rendering previews", ""},
    {WM_JOB_TYPE_OBJECT_BAKE, "OBJECT_BAKE", 0, "Object Baking", ""},
    {WM_JOB_TYPE_COMPOSITE, "COMPOSITE", 0, "Compositing", ""},
    {WM_JOB_TYPE_SHADER_COMPILATION, "SHADER_COMPILATION", 0, "Shader compilation", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_event_type_items[] = {
    /* - Note we abuse 'tooltip' message here to store a 'compact' form of some (too) long names.
     * - Intentionally excluded: #CAPSLOCKKEY, #UNKNOWNKEY.
     */
    {0, "NONE", 0, "", ""},
    {LEFTMOUSE, "LEFTMOUSE", 0, "Left Mouse", "LMB"},
    {MIDDLEMOUSE, "MIDDLEMOUSE", 0, "Middle Mouse", "MMB"},
    {RIGHTMOUSE, "RIGHTMOUSE", 0, "Right Mouse", "RMB"},
    {BUTTON4MOUSE, "BUTTON4MOUSE", 0, "Button4 Mouse", "MB4"},
    {BUTTON5MOUSE, "BUTTON5MOUSE", 0, "Button5 Mouse", "MB5"},
    {BUTTON6MOUSE, "BUTTON6MOUSE", 0, "Button6 Mouse", "MB6"},
    {BUTTON7MOUSE, "BUTTON7MOUSE", 0, "Button7 Mouse", "MB7"},
    API_ENUM_ITEM_SEPR,
    {TABLET_STYLUS, "PEN", 0, "Pen", ""},
    {TABLET_ERASER, "ERASER", 0, "Eraser", ""},
    API_ENUM_ITEM_SEPR,
    {MOUSEMOVE, "MOUSEMOVE", 0, "Mouse Move", "MsMov"},
    {INBETWEEN_MOUSEMOVE, "INBETWEEN_MOUSEMOVE", 0, "In-between Move", "MsSubMov"},
    {MOUSEPAN, "TRACKPADPAN", 0, "Mouse/Trackpad Pan", "MsPan"},
    {MOUSEZOOM, "TRACKPADZOOM", 0, "Mouse/Trackpad Zoom", "MsZoom"},
    {MOUSEROTATE, "MOUSEROTATE", 0, "Mouse/Trackpad Rotate", "MsRot"},
    {MOUSESMARTZOOM, "MOUSESMARTZOOM", 0, "Mouse/Trackpad Smart Zoom", "MsSmartZoom"},
    API_ENUM_ITEM_SEPR,
    {WHEELUPMOUSE, "WHEELUPMOUSE", 0, "Wheel Up", "WhUp"},
    {WHEELDOWNMOUSE, "WHEELDOWNMOUSE", 0, "Wheel Down", "WhDown"},
    {WHEELINMOUSE, "WHEELINMOUSE", 0, "Wheel In", "WhIn"},
    {WHEELOUTMOUSE, "WHEELOUTMOUSE", 0, "Wheel Out", "WhOut"},
    API_ENUM_ITEM_SEPR,
    {EVT_AKEY, "A", 0, "A", ""},
    {EVT_BKEY, "B", 0, "B", ""},
    {EVT_CKEY, "C", 0, "C", ""},
    {EVT_DKEY, "D", 0, "D", ""},
    {EVT_EKEY, "E", 0, "E", ""},
    {EVT_FKEY, "F", 0, "F", ""},
    {EVT_GKEY, "G", 0, "G", ""},
    {EVT_HKEY, "H", 0, "H", ""},
    {EVT_IKEY, "I", 0, "I", ""},
    {EVT_JKEY, "J", 0, "J", ""},
    {EVT_KKEY, "K", 0, "K", ""},
    {EVT_LKEY, "L", 0, "L", ""},
    {EVT_MKEY, "M", 0, "M", ""},
    {EVT_NKEY, "N", 0, "N", ""},
    {EVT_OKEY, "O", 0, "O", ""},
    {EVT_PKEY, "P", 0, "P", ""},
    {EVT_QKEY, "Q", 0, "Q", ""},
    {EVT_RKEY, "R", 0, "R", ""},
    {EVT_SKEY, "S", 0, "S", ""},
    {EVT_TKEY, "T", 0, "T", ""},
    {EVT_UKEY, "U", 0, "U", ""},
    {EVT_VKEY, "V", 0, "V", ""},
    {EVT_WKEY, "W", 0, "W", ""},
    {EVT_XKEY, "X", 0, "X", ""},
    {EVT_YKEY, "Y", 0, "Y", ""},
    {EVT_ZKEY, "Z", 0, "Z", ""},
    API_ENUM_ITEM_SEPR,
    {EVT_ZEROKEY, "ZERO", 0, "0", ""},
    {EVT_ONEKEY, "ONE", 0, "1", ""},
    {EVT_TWOKEY, "TWO", 0, "2", ""},
    {EVT_THREEKEY, "THREE", 0, "3", ""},
    {EVT_FOURKEY, "FOUR", 0, "4", ""},
    {EVT_FIVEKEY, "FIVE", 0, "5", ""},
    {EVT_SIXKEY, "SIX", 0, "6", ""},
    {EVT_SEVENKEY, "SEVEN", 0, "7", ""},
    {EVT_EIGHTKEY, "EIGHT", 0, "8", ""},
    {EVT_NINEKEY, "NINE", 0, "9", ""},
    API_ENUM_ITEM_SEPR,
    {EVT_LEFTCTRLKEY, "LEFT_CTRL", 0, "Left Ctrl", "CtrlL"},
    {EVT_LEFTALTKEY, "LEFT_ALT", 0, "Left Alt", "AltL"},
    {EVT_LEFTSHIFTKEY, "LEFT_SHIFT", 0, "Left Shift", "ShiftL"},
    {EVT_RIGHTALTKEY, "RIGHT_ALT", 0, "Right Alt", "AltR"},
    {EVT_RIGHTCTRLKEY, "RIGHT_CTRL", 0, "Right Ctrl", "CtrlR"},
    {EVT_RIGHTSHIFTKEY, "RIGHT_SHIFT", 0, "Right Shift", "ShiftR"},
    API_ENUM_ITEM_SEPR,
    {EVT_OSKEY, "OSKEY", 0, "OS Key", "Cmd"},
    {EVT_APPKEY, "APP", 0, "Application", "App"},
    {EVT_GRLESSKEY, "GRLESS", 0, "Grless", ""},
    {EVT_ESCKEY, "ESC", 0, "Esc", ""},
    {EVT_TABKEY, "TAB", 0, "Tab", ""},
    {EVT_RETKEY, "RET", 0, "Return", "Enter"},
    {EVT_SPACEKEY, "SPACE", 0, "Spacebar", "Space"},
    {EVT_LINEFEEDKEY, "LINE_FEED", 0, "Line Feed", ""},
    {EVT_BACKSPACEKEY, "BACK_SPACE", 0, "Backspace", "BkSpace"},
    {EVT_DELKEY, "DEL", 0, "Delete", "Del"},
    {EVT_SEMICOLONKEY, "SEMI_COLON", 0, ";", ""},
    {EVT_PERIODKEY, "PERIOD", 0, ".", ""},
    {EVT_COMMAKEY, "COMMA", 0, ",", ""},
    {EVT_QUOTEKEY, "QUOTE", 0, "\"", ""},
    {EVT_ACCENTGRAVEKEY, "ACCENT_GRAVE", 0, "`", ""},
    {EVT_MINUSKEY, "MINUS", 0, "-", ""},
    {EVT_PLUSKEY, "PLUS", 0, "+", ""},
    {EVT_SLASHKEY, "SLASH", 0, "/", ""},
    {EVT_BACKSLASHKEY, "BACK_SLASH", 0, "\\", ""},
    {EVT_EQUALKEY, "EQUAL", 0, "=", ""},
    {EVT_LEFTBRACKETKEY, "LEFT_BRACKET", 0, "[", ""},
    {EVT_RIGHTBRACKETKEY, "RIGHT_BRACKET", 0, "]", ""},
    {EVT_LEFTARROWKEY, "LEFT_ARROW", 0, "Left Arrow", "←"},
    {EVT_DOWNARROWKEY, "DOWN_ARROW", 0, "Down Arrow", "↓"},
    {EVT_RIGHTARROWKEY, "RIGHT_ARROW", 0, "Right Arrow", "→"},
    {EVT_UPARROWKEY, "UP_ARROW", 0, "Up Arrow", "↑"},
    {EVT_PAD2, "NUMPAD_2", 0, "Numpad 2", "Pad2"},
    {EVT_PAD4, "NUMPAD_4", 0, "Numpad 4", "Pad4"},
    {EVT_PAD6, "NUMPAD_6", 0, "Numpad 6", "Pad6"},
    {EVT_PAD8, "NUMPAD_8", 0, "Numpad 8", "Pad8"},
    {EVT_PAD1, "NUMPAD_1", 0, "Numpad 1", "Pad1"},
    {EVT_PAD3, "NUMPAD_3", 0, "Numpad 3", "Pad3"},
    {EVT_PAD5, "NUMPAD_5", 0, "Numpad 5", "Pad5"},
    {EVT_PAD7, "NUMPAD_7", 0, "Numpad 7", "Pad7"},
    {EVT_PAD9, "NUMPAD_9", 0, "Numpad 9", "Pad9"},
    {EVT_PADPERIOD, "NUMPAD_PERIOD", 0, "Numpad .", "Pad."},
    {EVT_PADSLASHKEY, "NUMPAD_SLASH", 0, "Numpad /", "Pad/"},
    {EVT_PADASTERKEY, "NUMPAD_ASTERIX", 0, "Numpad *", "Pad*"},
    {EVT_PAD0, "NUMPAD_0", 0, "Numpad 0", "Pad0"},
    {EVT_PADMINUS, "NUMPAD_MINUS", 0, "Numpad -", "Pad-"},
    {EVT_PADENTER, "NUMPAD_ENTER", 0, "Numpad Enter", "PadEnter"},
    {EVT_PADPLUSKEY, "NUMPAD_PLUS", 0, "Numpad +", "Pad+"},
    {EVT_F1KEY, "F1", 0, "F1", ""},
    {EVT_F2KEY, "F2", 0, "F2", ""},
    {EVT_F3KEY, "F3", 0, "F3", ""},
    {EVT_F4KEY, "F4", 0, "F4", ""},
    {EVT_F5KEY, "F5", 0, "F5", ""},
    {EVT_F6KEY, "F6", 0, "F6", ""},
    {EVT_F7KEY, "F7", 0, "F7", ""},
    {EVT_F8KEY, "F8", 0, "F8", ""},
    {EVT_F9KEY, "F9", 0, "F9", ""},
    {EVT_F10KEY, "F10", 0, "F10", ""},
    {EVT_F11KEY, "F11", 0, "F11", ""},
    {EVT_F12KEY, "F12", 0, "F12", ""},
    {EVT_F13KEY, "F13", 0, "F13", ""},
    {EVT_F14KEY, "F14", 0, "F14", ""},
    {EVT_F15KEY, "F15", 0, "F15", ""},
    {EVT_F16KEY, "F16", 0, "F16", ""},
    {EVT_F17KEY, "F17", 0, "F17", ""},
    {EVT_F18KEY, "F18", 0, "F18", ""},
    {EVT_F19KEY, "F19", 0, "F19", ""},
    {EVT_F20KEY, "F20", 0, "F20", ""},
    {EVT_F21KEY, "F21", 0, "F21", ""},
    {EVT_F22KEY, "F22", 0, "F22", ""},
    {EVT_F23KEY, "F23", 0, "F23", ""},
    {EVT_F24KEY, "F24", 0, "F24", ""},
    {EVT_PAUSEKEY, "PAUSE", 0, "Pause", ""},
    {EVT_INSERTKEY, "INSERT", 0, "Insert", "Ins"},
    {EVT_HOMEKEY, "HOME", 0, "Home", ""},
    {EVT_PAGEUPKEY, "PAGE_UP", 0, "Page Up", "PgUp"},
    {EVT_PAGEDOWNKEY, "PAGE_DOWN", 0, "Page Down", "PgDown"},
    {EVT_ENDKEY, "END", 0, "End", ""},
    API_ENUM_ITEM_SEPR,
    {EVT_MEDIAPLAY, "MEDIA_PLAY", 0, "Media Play/Pause", ">/||"},
    {EVT_MEDIASTOP, "MEDIA_STOP", 0, "Media Stop", "Stop"},
    {EVT_MEDIAFIRST, "MEDIA_FIRST", 0, "Media First", "|<<"},
    {EVT_MEDIALAST, "MEDIA_LAST", 0, "Media Last", ">>|"},
    API_ENUM_ITEM_SEPR,
    {KM_TEXTINPUT, "TEXTINPUT", 0, "Text Input", "TxtIn"},
    API_ENUM_ITEM_SEPR,
    {WINDEACTIVATE, "WINDOW_DEACTIVATE", 0, "Window Deactivate", ""},
    {TIMER, "TIMER", 0, "Timer", "Tmr"},
    {TIMER0, "TIMER0", 0, "Timer 0", "Tmr0"},
    {TIMER1, "TIMER1", 0, "Timer 1", "Tmr1"},
    {TIMER2, "TIMER2", 0, "Timer 2", "Tmr2"},
    {TIMERJOBS, "TIMER_JOBS", 0, "Timer Jobs", "TmrJob"},
    {TIMERAUTOSAVE, "TIMER_AUTOSAVE", 0, "Timer Autosave", "TmrSave"},
    {TIMERREPORT, "TIMER_REPORT", 0, "Timer Report", "TmrReport"},
    {TIMERREGION, "TIMERREGION", 0, "Timer Region", "TmrReg"},
    API_ENUM_ITEM_SEPR,
    {NDOF_MOTION, "NDOF_MOTION", 0, "NDOF Motion", "NdofMov"},
    /* buttons on all 3dconnexion devices */
    {NDOF_BUTTON_MENU, "NDOF_BUTTON_MENU", 0, "NDOF Menu", "NdofMenu"},
    {NDOF_BUTTON_FIT, "NDOF_BUTTON_FIT", 0, "NDOF Fit", "NdofFit"},
    /* view buttons */
    {NDOF_BUTTON_TOP, "NDOF_BUTTON_TOP", 0, "NDOF Top", "Ndof↑"},
    {NDOF_BUTTON_BOTTOM, "NDOF_BUTTON_BOTTOM", 0, "NDOF Bottom", "Ndof↓"},
    {NDOF_BUTTON_LEFT, "NDOF_BUTTON_LEFT", 0, "NDOF Left", "Ndof←"},
    {NDOF_BUTTON_RIGHT, "NDOF_BUTTON_RIGHT", 0, "NDOF Right", "Ndof→"},
    {NDOF_BUTTON_FRONT, "NDOF_BUTTON_FRONT", 0, "NDOF Front", "NdofFront"},
    {NDOF_BUTTON_BACK, "NDOF_BUTTON_BACK", 0, "NDOF Back", "NdofBack"},
    /* more views */
    {NDOF_BUTTON_ISO1, "NDOF_BUTTON_ISO1", 0, "NDOF Isometric 1", "NdofIso1"},
    {NDOF_BUTTON_ISO2, "NDOF_BUTTON_ISO2", 0, "NDOF Isometric 2", "NdofIso2"},
    /* 90 degree rotations */
    {NDOF_BUTTON_ROLL_CW, "NDOF_BUTTON_ROLL_CW", 0, "NDOF Roll CW", "NdofRCW"},
    {NDOF_BUTTON_ROLL_CCW, "NDOF_BUTTON_ROLL_CCW", 0, "NDOF Roll CCW", "NdofRCCW"},
    {NDOF_BUTTON_SPIN_CW, "NDOF_BUTTON_SPIN_CW", 0, "NDOF Spin CW", "NdofSCW"},
    {NDOF_BUTTON_SPIN_CCW, "NDOF_BUTTON_SPIN_CCW", 0, "NDOF Spin CCW", "NdofSCCW"},
    {NDOF_BUTTON_TILT_CW, "NDOF_BUTTON_TILT_CW", 0, "NDOF Tilt CW", "NdofTCW"},
    {NDOF_BUTTON_TILT_CCW, "NDOF_BUTTON_TILT_CCW", 0, "NDOF Tilt CCW", "NdofTCCW"},
    /* device control */
    {NDOF_BUTTON_ROTATE, "NDOF_BUTTON_ROTATE", 0, "NDOF Rotate", "NdofRot"},
    {NDOF_BUTTON_PANZOOM, "NDOF_BUTTON_PANZOOM", 0, "NDOF Pan/Zoom", "NdofPanZoom"},
    {NDOF_BUTTON_DOMINANT, "NDOF_BUTTON_DOMINANT", 0, "NDOF Dominant", "NdofDom"},
    {NDOF_BUTTON_PLUS, "NDOF_BUTTON_PLUS", 0, "NDOF Plus", "Ndof+"},
    {NDOF_BUTTON_MINUS, "NDOF_BUTTON_MINUS", 0, "NDOF Minus", "Ndof-"},
#if 0 /* Never used (converted to keyboard events by GHOST). */
    /* keyboard emulation */
    {NDOF_BUTTON_ESC, "NDOF_BUTTON_ESC", 0, "NDOF Esc", "NdofEsc"},
    {NDOF_BUTTON_ALT, "NDOF_BUTTON_ALT", 0, "NDOF Alt", "NdofAlt"},
    {NDOF_BUTTON_SHIFT, "NDOF_BUTTON_SHIFT", 0, "NDOF Shift", "NdofShift"},
    {NDOF_BUTTON_CTRL, "NDOF_BUTTON_CTRL", 0, "NDOF Ctrl", "NdofCtrl"},
#endif
    /* View buttons. */
    {NDOF_BUTTON_V1, "NDOF_BUTTON_V1", 0, "NDOF View 1", ""},
    {NDOF_BUTTON_V2, "NDOF_BUTTON_V2", 0, "NDOF View 2", ""},
    {NDOF_BUTTON_V3, "NDOF_BUTTON_V3", 0, "NDOF View 3", ""},
    /* general-purpose buttons */
    {NDOF_BUTTON_1, "NDOF_BUTTON_1", 0, "NDOF Button 1", "NdofB1"},
    {NDOF_BUTTON_2, "NDOF_BUTTON_2", 0, "NDOF Button 2", "NdofB2"},
    {NDOF_BUTTON_3, "NDOF_BUTTON_3", 0, "NDOF Button 3", "NdofB3"},
    {NDOF_BUTTON_4, "NDOF_BUTTON_4", 0, "NDOF Button 4", "NdofB4"},
    {NDOF_BUTTON_5, "NDOF_BUTTON_5", 0, "NDOF Button 5", "NdofB5"},
    {NDOF_BUTTON_6, "NDOF_BUTTON_6", 0, "NDOF Button 6", "NdofB6"},
    {NDOF_BUTTON_7, "NDOF_BUTTON_7", 0, "NDOF Button 7", "NdofB7"},
    {NDOF_BUTTON_8, "NDOF_BUTTON_8", 0, "NDOF Button 8", "NdofB8"},
    {NDOF_BUTTON_9, "NDOF_BUTTON_9", 0, "NDOF Button 9", "NdofB9"},
    {NDOF_BUTTON_10, "NDOF_BUTTON_10", 0, "NDOF Button 10", "NdofB10"},
    {NDOF_BUTTON_A, "NDOF_BUTTON_A", 0, "NDOF Button A", "NdofBA"},
    {NDOF_BUTTON_B, "NDOF_BUTTON_B", 0, "NDOF Button B", "NdofBB"},
    {NDOF_BUTTON_C, "NDOF_BUTTON_C", 0, "NDOF Button C", "NdofBC"},
    /* Action Zones. */
    {EVT_ACTIONZONE_AREA, "ACTIONZONE_AREA", 0, "ActionZone Area", "AZone Area"},
    {EVT_ACTIONZONE_REGION, "ACTIONZONE_REGION", 0, "ActionZone Region", "AZone Region"},
    {EVT_ACTIONZONE_FULLSCREEN,
     "ACTIONZONE_FULLSCREEN",
     0,
     "ActionZone Fullscreen",
     "AZone FullScr"},
    /* xr */
    {EVT_XR_ACTION, "XR_ACTION", 0, "XR Action", ""},
    {0, NULL, 0, NULL, NULL},
};

/**
 * This contains overlapping items from:
 * - api_enum_event_value_keymouse_items
 * - api_enum_event_value_tweak_items
 *
 * This is needed for `km.keymap_items.new` value argument,
 * to accept values from different types.
 */
const EnumPropItem api_enum_event_value_items[] = {
    {KM_ANY, "ANY", 0, "Any", ""},
    {KM_PRESS, "PRESS", 0, "Press", ""},
    {KM_RELEASE, "RELEASE", 0, "Release", ""},
    {KM_CLICK, "CLICK", 0, "Click", ""},
    {KM_DBL_CLICK, "DOUBLE_CLICK", 0, "Double Click", ""},
    {KM_CLICK_DRAG, "CLICK_DRAG", 0, "Click Drag", ""},
    /* Used for NDOF and trackpad events. */
    {KM_NOTHING, "NOTHING", 0, "Nothing", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_event_direction_items[] = {
    {KM_ANY, "ANY", 0, "Any", ""},
    {KM_DIRECTION_N, "NORTH", 0, "North", ""},
    {KM_DIRECTION_NE, "NORTH_EAST", 0, "North-East", ""},
    {KM_DIRECTION_E, "EAST", 0, "East", ""},
    {KM_DIRECTION_SE, "SOUTH_EAST", 0, "South-East", ""},
    {KM_DIRECTION_S, "SOUTH", 0, "South", ""},
    {KM_DIRECTION_SW, "SOUTH_WEST", 0, "South-West", ""},
    {KM_DIRECTION_W, "WEST", 0, "West", ""},
    {KM_DIRECTION_NW, "NORTH_WEST", 0, "North-West", ""},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_keymap_propvalue_items[] = {
    {0, "NONE", 0, "", ""},
    {0, NULL, 0, NULL, NULL},
};

/* Mask event types used in keymap items. */
const EnumPropItem rna_enum_event_type_mask_items[] = {
    {EVT_TYPE_MASK_KEYBOARD_MODIFIER, "KEYBOARD_MODIFIER", 0, "Keyboard Modifier", ""},
    {EVT_TYPE_MASK_KEYBOARD, "KEYBOARD", 0, "Keyboard", ""},
    {EVT_TYPE_MASK_MOUSE_WHEEL, "MOUSE_WHEEL", 0, "Mouse Wheel", ""},
    {EVT_TYPE_MASK_MOUSE_GESTURE, "MOUSE_GESTURE", 0, "Mouse Gesture", ""},
    {EVT_TYPE_MASK_MOUSE_BUTTON, "MOUSE_BUTTON", 0, "Mouse Button", ""},
    {EVT_TYPE_MASK_MOUSE, "MOUSE", 0, "Mouse", ""},
    {EVT_TYPE_MASK_NDOF, "NDOF", 0, "NDOF", ""},
    {EVT_TYPE_MASK_ACTIONZONE, "ACTIONZONE", 0, "Action Zone", ""},
    {0, NULL, 0, NULL, NULL},
};

#if 0
static const EnumPropItem keymap_modifiers_items[] = {
    {KM_ANY, "ANY", 0, "Any", ""},
    {0, "NONE", 0, "None", ""},
    {KM_MOD_HELD, "HELD", 0, "Held", ""},
    {0, NULL, 0, NULL, NULL},
};
#endif

const EnumPropItem api_enum_op_type_flag_items[] = {
    {OPTYPE_REGISTER,
     "REGISTER",
     0,
     "Register",
     "Display in the info window and support the redo toolbar panel"},
    {OPTYPE_UNDO, "UNDO", 0, "Undo", "Push an undo event (needed for operator redo)"},
    {OPTYPE_UNDO_GROUPED,
     "UNDO_GROUPED",
     0,
     "Grouped Undo",
     "Push a single undo event for repeated instances of this operator"},
    {OPTYPE_BLOCKING, "BLOCKING", 0, "Blocking", "Block anything else from using the cursor"},
    {OPTYPE_MACRO, "MACRO", 0, "Macro", "Use to check if an operator is a macro"},
    {OPTYPE_GRAB_CURSOR_XY,
     "GRAB_CURSOR",
     0,
     "Grab Pointer",
     "Use so the operator grabs the mouse focus, enables wrapping when continuous grab "
     "is enabled"},
    {OPTYPE_GRAB_CURSOR_X, "GRAB_CURSOR_X", 0, "Grab Pointer X", "Grab, only warping the X axis"},
    {OPTYPE_GRAB_CURSOR_Y, "GRAB_CURSOR_Y", 0, "Grab Pointer Y", "Grab, only warping the Y axis"},
    {OPTYPE_DEPENDS_ON_CURSOR,
     "DEPENDS_ON_CURSOR",
     0,
     "Depends on Cursor",
     "The initial cursor location is used, "
     "when running from a menus or buttons the user is prompted to place the cursor "
     "before beginning the operation"},
    {OPTYPE_PRESET, "PRESET", 0, "Preset", "Display a preset button with the operators settings"},
    {OPTYPE_INTERNAL, "INTERNAL", 0, "Internal", "Removes the operator from search results"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_op_return_items[] = {
    {OP_RUNNING_MODAL,
     "RUNNING_MODAL",
     0,
     "Running Modal",
     "Keep the operator running with blender"},
    {OP_CANCELLED,
     "CANCELLED",
     0,
     "Cancelled",
     "The operator exited without doing anything, so no undo entry should be pushed"},
    {OP_FINISHED,
     "FINISHED",
     0,
     "Finished",
     "The operator exited after completing its action"},
    /* used as a flag */
    {OP_PASS_THROUGH, "PASS_THROUGH", 0, "Pass Through", "Do nothing and pass the event on"},
    {OP_INTERFACE, "INTERFACE", 0, "Interface", "Handled but not executed (popup menus)"},
    {0, NULL, 0, NULL, NULL},
};

const EnumPropItem api_enum_op_prop_tags[] = {
    {OP_PROP_TAG_ADVANCED,
     "ADVANCED",
     0,
     "Advanced",
     "The prop is advanced so UI is suggested to hide it"},
    {0, NULL, 0, NULL, NULL},
};

/* flag/enum */
const EnumPropItem api_enum_wm_report_items[] = {
    {RPT_DEBUG, "DEBUG", 0, "Debug", ""},
    {RPT_INFO, "INFO", 0, "Info", ""},
    {RPT_OP, "OPERATOR", 0, "Operator", ""},
    {RPT_PROP, "PROPERTY", 0, "Property", ""},
    {RPT_WARNING, "WARNING", 0, "Warning", ""},
    {RPT_ERROR, "ERROR", 0, "Error", ""},
    {RPT_ERROR_INVALID_INPUT, "ERROR_INVALID_INPUT", 0, "Invalid Input", ""},
    {RPT_ERROR_INVALID_CONTEXT, "ERROR_INVALID_CONTEXT", 0, "Invalid Context", ""},
    {RPT_ERROR_OUT_OF_MEMORY, "ERROR_OUT_OF_MEMORY", 0, "Out of Memory", ""},
    {0, NULL, 0, NULL, NULL},
};

#ifdef API_RUNTIME

#  include "lib_string_utils.h"

#  include "wm_api.h"

#  include "types_object.h"
#  include "types_workspace.h"

#  include "ed_screen.h"

#  include "ui.h"

#  include "dune_global.h"
#  include "dune_idprop.h"

#  include "mem_guardedalloc.h"

#  ifdef WITH_PYTHON
#    include "BPY_extern.h"
#  endif

static wmOp *api_OpProps_find_op(ApiPtr *ptr)
{
  wmWindowManager *wm = (wmWindowManager *)ptr->owner_id;

  if (wm) {
    IdProp *props = (IdProp *)ptr->data;
    for (wmOp *op = wm->ops.last; op; op = op->prev) {
      if (op->props == props) {
        return op;
      }
    }
  }

  return NULL;
}

static ApiStruct *api_OpProps_refine(ApiPtr *ptr)
{
  WMOp *op = api_OpProps_find_op(ptr);

  if (op) {
    return op->type->sapi;
  }
  else {
    return ptr->type;
  }
}

static IdProp **api_opprops_idprops(ApiPtr *ptr)
{
  return (IdProp **)&ptr->data;
}

static void api_op_name_get(ApiPtrApi *ptr, char *value)
{
  WMOp *op = (WMOp *)ptr->data;
  strcpy(value, op->type->name);
}

static int api_op_name_length(ApiPtr *ptr)
{
  WMOp *op = (WMOp *)ptr->data;
  return strlen(op->type->name);
}

static bool api_op_has_reports_get(ApiPtr *ptr)
{
  WMOp *op = (WMOp *)ptr->data;
  return (op->reports && op->reports->list.first);
}

static ApiPtr api_op_options_get(ApiPtr *ptr)
{
  return api_ptr_inherit_refine(ptr, &ApiOpOptions, ptr->data);
}

static ApiPtr api_op_props_get(ApiPtr *ptr)
{
  wmOp *op = (WMOp *)ptr->data;

  ApiPtr result;
  wm_op_props_create_ptr(&result, op->type);
  result.data = op->properties;
  return result;
}

static ApiPtr api_op_macro_props_get(ApiPtr *ptr)
{
  wmOpTypeMacro *otmacro = (WMOpTypeMacro *)ptr->data;
  wmOpType *ot = wm_optype_find(otmacro->idname, true);

  ApiPtr result;
  wm_op_props_create_ptr(&result, ot);
  result.data = otmacro->props;
  return result;
}

static void api_Event_ascii_get(ApiPtr *ptr, char *value)
{
  const wmEvent *event = ptr->data;
  value[0] = wm_event_utf8_to_ascii(event);
  value[1] = '\0';
}

static int api_Event_ascii_length(ApiPtr *ptr)
{
  const wmEvent *event = ptr->data;
  return wm_event_utf8_to_ascii(event) ? 1 : 0;
}

static void api_Event_unicode_get(ApiPtr *ptr, char *value)
{
  /* utf8 buf isn't \0 terminated */
  const wmEvent *event = ptr->data;
  size_t len = 0;

  if (event->utf8_buf[0]) {
    if (lib_str_utf8_as_unicode_step_or_error(event->utf8_buf, sizeof(event->utf8_buf), &len) !=
        lib_UTF8_ERR)
      memcpy(value, event->utf8_buf, len);
  }

  value[len] = '\0';
}

static int api_Event_unicode_length(ApiPtr *ptr)
{

  const wmEvent *event = ptr->data;
  if (event->utf8_buf[0]) {
    /* invalid value is checked on assignment so we don't need to account for this */
    return lib_str_utf8_size(event->utf8_buf);
  }
  else {
    return 0;
  }
}

static bool api_Event_is_repeat_get(ApiPtr *ptr)
{
  const wmEvent *event = ptr->data;
  return (event->flag & WM_EVENT_IS_REPEAT) != 0;
}

static bool api_Event_is_consecutive_get(ApiPtr *ptr)
{
  const wmEvent *event = ptr->data;
  return (event->flag & WM_EVENT_IS_CONSECUTIVE) != 0;
}

static float api_Event_pressure_get(ApiPtr *ptr)
{
  const wmEvent *event = ptr->data;
  return wm_event_tablet_data(event, NULL, NULL);
}

static bool api_Event_is_tablet_get(ApiPtr *ptr)
{
  const wmEvent *event = ptr->data;
  return wm_event_is_tablet(event);
}

static void api_Event_tilt_get(ApiPtr *ptr, float *values)
{
  wmEvent *event = ptr->data;
  wm_event_tablet_data(event, NULL, values);
}

static ApiPtr api_Event_xr_get(ApiPtr *ptr)
{
#  ifdef WITH_XR_OPENXR
  wmEvent *event = ptr->data;
  wmXrActionData *actiondata = wm_event_is_xr(event) ? event->customdata : NULL;
  return api_ptr_inherit_refine(ptr, &ApiXrEventData, actiondata);
#  else
  UNUSED_VARS(ptr);
  return ApiPtrNULL;
#  endif
}

static ApiPtr api_PopupMenu_layout_get(ApiPtr *ptr)
{
  struct uiPopupMenu *pup = ptr->data;
  uiLayout *layout = ui_popup_menu_layout(pup);

  ApiPtr rptr;
  api_ptr_create(ptr->owner_id, &ApiUILayout, layout, &rptr);

  return rptr;
}

static ApiPtr api_PopoverMenu_layout_get(ApiPtr *ptr)
{
  struct uiPopover *pup = ptr->data;
  uiLayout *layout = ui_popover_layout(pup);

  ApiPtr rptr;
  api_ptr_create(ptr->owner_id, &ApiUILayout, layout, &rptr);

  return rptr;
}

static ApiPtr api_PieMenu_layout_get(ApiPtr *ptr)
{
  struct uiPieMenu *pie = ptr->data;
  uiLayout *layout = ui_pie_menu_layout(pie);

  ApiPtr rptr;
  api_ptr_create(ptr->owner_id, &ApiUILayout, layout, &rptr);

  return rptr;
}

static void api_window_scene_set(ApiPtr *ptr,
                                 ApiPtr value,
                                 struct ReportList *UNUSED(reports))
{
  wmWindow *win = ptr->data;

  if (value.data == NULL) {
    return;
  }

  win->new_scene = value.data;
}

static void api_Window_scene_update(Cxt *C, ApiPtr *ptr)
{
  Main *main = cxt_data_main(C);
  wmWindow *win = ptr->data;

  /* Exception: must use context so notifier gets to the right window. */
  if (win->new_scene) {
#  ifdef WITH_PYTHON
    BPy_BEGIN_ALLOW_THREADS;
#  endif

    wm_window_set_active_scene(main, C, win, win->new_scene);

#  ifdef WITH_PYTHON
    BPy_END_ALLOW_THREADS;
#  endif

    wmWindowManager *wm = cxt_wm_manager(C);
    wm_event_add_notifier_ex(wm, win, NC_SCENE | ND_SCENEBROWSE, win->new_scene);

    if (G.debug & G_DEBUG) {
      printf("scene set %p\n", win->new_scene);
    }

    win->new_scene = NULL;
  }
}

static ApiPtr api_Window_workspace_get(ApiPtr *ptr)
{
  wmWindow *win = ptr->data;
  return api_ptr_inherit_refine(
      ptr, &ApiWorkSpace, dune_workspace_active_get(win->workspace_hook));
}

static void api_Window_workspace_set(ApiPtr *ptr,
                                     ApiPtr value,
                                     struct ReportList *UNUSED(reports))
{
  wmWindow *win = (wmWindow *)ptr->data;

  /* disallow ID-browsing away from temp screens */
  if (wm_window_is_temp_screen(win)) {
    return;
  }
  if (value.data == NULL) {
    return;
  }

  /* exception: can't set workspaces inside of area/region handlers */
  win->workspace_hook->temp_workspace_store = value.data;
}

static void api_Window_workspace_update(Cxt *C, ApiPtr *ptr)
{
  wmWindow *win = ptr->data;
  WorkSpace *new_workspace = win->workspace_hook->temp_workspace_store;

  /* exception: can't set screens inside of area/region handlers,
   * and must use context so notifier gets to the right window */
  if (new_workspace) {
    wmWindowManager *wm = cxt_wm_manager(C);
    wm_event_add_notifier_ex(wm, win, NC_SCREEN | ND_WORKSPACE_SET, new_workspace);
    win->workspace_hook->temp_workspace_store = NULL;
  }
}

ApiPtr api_window_screen_get(ApiPtr *ptr)
{
  wmWindow *win = ptr->data;
  return api_pointer_inherit_refine(
      ptr, &ApiScreen, dune_workspace_active_screen_get(win->workspace_hook));
}

static void api_window_screen_set(ApiPtr *ptr,
                                  ApiPtr value,
                                  struct ReportList *UNUSED(reports))
{
  wmWindow *win = ptr->data;
  WorkSpace *workspace = dune_workspace_active_get(win->workspace_hook);
  WorkSpaceLayout *layout_new;
  const Screen *screen = dune_workspace_active_screen_get(win->workspace_hook);

  /* disallow ID-browsing away from temp screens */
  if (screen->temp) {
    return;
  }
  if (value.data == NULL) {
    return;
  }

  /* exception: can't set screens inside of area/region handlers */
  layout_new = dune_workspace_layout_find(workspace, value.data);
  win->workspace_hook->temp_layout_store = layout_new;
}

static bool api_Window_screen_assign_poll(ApiPtr *UNUSED(ptr), ApiPtr value)
{
  Screen *screen = (Screen *)value.owner_id;
  return !screen->temp;
}

static void api_workspace_screen_update(Cxt *C, ApiPtr *ptr)
{
  wmWindow *win = ptr->data;
  WorkSpaceLayout *layout_new = win->workspace_hook->temp_layout_store;

  /* exception: can't set screens inside of area/region handlers,
   * and must use context so notifier gets to the right window */
  if (layout_new) {
    wmWindowManager *wm = cxt_wm_manager(C);
    wm_event_add_notifier_ex(wm, win, NC_SCREEN | ND_LAYOUTBROWSE, layout_new);
    win->workspace_hook->temp_layout_store = NULL;
  }
}

static ApiPtr api_Window_view_layer_get(ApiPtr *ptr)
{
  wmWindow *win = ptr->data;
  Scene *scene = wm_window_get_active_scene(win);
  ViewLayer *view_layer = wm_window_get_active_view_layer(win);
  ApiPtr scene_ptr;

  api_id_ptr_create(&scene->id, &scene_ptr);
  return api_ptr_inherit_refine(&scene_ptr, &ApiViewLayer, view_layer);
}

static void api_Window_view_layer_set(ApiPtr *ptr,
                                      ApiPtr value,
                                      struct ReportList *UNUSED(reports))
{
  wmWindow *win = ptr->data;
  ViewLayer *view_layer = value.data;

  wm_window_set_active_view_layer(win, view_layer);
}

static void api_KeyMap_modal_event_values_items_begin(CollectionPropIter *iter,
                                                      ApiPtr *ptr)
{
  wmKeyMap *km = ptr->data;

  const EnumPropItem *items = api_enum_keymap_propvalue_ite
  if ((km->flag & KEYMAP_MODAL) != 0 && km->modal_items != NULL) {
    items = km->modal_items;
  }

  const int totitem = api_enum_items_count(items);

  api_iter_array_begin(iter, (void *)items, sizeof(EnumPropItem), totitem, false, NULL);
}

static ApiPtr api_KeyMapItem_props_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;

  if (kmi->ptr) {
    lib_assert(kmi->ptr->owner_id == NULL);
    return *(kmi->ptr);
  }

  // return api_ptr_inherit_refine(ptr, &ApiOpProps, op->props);
  return ApiPtr_NULL;
}

static int api_wmKeyMapItem_map_type_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;

  return wm_keymap_item_map_type_get(kmi);
}

static void api_wmKeyMapItem_map_type_set(ApiPtr *ptr, int value)
{
  wmKeyMapItem *kmi = ptr->data;
  int map_type = api_wmKeyMapItem_map_type_get(ptr);

  if (value != map_type) {
    switch (value) {
      case KMI_TYPE_KEYBOARD:
        kmi->type = EVT_AKEY;
        kmi->val = KM_PRESS;
        break;
      case KMI_TYPE_MOUSE:
        kmi->type = LEFTMOUSE;
        kmi->val = KM_PRESS;
        break;
      case KMI_TYPE_TEXTINPUT:
        kmi->type = KM_TEXTINPUT;
        kmi->val = KM_NOTHING;
        break;
      case KMI_TYPE_TIMER:
        kmi->type = TIMER;
        kmi->val = KM_NOTHING;
        break;
      case KMI_TYPE_NDOF:
        kmi->type = NDOF_MOTION;
        kmi->val = KM_NOTHING;
        break;
    }
  }
}

/** Assumes value to be an enum from api_enum_event_type_items.
 * Function makes sure key-modifiers are only valid keys, ESC keeps it unaltered */
static void api_wmKeyMapItem_keymod_set(ApiPtr *ptr, int value)
{
  wmKeyMapItem *kmi = ptr->data;

  /* XXX, this should really be managed in an _itemf function,
   * giving a list of valid enums, then silently changing them when they are set is not
   * a good precedent, don't do this unless you have a good reason! */
  if (value == EVT_ESCKEY) {
    /* pass */
  } else if (ISKEYBOARD(value) && !ISKEYMODIFIER(value)) {
    kmi->keymod = value;
  } else {
    kmi->keymod = 0;
  }
}

static const EnumPropItem *api_KeyMapItem_type_itemf(Ctx *UNUSED(C),
                                                     ApiPtr *ptr,
                                                     ApiProp *UNUSED(prop),
                                                     bool *UNUSED(r_free))
{
  int map_type = api_wmKeyMapItem_map_type_get(ptr);

  if (map_type == KMI_TYPE_MOUSE) {
    return event_mouse_type_items;
  }
  if (map_type == KMI_TYPE_TIMER) {
    return event_timer_type_items;
  }
  if (map_type == KMI_TYPE_NDOF) {
    return event_ndof_type_items;
  }
  if (map_type == KMI_TYPE_TEXTINPUT) {
    return event_textinput_type_items;
  }
  else {
    return api_enum_event_type_items;
  }
}

static const EnumPropItem *api_KeyMapItem_propvalue_itemf(Cxt *C,
                                                          ApiPtr *ptr,
                                                          ApiProp *UNUSED(prop),
                                                          bool *UNUSED(r_free))
{
  wmWindowManager *wm = cxt_wm_manager(C);
  wmKeyConfig *kc;
  wmKeyMap *km;

  for (kc = wm->keyconfigs.first; kc; kc = kc->next) {
    for (km = kc->keymaps.first; km; km = km->next) {
      /* only check if it's a modal keymap */
      if (km->modal_items) {
        wmKeyMapItem *kmi;
        for (kmi = km->items.first; kmi; kmi = kmi->next) {
          if (kmi == ptr->data) {
            return km->modal_items;
          }
        }
      }
    }
  }

  return api_enum_keymap_propvalue_items; /* ERROR */
}

static bool api_KeyMapItem_any_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;

  if (kmi->shift == KM_ANY && kmi->ctrl == KM_ANY && kmi->alt == KM_ANY && kmi->oskey == KM_ANY) {
    return 1;
  } else {
    return 0;
  }
}

static void api_KeyMapItem_any_set(ApiPtr *ptr, bool value)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;

  if (value) {
    kmi->shift = kmi->ctrl = kmi->alt = kmi->oskey = KM_ANY;
  } else {
    kmi->shift = kmi->ctrl = kmi->alt = kmi->oskey = 0;
  }
}

static bool api_KeyMapItem_shift_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;
  return kmi->shift != 0;
}

static bool api_KeyMapItem_ctrl_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;
  return kmi->ctrl != 0;
}

static bool api_KeyMapItem_alt_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;
  return kmi->alt != 0;
}

static bool api_KeyMapItem_oskey_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = (wmKeyMapItem *)ptr->data;
  return kmi->oskey != 0;
}

static ApiPtr api_WindowManager_active_keyconfig_get(ApiPtr *ptr)
{
  wmWindowManager *wm = ptr->data;
  wmKeyConfig *kc;

  kc = lib_findstring(&wm->keyconfigs, U.keyconfigstr, offsetof(wmKeyConfig, idname));

  if (!kc) {
    kc = wm->defaultconf;
  }

  return api_ptr_inherit_refine(ptr, &ApiKeyConfig, kc);
}

static void api_WindowManager_active_keyconfig_set(ApiPtr *ptr,
                                                   ApiPtr value,
                                                   struct ReportList *UNUSED(reports))
{
  wmWindowManager *wm = ptr->data;
  wmKeyConfig *kc = value.data;

  if (kc) {
    wm_keyconfig_set_active(wm, kc->idname);
  }
}

/* -------------------------------------------------------------------- */
/** Key Config Preferences **/
static ApiPtr api_wmKeyConfig_prefs_get(ApiPtr *ptr)
{
  wmKeyConfig *kc = ptr->data;
  wmKeyConfigPrefType_Runtime *kpt_rt = dune_keyconfig_pref_type_find(kc->idname, true);
  if (kpt_rt) {
    wmKeyConfigPref *kpt = dune_keyconfig_pref_ensure(&U, kc->idname);
    return api_ptr_inherit_refine(ptr, kpt_rt->api_ext.sapi, kpt->prop);
  } else {
    return ApiPtr_NULL;
  }
}

static IdProp **api_wmKeyConfigPref_idprops(ApiPtr *ptr)
{
  return (IdProp **)&ptr->data;
}

static bool api_wmKeyConfigPref_unregister(Main *UNUSED(main), ApiStruct *type)
{
  wmKeyConfigPrefType_Runtime *kpt_rt = api_struct_dune_type_get(type);

  if (!kpt_rt) {
    return false;
  }

  api_struct_free_extension(type, &kpt_rt->api_ext);
  api_struct_free(&DUNE_API, type);

  /* Possible we're not in the preferences if they have been reset. */
  dune_keyconfig_pref_type_remove(kpt_rt);

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);
  return true;
}

static ApiStruct *api_wmKeyConfigPref_register(Main *main,
                                               ReportList *reports,
                                               void *data,
                                               const char *id,
                                               StructValidateFn validate,
                                               StructCbFn call,
                                               StructFreeFn free)
{
  const char *error_prefix = "Registering key-config preferences class:";
  wmKeyConfigPrefType_Runtime *kpt_rt, dummy_kpt_rt = {{'\0'}};
  wmKeyConfigPref dummy_kpt = {NULL};
  ApiPtr dummy_kpt_ptr;
  // bool have_function[1];

  /* setup dummy keyconf-prefs & keyconf-prefs type to store static properties in */
  api_ptr_create(NULL, &ApiKeyConfigPrefs, &dummy_kpt, &dummy_kpt_ptr);

  /* validate the python class */
  if (validate(&dummy_kpt_ptr, data, NULL /* have_function */) != 0) {
    return NULL;
  }

  STRNCPY(dummy_kpt_rt.idname, dummy_kpt.idname);
  if (strlen(id) >= sizeof(dummy_kpt_rt.idname)) {
    dune_reportf(reports,
                RPT_ERROR,
                "%s '%s' is too long, maximum length is %d",
                error_prefix,
                id,
                (int)sizeof(dummy_kpt_rt.idname));
    return NULL;
  }

  /* check if we have registered this keyconf-prefs type before, and remove it */
  kpt_rt = dune_keyconfig_pref_type_find(dummy_kpt.idname, true);
  if (kpt_rt) {
    ApiStruct *sapi = kpt_rt->api_ext.srna;
    if (!(sapi && api_wmKeyConfigPref_unregister(main, sapi))) {
      dune_reportf(reports,
                  RPT_ERROR,
                  "%s '%s', bl_idname '%s' %s",
                  error_prefix,
                  id,
                  dummy_kpt.idname,
                  sapi ? "is built-in" : "could not be unregistered");
      return NULL;
    }
  }

  /* create a new keyconf-prefs type */
  kpt_rt = mem_mallocn(sizeof(wmKeyConfigPrefType_Runtime), "keyconfigpreftype");
  memcpy(kpt_rt, &dummy_kpt_rt, sizeof(dummy_kpt_rt));

  dune_keyconfig_pref_type_add(kpt_rt);

  kpt_rt->api_ext.sapi = api_def_struct_ptr(&DUNE_API, id, &APIKeyConfigPrefs);
  kpt_rt->api_ext.data = data;
  kpt_rt->api_ext.call = call;
  kpt_rt->api_ext.free = free;
  api_struct_dune_type_set(kpt_rt->api_ext.sapi, kpt_rt);

  //  kpt_rt->draw = (have_function[0]) ? header_draw : NULL;

  /* update while blender is running */
  wm_main_add_notifier(NC_WINDOW, NULL);

  return kpt_rt->api_ext.sapi;
}

/* placeholder, doesn't do anything useful yet */
static ApiStruct *api_wmKeyConfigPref_refine(ApiPtr *ptr)
{
  return (ptr->type) ? ptr->type : &ApiKeyConfigPrefs;
}

static void api_wmKeyMapItem_idname_get(ApiPtr *ptr, char *value)
{
  wmKeyMapItem *kmi = ptr->data;
  /* Pass in a fixed size buffer as the value may be allocated based on the callbacks length. */
  char value_buf[OP_MAX_TYPENAME];
  int len = wm_op_py_idname(value_buf, kmi->idname);
  memcpy(value, value_buf, len + 1);
}

static int api_wmKeyMapItem_idname_length(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;
  char pyname[OP_MAX_TYPENAME];
  return wm_op_py_idname(pyname, kmi->idname);
}

static void api_wmKeyMapItem_idname_set(ApiPtr *ptr, const char *value)
{
  wmKeyMapItem *kmi = ptr->data;
  char idname[OP_MAX_TYPENAME];

  wm_op_bl_idname(idname, value);

  if (!STREQ(idname, kmi->idname)) {
    STRNCPY(kmi->idname, idname);

    wm_keymap_item_props_reset(kmi, NULL);
  }
}

static void api_wmKeyMapItem_name_get(ApiPtr *ptr, char *value)
{
  wmKeyMapItem *kmi = ptr->data;
  wmOpType *ot = wm_optype_find(kmi->idname, 1);
  strcpy(value, ot ? wm_optype_name(ot, kmi->ptr) : kmi->idname);
}

static int api_wmKeyMapItem_name_length(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;
  wmOpType *ot = wm_optype_find(kmi->idname, 1);
  return strlen(ot ? wm_optype_name(ot, kmi->ptr) : kmi->idname);
}

static bool api_KeyMapItem_userdefined_get(ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;
  return kmi->id < 0;
}

static ApiPtr api_WindowManager_xr_session_state_get(ApiPtr *ptr)
{
  wmWindowManager *wm = ptr->data;
  struct wmXrSessionState *state =
#  ifdef WITH_XR_OPENXR
      wm_xr_session_state_handle_get(&wm->xr);
#  else
      NULL;
  UNUSED_VARS(wm);
#  endif

  return api_ptr_inherit_refine(ptr, &ApiXrSessionState, state);
}

#  ifdef WITH_PYTHON

static bool api_op_poll_cb(Cxt *C, wmOpType *ot)
{
  extern ApiFn api_ap_poll_fb;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  bool visible;

  api_ptr_create(NULL, ot->api_ext.sapi, NULL, &ptr); /* dummy */
  fn = &api_op_poll_fn; /* api_struct_find_fn(&ptr, "poll"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  ot->api_ext.call(C, &ptr, fn, &list);

  api_param_get_lookup(&list, "visible", &ret);
  visible = *(bool *)ret;

  api_param_list_free(&list);

  return visible;
}

static int api_op_ex_cb(Cxt *C, wmOp *op)
{
  extern apiFn api_op_ex_fn;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  int result;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &api_op_ex_fn; /* api_struct_find_fn(&opr, "execute"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_get_lookup(&list, "result", &ret);
  result = *(int *)ret;

  api_param_list_free(&list);

  return result;
}

/* same as execute() but no return value */
static bool api_op_check_cb(Cxt *C, wmOp *op)
{
  extern ApiFn api_op_check_fn;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  bool result;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &api_op_check_fn; /* api_struct_find_fn(&opr, "check"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_get_lookup(&list, "result", &ret);
  result = (*(bool *)ret) != 0;

  api_param_list_free(&list);

  return result;
}

static int api_op_invoke_cb(Cxt *C, wmOp *op, const wmEvent *event)
{
  extern ApiFn api_op_invoke_fb;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  int result;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &ap_op_invoke_fn; /* api_struct_find_fn(&opr, "invoke"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  api_param_set_lookup(&list, "event", &event);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_get_lookup(&list, "result", &ret);
  result = *(int *)ret;

  api_param_list_free(&list);

  return result;
}

/* same as invoke */
static int api_op_modal_cb(Cxt *C, wmOp *op, const wmEvent *event)
{
  extern ApiFn api_op_modal_fn;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  int result;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &api_op_modal_fn; /* api_struct_find_fn(&opr, "modal"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  api_param_set_lookup(&list, "event", &event);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_get_lookup(&list, "result", &ret);
  result = *(int *)ret;

  api_param_list_free(&list);

  return result;
}

static void api_op_draw_cb(Cxt *C, wmOp *op)
{
  extern ApiFn api_op_draw_fn;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &api_op_draw_fn; /* api_struct_find_function(&opr, "draw"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_list_free(&list);
}

/* same as exec(), but call cancel */
static void api_op_cancel_cb(Cxt *C, wmOp *op)
{
  extern ApiFn api_op_cancel_fn;

  ApiPtr opr;
  ParamList list;
  ApiFn *fn;

  api_ptr_create(NULL, op->type->api_ext.sapi, op, &opr);
  fn = &api_op_cancel_fn; /* api_struct_find_fn(&opr, "cancel"); */

  api_param_list_create(&list, &opr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  op->type->api_ext.call(C, &opr, fn, &list);

  api_param_list_free(&list);
}

static char *api_op_description_cb(Cxt *C, wmOpType *ot, ApiPtr *prop_ptr)
{
  extern ApiFn api_op_description_fn;

  ApiPtr ptr;
  ParamList list;
  ApiFn *fn;
  void *ret;
  char *result;

  api_ptr_create(NULL, ot->api_ext.sapi, NULL, &ptr); /* dummy */
  fn = &api_op_description_fn; /* api_struct_find_function(&ptr, "description"); */

  api_param_list_create(&list, &ptr, fn);
  api_param_set_lookup(&list, "cxt", &C);
  api_param_set_lookup(&list, "props", prop_ptr);
  ot->api_ext.call(C, &ptr, fn, &list);

  api_param_get_lookup(&list, "result", &ret);
  result = (char *)ret;

  if (result && result[0]) {
    result = lib_strdup(result);
  } else {
    result = NULL;
  }

  api_param_list_free(&list);

  return result;
}

static bool api_op_unregister(struct Main *main, ApiStruct *type);

/* bpy_oper_wrap.c */
extern void BPY_api_op_wrapper(wmOpType *ot, void *userdata);
extern void BPY_api_op_macro_wrapper(wmOpType *ot, void *userdata);

static ApiStruct *api_op_register(Main *main,
                                  ReportList *reports,
                                  void *data,
                                  const char *id,
                                  StructValidateFn validate,
                                  StructCbFn call,
                                  StructFreeFn free)
{
  const char *error_prefix = "Registering op class:";
  wmOpType dummy_ot = {NULL};
  wmOp dummy_op = {NULL};
  ApiPtr dummy_op_ptr;
  bool have_fn[8];

  struct {
    char idname[OP_MAX_TYPENAME];
    char name[OP_MAX_TYPENAME];
    char description[API_DYN_DESCR_MAX];
    char lang_cxt[DUNE_ST_MAXNAME];
    char undo_group[OP_MAX_TYPENAME];
  } temp_buffers;

  /* setup dummy operator & operator type to store static properties in */
  dummy_op.type = &dummy_ot;
  dummy_ot.idname = temp_buffers.idname;           /* only assign the pointer, string is NULL'd */
  dummy_ot.name = temp_buffers.name;               /* only assign the pointer, string is NULL'd */
  dummy_ot.description = temp_buffers.description; /* only assign the pointer, string is NULL'd */
  dummy_ot.lang_cxt =
      temp_buffers.lang_cxt;          /* only assign the pointer, string is NULL'd */
  dummy_ot.undo_group = temp_buffers.undo_group; /* only assign the pointer, string is NULL'd */
  api_ptr_create(NULL, &ApiOp, &dummy_op, &dummy_op_ptr);

  /* clear in case they are left unset */
  temp_buffers.idname[0] = temp_buffers.name[0] = temp_buffers.description[0] =
      temp_buffers.undo_group[0] = temp_buffers.lang_cxt[0] = '\0';

  /* validate the python class */
  if (validate(&dummy_op_ptr, data, have_fn) != 0) {
    return NULL;
  }

  /* check if we have registered this operator type before, and remove it */
  {
    wmOpType *ot = wm_optype_find(dummy_ot.idname, true);
    if (ot) {
      ApiStruct *sapi = ot->api_ext.sapi;
      if (!(sapi && api_op_unregister(main, sapi))) {
        dune_reportf(reports,
                    RPT_ERROR,
                    "%s '%s', bl_idname '%s' %s",
                    error_prefix,
                    id,
                    dummy_ot.idname,
                    sapi ? "is built-in" : "could not be unregistered");
        return NULL;
      }
    }
  }

  if (!wm_op_py_idname_ok_or_report(reports, id, dummy_ot.idname)) {
    return NULL;
  }

  char idname_conv[sizeof(dummy_op.idname)];
  wm_op_bl_idname(idname_conv, dummy_ot.idname); /* convert the idname from python */

  if (!api_struct_available_or_report(reports, idname_conv)) {
    return NULL;
  }

  /* We have to set default context if the class doesn't define it. */
  if (temp_buffers.lang_cxt[0] == '\0') {
    STRNCPY(temp_buffers.lang_cxt, LANG_OP_DEFAULT);
  }

  /* Convert foo.bar to FOO_OT_bar
   * allocate all strings at once. */
  {
    const char *strings[] = {
        idname_conv,
        temp_buffers.name,
        temp_buffers.description,
        temp_buffers.translation_cxt,
        temp_buffers.undo_group,
    };
    char *strings_table[ARRAY_SIZE(strings)];
    lib_string_join_array_by_sep_char_with_tablen(
        '\0', strings_table, strings, ARRAY_SIZE(strings));

    dummy_ot.idname = strings_table[0]; /* allocated string stored here */
    dummy_ot.name = strings_table[1];
    dummy_ot.description = *strings_table[2] ? strings_table[2] : NULL;
    dummy_ot.lang_cxt = strings_table[3];
    dummy_ot.undo_group = strings_table[4];
    lib_assert(ARRAY_SIZE(strings) == 5);
  }

  /* XXX, this doubles up with the op name #29666.
   * for now just remove from dir(bpy.types) */

  /* create a new operator type */
  dummy_ot.api_ext.sapi = api_def_struct_ptr(&DUNE_API, dummy_ot.idname, &Api_Op);

  /* Operator properties are registered separately. */
  api_def_struct_flag(dummy_ot.api_ext.sapi, STRUCT_NO_IDPROPS);

  api_def_struct_pr_tags(dummy_ot.api_ext.sapi, api_enum_op_prop_tags);
  api_def_struct_translation_context(dummy_ot.api_ext.sapi, dummy_ot.translation_context);
  dummy_ot.api_ext.data = data;
  dummy_ot.api_ext.call = call;
  dummy_ot.api_ext.free = free;

  dummy_ot.pyop_poll = (have_fn[0]) ? api_op_poll_cb : NULL;
  dummy_ot.ex = (have_fn[1]) ? api_op_ex_cb : NULL;
  dummy_ot.check = (have_fn[2]) ? api_op_check_cb : NULL;
  dummy_ot.invoke = (have_fn[3]) ? api_op_invoke_cb : NULL;
  dummy_ot.modal = (have_fn[4]) ? api_op_modal_cb : NULL;
  dummy_ot.ui = (have_fn[5]) ? api_op_draw_cb : NULL;
  dummy_ot.cancel = (have_fn[6]) ? api_op_cancel_cb : NULL;
  dummy_ot.get_description = (have_fn[7]) ? api_op_description_cb : NULL;
  wm_optype_append_ptr(BPY_api_op_wrapper, (void *)&dummy_ot);

  /* update while dune sculpt is running */
  wm_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

  return dummy_ot.api_ext.sapi;
}

static bool api_op_unregister(struct Main *main, ApiStruct *type)
{
  const char *idname;
  wmOpType *ot = api_struct_dune_type_get(type);
  wmWindowManager *wm;

  if (!ot) {
    return false;
  }

  /* update while dune is running */
  wm = main->wm.first;
  if (wm) {
    wm_op_stack_clear(wm);

    wm_op_handlers_clear(wm, ot);
  }
  wm_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

  api_struct_free_extension(type, &ot->rna_ext);

  idname = ot->idname;
  wm_optype_remove_ptr(ot);

  /* Not to be confused with the RNA_struct_free that WM_operatortype_remove calls,
   * they are 2 different srna's. */
  api_struct_free(&DUNE_API, type);

  mem_freen((void *)idname);
  return true;
}

static void **api_op_instance(ApiPtr *ptr)
{
  wmOp *op = ptr->data;
  return &op->py_instance;
}

static ApiStruct *api_MacroOp_register(Main *main,
                                       ReportList *reports,
                                       void *data,
                                       const char *id,
                                       StructValidateFn validate,
                                       StructCbFn call,
                                       StructFreeFn free)
{
  const char *error_prefix = "Registering op macro class:";
  wmOpType dummy_ot = {NULL};
  wmOp dummy_op = {NULL};
  ApiPtr dummy_op_ptr;
  bool have_fn[4];

  struct {
    char idname[OP_MAX_TYPENAME];
    char name[OP_MAX_TYPENAME];
    char description[API_DYN_DESCR_MAX];
    char translation_ctx[DUNE_ST_MAXNAME];
    char undo_group[OP_MAX_TYPENAME];
  } temp_buffers;

  /* setup dummy operator & operator type to store static properties in */
  dummy_op.type = &dummy_ot;
  dummy_ot.idname = temp_buffers.idname;           /* only assign the pointer, string is NULL'd */
  dummy_ot.name = temp_buffers.name;               /* only assign the pointer, string is NULL'd */
  dummy_ot.description = temp_buffers.description; /* only assign the pointer, string is NULL'd */
  dummy_ot.lang_cxt =
      temp_buffers.translation_cxt;          /* only assign the pointer, string is NULL'd */
  dummy_ot.undo_group = temp_buffers.undo_group; /* only assign the pointer, string is NULL'd */
  api_ptr_create(NULL, &ApiMacro, &dummy_op, &dummy_op_ptr);

  /* clear in case they are left unset */
  temp_buffers.idname[0] = temp_buffers.name[0] = temp_buffers.description[0] =
      temp_buffers.undo_group[0] = temp_buffers.translation_ctx[0] = '\0';

  /* validate the python class */
  if (validate(&dummy_op_ptr, data, have_fn) != 0) {
    return NULL;
  }

  if (!wm_op_py_idname_ok_or_report(reports, id, dummy_ot.idname)) {
    return NULL;
  }

  /* check if we have registered this operator type before, and remove it */
  {
    wmOpType *ot = wm_optype_find(dummy_ot.idname, true);
    if (ot) {
      ApiStruct *sapi = ot->api_ext.sapi;
      if (!(sapi && api_op_unregister(main, sapi))) {
        dune_reportf(reports,
                    RPT_ERROR,
                    "%s '%s', bl_idname '%s' %s",
                    error_prefix,
                    id,
                    dummy_ot.idname,
                    sapi ? "is built-in" : "could not be unregistered");
        return NULL;
      }
    }
  }

  char idname_conv[sizeof(dummy_op.idname)];
  wm_op_bl_idname(idname_conv, dummy_ot.idname); /* convert the idname from python */

  if (!api_struct_available_or_report(reports, idname_conv)) {
    return NULL;
  }

  /* We have to set default context if the class doesn't define it. */
  if (temp_buffers.lang_cxt[0] == '\0') {
    STRNCPY(temp_buffers.lang_cxt, LANG_OP_DEFAULT);
  }

  /* Convert foo.bar to FOO_OT_bar
   * allocate all strings at once. */
  {
    const char *strings[] = {
        idname_conv,
        temp_buffers.name,
        temp_buffers.description,
        temp_buffers.lang_cxt,
        temp_buffers.undo_group,
    };
    char *strings_table[ARRAY_SIZE(strings)];
    lib_string_join_array_by_sep_char_with_tablen(
        '\0', strings_table, strings, ARRAY_SIZE(strings));

    dummy_ot.idname = strings_table[0]; /* allocated string stored here */
    dummy_ot.name = strings_table[1];
    dummy_ot.description = *strings_table[2] ? strings_table[2] : NULL;
    dummy_ot.lang_cxt = strings_table[3];
    dummy_ot.undo_group = strings_table[4];
    lib_assert(ARRAY_SIZE(strings) == 5);
  }

  /* XXX, this doubles up with the operator name #29666.
   * for now just remove from dir(bpy.types) */

  /* create a new operator type */
  dummy_ot.api_ext.sapi = api_def_struct_ptr(&DUNE_API, dummy_ot.idname, &ApiOp);
  api_def_struct_lang_cxt(dummy_ot.api_ext.sapi, dummy_ot.translation_ctx);
  dummy_ot.api_ext.data = data;
  dummy_ot.api_ext.call = call;
  dummy_ot.api_ext.free = free;

  dummy_ot.pyop_poll = (have_fn[0]) ? api_op_poll_cb : NULL;
  dummy_ot.ui = (have_fn[3]) ? api_op_draw_cb : NULL;

  wm_optype_append_macro_ptr(BPY_api_op_macro_wrapper, (void *)&dummy_ot);

  /* update while blender is running */
  wm_main_add_notifier(NC_SCREEN | NA_EDITED, NULL);

  return dummy_ot.api_ext.sapi;
}
#  endif /* WITH_PYTHON */

static ApiStrut *api_op_refine(ApiPtr *ptr)
{
  wmOp *op = (wmOp *)opr->data;
  return (op->type && op->type->api_ext.sapi) ? op->type->api_ext.sapi : &ApiOp;
}

static ApiStruct *api_MacroOp_refine(ApiPtr *opr)
{
  wmOp *op = (wmOp *)opr->data;
  return (op->type && op->type->api_ext.sapi) ? op->type->api_ext.sapi : &ApiMacro;
}

/* just to work around 'const char *' warning and to ensure this is a python op */
static void api_op_bl_idname_set(ApiPtr *ptr, const char *value)
{
  wmOp *data = (wmOp *)(ptr->data);
  char *str = (char *)data->type->idname;
  if (!str[0]) {
    lib_strncpy(str, value, OP_MAX_TYPENAME); /* utf8 already ensured */
  }
  else {
    lib_assert_msg(0, "setting the bl_idname on a non-builtin op");
  }
}

static void api_op_bl_label_set(ApiPtr *ptr, const char *value)
{
  wmOp *data = (wmOp *)(ptr->data);
  char *str = (char *)data->type->name;
  if (!str[0]) {
    lib_strncpy(str, value, OP_MAX_TYPENAME); /* utf8 already ensured */
  }
  else {
    lib_assert_msg(0, "setting the bl_label on a non-builtin operator");
  }
}

/**
 * Use callbacks that check for NULL instead of clearing #PROP_NEVER_NULL on the string property,
 * so the internal value may be NULL, without allowing Python to assign `None` which doesn't
 * make any sense in this case.
 */
#  define OP_STR_MAYBE_NULL_GETSET(attr, len) \
    static void api_op_bl_##attr##_set(ApiPtr *ptr, const char *value) \
    { \
      wmOp *data = (wmOp *)(ptr->data); \
      char *str = (char *)data->type->attr; \
      if (str && !str[0]) { \
        lib_strncpy(str, value, len); /* utf8 already ensured */ \
      } \
      else { \
        lib_assert( \
            !"setting the bl_" STRINGIFY(lang_cxt) " on a non-builtin operator"); \
      } \
    } \
    static void api_op_bl_##attr##_get(ApiPtr *ptr, char *value) \
    { \
      const wmOp *data = (wmOp *)(ptr->data); \
      const char *str = data->type->attr; \
      strcpy(value, str ? str : ""); \
    } \
    static int api_op_bl_##attr##_length(ApiPtr *ptr) \
    { \
      const wmOp *data = (ApiPtr *)(ptr->data); \
      const char *str = data->type->attr; \
      return str ? strlen(str) : 0; \
    }

OP_STR_MAYBE_NULL_GETSET(translation_ctx, DUNE_ST_MAXNAME)
OP_STR_MAYBE_NULL_GETSET(description, API_DYN_DESCR_MAX)
OP_STR_MAYBE_NULL_GETSET(undo_group, OP_MAX_TYPENAME)

static void api_KeyMapItem_update(Main *UNUSED(main), Scene *UNUSED(scene), ApiPtr *ptr)
{
  wmKeyMapItem *kmi = ptr->data;
  wm_keyconfig_update_tag(NULL, kmi);
}

#else /* API_RUNTIME */

/* expose `Op.options` as its own type so we can control each flags use
 * (some are read-only). */
static void api_def_op_options_runtime(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "OpOptions", NULL);
  api_def_struct_ui_text(sapi, "Op Options", "Runtime options");
  api_def_struct_stype(sapi, "wmOp");

  prop = api_def_prop(sapi, "is_grab_cursor", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stypr(prop, NULL, "flag", OP_IS_MODAL_GRAB_CURSOR);
  api_def_prop_ui_text(prop, "Grab Cursor", "True when the cursor is grabbed");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "is_invoke", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", OP_IS_INVOKE);
  api_def_prop_ui_text(
      prop, "Invoke", "True when invoked (even if only the execute callbacks available)");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "is_repeat", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", OP_IS_REPEAT);
  api_def_prop_ui_text(prop, "Repeat", "True when run from the 'Adjust Last Operation' panel");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "is_repeat_last", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", OP_IS_REPEAT_LAST);
  api_def_prop_ui_text(prop, "Repeat Call", "True when run from the operator 'Repeat Last'");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);

  prop = api_def_prop(sapi, "use_cursor_region", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", OP_IS_MODAL_CURSOR_REGION);
  api_def_prop_ui_text(
      prop, "Focus Region", "Enable to use the region under the cursor for modal execution");
}

static void api_def_op_common(ApiStruct *sapi)
{
  ApiProp *prop;

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(prop, "api_op_name_get", "api_op_name_length", NULL);
  api_def_prop_ui_text(prop, "Name", "");

  prop = api_def_prop(sapi, "props", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "OpProps");
  api_def_prop_ui_text(prop, "Props", "");
  api_def_prop_ptr_fns(prop, "api_op_props_get", NULL, NULL, NULL);

  prop = api_def_prop(sapi, "has_reports", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE); /* this is 'virtual' property */
  api_def_prop_bool_fns(prop, "api_Op_has_reports_get", NULL);
  api_def_prop_ui_text(
      prop,
      "Has Reports",
      "Operator has a set of reports (warnings and errors) from last execution");

  /* Registration */
  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->idname");
  /* String stored here is the 'BL' identifier (`OPMODULE_OT_my_op`),
   * not the 'python' identifier (`opmodule.my_op`). */
  api_def_prop_string_maxlength(prop, OP_MAX_TYPENAME);
  api_def_prop_string_fns(prop, NULL, NULL, "api_op_bl_idname_set");
  // RNA_def_property_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_REGISTER);
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "bl_label", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->name");
  api_def_prop_string_maxlength(prop, OP_MAX_TYPENAME); /* else it uses the pointer size! */
  api_def_prop_string_fns(prop, NULL, NULL, "api_op_bl_label_set");
  // api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_REGISTER);

  prop = api_def_prop(sapi, "bl_translation_ctx", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->translation_ctx");
  api_def_prop_string_maxlength(prop, DUNE_ST_MAXNAME); /* else it uses the pointer size! */
  api_def_prop_string_fns(prop,
                          "api_op_bl_translation_ctx_get",
                          "api_op_bl_translation_ctx_length",
                          "api_op_bl_translation_ctx_set");
  api_def_prop_flag(prop, LANG_OP_DEFAULT);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);

  prop = api_def_prop(sapi, "bl_description", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->description");
  api_def_prop_string_maxlength(prop, API_DYN_DESCR_MAX); /* else it uses the pointer size! */
  api_def_prop_string_fns(prop,
                          "api_op_bl_description_get",
                          "api_op_bl_description_length
                          "api_op_bl_description_set");
  // api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);

  prop = api_def_prop(sapi, "bl_undo_group", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "type->undo_group");
  api_def_prop_string_maxlength(prop, OP_MAX_TYPENAME); /* else it uses the pointer size! */
  api_def_prop_string_fns(prop,
                          "api_op_bl_undo_group_get",
                          "api_op_bl_undo_group_length",
                          "api_op_bl_undo_group_set");
  // api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);

  prop = api_def_prop(sapi, "bl_options", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type->flag");
  api_def_prop_enum_items(prop, api_enum_operator_type_flag_items);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL | PROP_ENUM_FLAG);
  api_def_prop_ui_text(prop, "Options", "Options for this operator type");

  prop = api_def_prop(sapi, "bl_cursor_pending", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type->cursor_pending");
  api_def_prop_enum_items(prop, rna_enum_window_cursor_items);
  api_def_prop_flag(prop, PROP_REGISTER_OPTIONAL);
  api_def_prop_ui_text(
      prop,
      "Idle Cursor",
      "Cursor to use when waiting for the user to select a location to activate the operator "
      "(when ``bl_options`` has ``DEPENDS_ON_CURSOR`` set)");
}

static void api_def_op(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Op", NULL);
  api_def_struct_ui_text(
      sapi, "Op", "Storage of an op being executed, or registered after execution");
  api_def_struct_stype(sapi, "wmOp");
  api_def_struct_refine_fn(sapi, "api_op_refine");
#  ifdef WITH_PYTHON
  api_def_struct_register_fns(
      sapi, "api_op_register", "api_op_unregister", "api_op_instance");
#  endif
  api_def_struct_translation_ctx(sapi, LANG_OP_DEFAULT);
  api_def_struct_flag(sapi, STRUCT_PUBLIC_NAMESPACE_INHERIT);

  api_def_op_common(dapi);

  prop = api_def_prop(sapi, "layout", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "UILayout");

  prop = api_def_prop(sapi, "options", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "OpOptions");
  api_def_prop_ptr_fns(prop, "api_op_options_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Options", "Runtime options");

  prop = api_def_prop(sapi, "macros", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "macro", NULL);
  api_def_prop_struct_type(prop, "Macro");
  api_def_prop_ui_text(prop, "Macros", "");

  api_api_op(sapi);

  srna = api_def_struct(dapi, "OpProps", NULL);
  api_def_struct_ui_text(dapi, "Op Props", "Input props of an op");
  api_def_struct_refine_fn(sapi, "api_OpProps_refine");
  api_def_struct_idprops_fn(sapi, "api_OpProps_idprops");
  api_def_struct_prop_tags(sapi, api_enum_op_prop_tags);
  api_def_struct_flag(sapi, STRUCT_NO_DATABLOCK_IDPROPS | STRUCT_NO_CONTEXT_WITHOUT_OWNER_ID);
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
}

static void api_def_macro_op(DuneApi *dapi)
{
  ApiStruct *sapi;

  sapi = api_def_struct(dapi, "Macro", NULL);
  api_def_struct_ui_text(
      sapi,
      "Macro Op",
      "Storage of a macro operator being executed, or registered after execution");
  api_def_struct_stype(sapi, "wmOp");
  api_def_struct_refine_fn(sapi, "api_MacroOp_refine");
#  ifdef WITH_PYTHON
  api_def_struct_register_fns(
      sapi, "api_MacroOp_register", "api_op_unregister", "api_op_instance");
#  endif
  api_def_struct_translation_cxt(sapi, LANG_OP_DEFAULT);
  api_def_struct_flag(sapi, STRUCT_PUBLIC_NAMESPACE_INHERIT);

  api_def_op_common(sapi);

  api_api_macro(sapi);
}

static void api_def_op_type_macro(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "OpMacro", NULL);
  api_def_struct_ui_text(
      sapi, "Operator Macro", "Storage of a sub operator in a macro after it has been added");
  api_def_struct_stype(sapi, "wmOpTypeMacro");

#  if 0
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_ui_text(prop, "Name", "Name of the sub operator");
  api_def_struct_name_prop(sapi, prop);
#  endif

  prop = api_def_prop(sapi, "props", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "OpProps");
  api_def_prop_ui_text(prop, "Props", "");
  api_def_prop_ptr_fns(prop, "api_OpMacro_props_get", NULL, NULL, NULL);
}

static void api_def_op_utils(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "OpMousePath", "PropGroup");
  api_def_struct_ui_text(
      sapi, "Op Mouse Path", "Mouse path values for operators that record such paths");

  prop = api_def_prop(sapi, "loc", PROP_FLOAT, PROP_XYZ);
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_array(prop, 2);
  api_def_prop_ui_text(prop, "Location", "Mouse location");

  prop = api_def_prop(sapi, "time", PROP_FLOAT, PROP_NONE);
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_ui_text(prop, "Time", "Time of mouse location");
}

static void api_def_op_filelist_element(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "OpFileListElement", "PropGroup");
  api_def_struct_ui_text(sapi, "Op File List Element", "");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_FILENAME);
  api_def_prop_flag(prop, PROP_IDPROP);
  api_def_prop_ui_text(prop, "Name", "Name of a file or directory within a file list");
}

static void api_def_event(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Event", NULL);
  api_def_struct_ui_text(sapi, "Event", "Window Manager Event");
  api_def_struct_stype(sapi, "wmEvent");

  spi_define_verify_sapi(0); /* not in sdna */

  /* strings */
  prop = api_def_prop(sapi, "ascii", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(prop, "rna_Event_ascii_get", "api_Event_ascii_length", NULL);
  api_def_prop_ui_text(prop, "ASCII", "Single ASCII character for this event");

  prop = api_def_prop(sapi, "unicode", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_string_fns(prop, "api_Event_unicode_get", "api_Event_unicode_length", NULL);
  api_def_prop_ui_text(prop, "Unicode", "Single unicode character for this event");

  /* enums */
  prop = api_def_prop(sapi, "value", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "val");
  api_def_prop_enum_items(prop, api_enum_event_value_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Value", "The type of event, only applies to some");

  prop = api_def_prop(sapi, "value_prev", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "prev_val");
  api_def_prop_enum_items(prop, api_enum_event_value_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Previous Value", "The type of event, only applies to some");

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_event_type_items);
  api_def_prop_translation_context(prop, BLT_I18NCONTEXT_UI_EVENTS);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Type", "");

  prop = api_def_prop(sapi, "type_prev", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "prev_type");
  api_def_prop_enum_items(prop, api_enum_event_type_items);
  api_def_prop_translation_cxt(prop, LANG_UI_EVENTS);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Previous Type", "");

  prop = api_def_prop(sapi, "direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "direction");
  api_def_prop_enum_items(prop, api_enum_event_direction_items);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Direction", "The direction (only applies to drag events)");

  /* keyboard */
  prop = api_def_prop(sapo, "is_repeat", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Event_is_repeat_get", NULL);
  api_def_prop_ui_text(prop, "Is Repeat", "The event is generated by holding a key down");

  /* Track-pad & NDOF. */
  prop = api_def_prop(sapi, "is_consecutive", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Event_is_consecutive_get", NULL);
  api_def_prop_ui_text(prop,
                       "Is Consecutive",
                       "Part of a track-pad or NDOF motion, "
                       "interrupted by cursor motion, button or key press events");

  /* mouse */
  prop = api_def_prop(sapi, "mouse_x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "xy[0]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse X Position", "The window relative horizontal location of the mouse");

  prop = api_def_prop(sapi, "mouse_y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "xy[1]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse Y Position", "The window relative vertical location of the mouse");

  prop = api_def_prop(sapi, "mouse_region_x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "mval[0]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse X Position", "The region relative horizontal location of the mouse");

  prop = api_def_prop(sapi, "mouse_region_y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "mval[1]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse Y Position", "The region relative vertical location of the mouse");

  prop = api_def_prop(sapi, "mouse_prev_x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "prev_xy[0]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse Previous X Position", "The window relative horizontal location of the mouse");

  prop = api_def_prop(sapi, "mouse_prev_y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "prev_xy[1]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "Mouse Previous Y Position", "The window relative vertical location of the mouse");

  prop = api_def_prop(sapi, "mouse_prev_press_x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "prev_press_xy[0]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop,
                           "Mouse Previous X Press Position",
                           "The window relative horizontal location of the last press event");

  prop = apo_def_prop(sapi, "mouse_prev_press_y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "prev_press_xy[1]");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop,
                       "Mouse Previous Y Press Position",
                       "The window relative vertical location of the last press event");

  prop = api_def_prop(sapi, "pressure", PROP_FLOAT, PROP_FACTOR);
  api_def_prop_float_default(prop, 1.0f);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_fns(prop, "api_Event_pressure_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Tablet Pressure", "The pressure of the tablet or 1.0 if no tablet present");

  prop = api_def_prop(sapi, "tilt", PROP_FLOAT, PROP_XYZ_LENGTH);
  api_def_prop_array(prop, 2);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_float_fns(prop, "api_Event_tilt_get", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Tablet Tilt", "The pressure of the tablet or zeroes if no tablet present");

  prop = api_def_prop(sapi, "is_tablet", PROP_BOOL, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_bool_fns(prop, "api_Event_is_tablet_get", NULL);
  api_def_prop_ui_text(prop, "Is Tablet", "The event has tablet data");

  prop = api_def_prop(sapi, "is_mouse_absolute", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "tablet.is_motion_absolute", 1);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Absolute Motion", "The last motion event was an absolute input");

  /* xr */
  prop = api_def_prop(sapi, "xr", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "XrEventData");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ptr_fns(prop, "api_Event_xr_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "XR", "XR event data");

  /* modifiers */
  prop = api_def_prop(sapi, "shift", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier", KM_SHIFT);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Shift", "True when the Shift key is held");
  api_def_prop_translation_ctx(prop, LANG_ID_WINDOWMANAGER);

  prop = api_def_prop(sapi, "ctrl", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier", KM_CTRL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Ctrl", "True when the Ctrl key is held");

  prop = api_def_prop(sapi, "alt", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier", KM_ALT);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Alt", "True when the Alt/Option key is held");

  prop = api_def_prop(sapi, "oskey", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "modifier", KM_OSKEY);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "OS Key", "True when the Cmd key is held");

  api_define_verify_stype(1); /* not in sdna */
}

static void api_def_timer(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Timer", NULL);
  api_def_struct_ui_text(sapi, "Timer", "Window event timer");
  api_def_struct_stype(sapi, "wmTimer");

  api_define_verify_stype(0); /* not in sdna */

  /* could wrap more, for now this is enough */
  prop = api_def_prop(sapi, "time_step", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "timestep");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Time Step", "");

  prop = api_def_prop(sapi, "time_delta", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "delta");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Delta", "Time since last step in seconds");

  prop = api_def_prop(sapi, "time_duration", PROP_FLOAT, PROP_NONE);
  api_def_prop_float_stype(prop, NULL, "duration");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Delta", "Time since last step in seconds");

  api_define_verify_stype(1); /* not in sdna */
}

static void api_def_popup_menu_wrapper(DuneApi *dapi,
                                       const char *api_type,
                                       const char *c_type,
                                       const char *layout_get_fn)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, api_type, NULL);
  /* UI name isn't visible, name same as type. */
  api_def_struct_ui_text(sapi, api_type, "");
  api_def_struct_stype(sapi, c_type);

  api_define_verify_stype(0); /* not in sdna */

  /* could wrap more, for now this is enough */
  prop = api_def_prop(sapi, "layout", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "UILayout");
  api_def_prop_ptr_fns(prop, layout_get_fn, NULL, NULL, NULL);

  api_define_verify_sdna(1); /* not in sdna */
}

static void soi_def_popupmenu(DuneApi *dapi)
{
  api_def_popup_menu_wrapper(dapi, "UIPopupMenu", "uiPopupMenu", "rna_PopupMenu_layout_get");
}

static void api_def_popovermenu(DuneApi *dapi)
{
  api_def_popup_menu_wrapper(dapi, "UIPopover", "uiPopover", "rna_PopoverMenu_layout_get");
}

static void api_def_piemenu(DuneApi *dapi)
{
  api_def_popup_menu_wrapper(dapi, "UIPieMenu", "uiPieMenu", "rna_PieMenu_layout_get");
}

static void api_def_window_stereo3d(DuneApi dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  srna = api_def_struct(dapi, "Stereo3dDisplay", NULL);
  api_def_struct_stype(sapi, "Stereo3dFormat");
  api_def_struct_clear_flag(sapi, STRUCT_UNDO);
  api_def_struct_ui_text(sapi, "Stereo 3D Display", "Settings for stereo 3D display");

  prop = api_def_prop(sapi, "display_mode", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_stereo3d_display_items);
  api_def_prop_ui_text(prop, "Display Mode", "");

  prop = api_def_prop(sapi, "anaglyph_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_stereo3d_anaglyph_type_items);
  api_def_prop_ui_text(prop, "Anaglyph Type", "");

  prop = api_def_prop(sapi, "interlace_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_items(prop, api_enum_stereo3d_interlace_type_items);
  api_def_prop_ui_text(prop, "Interlace Type", "");

  prop = api_def_prop(sapi, "use_interlace_swap", PROP_BOOLEAN, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", S3D_INTERLACE_SWAP);
  api_def_prop_ui_text(prop, "Swap Left/Right", "Swap left and right stereo channels");

  prop = api_def_prop(sapi, "use_sidebyside_crosseyed", PROP_BOOL, PROP_BOOL);
  api_def_prop_bool_stype(prop, NULL, "flag", S3D_SIDEBYSIDE_CROSSEYED);
  api_def_prop_ui_text(prop, "Cross-Eyed", "Right eye should see left image and vice versa");
}

static void api_def_window(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "Window", NULL);
  api_def_struct_ui_text(sapi, "Window", "Open window");
  api_def_struct_stype(sapi, "wmWindow");

  prop = api_def_prop(sapi, "parent", PROP_PTR, PROP_NONE);
  api_def_prop_ui_text(prop, "Parent Window", "Active workspace and scene follow this window");

  api_def_window_stereo3d(brna);

  prop = api_def_prop(sapi, "scene", PROP_PTR, PROP_NONE);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
  api_def_prop_ptr_fns(prop, NULL, "rna_Window_scene_set", NULL, NULL);
  api_def_prop_ui_text(prop, "Scene", "Active scene to be edited in the window");
  api_def_prop_flag(prop, PROP_CTX_UPDATE);
  api_def_prop_update(prop, 0, "api_Window_scene_update");

  prop = api_def_prop(sapi, "workspace", PROP_POINTER, PROP_NONE);
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "WorkSpace");
  api_def_prop_ui_text(prop, "Workspace", "Active workspace showing in the window");
  api_def_prop_ptr_fns(
      prop, "api_Window_workspace_get", "rna_Window_workspace_set", NULL, NULL);
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_CONTEXT_UPDATE);
  api_def_prop_update(prop, 0, "rna_Window_workspace_update");

  prop = api_def_prop(sapi, "screen", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "Screen");
  api_def_prop_ui_text(prop, "Screen", "Active workspace screen showing in the window");
  api_def_prop_ptr_fns(prop,
                       "api_Window_screen_get",
                       "api_Window_screen_set",
                       NULL,
                       "api_Window_screen_assign_poll");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_SCREEN);
  api_def_prop_flag(prop, PROP_NEVER_NULL | PROP_EDITABLE | PROP_CONTEXT_UPDATE);
  api_def_prop_update(prop, 0, "api_workspace_screen_update");

  prop = api_def_prop(sapi, "view_layer", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "ViewLayer");
  api_def_prop_ptr_fns(
      prop, "api_Window_view_layer_get", "rna_Window_view_layer_set", NULL, NULL);
  api_def_prop_ui_text(
      prop, "Active View Layer", "The active workspace view layer showing in the window");
  api_def_prop_flag(prop, PROP_EDITABLE | PROP_NEVER_NULL);
  api_def_prop_update(prop, NC_SCREEN | ND_LAYER, NULL);

  prop = api_def_prop(sapi, "x", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "posx");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "X Position", "Horizontal location of the window");

  prop = api_def_prop(sapi, "y", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "posy");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Y Position", "Vertical location of the window");

  prop = api_def_prop(sapi, "width", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "sizex");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Width", "Window width");

  prop = api_def_prop(sapi, "height", PROP_INT, PROP_UNSIGNED);
  api_def_prop_int_stype(prop, NULL, "sizey");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Height", "Window height");

  prop = api_def_prop(sapi, "stereo_3d_display", PROP_POINTER, PROP_NONE);
  api_def_prop_ptr_stype(prop, NULL, "stereo3d_format");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_struct_type(prop, "Stereo3dDisplay");
  api_def_prop_ui_text(prop, "Stereo 3D Display", "Settings for stereo 3D display");

  api_window(sapi);
}

/* curve.splines */
static void api_def_wm_keyconfigs(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;
  ApiProp *prop;

  api_def_prop_sapi(cprop, "KeyConfigurations");
  sapi = api_def_struct(dapi, "KeyConfigurations", NULL);
  api_def_struct_stype(sapi, "wmWindowManager");
  api_def_struct_ui_text(sapi, "KeyConfigs", "Collection of KeyConfigs");

  prop = api_def_prop(sapi, "active", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyConfig");
  api_def_prop_ptr_fns(prop,
                       "api_WindowManager_active_keyconfig_get",
                       "api_WindowManager_active_keyconfig_set",
                       NULL,
                       NULL);
  api_def_prop_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Active KeyConfig", "Active key configuration (preset)");

  prop = api_def_prop(sapi, "default", PROP_PTR, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "defaultconf");
  api_def_prop_struct_type(prop, "KeyConfig");
  api_def_prop_ui_text(prop, "Default Key Configuration", "Default builtin key configuration");

  prop = api_def_prop(sapi, "addon", PROP_PTR, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "addonconf");
  api_def_prop_struct_type(prop, "KeyConfig");
  api_def_prop_ui_text(
      prop,
      "Add-on Key Configuration",
      "Key configuration that can be extended by add-ons, and is added to the active "
      "configuration when handling events");

  prop = api_def_prop(sapi, "user", PROP_PTR, PROP_NEVER_NULL);
  api_def_prop_ptr_stype(prop, NULL, "userconf");
  api_def_prop_struct_type(prop, "KeyConfig");
  api_def_prop_ui_text(
      prop,
      "User Key Configuration",
      "Final key configuration that combines keymaps from the active and add-on configurations, "
      "and can be edited by the user");

  api_keyconfigs(sapi);
}

static void api_def_windowmanager(DuneApi *dapi)
{
  ApiStruct *srna;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "WindowManager", "ID");
  api_def_struct_ui_text(
      sapi,
      "Window Manager",
      "Window manager data-block defining open windows and other user interface data");
  api_def_struct_clear_flag(sapi, STRUCT_ID_REFCOUNT);
  api_def_struct_stype(sapi, "wmWindowManager");

  prop = api_def_prop(sapi, "ops", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Op");
  api_def_prop_ui_text(prop, "Ops", "Operator registry");

  prop = api_def_prop(sapi, "windows", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "Window");
  api_def_prop_ui_text(prop, "Windows", "Open windows");

  prop = api_def_prop(sapi, "keyconfigs", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyConfig");
  api_def_prop_ui_text(prop, "Key Configurations", "Registered key configurations");
  api_def_wm_keyconfigs(dapi, prop);

  prop = api_def_prop(sapi, "xr_session_settings", PROP_PTR, PROP_NONE);
  api_def_prop_ptr_sapi(prop, NULL, "xr.session_settings");
  api_def_prop_flag(prop, PROP_NEVER_NULL);
  api_def_prop_ui_text(prop, "XR Session Settings", "");

  prop = api_def_prop(sapi, "xr_session_state", PROP_POINTER, PROP_NONE);
  api_def_prop_struct_type(prop, "XrSessionState");
  api_def_prop_ptr_fns(prop, "rna_WindowManager_xr_session_state_get", NULL, NULL, NULL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "XR Session State", "Runtime state information about the VR session");

  api_api_wm(sapi);
}

/* keyconfig.items */
static void api_def_keymap_items(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  api_def_prop_sapi(cprop, "KeyMapItems");
  sapi = api_def_struct(sapi, "KeyMapItems", NULL);
  api_def_struct_stype(sapi, "wmKeyMap");
  api_def_struct_ui_text(sapi, "KeyMap Items", "Collection of keymap items");

  api_api_keymapitems(sapi);
}

static void api_def_wm_keymaps(DuneApi *dapi, ApiProp *cprop)
{
  ApiStruct *sapi;

  api_def_prop_sapi(cprop, "KeyMaps");
  sapi = api_def_struct(dapi, "KeyMaps", NULL);
  api_def_struct_stype(sapi, "wmKeyConfig");
  api_def_struct_ui_text(sapi, "Key Maps", "Collection of keymaps");

  api_api_keymaps(sapi);
}

static void api_def_keyconfig_prefs(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  sapi = api_def_struct(dapi, "KeyConfigPrefs", NULL);
  api_def_struct_ui_text(sapi, "Key-Config Prefs", "");
  api_def_struct_stype(sapi, "wmKeyConfigPref"); /* WARNING: only a bAddon during registration */

  api_def_struct_refine_fn(sapi, "rna_wmKeyConfigPref_refine");
  api_def_struct_register_fns(
      sapi, "api_wmKeyConfigPref_register", "rna_wmKeyConfigPref_unregister", NULL);
  api_def_struct_idprops_fns(sapi, "rna_wmKeyConfigPref_idprops");
  api_def_struct_flag(sapi, STRUCT_NO_DATABLOCK_IDPROPS); /* Mandatory! */

  /* registration */
  api_define_verify_stype(0);
  prop = api_def_prop(sapi, "bl_idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_flag(prop, PROP_REGISTER);
  api_define_verify_stype(1);
}

static void api_def_keyconfig(DuneApi *dapi)
{
  ApiStruct *sapi;
  ApiProp *prop;

  static const EnumPropItem map_type_items[] = {
      {KMI_TYPE_KEYBOARD, "KEYBOARD", 0, "Keyboard", ""},
      {KMI_TYPE_MOUSE, "MOUSE", 0, "Mouse", ""},
      {KMI_TYPE_NDOF, "NDOF", 0, "NDOF", ""},
      {KMI_TYPE_TEXTINPUT, "TEXTINPUT", 0, "Text Input", ""},
      {KMI_TYPE_TIMER, "TIMER", 0, "Timer", ""},
      {0, NULL, 0, NULL, NULL},
  };

  /* KeyConfig */
  sapi = api_def_struct(dapi, "KeyConfig", NULL);
  api_def_struct_stype(sapi, "wmKeyConfig");
  api_def_struct_ui_text(sapi, "Key Configuration", "Input configuration, including keymaps");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_ui_text(prop, "Name", "Name of the key configuration");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "keymaps", PROP_COLLECTION, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyMap");
  api_def_prop_ui_text(prop, "Key Maps", "Key maps configured as part of this configuration");
  api_def_wm_keymaps(dapi, prop);

  prop = api_def_prop(sapi, "is_user_defined", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYCONF_USER);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop, "User Defined", "Indicates that a keyconfig was defined by the user");

  /* Collection active property */
  prop = api_def_prop(sapi, "prefs", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "KeyConfigPrefs");
  api_def_prop_ptr_fns(prop, "api_wmKeyConfig_prefs_get", NULL, NULL, NULL);

  api_api_keyconfig(sapi);

  /* KeyMap */
  sapi = api_def_struct(dapi, "KeyMap", NULL);
  api_def_struct_stype(sapi, "wmKeyMap");
  api_def_struct_ui_text(sapi, "Key Map", "Input configuration, including keymaps");

  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Name", "Name of the key map");
  api_def_struct_name_prop(sapi, prop);

  prop = api_def_prop(sapi, "bl_owner_id", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "owner_id");
  api_def_prop_ui_text(prop, "Owner", "Internal owner");

  prop = api_def_prop(sapi, "space_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "spaceid");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_items(prop, api_enum_space_type_items);
  api_def_prop_ui_text(prop, "Space Type", "Optional space type keymap is associated with");

  prop = api_def_prop(sapi, "region_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "regionid");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_enum_items(prop, api_enum_region_type_items);

  api_def_prop_ui_text(prop, "Region Type", "Optional region type keymap is associated with");

  prop = api_def_prop(sapi, "keymap_items", PROP_COLLECTION, PROP_NONE);
  api_def_prop_collection_stype(prop, NULL, "items", NULL);
  api_def_prop_struct_type(prop, "KeyMapItem");
  api_def_prop_ui_text(
      prop, "Items", "Items in the keymap, linking an operator to an input event");
  api_def_keymap_items(dapi, prop);

  prop = api_def_prop(sapi, "is_user_modified", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYMAP_USER_MODIFIED);
  api_def_prop_ui_text(prop, "User Defined", "Keymap is defined by the user");

  prop = api_def_prop(sapi, "is_modal", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYMAP_MODAL);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop,
      "Modal Keymap",
      "Indicates that a keymap is used for translate modal events for an operator");

  prop = api_def_prop(sapi, "show_expanded_items", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYMAP_EXPANDED);
  api_def_prop_ui_text(prop, "Items Expanded", "Expanded in the user interface");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);

  prop = api_def_prop(sapi, "show_expanded_children", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KEYMAP_CHILDREN_EXPANDED);
  api_def_prop_ui_text(prop, "Children Expanded", "Children expanded in the user interface");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);

  prop = api_def_prop(sapi, "modal_event_values", PROP_COLLECTION, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_struct_type(prop, "EnumPropItem");
  api_def_prop_collection_fns(prop,
                              "api_KeyMap_modal_event_values_items_begin",
                              "api_iter_array_next",
                              "api_iter_array_end",
                              "api_iter_array_get",
                              NULL,
                              NULL,
                              NULL,
                              NULL);
  api_def_prop_ui_text(prop,
                       "Modal Events",
                       "Give access to the possible event values of this modal keymap's items "
                       "(#KeyMapItem.propvalue), for API introspection");

  api_api_keymap(sapi);

  /* KeyMapItem */
  sapi = api_def_struct(dapi, "KeyMapItem", NULL);
  api_def_struct_stype(sapi, "wmKeyMapItem");
  api_def_struct_ui_text(sapi, "Key Map Item", "Item in a Key Map");

  prop = api_def_prop(sapi, "idname", PROP_STRING, PROP_NONE);
  api_def_prop_string_stype(prop, NULL, "idname");
  api_def_prop_ui_text(prop, "Id", "Id of op to call on input event");
  api_def_prop_string_fns(prop,
                          "api_wmKeyMapItem_idname_get",
                          "api_wmKeyMapItem_idname_length",
                          "api_wmKeyMapItem_idname_set");
  api_def_prop_string_search_fn(prop,
                               "wm_optype_idname_visit_for_search",
                                PROP_STRING_SEARCH_SORT | PROP_STRING_SEARCH_SUGGESTION);
  api_def_struct_name_prop(sapi, prop);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  /* this is in fact the op name, but if the op can't be found we
   * fallback on the op ID */
  prop = api_def_prop(sapi, "name", PROP_STRING, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "Name", "Name of op (translated) to call on input event");
  api_def_prop_string_fns(
      prop, "api_wmKeyMapItem_name_get", "api_wmKeyMapItem_name_length", NULL);

  prop = api_def_prop(sapi, "props", PROP_PTR, PROP_NONE);
  api_def_prop_struct_type(prop, "OpProps");
  api_def_prop_ptr_fns(prop, "api_KeyMapItem_props_get", NULL, NULL, NULL);
  api_def_prop_ui_text(prop, "Props", "Props to set when the op is called");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "map_type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "maptype");
  api_def_prop_enum_items(prop, map_type_items);
  api_def_prop_enum_fns(
      prop, "api_wmKeyMapItem_map_type_get", "api_wmKeyMapItem_map_type_set", NULL);
  api_def_prop_ui_text(prop, "Map Type", "Type of event mapping");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "type", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "type");
  api_def_prop_enum_items(prop, api_enum_event_type_items);
  api_def_prop_lang_cxt(prop, LANG_CTX_UI_EVENTS);
  api_def_prop_enum_fns(prop, NULL, NULL, "rna_KeyMapItem_type_itemf");
  api_def_prop_ui_text(prop, "Type", "Type of event");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "value", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "val");
  api_def_prop_enum_items(prop, api_enum_event_value_items);
  api_def_prop_ui_text(prop, "Value", "");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "direction", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "direction");
  api_def_prop_enum_items(prop, api_enum_event_direction_items);
  api_def_prop_ui_text(prop, "Direction", "The direction (only applies to drag events)");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "id", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "id");
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(prop, "ID", "Id of the item");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "any", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_fns(prop, "api_KeyMapItem_any_get", "rna_KeyMapItem_any_set");
  api_def_prop_ui_text(prop, "Any", "Any modifier keys pressed");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "shift", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "shift");
  api_def_prop_range(prop, KM_ANY, KM_MOD_HELD);
  api_def_prop_ui_text(prop, "Shift", "Shift key pressed, -1 for any state");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_WINDOWMANAGER);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "ctrl", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "ctrl");
  api_def_prop_range(prop, KM_ANY, KM_MOD_HELD);
  api_def_prop_ui_text(prop, "Ctrl", "Control key pressed, -1 for any state");
  update_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "alt", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "alt");
  api_def_prop_range(prop, KM_ANY, KM_MOD_HELD);
  api_def_prop_ui_text(prop, "Alt", "Alt key pressed, -1 for any state");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "oskey", PROP_INT, PROP_NONE);
  api_def_prop_int_stype(prop, NULL, "oskey");
  api_def_prop_range(prop, KM_ANY, KM_MOD_HEL
  api_def_prop_ui_text(prop, "OS Key", "Op system key pressed, -1 for any state");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  /* XXX: the `*_ui` suffix is only for the UI, may be removed,
   * since this is only exposed so the UI can show these settings as toggle-buttons. */
  prop = api_def_prop(sapi, "shift_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "shift", 0);
  api_def_prop_bool_fns(prop, "api_KeyMapItem_shift_get", NULL);
  /* api_def_prop_enum_items(prop, keymap_mod_items); */
  api_def_prop_ui_text(prop, "Shift", "Shift key pressed");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_WM);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "ctrl_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "ctrl", 0);
  api_def_prop_bool_fns(prop, "api_KeyMapItem_ctrl_get", NULL);
  /*  api_def_prop_enum_items(prop, keymap_modifiers_items); */
  api_def_prop_ui_text(prop, "Ctrl", "Control key pressed");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "alt_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "alt", 0);
  api_def_prop_bool_fns(prop, "rna_KeyMapItem_alt_get", NULL);
  /*  api_def_prop_enum_items(prop, keymap_mod_items); */
  api_def_prop_ui_text(prop, "Alt", "Alt key pressed");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "oskey_ui", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "oskey", 0);
  api_def_prop_bool_fns(prop, "api_KeyMapItem_oskey_get", NULL);
  /*  api_def_prop_enum_items(prop, keymap_mod_items); */
  api_def_prop_ui_text(prop, "OS Key", "Op system key pressed");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");
  /* End `_ui` mods. */

  prop = api_def_prop(sapi, "key_mod", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "keymod");
  api_def_prop_enum_items(prop, api_enum_event_type_items);
  api_def_prop_lang_cxt(prop, LANG_CXT_UI_EVENTS);
  api_def_prop_enum_fns(prop, NULL, "api_wmKeyMapItem_keymod_set", NULL);
  api_def_prop_ui_text(prop, "Key Modifier", "Regular key pressed as a modifier");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "repeat", PROP_BOOL, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", KMI_REPEAT_IGNORE);
  api_def_prop_ui_text(prop, "Repeat", "Active on key-repeat events (when a key is held)");
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "show_expanded", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_flag(prop, PROP_NO_DEG_UPDATE);
  api_def_prop_bool_stype(prop, NULL, "flag", KMI_EXPANDED);
  api_def_prop_ui_text(
      prop, "Expanded", "Show key map event and property details in the user interface");
  api_def_prop_ui_icon(prop, ICON_DISCLOSURE_TRI_RIGHT, 1);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "propvalue", PROP_ENUM, PROP_NONE);
  api_def_prop_enum_stype(prop, NULL, "propvalue");
  api_def_prop_enum_items(prop, api_enum_keymap_propvalue_items);
  api_def_prop_enum_fns(prop, NULL, NULL, "api_KeyMapItem_propvalue_itemf");
  api_def_prop_ui_text(
      prop, "Prop Value", "The value this event translates to in a modal keymap");
  api_def_prop_lang_cxt(prop, LANG_CXT_ID_WM);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "active", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_negative_stype(prop, NULL, "flag", KMI_INACTIVE);
  api_def_prop_ui_text(prop, "Active", "Activate or deactivate item");
  api_def_prop_ui_icon(prop, ICON_CHECKBOX_DEHLT, 1);
  api_def_prop_update(prop, 0, "api_KeyMapItem_update");

  prop = api_def_prop(sapi, "is_user_mod", PROP_BOOL, PROP_NONE);
  api_def_prop_bool_stype(prop, NULL, "flag", KMI_USER_MOD);
  api_def_prop_clear_flag(prop, PROP_EDITABLE
  api_def_prop_ui_text(prop, "User Modified", "Is this keymap item modified by the user");

  prop = api_def_prop(sapi, "is_user_defined", PROP_BOOLEAN, PROP_NONE);
  api_def_prop_clear_flag(prop, PROP_EDITABLE);
  api_def_prop_ui_text(
      prop,
      "User Defined",
      "Is this keymap item user defined (doesn't just replace a builtin item)");
  api_def_prop_bool_fns(prop, "api_KeyMapItem_userdefined_get", NULL);

  api_api_keymapitem(sapi);
}

void api_def_wm(DuneApi *dapi)
{
  api_def_op(dapi);
  api_def_op_options_runtime(dapi);
  api_def_op_utils(dapi);
  api_def_op_filelist_element(dapi);
  api_def_macro_op(dapi);
  api_def_op_type_macro(dapi);
  api_def_event(dapi);
  api_def_timer(dapi);
  api_def_popupmenu(dapi);
  api_def_popovermenu(dapi);
  api_def_piemenu(dapi);
  api_def_window(dapi);
  rna_def_windowmanager(dapi);
  rna_def_keyconfig_prefs(dapi);
  rna_def_keyconfig(dapi);
}

#endif /* API_RUNTIME */
