/** \file
 * \ingroup bke
 * Implementation of generic geometry attributes management. This is built
 * on top of CustomData, which manages individual domains.
 */

#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_curves_types.h"
#include "DNA_customdata_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_string_utf8.h"

#include "BKE_attribute.h"
#include "BKE_curves.h"
#include "BKE_customdata.h"
#include "BKE_editmesh.h"
#include "BKE_pointcloud.h"
#include "BKE_report.h"

#include "RNA_access.h"
