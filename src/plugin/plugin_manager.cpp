#include "plugin/plugin_manager.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tamias {

PluginManager::PluginManager(PluginHost& host) : host_(host) {}

bool PluginManager::is_enabled(std::string_view plugin_id) const {
  if (plugin_id.empty()) {
    return true;
  }
  return disabled_ids_.find(std::string(plugin_id)) == disabled_ids_.end();
}

bool PluginManager::is_command_enabled(std::string_view command_id) const {
  for (const PluginCommand& command : host_.commands()) {
    if (command.id == command_id) {
      return is_enabled(command.plugin_id);
    }
  }
  return false;
}

void PluginManager::set_disabled_ids(
    std::unordered_set<std::string> disabled_ids) {
  disabled_ids_ = std::move(disabled_ids);
}

void PluginManager::commit_disabled_among_loaded(
    const std::unordered_set<std::string>& disabled_loaded_ids) {
  for (const auto& plugin : host_.plugins()) {
    disabled_ids_.erase(plugin.id);
  }
  disabled_ids_.insert(disabled_loaded_ids.begin(),
                       disabled_loaded_ids.end());
}

void PluginManager::set_command_order(std::vector<std::string> command_order) {
  command_order_ = std::move(command_order);
}

std::vector<const PluginCommand*> PluginManager::ordered_commands(
    std::string_view page_id, std::string_view group_id) const {
  std::vector<const PluginCommand*> result;
  for (const PluginCommand& command : host_.commands()) {
    if (command.placement.page_id == page_id &&
        command.placement.group_id == group_id) {
      result.push_back(&command);
    }
  }

  std::unordered_map<std::string, std::size_t> rank;
  rank.reserve(command_order_.size());
  for (std::size_t i = 0; i < command_order_.size(); ++i) {
    rank.emplace(command_order_[i], i);
  }
  std::stable_sort(result.begin(), result.end(),
                   [&rank](const PluginCommand* a, const PluginCommand* b) {
                     const auto ar = rank.find(a->id);
                     const auto br = rank.find(b->id);
                     if (ar != rank.end() || br != rank.end()) {
                       if (ar == rank.end()) {
                         return false;
                       }
                       if (br == rank.end()) {
                         return true;
                       }
                       return ar->second < br->second;
                     }
                     if (a->placement.order != b->placement.order) {
                       return a->placement.order < b->placement.order;
                     }
                     return a->id < b->id;
                   });
  return result;
}

void PluginManager::commit_command_order_among_loaded(
    const std::vector<std::string>& ordered_loaded_command_ids) {
  std::unordered_set<std::string> loaded;
  loaded.reserve(host_.commands().size());
  for (const PluginCommand& command : host_.commands()) {
    loaded.insert(command.id);
  }

  std::vector<std::string> merged;
  merged.reserve(ordered_loaded_command_ids.size() + command_order_.size());
  for (const std::string& id : ordered_loaded_command_ids) {
    if (loaded.contains(id) &&
        std::find(merged.begin(), merged.end(), id) == merged.end()) {
      merged.push_back(id);
    }
  }
  for (const std::string& id : command_order_) {
    if (!loaded.contains(id) &&
        std::find(merged.begin(), merged.end(), id) == merged.end()) {
      merged.push_back(id);
    }
  }
  command_order_ = std::move(merged);
}

}  // namespace tamias
