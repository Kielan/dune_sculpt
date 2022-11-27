#pragma once

#include "GHOST_Types.h"

/**
 * This struct stores the state of the mouse buttons.
 * Buttons can be set using button masks.
 */
struct GHOST_Buttons {
  /**
   * Returns the state of a single button.
   * param mask: Key button to return.
   * return The state of the button (pressed == true).
   */
  bool get(GHOST_TButton mask) const;

  /**
   * Updates the state of a single button.
   * param mask: Button state to update.
   * param down: The new state of the button.
   */
  void set(GHOST_TButton mask, bool down);

  /**
   * Sets the state of all buttons to up.
   */
  void clear();

  uint8_t m_ButtonLeft : 1;
  uint8_t m_ButtonMiddle : 1;
  uint8_t m_ButtonRight : 1;
  uint8_t m_Button4 : 1;
  uint8_t m_Button5 : 1;
  uint8_t m_Button6 : 1;
  uint8_t m_Button7 : 1;
};
