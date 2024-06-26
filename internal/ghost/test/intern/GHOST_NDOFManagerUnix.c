#include "GHOST_NDOFManagerUnix.h"
#include "GHOST_System.h"

#include <spnav.h>
#include <stdio.h>
#include <unistd.h>

#define SPNAV_SOCK_PATH "/var/run/spnav.sock"

GHOST_NDOFManagerUnix::GHOST_NDOFManagerUnix(GHOST_System &sys)
    : GHOST_NDOFManager(sys), available_(false)
{
  if (access(SPNAV_SOCK_PATH, F_OK) != 0) {
#ifdef DEBUG
    /* annoying for official builds, just adds noise and most people don't own these */
    puts("ndof: spacenavd not found");
    /* This isn't a hard error, just means the user doesn't have a 3D mouse. */
#endif
  }
  else if (spnav_open() != -1) {
    available_ = true;

    /* determine exactly which device (if any) is plugged in */

#define MAX_LINE_LENGTH 100

    /* look for USB devices with Logitech or 3Dconnexion's vendor ID */
    FILE *command_output = popen("lsusb | grep '046d:\\|256f:'", "r");
    if (command_output) {
      char line[MAX_LINE_LENGTH] = {0};
      while (fgets(line, MAX_LINE_LENGTH, command_output)) {
        ushort vendor_id = 0, product_id = 0;
        if (sscanf(line, "Bus %*d Device %*d: ID %hx:%hx", &vendor_id, &product_id) == 2) {
          if (setDevice(vendor_id, product_id)) {
            break; /* stop looking once the first 3D mouse is found */
          }
        }
      }
      pclose(command_output);
    }
  }
}

GHOST_NDOFManagerUnix::~GHOST_NDOFManagerUnix()
{
  if (available_) {
    spnav_close();
  }
}

bool GHOST_NDOFManagerUnix::available()
{
  return available_;
}

/*
 * Workaround for a problem where we don't enter the 'GHOST_kFinished' state,
 * this causes any proceeding event to have a very high 'dt' (time delta),
 * many seconds for eg, causing the view to jump.
 *
 * this workaround expects continuous events, if we miss a motion event,
 * immediately send a dummy event with no motion to ensure the finished state is reached.
 */
#define USE_FINISH_GLITCH_WORKAROUND
/* TODO: make this available on all platforms */

#ifdef USE_FINISH_GLITCH_WORKAROUND
static bool motion_test_prev = false;
#endif

bool GHOST_NDOFManagerUnix::processEvents()
{
  bool anyProcessed = false;

  if (available_) {
    spnav_event e;

#ifdef USE_FINISH_GLITCH_WORKAROUND
    bool motion_test = false;
#endif

    while (spnav_poll_event(&e)) {
      switch (e.type) {
        case SPNAV_EVENT_MOTION: {
          /* convert to blender view coords */
          uint64_t now = system_.getMilliSeconds();
          const int t[3] = {int(e.motion.x), int(e.motion.y), int(-e.motion.z)};
          const int r[3] = {int(-e.motion.rx), int(-e.motion.ry), int(e.motion.rz)};

          updateTranslation(t, now);
          updateRotation(r, now);
#ifdef USE_FINISH_GLITCH_WORKAROUND
          motion_test = true;
#endif
          break;
        }
        case SPNAV_EVENT_BUTTON:
          uint64_t now = system_.getMilliSeconds();
          updateButton(e.button.bnum, e.button.press, now);
          break;
      }
      anyProcessed = true;
    }

#ifdef USE_FINISH_GLITCH_WORKAROUND
    if (motion_test_prev == true && motion_test == false) {
      uint64_t now = system_.getMilliSeconds();
      const int v[3] = {0, 0, 0};

      updateTranslation(v, now);
      updateRotation(v, now);

      anyProcessed = true;
    }
    motion_test_prev = motion_test;
#endif
  }

  return anyProcessed;
}
