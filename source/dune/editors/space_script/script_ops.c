#include <math.h>
#include <stdlib.h>

#include "WM_api.h"

#include "script_intern.h"

/* ************************** registration **********************************/

void script_operatortypes(void)
{
  WM_operatortype_append(SCRIPT_OT_python_file_run);
  WM_operatortype_append(SCRIPT_OT_reload);
}

void script_keymap(wmKeyConfig *UNUSED(keyconf))
{
  /* Script space is deprecated, and doesn't need a keymap */
}
