#include "engine/document/scene_capture.h"

#include "engine/math/camera.h"

#include <algorithm>
#include <utility>

namespace tamias {

Result<std::vector<std::uint8_t>> capture_document_rgba(RenderThread& thread,
                                                        const Document& document,
                                                        const SceneCaptureRequest& request) {
  if (thread.config().synchronous) {
    return Err("capture_document_rgba: synchronous render threads are not supported");
  }
  const std::uint32_t width = std::max(1u, request.width);
  const std::uint32_t height = std::max(1u, request.height);

  const std::uint64_t channel = thread.create_channel();
  thread.resize_offscreen_surface(channel, width, height);

  // 资产先上传（按 asset id 幂等，重复导出不会重复上传）。
  for (const auto& entry : document.meshes()) {
    const MeshAsset& asset = entry.second;
    if (asset.cpu.vertices.empty() || asset.cpu.indices.empty()) {
      continue;
    }
    if (auto uploaded = thread.upload_mesh(asset.id, asset.cpu); !uploaded) {
      thread.destroy_channel(channel);
      return Err("capture_document_rgba: mesh upload failed: " + uploaded.error());
    }
  }
  for (const auto& entry : document.textures()) {
    const TextureAsset& texture = entry.second;
    if (!texture.rgba.empty()) {
      (void)thread.upload_texture(entry.first, texture);  // 贴图失败不致命：材质退回纯色
    }
  }

  TurntableCamera camera;
  camera.set_target(request.target);
  camera.set_distance(request.distance);
  camera.set_yaw_pitch(request.yaw, request.pitch);
  camera.set_fovy(request.fovy);
  camera.set_orthographic(request.orthographic);

  FrameSubmission frame{};
  frame.width = width;
  frame.height = height;
  frame.view = camera.view_matrix();
  frame.proj = camera.proj_matrix(static_cast<float>(width) / static_cast<float>(height));
  frame.eye_position = camera.eye_position();
  frame.view_distance = camera.distance();
  frame.fovy = camera.fovy();
  frame.mode = request.mode;
  frame.xray = request.xray;
  frame.show_axes = false;  // 导出图里不要世界坐标轴
  frame.items = document.render_items();
  frame.lod_sets = document.tess_cache().snapshot();
  frame.scene_generation = document.scene().generation();
  thread.submit_frame(channel, std::move(frame));

  std::vector<std::uint8_t> pixels;
  const Result<void> read = thread.read_channel_pixels(channel, pixels);
  thread.destroy_channel(channel);
  if (!read) {
    return Err("capture_document_rgba: " + read.error());
  }
  return pixels;
}

}  // namespace tamias
