#pragma once

#include "engine/base/result.h"
#include "engine/graphics/graphics_backend.h"
#include "engine/render/rhi/device.h"
#include "engine/render/rhi/rhi_blocklist.h"
#include "engine/render/rhi/rhi_gpu_identity.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tamias {

// 探测深度。
//   Device：只建设备（几十毫秒，覆盖「缺 runtime / 没设备 / 建不出 / 分配不了显存」）。
//   Submit：再提交一次空命令并等它完成，多抓「建得出但提交就坏」——坏驱动、TDR 之后的
//           设备、远程会话下的提交路径。建得出 ≠ 能提交，这是最阴的一类。
enum class RhiProbeDepth : std::uint8_t {
  Device = 0,
  Submit = 1,
};

struct RhiProbeOptions {
  // 按顺序试，第一个通过的就算 chosen。
  std::vector<GraphicsBackend> candidates{GraphicsBackend::Vulkan, GraphicsBackend::OpenGL};
  // 探测默认不开校验层：更慢、更吵，而且探测本身不需要它。
  bool enable_validation = false;
  RhiProbeDepth depth = RhiProbeDepth::Device;
  // 可空。命中只做两件事：跳过某后端 / 改走另一个后端。
  const RhiBlocklist* blocklist = nullptr;
  // 策略显式 override 时不让块名单改道（IT 明确要求就得听，出了事日志里能查到为什么）。
  bool blocklist_override = false;
};

struct RhiProbeResult {
  GraphicsBackend backend = GraphicsBackend::Vulkan;
  bool ok = false;
  std::string reason;         // 失败原因 / 被跳过 / 被改道，直接进日志
  std::string matched_entry;  // 命中的块名单 id（空 = 没命中）
  RhiGpuIdentity identity;    // 建出设备就有；失败时可能为空
};

struct RhiProbeReport {
  std::vector<RhiProbeResult> attempts;  // 按探测顺序
  std::optional<GraphicsBackend> chosen;
  std::string summary;  // 一行：日志 / 对话框 / --probe-rhi 都用它

  [[nodiscard]] std::string to_json() const;
};

// 设备工厂可注入：单测里造「Vulkan 失败 → OpenGL 成功」，不用真 GPU。
using RhiDeviceFactory =
    std::function<Result<std::unique_ptr<RHIDevice>>(const DeviceCreateInfo&)>;

// 按 candidates 顺序试：建设备 → 读 GPU 身份 → 查块名单 → 按 depth 决定要不要空提交。
// 失败不抛异常、不提前放弃，一律记进 attempts —— 这就是「可降级」的全部含义。
// 每个候选的设备在试下一个之前一定会被销毁（volk 的表是进程全局的，不能并存）。
[[nodiscard]] RhiProbeReport probe_rhi(const RhiProbeOptions& options,
                                       const RhiDeviceFactory& create_device = RHIDevice::create);

}  // namespace tamias
