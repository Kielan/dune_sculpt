#pragma once

struct Main;
struct MemFileUndoData;
struct bContext;
	
enum eUndoStepDir;

#define DUNE_UNDO_STR_MAX 64

struct MemFileUndoData *DUNE_memfile_undo_encode(struct Main *duneMain,
                                                struct MemFileUndoData *mfu_prev);
bool DUNE_memfile_undo_decode(struct MemFileUndoData *mfu,
                             enum eUndoStepDir undo_direction,
                             bool use_old_duneMain_data,
                             struct duneContext *C);
void DUNE_memfile_undo_free(struct MemFileUndoData *mfu);
