#include "GHOST_ISystemPaths.h"

#ifdef WIN32
#  include "GHOST_SystemPathsWin32.h"
#else
#  ifdef __APPLE__
#    include "GHOST_SystemPathsCocoa.h"
#  else
#    include "GHOST_SystemPathsUnix.h"
#  endif
#endif

GHOST_ISystemPaths *GHOST_ISystemPaths::m_systemPaths = nullptr;

GHOST_TSuccess GHOST_ISystemPaths::create()
{
  GHOST_TSuccess success;
  if (!m_systemPaths) {
#ifdef WIN32
    m_systemPaths = new GHOST_SystemPathsWin32();
#else
#  ifdef __APPLE__
    m_systemPaths = new GHOST_SystemPathsCocoa();
#  else
    m_systemPaths = new GHOST_SystemPathsUnix();
#  endif
#endif
    success = m_systemPaths != nullptr ? GHOST_kSuccess : GHOST_kFailure;
  }
  else {
    success = GHOST_kFailure;
  }
  return success;
}

GHOST_TSuccess GHOST_ISystemPaths::dispose()
{
  GHOST_TSuccess success = GHOST_kSuccess;
  if (m_systemPaths) {
    delete m_systemPaths;
    m_systemPaths = nullptr;
  }
  else {
    success = GHOST_kFailure;
  }
  return success;
}

GHOST_ISystemPaths *GHOST_ISystemPaths::get()
{
  if (!m_systemPaths) {
    create();
  }
  return m_systemPaths;
}
