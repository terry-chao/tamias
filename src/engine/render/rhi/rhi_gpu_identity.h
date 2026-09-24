#pragma once

#include "engine/render/rhi/rhi_os.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace tamias {

// 「哪块 GPU + 哪个驱动」的身份。块名单拿它去匹配——粒度必须细到设备号 + 驱动版本，
// 否则只能一刀切把整个后端点掉（代价是性能和功能）。
//
// 谁能填什么：
//   Vulkan  adapter_name / vendor_id / device_id / api_version / driver_version 都有
//   OpenGL  driver_name(GL_VENDOR) / adapter_name(GL_RENDERER)；vendor_id 由名字映射，
//           device_id 与 driver_version 通常拿不到（Windows 上要读注册表，未做）
//   os / remote_session 由探测侧统一补：后端不碰平台 API。
struct RhiGpuIdentity {
  RhiOs os = RhiOs::Unknown;
  std::string adapter_name;  // "NVIDIA GeForce RTX 4070" / GL_RENDERER
  std::string driver_name;   // 厂商名："NVIDIA" / "Intel" / "AMD"
  std::uint32_t vendor_id = 0;  // PCI vendor：0x10DE NVIDIA / 0x8086 Intel / 0x1002 AMD
  std::uint32_t device_id = 0;
  std::uint32_t api_version = 0;  // Vulkan apiVersion；OpenGL 填 0
  std::string driver_version;     // "566.03"；拿不到就是空
  bool remote_session = false;    // RDP / 远程会话
  bool software_renderer = false; // llvmpipe / Basic Render Driver 之类

  [[nodiscard]] bool valid() const { return vendor_id != 0 || !adapter_name.empty(); }
};

// 「上次可用的后端」要不要重新探测：换显卡 / 升驱动 → 指纹变。
[[nodiscard]] std::string rhi_gpu_fingerprint(const RhiGpuIdentity& identity);

// 当前是不是远程会话（Windows 用 SM_REMOTESESSION；其它平台暂返回 false）。
[[nodiscard]] bool rhi_is_remote_session();

// 从厂商名字串猜 PCI vendor id（OpenGL 只给字符串时用）。认不出返回 0。
[[nodiscard]] std::uint32_t rhi_vendor_id_from_name(std::string_view name);

}  // namespace tamias
