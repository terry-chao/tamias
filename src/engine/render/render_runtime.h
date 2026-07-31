#pragma once

#include "engine/rhi/device.h"
#include "graphics/mesh.h"
#include "math/camera.h"

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

enum class RenderMode { Wireframe, Shaded, Realistic };

struct GpuMesh {
  std::unique_ptr<Buffer> vertex_buffer;
  std::unique_ptr<Buffer> index_buffer;
  std::uint32_t index_count = 0;
  Aabb bounds{};
};

struct SceneDrawItem {
  std::uint64_t node_id = 0;
  std::uint64_t mesh_id = 0;
  Mat4 transform = Mat4::identity();
  Vec3 color{0.75f, 0.78f, 0.82f};
  bool selected = false;
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

  // Upload mesh on the render thread; returns assigned mesh id.
  Result<std::uint64_t> upload_mesh(MeshCpu mesh);
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
  std::unordered_map<std::uint64_t, GpuMesh> meshes_;
  std::unordered_map<std::uint64_t, ChannelState> channels_;
  std::uint64_t next_mesh_id_ = 1;
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
Result<std::string> load_text_file(const std::string& path);

}  // namespace tamias
