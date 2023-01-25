#include "KERNEL_subdiv.h"

#include "LIB_utildefines.h"

#include "MEM_guardedalloc.h"

void KERNEL_subdiv_displacement_detach(Subdiv *subdiv)
{
  if (subdiv->displacement_evaluator == NULL) {
    return;
  }
  if (subdiv->displacement_evaluator->free != NULL) {
    subdiv->displacement_evaluator->free(subdiv->displacement_evaluator);
  }
  MEM_freeN(subdiv->displacement_evaluator);
  subdiv->displacement_evaluator = NULL;
}
