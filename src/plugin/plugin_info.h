#pragma once

#include <string>

namespace tamias {

struct PluginInfo {
  std::string id;
  std::string title;
  std::string author;
  std::string version;
  std::string release_date;
  std::string description;
  std::string homepage_url;
  std::string icon_path;
  bool built_in = false;
};

}  // namespace tamias
