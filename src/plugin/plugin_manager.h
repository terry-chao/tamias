#pragma once

#include "plugin/plugin_command.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_info.h"

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tamias {

// Runtime enablement and persisted Ribbon ordering for loaded plugins.
class PluginManager {
 public:
  explicit PluginManager(PluginHost& host);

  [[nodiscard]] const std::vector<PluginInfo>& plugins() const { return host_.plugins(); }
  [[nodiscard]] const std::vector<PluginCommand>& commands() const { return host_.commands(); }

  [[nodiscard]] bool is_enabled(std::string_view plugin_id) const;
  [[nodiscard]] bool is_command_enabled(std::string_view command_id) const;
  [[nodiscard]] const std::unordered_set<std::string>& disabled_ids() const {
    return disabled_ids_;
  }
  [[nodiscard]] const std::vector<std::string>& command_order() const {
    return command_order_;
  }
  [[nodiscard]] std::vector<const PluginCommand*> ordered_commands(
      std::string_view page_id, std::string_view group_id) const;

  void commit_disabled_among_loaded(
      const std::unordered_set<std::string>& disabled_loaded_ids);
  void set_disabled_ids(std::unordered_set<std::string> disabled_ids);
  void commit_command_order_among_loaded(
      const std::vector<std::string>& ordered_loaded_command_ids);
  void set_command_order(std::vector<std::string> command_order);

  // Source compatibility for callers using the former visibility terminology.
  [[nodiscard]] bool is_visible(std::string_view plugin_id) const {
    return is_enabled(plugin_id);
  }
  [[nodiscard]] const std::unordered_set<std::string>& hidden_ids() const {
    return disabled_ids_;
  }
  void commit_hidden_among_loaded(
      const std::unordered_set<std::string>& ids) {
    commit_disabled_among_loaded(ids);
  }
  void set_hidden_ids(std::unordered_set<std::string> ids) {
    set_disabled_ids(std::move(ids));
  }

 private:
  PluginHost& host_;
  std::unordered_set<std::string> disabled_ids_;
  std::vector<std::string> command_order_;
};

}  // namespace tamias
