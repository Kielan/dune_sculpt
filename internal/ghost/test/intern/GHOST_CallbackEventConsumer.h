#pragma once

#include "GHOST_C-api.h"
#include "GHOST_IEventConsumer.h"

/**
 * Event consumer that will forward events to a call-back routine.
 * Especially useful for the C-API.
 */
struct GHOST_CallbackEventConsumer {
  /**
   * Constructor.
   * param eventCallback: The call-back routine invoked.
   * param userData: The data passed back through the call-back routine.
   */

  /**
   * This method is called by an event producer when an event is available.
   * param event: The event that can be handled or ignored.
   * return Indication as to whether the event was handled.
   */
  bool processEvent(GHOST_IEvent *event);

  /** The call-back routine invoked. */
  GHOST_EventCallbackProcPtr m_eventCallback;
  /** The data passed back through the call-back routine. */
  GHOST_TUserDataPtr m_userData;
};

/*
constructor
GHOST_CallbackEventConsumer(GHOST_EventCallbackProcPtr eventCallback,
                              GHOST_TUserDataPtr userData);
*/
