/* Share between ui_rgn_*.cc files. */
#pragma once
/* interface_rgn_menu_popup.cc */
uint ui_popup_menu_hash(const char *str);

/* ui_rgns.cc */
ARgn *ui_rgn_tmp_add(Screen *screen);
void ui_rgn_tmp_remove(Cxt *C, Screen *screen, ARgn *rgn);
