#include "engine/render/rhi/rhi_blocklist.h"

#include <algorithm>
#include <cctype>

namespace tamias {
namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  return lowercase(std::string(haystack)).find(lowercase(std::string(needle))) !=
         std::string::npos;
}

// 找 "key" 后面跟的第一个**裸值**（字符串去掉引号，数字/true/false 原样）。
std::optional<std::string> find_raw_value(std::string_view text, std::string_view key) {
  const std::string pattern = "\"" + std::string(key) + "\"";
  std::size_t pos = text.find(pattern);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  pos = text.find(':', pos + pattern.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos >= text.size()) {
    return std::nullopt;
  }
  if (text[pos] == '"') {  // 字符串值
    const std::size_t end = text.find('"', pos + 1);
    if (end == std::string_view::npos) {
      return std::nullopt;
    }
    return std::string(text.substr(pos + 1, end - pos - 1));
  }
  std::size_t end = pos;  // 数字 / true / false：读到分隔符为止
  while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != '\n') {
    ++end;
  }
  while (end > pos && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return std::string(text.substr(pos, end - pos));
}

std::optional<std::string> find_string(std::string_view text, std::string_view key) {
  return find_raw_value(text, key);
}

std::optional<double> find_number(std::string_view text, std::string_view key) {
  const std::optional<std::string> raw = find_raw_value(text, key);
  if (!raw.has_value() || raw->empty()) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const double value = std::stod(*raw, &consumed);
    return consumed == 0 ? std::nullopt : std::optional<double>(value);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<bool> find_bool(std::string_view text, std::string_view key) {
  const std::optional<std::string> raw = find_raw_value(text, key);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  const std::string lowered = lowercase(*raw);
  if (lowered == "true" || lowered == "yes") {
    return true;
  }
  if (lowered == "false" || lowered == "no") {
    return false;
  }
  return std::nullopt;
}

// 取出所有**叶子** {...} 对象（内部不再嵌套对象的那些），保持原文顺序。
// 条目可以跨行、也可以挤在一行；外层 documents 有嵌套，自然被排除掉。
// 用帧栈按源码位置切片，所以字符串里的花括号不会被误当结构。
std::vector<std::string> split_leaf_objects(std::string_view text) {
  struct Frame {
    std::size_t start = 0;
    bool has_nested = false;
  };
  std::vector<std::string> objects;
  std::vector<Frame> frames;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      continue;
    }
    if (c == '{') {
      if (!frames.empty()) {
        frames.back().has_nested = true;
      }
      frames.push_back(Frame{i, false});
      continue;
    }
    if (c == '}') {
      if (frames.empty()) {
        continue;
      }
      const Frame frame = frames.back();
      frames.pop_back();
      if (!frame.has_nested) {
        objects.push_back(std::string(text.substr(frame.start, i - frame.start + 1)));
      }
      continue;
    }
  }
  return objects;
}

}  // namespace

int compare_versions(std::string_view a, std::string_view b) {
  std::size_t ia = 0;
  std::size_t ib = 0;
  // 游标一定会被推进（先跳非数字，再读数字），所以不会空转。
  const auto next_component = [](std::string_view text, std::size_t& index) -> long long {
    while (index < text.size() && !std::isdigit(static_cast<unsigned char>(text[index]))) {
      ++index;  // 分隔符 / 空白 / "beta" 这类尾巴
    }
    long long value = 0;
    while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
      value = value * 10 + (text[index] - '0');
      ++index;
    }
    return value;
  };
  while (ia < a.size() || ib < b.size()) {
    // 一边到底了就当 0：这样 "24" == "24.0" == "24.0.0"。
    const long long va = ia < a.size() ? next_component(a, ia) : 0;
    const long long vb = ib < b.size() ? next_component(b, ib) : 0;
    if (va != vb) {
      return va < vb ? -1 : 1;
    }
  }
  return 0;
}

bool RhiBlockEntry::matches(const RhiGpuIdentity& gpu) const {
  if (os != RhiOs::Unknown && gpu.os != os) {
    return false;
  }
  if (vendor_id != 0 && gpu.vendor_id != vendor_id) {
    return false;
  }
  if (device_id != 0 && gpu.device_id != device_id) {
    return false;
  }
  if (remote_session.has_value() && gpu.remote_session != *remote_session) {
    return false;
  }
  if (software.has_value() && gpu.software_renderer != *software) {
    return false;
  }
  if (!adapter_contains.empty() && !contains_case_insensitive(gpu.adapter_name, adapter_contains)) {
    return false;
  }
  const bool wants_driver = !driver_exact.empty() || !driver_min.empty() || !driver_max.empty();
  if (wants_driver) {
    if (gpu.driver_version.empty()) {
      return false;  // 条目点名了驱动版本，但这台机器报不出来 → 宁可不命中
    }
    if (!driver_exact.empty() && compare_versions(gpu.driver_version, driver_exact) != 0) {
      return false;
    }
    if (!driver_min.empty() && compare_versions(gpu.driver_version, driver_min) < 0) {
      return false;
    }
    if (!driver_max.empty() && compare_versions(gpu.driver_version, driver_max) > 0) {
      return false;
    }
  }
  return true;
}

const RhiBlockEntry* RhiBlocklist::match(const RhiGpuIdentity& gpu) const {
  if (!gpu.valid()) {
    return nullptr;  // 身份都没拿到，不乱猜
  }
  for (const RhiBlockEntry& entry : entries) {
    if (entry.matches(gpu)) {
      return &entry;
    }
  }
  return nullptr;
}

RhiBlocklist parse_rhi_blocklist(std::string_view text, std::vector<std::string>* warnings) {
  RhiBlocklist list;
  const auto warn = [warnings](std::string message) {
    if (warnings != nullptr) {
      warnings->push_back(std::move(message));
    }
  };
  if (const auto version = find_number(text, "version")) {
    list.version = static_cast<int>(*version);
  }
  // 逐对象解析：每个叶子对象里有 "id" 就按条目读。条目可以跨行写，也可以和别的条目
  // 挤在一行——排版怎么摆都不影响。
  for (const std::string& object : split_leaf_objects(text)) {
    const auto id = find_string(object, "id");
    if (!id.has_value() || id->empty()) {
      continue;
    }
    RhiBlockEntry entry;
    entry.id = *id;
    entry.why = find_string(object, "why").value_or("");
    if (const auto os = find_string(object, "os")) {
      entry.os = rhi_os_from_name(*os);
      if (entry.os == RhiOs::Unknown) {
        warn("blocklist: entry " + entry.id + " 的 os 认不出，按「不限」处理");
      }
    }
    if (const auto vendor = find_number(object, "vendor")) {
      entry.vendor_id = static_cast<std::uint32_t>(*vendor);
    }
    if (const auto device = find_number(object, "device")) {
      entry.device_id = static_cast<std::uint32_t>(*device);
    }
    entry.driver_min = find_string(object, "driver_min").value_or("");
    entry.driver_max = find_string(object, "driver_max").value_or("");
    entry.driver_exact = find_string(object, "driver_exact").value_or("");
    entry.remote_session = find_bool(object, "remote");
    entry.software = find_bool(object, "software");
    entry.adapter_contains = find_string(object, "adapter_contains").value_or("");

    const auto skip = find_string(object, "skip");
    const auto force = find_string(object, "force");
    if (skip.has_value()) {
      const auto backend = try_backend_from_name(*skip);
      if (!backend.has_value()) {
        warn("blocklist: entry " + entry.id + " 的 skip 值认不出，已跳过该条目");
        continue;
      }
      entry.action = RhiBlockActionKind::SkipBackend;
      entry.backend = *backend;
    } else if (force.has_value()) {
      const auto backend = try_backend_from_name(*force);
      if (!backend.has_value()) {
        warn("blocklist: entry " + entry.id + " 的 force 值认不出，已跳过该条目");
        continue;
      }
      entry.action = RhiBlockActionKind::ForceBackend;
      entry.backend = *backend;
    } else {
      warn("blocklist: entry " + entry.id + " 既没有 skip 也没有 force，已跳过");
      continue;
    }
    list.entries.push_back(std::move(entry));
  }
  return list;
}

RhiPolicy parse_rhi_policy(std::string_view text, std::vector<std::string>* warnings) {
  RhiPolicy policy;
  const auto warn = [warnings](std::string message) {
    if (warnings != nullptr) {
      warnings->push_back(std::move(message));
    }
  };
  if (const auto backend = find_string(text, "force_backend")) {
    const auto parsed = try_backend_from_name(*backend);
    if (parsed.has_value()) {
      policy.force_backend = *parsed;
    } else {
      warn("policy: force_backend 认不出（" + *backend + "），忽略");
    }
  }
  policy.lock = find_bool(text, "lock").value_or(false);
  policy.override_blocklist = find_bool(text, "override_blocklist").value_or(false);
  return policy;
}

}  // namespace tamias
