#pragma once

#include <string>

namespace tamias {

struct RibbonPlacement {
  std::string page_id = "home";
  std::string group_id = "plugins";
  std::string icon_path;
  int order = 0;
  bool checkable = false;
};

// Empty placement, or the retired plugins tab, lands on Home → Plugins.
inline void resolve_ribbon_placement(RibbonPlacement& placement) {
  const bool legacy_plugins_page =
      placement.page_id.empty() || placement.page_id == "plugins";
  if (legacy_plugins_page) {
    placement.page_id = "home";
    if (placement.group_id.empty() || placement.group_id == "commands" ||
        placement.group_id == "manage") {
      placement.group_id = "plugins";
    }
    return;
  }
  if (placement.group_id.empty()) {
    placement.group_id = "plugins";
  }
}

}  // namespace tamias
