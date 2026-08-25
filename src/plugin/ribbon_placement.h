#pragma once

#include <string>

namespace tamias {

struct RibbonPlacement {
  std::string page_id = "plugins";
  std::string group_id = "commands";
  std::string icon_path;
  int order = 0;
  bool checkable = false;
};

}  // namespace tamias
