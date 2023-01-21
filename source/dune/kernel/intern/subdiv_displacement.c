#include "BKE_subdiv.h"

#include "BLI_utildefines.h"

#include "MEM_guardedalloc.h"

void BKE_subdiv_displacement_detach(Subdiv *subdiv)
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
