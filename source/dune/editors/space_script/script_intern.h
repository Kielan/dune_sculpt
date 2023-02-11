
#pragma once

/* internal exports only */

/* script_ops.c */
void script_operatortypes(void);
void script_keymap(struct wmKeyConfig *keyconf);

/* script_edit.c */
void SCRIPT_OT_reload(struct wmOperatorType *ot);
void SCRIPT_OT_python_file_run(struct wmOperatorType *ot);
