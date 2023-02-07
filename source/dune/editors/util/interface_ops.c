#include <string.h>

#include "MEM_guardedalloc.h"

#include "TYPES_armature.h"
#include "TYPES_material.h"
#include "TYPES_modifier.h" /* for handling geometry nodes properties */
#include "TYPES_object.h"   /* for OB_DATA_SUPPORT_ID */
#include "TYPES_screen.h"
#include "TYPES_text.h"

#include "LIB_dunelib.h"
#include "LIB_math_color.h"

#include "BLF_api.h"
#include "BLT_lang.h"

#include "DUNE_context.h"
#include "DUNE_global.h"
#include "DUNE_idprop.h"
#include "DUNE_layer.h"
#include "DUNE_lib_id.h"
#include "DUNE_lib_override.h"
#include "DUNE_material.h"
#include "DUNE_node.h"
#include "DUNE_report.h"
#include "DUNE_screen.h"
#include "DUNE_text.h"

#include "IMB_colormanagement.h"

#include "DEG_depsgraph.h"

#include "API_access.h"
#include "API_define.h"
#include "API_prototypes.h"
#include "API_types.h"

#include "UI_interface.h"

#include "interface_intern.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_object.h"
#include "ED_paint.h"

/* for Copy As Driver */
#include "ED_keyframing.h"

/* only for UI_OT_editsource */
#include "DUNE_main.h"
#include "LIB_ghash.h"
#include "ED_screen.h"
#include "ED_text.h"

/* -------------------------------------------------------------------- */
/** Immediate redraw helper
 *
 * Generally handlers shouldn't do any redrawing, that includes the layout/button definitions.
 * Handlers violates the Model-View-Controller pattern.
 *
 * But there are some operators which really need to re-run the layout definitions for various
 * reasons. For example, "Edit Source" does it to find out which exact Python code added a button.
 * Other operators may need to access buttons that aren't currently visible. In Dune's UI code
 * design that typically means just not adding the button in the first place, for a particular
 * redraw. So the operator needs to change context and re-create the layout, so the button becomes
 * available to act on.
 *
 **/

