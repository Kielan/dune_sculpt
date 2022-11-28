#pragma once

#include "GHOST_Event.h"

/**
 * Mouse wheel event.
 * The displacement of the mouse wheel is counted in ticks.
 * A positive value means the wheel is turned away from the user.
 */
class GHOST_EventWheel : public GHOST_Event {
 public:
  /**
   * Constructor.
   * param msec: The time this event was generated.
   * param window: The window of this event.
   * param z: The displacement of the mouse wheel.
   */
  GHOST_EventWheel(uint64_t msec, GHOST_IWindow *window, int32_t z)
      : GHOST_Event(msec, GHOST_kEventWheel, window)
  {
    m_wheelEventData.z = z;
    m_data = &m_wheelEventData;
  }

 protected:
  /** The z-displacement of the mouse wheel. */
  GHOST_TEventWheelData m_wheelEventData;
};
