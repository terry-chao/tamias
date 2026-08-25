#include "plugin/plugin_manager.h"

#include <utility>

namespace tamias {

PluginManager::PluginManager(PluginHost& host) : host_(host) {}

bool PluginManager::is_visible(std::string_view plugin_id) const {
  if (plugin_id.empty()) {
    return true;
  }
  return hidden_ids_.find(std::string(plugin_id)) == hidden_ids_.end();
}

void PluginManager::set_hidden_ids(std::unordered_set<std::string> hidden_ids) {
  hidden_ids_ = std::move(hidden_ids);
}

void PluginManager::commit_hidden_among_loaded(
    const std::unordered_set<std::string>& hidden_loaded_ids) {
  for (const auto& plugin : host_.plugins()) {
    hidden_ids_.erase(plugin.id);
  }
  hidden_ids_.insert(hidden_loaded_ids.begin(), hidden_loaded_ids.end());
}

}  // namespace tamias
