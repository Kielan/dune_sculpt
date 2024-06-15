#include "lib_vector.hh"

#include "dune_screen.h"

#include "api_access.h"

#include "ed_screen.h"

#include "ui.h"
#include "ui.hh"
#include "ui_resources.h"

#include "win_api.h"

namespace dune::ui {

void cxt_path_add_generic(Vector<CxtPathItem> &path,
                          ApiStruct &api_type,
                          void *ptr,
                          const BIFIconId icon_override)
{
  /* Add the null check here to make calling fns less verbose. */
  if (!ptr) {
    return;
  }

  ApitPtr api_ptr;
  api_ptr_create(nullptr, &api_type, ptr, &rna_ptr);
  char name[128];
  api_struct_name_get_alloc(&api_ptr, name, sizeof(name), nullptr);

  /* Use a blank icon by default to check whether to retrieve it automatically from the type. */
  const BIFIconId icon = icon_override == ICON_NONE ?
                             static_cast<BIFIconId>(api_struct_ui_icon(api_ptr.type)) :
                             icon_override;

  path.append({name, static_cast<int>(icon)});
}

/* Breadcrumb Template */
void template_breadcrumbs(uiLayout &layout, Span<CxtPathItem> cxt_path)
{
  uiLayout *row = uiLayoutRow(&layout, true);
  uiLayoutSetAlignment(&layout, UI_LAYOUT_ALIGN_LEFT);

  for (const int i : cxt_path.index_range()) {
    uiLayout *sub_row = uiLayoutRow(row, true);
    uiLayoutSetAlignment(sub_row, UI_LAYOUT_ALIGN_LEFT);

    if (i > 0) {
      uiItemL(sub_row, "", ICON_RIGHTARROW_THIN);
    }
    uiItemL(sub_row, cxt_path[i].name.c_str(), cxt_path[i].icon);
  }
}

}  // namespace dune::ui
