#pragma once

#include "engine/base/result.h"
#include "engine/document/document.h"
#include "engine/render/runtime/render_runtime.h"
#include "engine/render/runtime/render_types.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 无窗口把文档渲染成一张图。给「导出 PNG / 缩略图 / 像素级金样」共用。
// 相机参数和交互里那台一样（TurntableCamera），所以导出的角度和屏幕上看到的一致。
struct SceneCaptureRequest {
  Vec3 target{};
  float distance = 10.f;
  float yaw = 0.785398163f;
  float pitch = 0.35f;
  float fovy = 0.8f;
  bool orthographic = false;
  RenderMode mode = RenderMode::Shaded;
  float xray = 0.f;
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
};

// 渲染一帧并读回 RGBA8（行优先、左上原点）。
// thread 必须是**非同步**的：这些接口靠任务队列 + 等待完成，同步模式会自己等自己。
[[nodiscard]] Result<std::vector<std::uint8_t>> capture_document_rgba(
    RenderThread& thread, const Document& document, const SceneCaptureRequest& request);

}  // namespace tamias
