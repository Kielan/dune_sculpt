#pragma once

void ED_undosys_type_free(void);

/* memfile_undo.c */
struct MemFile *ED_undosys_stack_memfile_get_active(struct UndoStack *ustack);
