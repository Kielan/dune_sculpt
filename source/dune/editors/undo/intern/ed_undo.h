#pragma once

void ed_undosys_type_free(void);

/* memfile_undo.c */
struct MemFile *ed_undosys_stack_memfile_get_active(struct UndoStack *ustack);
