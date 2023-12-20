#pragma once
struct WinOp;

/* internal api*/
/* Cxt can be null in background mode bc
 * no need for ev handling. */
void win_ghost_init(Cxt *C);
void win_ghost_init_background(void);
void win_ghost_exit(void);

/* This one should correctly check for apple top header...
 * done for Cocoa: returns window contents (and not frame) max size */
void win_get_screensize(int *r_width, int *r_height);
/* Size of all screens (desktop), useful since the mouse is bound by this. */
void win_get_desktopsize(int *r_width, int *r_height);

/* Don't change cxt itself. */
Win *win_new(const struct Main *main,
             WinMngr *wm,
             Win *parent,
             bool dialog);
/* Part of `win.c` API. */
Win *win_copy(
    struct Main *main, WinMngr *wm, Win *win_src, bool dup_layout, bool child);
/* A higher level version of copy that tests the new win can be added.
 * (called from the op directly). */
Win *win_copy_test(Cxt *C, Win *win_src, bool dup_layout, bool child);
/* Including win itself.
 * param C: can be NULL.
 * note win_screen_exit should have been called. */
void win_free(Cxt *C, WinMngr *wm, Win *win);
/* This is ev from ghost, or exit-Dune op. */
void wi _win_close(Cxt *C, WinMngr *wm, Win *win);

void win_title(WinMngr *wm, Win *win);
/* Init Win wo `ghostwin`, open these and clear.
 * Win size is read from win, if 0 it uses prefsize
 * called in win_check, also init stuff after file read.
 *
 * warning: After running `win->ghostwin` can be NULL in rare cases
 * (where OpenGL driver fails to create a cxt for eg).
 * Could remove them w win_ghostwindows_remove_invalid
 * but better not bc caller may continue to use.
 * Instead, caller needs to handle the error case and cleanup. */
void win_ghostwindows_ensure(WinMngr *wm);
/* Call after win_ghostwindows_ensure or win_check
 * (after loading a new file) in the unlikely ev a win couldn't be created. */
void win_ghostwindows_remove_invalid(Cxt *C, WinMngr *wm);
void win_process_evs(const Cxt *C);

void win_clear_drawable(WinMngr *wm);
void win_make_drawable(WinMngr *wm, Win *win);
/* Reset active the current win opengl drw cxt. */
void win_reset_drawable(void);

void win_raise(Win *win);
void win_lower(Win *win);
void win_set_size(Win *win, int width, int height);
void win_get_position(Win *win, int *r_pos_x, int *r_pos_y);
/* Push rendered buf to the screen. */
void win_swap_bufs(Win *win);
void win_set_swap_interval(Win *win, int interval);
bool win_get_swap_interval(Win *win, int *intervalOut);

void win_cursor_position_get(Win *win, int *r_x, int *r_y);
void win_cursor_position_from_ghost_screen_coords(Win *win, int *r_x, int *r_y);
void win_cursor_position_to_ghost_screen_coords(Win *win, int *x, int *y);

void win_cursor_position_from_ghost_client_coords(Win *win, int *x, int *y);
void win_cursor_position_to_ghost_client_coords(Win *win, int *x, int *y);

#ifdef WITH_INPUT_IME
void win_IME_begin(Win *win, int x, int y, int w, int h, bool complete);
void win_IME_end(Win *win);
#endif

/* win ops */
int win_close_ex(Cxt *C, struct WinOp *op);
/* Full-screen op cb. */
int win_fullscreen_toggle_ex(Cxt *C, struct WinOp *op);
/* Call the quit confirmation prompt or exit directly if needed. The use can
 * still cancel via the confirmation popup. Also, this may not quit Dune
 * immediately, but rather schedule the closing.
 * win: The window to show the confirmation popup/window in. */
void win_quit_with_optional_confirmation_prompt(Cxt *C, Win *win) ATTR_NONNULL();

int win_new_ex(Cxt *C, struct WinOp *op);
int win_new_main_ex(Cxt *C, struct WinOp *op);

void win_test_autorun_revert_action_set(struct WinOpType *ot, struct ApiPtr *ptr);
void win_test_autorun_warning(Cxt *C);
