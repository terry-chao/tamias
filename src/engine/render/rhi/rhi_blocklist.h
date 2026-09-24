#pragma once

#include "engine/graphics/graphics_backend.h"
#include "engine/render/rhi/rhi_gpu_identity.h"
#include "engine/render/rhi/rhi_os.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// 版本按**数值分段**比较："24.9" < "24.20"（字符串比较会搞反）。
// 每段的非数字尾巴忽略（"24.20.0 beta" 当 24.20.0）。返回 <0 / 0 / >0。
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

// 一条块名单能做的事。现在只有「跳过这个后端」和「改走另一个后端」两种——都是后端
// 选择层面的；feature 级 workaround（只关某个扩展）等真有案例再加，别先加空壳。
enum class RhiBlockActionKind : std::uint8_t {
  SkipBackend = 0,   // 命中就别用 backend 这个后端
  ForceBackend = 1,  // 命中就改走 backend 这个后端
};

// 一条「已知问题」记录。匹配粒度：平台 / 厂商 / 设备号 / 驱动版本区间 / 是否远程会话 /
// 是否软件渲染 / 名字子串。**任何一项没填 = 不限**。
struct RhiBlockEntry {
  std::string id;   // 稳定标识：日志和工单里引用它
  std::string why;  // 人读的原因
  RhiOs os = RhiOs::Unknown;
  std::uint32_t vendor_id = 0;
  std::uint32_t device_id = 0;
  std::string driver_min;
  std::string driver_max;
  std::string driver_exact;
  std::optional<bool> remote_session;  // 空 = 不限
  std::optional<bool> software;        // 空 = 不限
  std::string adapter_contains;        // 大小写无关子串；空 = 不限
  RhiBlockActionKind action = RhiBlockActionKind::SkipBackend;
  GraphicsBackend backend = GraphicsBackend::Vulkan;  // Skip：跳过谁；Force：改走谁

  [[nodiscard]] bool matches(const RhiGpuIdentity& gpu) const;
};

// 顺序即优先级：命中第一条就停（越具体的条目应该写越前面）。
struct RhiBlocklist {
  int version = 0;
  std::vector<RhiBlockEntry> entries;

  [[nodiscard]] const RhiBlockEntry* match(const RhiGpuIdentity& gpu) const;
  [[nodiscard]] bool empty() const { return entries.empty(); }
};

// IT 下发的策略：一律用哪个后端 / 锁不锁 / 能不能压过块名单。
// 和块名单分开：块名单是「我们（厂商）知道它坏」，策略是「你们（客户）要求这么用」。
struct RhiPolicy {
  std::optional<GraphicsBackend> force_backend;
  bool lock = false;              // 锁住：界面里不让用户改
  bool override_blocklist = false;  // 显式压过块名单（默认 false：防崩优先）
};

// 文件是 JSON 的**扁平子集**，一条条目占一行（和 render_scene_golden 的手写读法同一风格：
// 只认 "key": value，不建完整解析器）。好处是纯 engine、可单测、IT 用记事本就能改。
//
//   { "version": 1,
//     "entries": [
//       { "id": "intel-24x-rdp", "why": "Intel 24.20-24.29 在 RDP 下建 swapchain 会崩",
//         "os": "windows", "vendor": 32902, "driver_min": "24.20.0.0",
//         "driver_max": "24.29.9.9", "remote": true, "skip": "vulkan" } ] }
//
// 规则：坏行跳过并记进 warnings，**不因为一行写错就让整份名单失效**；认不出的 skip/force
// 值也当坏行。条目里没有 skip/force 就没有动作，同样跳过。
[[nodiscard]] RhiBlocklist parse_rhi_blocklist(std::string_view text,
                                               std::vector<std::string>* warnings = nullptr);

// 策略文件（扁平 JSON，同上）：
//   { "force_backend": "opengl", "lock": true, "override_blocklist": false }
[[nodiscard]] RhiPolicy parse_rhi_policy(std::string_view text,
                                         std::vector<std::string>* warnings = nullptr);

}  // namespace tamias
