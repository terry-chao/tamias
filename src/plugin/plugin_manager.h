#pragma once

#include "plugin/plugin_command.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_info.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace tamias {

// Visibility policy for loaded plugins. Hiding a plugin only affects Ribbon
// commands; the plugin stays loaded and can still be invoked.
class PluginManager {
 public:
  explicit PluginManager(PluginHost& host);

  [[nodiscard]] const std::vector<PluginInfo>& plugins() const { return host_.plugins(); }
  [[nodiscard]] const std::vector<PluginCommand>& commands() const { return host_.commands(); }

  [[nodiscard]] bool is_visible(std::string_view plugin_id) const;
  [[nodiscard]] const std::unordered_set<std::string>& hidden_ids() const { return hidden_ids_; }

  // Replace hidden ids among currently loaded plugins. Ids for plugins that
  // are not loaded this session are kept so a later load stays hidden.
  void commit_hidden_among_loaded(const std::unordered_set<std::string>& hidden_loaded_ids);
  void set_hidden_ids(std::unordered_set<std::string> hidden_ids);

 private:
  PluginHost& host_;
  std::unordered_set<std::string> hidden_ids_;
};

}  // namespace tamias
