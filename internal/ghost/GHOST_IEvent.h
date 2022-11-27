#pragma once

#include "GHOST_Types.h"
#include <stddef.h>

/**
 * Interface class for events received from GHOST.
 * You should not need to inherit this class. The system will pass these events
 * to the processEvent() method of event consumers.
 * Use the getType() method to retrieve the type of event and the getData()
 * method to get the event data out. Using the event type you can cast the
 * event data to the correct event data structure.
 * see processEvent
 * see GHOST_TEventType
 */
struct GHOST_IEvent {
  /**
   * Returns the event type.
   * return The event type.
   */
  virtual GHOST_TEventType getType() = 0;

  /**
   * Returns the time this event was generated.
   * return The event generation time.
   */
  virtual uint64_t getTime() = 0;
};

/**
 * Returns the window this event was generated on,
 * or NULL if it is a 'system' event.
 * return The generating window.
 */
virtual GHOST_IWindow *getWindow() =0;

/**
  * Returns the event data.
  * return The event data.
  */
virtual GHOST_TEventDataPtr getData() = 0;
