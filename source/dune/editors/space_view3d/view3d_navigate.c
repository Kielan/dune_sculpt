#include "TYPES_curve.h"
#include "TYPES_gpencil.h"

#include "MEM_guardedalloc.h"

#include "LIB_math.h"
#include "LIB_rect.h"

#include "LANG_translation.h"

#include "DUNE_armature.h"
#include "DUNE_context.h"
#include "DUNE_gpencil_geom.h"
#include "DUNE_layer.h"
#include "DUNE_object.h"
#include "DUNE_paint.h"
#include "DUNE_scene.h"
#include "DUNE_screen.h"
#include "DUNE_vfont.h"

#include "DEG_depsgraph_query.h"

#include "ED_mesh.h"
#include "ED_particle.h"
#include "ED_screen.h"
#include "ED_transform.h"

#include "WM_api.h"
#include "WM_message.h"

#include "API_access.h"
#include "API_define.h"

#include "UI_resources.h"

#include "view3d_intern.h"

#include "view3d_navigate.h" /* own include */

/* -------------------------------------------------------------------- */
/** Navigation Polls **/

static bool view3d_navigation_poll_impl(duneContext *C, const char viewlock)
{
  if (!ED_operator_region_view3d_active(C)) {
    return false;
  }

  const RegionView3D *rv3d = CTX_wm_region_view3d(C);
  return !(RV3D_LOCK_FLAGS(rv3d) & viewlock);
}

bool view3d_location_poll(duneContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_LOCATION);
}

bool view3d_rotation_poll(duneContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ROTATION);
}

bool view3d_zoom_or_dolly_poll(dContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ZOOM_AND_DOLLY);
}
