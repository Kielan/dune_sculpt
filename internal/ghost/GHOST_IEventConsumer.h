#pragma once

#include "GHOST_IEvent.h"

/**
 * Interface class for objects interested in receiving events.
 * Objects interested in events should inherit this class and implement the
 * processEvent() method. They should then be registered with the system that
 * they want to receive events. The system will call the processEvent() method
 * for every installed event consumer to pass events.
 * see addEventConsumer
 */
struct GHOST_IEventConsumer {
  /**
   * This method is called by the system when it has events to dispatch.
   * see dispatchEvents
   * param event: The event that can be handled or ignored.
   * return Indication as to whether the event was handled.
   */
  virtual bool processEvent(GHOST_IEvent *event) = 0;
};
