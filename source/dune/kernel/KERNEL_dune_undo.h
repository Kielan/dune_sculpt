#pragma once

struct Main;
struct MemFileUndoData;
struct bContext;
	
enum eUndoStepDir;

#define KERNEL_UNDO_STR_MAX 64

struct MemFileUndoData *KERNEL_memfile_undo_encode(struct Main *bmain,
                                                struct MemFileUndoData *mfu_prev);
bool KERNEL_memfile_undo_decode(struct MemFileUndoData *mfu,
                             enum eUndoStepDir undo_direction,
                             bool use_old_bmain_data,
                             struct bContext *C);
void KERNEL_memfile_undo_free(struct MemFileUndoData *mfu);
