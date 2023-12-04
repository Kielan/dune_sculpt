/* Cbs for win_paint_cursor_activate */
/* Poll cb for cursor drwing:
 * win_paint_cursor_activate */
bool transform_drw_cursor_poll(Cxt *C);
/* Cursor and help-line drwing, cb for:
 * win_paint_cursor_activate */
void transform_draw_cursor_drw(Cxt *C, int x, int y, void *customdata);
