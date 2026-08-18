#pragma once

#include "engine/render/rhi/device.h"
#include "engine/graphics/mesh.h"
#include "engine/math/camera.h"
#include "engine/render/material.h"
#include "engine/render/render_types.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tamias {

struct GpuMesh {
  std::unique_ptr<Buffer> vertex_buffer;
  std::unique_ptr<Buffer> index_buffer;
  std::uint32_t index_count = 0;
  Aabb bounds{};
};

struct GpuTexture {
  std::unique_ptr<Texture> texture;
};

struct FrameSubmission {
  NativeWindowHandle window{};
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  Mat4 view = Mat4::identity();
  Mat4 proj = Mat4::identity();
  Vec3 eye_position{};
  RenderMode mode = RenderMode::Shaded;
  std::vector<SceneDrawItem> items;
  bool show_axes = true;  // 世界坐标轴（X红/Y绿/Z蓝）
  bool show_preview_line = false;
  Vec3 preview_start{};
  Vec3 preview_end{};
  float clear_color[4] = {0.12f, 0.13f, 0.15f, 1.f};
};

// Shared execution config: Vulkan views with matching validation can share a device/thread.
struct RenderDeviceConfig {
  GraphicsBackend backend = GraphicsBackend::Vulkan;
  bool enable_validation = true;

  [[nodiscard]] bool shares_execution_thread_with(const RenderDeviceConfig& other) const {
    return backend != GraphicsBackend::OpenGL && backend == other.backend &&
           enable_validation == other.enable_validation;
  }
};

class RenderThread {
 public:
  explicit RenderThread(RenderDeviceConfig config);
  ~RenderThread();

  RenderThread(const RenderThread&) = delete;
  RenderThread& operator=(const RenderThread&) = delete;

  [[nodiscard]] const RenderDeviceConfig& config() const { return config_; }
  [[nodiscard]] RHIDevice* device() const { return device_.get(); }

  Result<void> start();
  void stop();

  // Upload mesh on the render thread and map it from a semantic asset id; returns
  // the assigned GPU mesh id. The semantic side refers to geometry by asset id.
  Result<std::uint64_t> upload_mesh(std::uint64_t asset_id, MeshCpu mesh);
  // 上传纹理资产（幂等：已缓存则直接返回已有 GPU 纹理 id）。
  Result<std::uint64_t> upload_texture(std::uint64_t asset_id, TextureAsset asset);
  void submit_frame(std::uint64_t channel_id, FrameSubmission frame);
  void resize_surface(std::uint64_t channel_id, NativeWindowHandle window, std::uint32_t w,
                      std::uint32_t h);
  std::uint64_t create_channel();
  void destroy_channel(std::uint64_t channel_id);

 private:
  struct ChannelState {
    std::unique_ptr<SwapChain> swap_chain;
    std::unique_ptr<CommandList> command_list;
    std::optional<FrameSubmission> latest;
    NativeWindowHandle window{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool needs_recreate = true;
  };

  struct PushConstants {
    Mat4 mvp;
    Mat4 model;
    float color[4];
    float material[4];  // x=roughness, y=metallic, z=has_albedo, w=has_normal
    float light_dir_selected[4];
    float eye_pos_mode[4];
  };

  void thread_main();
  Result<void> ensure_pipelines();
  Result<void> draw_channel(std::uint64_t id, ChannelState& channel);

  RenderDeviceConfig config_{};
  std::unique_ptr<RHIDevice> device_;
  std::unique_ptr<ShaderModule> vs_;
  std::unique_ptr<ShaderModule> fs_;
  std::unique_ptr<PipelineState> shaded_pipeline_;
  std::unique_ptr<PipelineState> wire_pipeline_;
  std::unique_ptr<PipelineState> line_pipeline_;
  GpuMesh axes_mesh_;
  std::unique_ptr<ShaderModule> sky_vs_;
  std::unique_ptr<ShaderModule> sky_fs_;
  std::unique_ptr<PipelineState> sky_pipeline_;
  std::unique_ptr<ShaderModule> grid_vs_;
  std::unique_ptr<ShaderModule> grid_fs_;
  std::unique_ptr<PipelineState> grid_pipeline_;
  GpuMesh sky_mesh_;
  GpuMesh grid_mesh_;
  GpuMesh preview_line_mesh_;
  std::unordered_map<std::uint64_t, GpuMesh> meshes_;
  std::unordered_map<std::uint64_t, std::uint64_t> asset_to_gpu_;  // asset id -> gpu mesh id
  std::unordered_map<std::uint64_t, GpuTexture> textures_;
  std::unordered_map<std::uint64_t, std::uint64_t> texture_asset_to_gpu_;  // asset id -> gpu texture id
  std::unique_ptr<Texture> default_texture_;  // 1x1 白纹理，无贴图物体兜底
  bool logged_texture_diag_ = false;  // 只打一次贴图诊断日志
  std::unordered_map<std::uint64_t, ChannelState> channels_;
  std::uint64_t next_mesh_id_ = 1;
  std::uint64_t next_texture_id_ = 1;
  std::uint64_t next_channel_id_ = 1;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  bool stop_requested_ = false;
  std::queue<std::function<void()>> tasks_;
};

class RenderChannel {
 public:
  RenderChannel(std::shared_ptr<RenderThread> thread, std::uint64_t id);
  ~RenderChannel();

  void submit(FrameSubmission frame);
  void resize(NativeWindowHandle window, std::uint32_t w, std::uint32_t h);
  [[nodiscard]] RenderThread& thread() const { return *thread_; }
  [[nodiscard]] std::uint64_t id() const { return id_; }

 private:
  std::shared_ptr<RenderThread> thread_;
  std::uint64_t id_ = 0;
};

class RenderThreadPool {
 public:
  static RenderThreadPool& instance();
  std::shared_ptr<RenderThread> acquire(const RenderDeviceConfig& config);
  void shutdown();

 private:
  std::mutex mutex_;
  std::vector<std::shared_ptr<RenderThread>> threads_;
};

Result<std::vector<std::uint32_t>> load_spirv_file(const std::string& path);

}  // namespace tamias
