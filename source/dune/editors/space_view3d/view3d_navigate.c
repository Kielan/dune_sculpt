#include "TYPES_curve.h"
#include "TYPES_gpencil.h"

#include "MEM_guardedalloc.h"

#include "BLI_math.h"
#include "BLI_rect.h"

#include "BLT_translation.h"

#include "BKE_armature.h"
#include "BKE_context.h"
#include "BKE_gpencil_geom.h"
#include "BKE_layer.h"
#include "BKE_object.h"
#include "BKE_paint.h"
#include "BKE_scene.h"
#include "BKE_screen.h"
#include "BKE_vfont.h"

#include "DEG_depsgraph_query.h"

#include "ED_mesh.h"
#include "ED_particle.h"
#include "ED_screen.h"
#include "ED_transform.h"

#include "WM_api.h"
#include "WM_message.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "UI_resources.h"

#include "view3d_intern.h"

#include "view3d_navigate.h" /* own include */

/* -------------------------------------------------------------------- */
/** \name Navigation Polls
 * \{ */

static bool view3d_navigation_poll_impl(bContext *C, const char viewlock)
{
  if (!ED_operator_region_view3d_active(C)) {
    return false;
  }

  const RegionView3D *rv3d = CTX_wm_region_view3d(C);
  return !(RV3D_LOCK_FLAGS(rv3d) & viewlock);
}

bool view3d_location_poll(bContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_LOCATION);
}

bool view3d_rotation_poll(bContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ROTATION);
}

bool view3d_zoom_or_dolly_poll(bContext *C)
{
  return view3d_navigation_poll_impl(C, RV3D_LOCK_ZOOM_AND_DOLLY);
}
