#include "lib_vector.hh"

#include "dune_screen.h"

#include "api_access.h"

#include "ED_screen.h"

#include "ui.h"
#include "ui.hh"
#include "ui_resources.h"

#include "WM_api.h"

namespace dune::ui {

void ctx_path_add_generic(Vector<ContextPathItem> &path,
                              ApiStruct &api_type,
                              void *ptr,
                              const BIFIconID icon_override)
{
  /* Add the null check here to make calling functions less verbose. */
  if (!ptr) {
    return;
  }

  ApitPtr api_ptr;
  api_ptr_create(nullptr, &api_type, ptr, &rna_ptr);
  char name[128];
  api_struct_name_get_alloc(&api_ptr, name, sizeof(name), nullptr);

  /* Use a blank icon by default to check whether to retrieve it automatically from the type. */
  const BIFIconID icon = icon_override == ICON_NONE ?
                             static_cast<BIFIconID>(api_struct_ui_icon(api_ptr.type)) :
                             icon_override;

  path.append({name, static_cast<int>(icon)});
}

/* -------------------------------------------------------------------- */
/** Breadcrumb Template **/

void template_breadcrumbs(uiLayout &layout, Span<ContextPathItem> context_path)
{
  uiLayout *row = uiLayoutRow(&layout, true);
  uiLayoutSetAlignment(&layout, UI_LAYOUT_ALIGN_LEFT);

  for (const int i : context_path.index_range()) {
    uiLayout *sub_row = uiLayoutRow(row, true);
    uiLayoutSetAlignment(sub_row, UI_LAYOUT_ALIGN_LEFT);

    if (i > 0) {
      uiItemL(sub_row, "", ICON_RIGHTARROW_THIN);
    }
    uiItemL(sub_row, context_path[i].name.c_str(), context_path[i].icon);
  }
}

}  // namespace dune::ui
