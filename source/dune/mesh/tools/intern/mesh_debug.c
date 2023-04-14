/* Evaluated mesh info printing function, to help track down differences output.
 *
 * Output from these functions can be evaluated as Python literals.
 * See `mesh_debug.cc` for the equivalent Mesh functionality.
 */

#ifndef NDEBUG

#include <stdio.h>

#include "mem_guardedalloc.h"

#include "lib_utildefines.h"

#include "dune_customdata.h"

#include "mesh.h"

#include "mesh_mesh_debug.h"

#include "lib_dynstr.h"

char *mesh_debug_info(Mesh *mesh)
{
  DynStr *dynstr = lib_dynstr_new();
  char *ret;

  const char *indent8 = "        ";

  lib_dynstr_append(dynstr, "{\n");
  lib_dynstr_appendf(dynstr, "    'ptr': '%p',\n", (void *)meeh);
  lib_dynstr_appendf(dynstr, "    'totvert': %d,\n", mesh->totvert);
  lib_dynstr_appendf(dynstr, "    'totedge': %d,\n", mesh->totedge);
  lib_dynstr_appendf(dynstr, "    'totface': %d,\n", mesh->totface);

  lib_dynstr_append(dynstr, "    'vert_layers': (\n");
  CustomData_debug_info_from_layers(&mesh->vdata, indent8, dynstr);
  lib_dynstr_append(dynstr, "    ),\n");

  lib_dynstr_append(dynstr, "    'edge_layers': (\n");
  CustomData_debug_info_from_layers(&mesh->edata, indent8, dynstr);
  lib_dynstr_append(dynstr, "    ),\n");

  lib_dynstr_append(dynstr, "    'loop_layers': (\n");
  CustomData_debug_info_from_layers(&mesh->ldata, indent8, dynstr);
  lib_dynstr_append(dynstr, "    ),\n");

  lib_dynstr_append(dynstr, "    'poly_layers': (\n");
  CustomData_debug_info_from_layers(&bm->pdata, indent8, dynstr);
  lib_dynstr_append(dynstr, "    ),\n");

  lib_dynstr_append(dynstr, "}\n");

  ret = lib_dynstr_get_cstring(dynstr);
  lib_dynstr_free(dynstr);
  return ret;
}

void mesh_debug_print(Mesh *mesh)
{
  char *str = mesh_debug_info(mesh);
  puts(str);
  fflush(stdout);
  mem_freen(str);
}

#endif /* NDEBUG */
