/* Implements ED Undo System */
typedef struct MemFileUndoStep {
   UndoStep step;
   MemFileUndoData *data;
} MemFileUndoStep;
	
