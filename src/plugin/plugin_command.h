#pragma once

#include "plugin/ribbon_placement.h"

#include <string>

namespace tamias {

struct PluginCommand {
  std::string id;
  std::string title;
  std::string tooltip;
  std::string plugin_id;
  RibbonPlacement placement;
};

}  // namespace tamias
