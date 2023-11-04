#include "ui.hh"

#include "lib_string.h"

namespace dune::ui {

DragInfo::DragInfo(const WinDrag &drag, const WinEvent &event, const DropLocation drop_location)
    : drag_data(drag), event(event), drop_location(drop_location)
{
}

std::optional<DropLocation> DropTargetInterface::choose_drop_location(
    const ARegion & /*region*/, const wmEvent & /*event*/) const
{
  return DropLocation::Into;
}

bool drop_target_apply_drop(Cxt &C,
                            const ARegion &region,
                            const WinEvent &event,
                            const DropTargetInterface &drop_target,
                            const List &drags)
{
  const char *disabled_hint_dummy = nullptr;
  LIST_FOREACH (const WinDrag *, drag, &drags) {
    if (!drop_target.can_drop(*drag, &disabled_hint_dummy)) {
      return false;
    }

    const std::optional<DropLocation> drop_location = drop_target.choose_drop_location(region,
                                                                                       event);
    if (!drop_location) {
      return false;
    }

    const DragInfo drag_info{*drag, event, *drop_location};
    return drop_target.on_drop(&C, drag_info);
  }

  return false;
}

char *drop_target_tooltip(const ARegion &region,
                          const DropTargetInterface &drop_target,
                          const WinDrag &drag,
                          const WinEvent &event)
{
  const char *disabled_hint_dummy = nullptr;
  if (!drop_target.can_drop(drag, &disabled_hint_dummy)) {
    return nullptr;
  }

  const std::optional<DropLocation> drop_location = drop_target.choose_drop_location(region,
                                                                                     event);
  if (!drop_location) {
    return nullptr;
  }

  const DragInfo drag_info{drag, event, *drop_location};
  const std::string tooltip = drop_target.drop_tooltip(drag_info);
  return tooltip.empty() ? nullptr : lib_strdup(tooltip.c_str());
}

}  // namespace dune::ui
