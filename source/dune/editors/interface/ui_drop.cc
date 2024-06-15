#include "ui.hh"

#include "lib_string.h"

namespace dune::ui {

DragInfo::DragInfo(const WinDrag &drag, const WinEv &ev, const DropLocation drop_location)
    : drag_data(drag), ev(ev), drop_location(drop_location)
{
}

std::optional<DropLocation> DropTargetInterface::choose_drop_location(
    const ARgn & /*region*/, const WinEv & /*event*/) const
{
  return DropLocation::Into;
}

bool drop_target_apply_drop(Cxt &C,
                            const ARgn &rgn,
                            const WinEv &ev,
                            const DropTargetInterface &drop_target,
                            const List &drags)
{
  const char *disabled_hint_dummy = nullptr;
  LIST_FOREACH (const WinDrag *, drag, &drags) {
    if (!drop_target.can_drop(*drag, &disabled_hint_dummy)) {
      return false;
    }

    const std::optional<DropLocation> drop_location = drop_target.choose_drop_location(rgn,
                                                                                       ev);
    if (!drop_location) {
      return false;
    }

    const DragInfo drag_info{*drag, ev, *drop_location};
    return drop_target.on_drop(&C, drag_info);
  }

  return false;
}

char *drop_target_tooltip(const ARgn &rgn,
                          const DropTargetInterface &drop_target,
                          const WinDrag &drag,
                          const WinEv &ev)
{
  const char *disabled_hint_dummy = nullptr;
  if (!drop_target.can_drop(drag, &disabled_hint_dummy)) {
    return nullptr;
  }

  const std::optional<DropLocation> drop_location = drop_target.choose_drop_location(rgn,
                                                                                     ev);
  if (!drop_location) {
    return nullptr;
  }

  const DragInfo drag_info{drag, ev, *drop_location};
  const std::string tooltip = drop_target.drop_tooltip(drag_info);
  return tooltip.empty() ? nullptr : lib_strdup(tooltip.c_str());
}

}  // namespace dune::ui
