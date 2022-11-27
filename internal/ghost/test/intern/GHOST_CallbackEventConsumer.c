#include "GHOST_CallbackEventConsumer.h"
#include "GHOST_C-api.h"
#include "GHOST_Debug.h"

GHOST_CallbackEventConsumer(GHOST_EventCallbackProcPtr eventCallback,
                                                         GHOST_TUserDataPtr userData)
{
  m_eventCallback = eventCallback;
  m_userData = userData;
}

bool processEvent(GHOST_IEvent *event)
{
  return m_eventCallback((GHOST_EventHandle)event, m_userData);
}
