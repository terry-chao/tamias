#include "render_runtime.h"

#include "core/log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <span>

namespace tamias {

Result<std::vector<std::uint32_t>> load_spirv_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return Err("failed to open spirv: " + path);
  }
  const auto size = file.tellg();
  if (size <= 0 || (size % 4) != 0) {
    return Err("invalid spirv size: " + path);
  }
  file.seekg(0);
  std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
  file.read(reinterpret_cast<char*>(words.data()), size);
  return words;
}

RenderThread::RenderThread(RenderDeviceConfig config) : config_(config) {}

RenderThread::~RenderThread() { stop(); }

Result<void> RenderThread::start() {
  if (running_) {
    return {};
  }
  DeviceCreateInfo info{};
  info.backend = config_.backend;
  info.enable_validation = config_.enable_validation;
  auto device = RHIDevice::create(info);
  if (!device) {
    return Err(device.error());
  }
  device_ = std::move(*device);
  stop_requested_ = false;
  running_ = true;
  thread_ = std::thread([this] { thread_main(); });
  return {};
}

void RenderThread::stop() {
  {
    std::scoped_lock lock(mutex_);
    stop_requested_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
  running_ = false;
  channels_.clear();
  meshes_.clear();
  shaded_pipeline_.reset();
  wire_pipeline_.reset();
  vs_.reset();
  fs_.reset();
  if (device_) {
    device_->wait_idle();
    device_.reset();
  }
}

std::uint64_t RenderThread::create_channel() {
  std::scoped_lock lock(mutex_);
  const auto id = next_channel_id_++;
  channels_.emplace(id, ChannelState{});
  return id;
}

void RenderThread::destroy_channel(std::uint64_t channel_id) {
  // Channel GPU objects must be destroyed on the render thread, and never while
  // draw_channel() holds a bare pointer into channels_.
  if (!running_ || !thread_.joinable() || std::this_thread::get_id() == thread_.get_id()) {
    std::scoped_lock lock(mutex_);
    channels_.erase(channel_id);
    return;
  }

  auto promise = std::make_shared<std::promise<void>>();
  auto future = promise->get_future();
  {
    std::scoped_lock lock(mutex_);
    tasks_.push([this, channel_id, promise]() {
      {
        std::scoped_lock task_lock(mutex_);
        channels_.erase(channel_id);
      }
      promise->set_value();
    });
  }
  cv_.notify_one();
  future.get();
}

Result<std::uint64_t> RenderThread::upload_mesh(MeshCpu mesh) {
  auto promise = std::make_shared<std::promise<Result<std::uint64_t>>>();
  auto future = promise->get_future();
  {
    std::scoped_lock lock(mutex_);
    tasks_.push([this, mesh = std::move(mesh), promise]() mutable {
      BufferDesc vb{};
      vb.size = mesh.vertices.size() * sizeof(Vertex);
      vb.usage = BufferDesc::Usage::Vertex;
      vb.host_visible = true;
      auto vbuf = device_->create_buffer(vb);
      if (!vbuf) {
        promise->set_value(Err(vbuf.error()));
        return;
      }
      auto bytes = std::as_bytes(std::span(mesh.vertices));
      if (auto w = (*vbuf)->write(0, bytes); !w) {
        promise->set_value(Err(w.error()));
        return;
      }

      BufferDesc ib{};
      ib.size = mesh.indices.size() * sizeof(std::uint32_t);
      ib.usage = BufferDesc::Usage::Index;
      ib.host_visible = true;
      auto ibuf = device_->create_buffer(ib);
      if (!ibuf) {
        promise->set_value(Err(ibuf.error()));
        return;
      }
      auto ibytes = std::as_bytes(std::span(mesh.indices));
      if (auto w = (*ibuf)->write(0, ibytes); !w) {
        promise->set_value(Err(w.error()));
        return;
      }

      GpuMesh gpu;
      gpu.vertex_buffer = std::move(*vbuf);
      gpu.index_buffer = std::move(*ibuf);
      gpu.index_count = static_cast<std::uint32_t>(mesh.indices.size());
      gpu.bounds = mesh.bounds;
      const auto id = next_mesh_id_++;
      meshes_.emplace(id, std::move(gpu));
      promise->set_value(id);
    });
  }
  cv_.notify_one();
  return future.get();
}

void RenderThread::submit_frame(std::uint64_t channel_id, FrameSubmission frame) {
  std::scoped_lock lock(mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return;
  }
  it->second.latest = std::move(frame);
  cv_.notify_one();
}

void RenderThread::resize_surface(std::uint64_t channel_id, NativeWindowHandle window,
                                  std::uint32_t w, std::uint32_t h) {
  std::scoped_lock lock(mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return;
  }
  it->second.window = window;
  it->second.width = std::max(1u, w);
  it->second.height = std::max(1u, h);
  it->second.needs_recreate = true;
  cv_.notify_one();
}

Result<void> RenderThread::ensure_pipelines() {
  if (shaded_pipeline_ && wire_pipeline_) {
    return {};
  }
  const auto shader_dir = std::filesystem::current_path() / "shaders";
  auto vs_spirv = load_spirv_file((shader_dir / "mesh.vert.spv").string());
  if (!vs_spirv) {
    vs_spirv = load_spirv_file((std::filesystem::path(TAMIAS_SHADER_DIR) / "mesh.vert.spv").string());
  }
  if (!vs_spirv) {
    return Err(vs_spirv.error());
  }
  auto fs_spirv = load_spirv_file((shader_dir / "mesh.frag.spv").string());
  if (!fs_spirv) {
    fs_spirv = load_spirv_file((std::filesystem::path(TAMIAS_SHADER_DIR) / "mesh.frag.spv").string());
  }
  if (!fs_spirv) {
    return Err(fs_spirv.error());
  }

  ShaderModuleDesc vs_desc{};
  vs_desc.spirv = *vs_spirv;
  auto vs = device_->create_shader_module(vs_desc);
  if (!vs) {
    return Err(vs.error());
  }
  ShaderModuleDesc fs_desc{};
  fs_desc.spirv = *fs_spirv;
  auto fs = device_->create_shader_module(fs_desc);
  if (!fs) {
    return Err(fs.error());
  }
  vs_ = std::move(*vs);
  fs_ = std::move(*fs);

  PipelineDesc shaded{};
  shaded.vertex_shader = vs_.get();
  shaded.fragment_shader = fs_.get();
  shaded.wireframe = false;
  auto p0 = device_->create_pipeline(shaded);
  if (!p0) {
    return Err(p0.error());
  }
  PipelineDesc wire = shaded;
  wire.wireframe = true;
  auto p1 = device_->create_pipeline(wire);
  if (!p1) {
    return Err(p1.error());
  }
  shaded_pipeline_ = std::move(*p0);
  wire_pipeline_ = std::move(*p1);
  return {};
}

Result<void> RenderThread::draw_channel(std::uint64_t, ChannelState& channel) {
  if (!channel.latest || channel.width == 0 || channel.height == 0 || !channel.window.valid()) {
    return {};
  }
  if (auto r = ensure_pipelines(); !r) {
    return r;
  }

  if (channel.needs_recreate || !channel.swap_chain) {
    SwapChainDesc desc{};
    desc.window = channel.window;
    desc.width = channel.width;
    desc.height = channel.height;
    if (channel.swap_chain) {
      if (auto r = channel.swap_chain->resize(desc.width, desc.height); !r) {
        channel.swap_chain.reset();
        auto sc = device_->create_swap_chain(desc);
        if (!sc) {
          return Err(sc.error());
        }
        channel.swap_chain = std::move(*sc);
      }
    } else {
      auto sc = device_->create_swap_chain(desc);
      if (!sc) {
        return Err(sc.error());
      }
      channel.swap_chain = std::move(*sc);
    }
    if (!channel.command_list) {
      auto cmd = device_->create_command_list();
      if (!cmd) {
        return Err(cmd.error());
      }
      channel.command_list = std::move(*cmd);
    }
    channel.needs_recreate = false;
  }

  const FrameSubmission& frame = *channel.latest;
  if (auto r = device_->begin_frame(*channel.swap_chain); !r) {
    channel.needs_recreate = true;
    return r;
  }

  channel.command_list->begin();
  channel.command_list->begin_render_pass(*channel.swap_chain, frame.clear_color, 1.f);
  channel.command_list->set_viewport(0.f, 0.f, static_cast<float>(channel.swap_chain->width()),
                                     static_cast<float>(channel.swap_chain->height()));
  channel.command_list->set_scissor(0, 0, channel.swap_chain->width(), channel.swap_chain->height());
  channel.command_list->set_pipeline(frame.mode == RenderMode::Wireframe ? *wire_pipeline_
                                                                         : *shaded_pipeline_);

  const Mat4 clip = device_->clip_space_correction_matrix();
  const Mat4 view_proj = clip * frame.proj * frame.view;
  // 0 = wireframe (unlit), 1 = shaded, 2 = realistic
  const float mode_value = frame.mode == RenderMode::Wireframe  ? 0.f
                           : frame.mode == RenderMode::Realistic ? 2.f
                                                                 : 1.f;

  for (const auto& item : frame.items) {
    auto mesh_it = meshes_.find(item.mesh_id);
    if (mesh_it == meshes_.end()) {
      continue;
    }
    const GpuMesh& mesh = mesh_it->second;
    PushConstants pc{};
    pc.mvp = view_proj * item.transform;
    pc.model = item.transform;
    pc.color[0] = item.color.x;
    pc.color[1] = item.color.y;
    pc.color[2] = item.color.z;
    pc.color[3] = 1.f;
    pc.light_dir_selected[0] = 0.45f;
    pc.light_dir_selected[1] = 0.35f;
    pc.light_dir_selected[2] = 0.82f;
    pc.light_dir_selected[3] = item.selected ? 1.f : 0.f;
    pc.eye_pos_mode[0] = frame.eye_position.x;
    pc.eye_pos_mode[1] = frame.eye_position.y;
    pc.eye_pos_mode[2] = frame.eye_position.z;
    pc.eye_pos_mode[3] = mode_value;
    channel.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
    channel.command_list->set_vertex_buffer(*mesh.vertex_buffer);
    channel.command_list->set_index_buffer(*mesh.index_buffer);
    DrawIndexedDesc draw{};
    draw.index_count = mesh.index_count;
    channel.command_list->draw_indexed(draw);
  }

  channel.command_list->end_render_pass();
  channel.command_list->end();
  if (auto r = device_->execute(*channel.command_list); !r) {
    return r;
  }
  if (auto r = device_->end_frame(*channel.swap_chain); !r) {
    channel.needs_recreate = true;
    return r;
  }
  return {};
}

void RenderThread::thread_main() {
  log_info("RenderThread started");
  for (;;) {
    std::vector<std::function<void()>> tasks;
    std::vector<std::uint64_t> dirty_channels;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [&] {
        return stop_requested_ || !tasks_.empty() ||
               std::any_of(channels_.begin(), channels_.end(),
                           [](const auto& kv) { return kv.second.latest.has_value(); });
      });
      if (stop_requested_ && tasks_.empty()) {
        break;
      }
      while (!tasks_.empty()) {
        tasks.push_back(std::move(tasks_.front()));
        tasks_.pop();
      }
    }
    // Run tasks (including channel destruction) before sampling dirty channels so
    // destroy_channel() never races with draw_channel().
    for (auto& task : tasks) {
      task();
    }
    {
      std::scoped_lock lock(mutex_);
      for (auto& [id, ch] : channels_) {
        if (ch.latest) {
          dirty_channels.push_back(id);
        }
      }
    }
    for (auto id : dirty_channels) {
      ChannelState* channel = nullptr;
      {
        std::scoped_lock lock(mutex_);
        auto it = channels_.find(id);
        if (it != channels_.end()) {
          channel = &it->second;
        }
      }
      if (!channel) {
        continue;
      }
      if (auto r = draw_channel(id, *channel); !r) {
        log_warn(r.error());
      }
    }
  }
  log_info("RenderThread stopped");
}

RenderChannel::RenderChannel(std::shared_ptr<RenderThread> thread, std::uint64_t id)
    : thread_(std::move(thread)), id_(id) {}

RenderChannel::~RenderChannel() {
  if (thread_) {
    thread_->destroy_channel(id_);
  }
}

void RenderChannel::submit(FrameSubmission frame) { thread_->submit_frame(id_, std::move(frame)); }

void RenderChannel::resize(NativeWindowHandle window, std::uint32_t w, std::uint32_t h) {
  thread_->resize_surface(id_, window, w, h);
}

RenderThreadPool& RenderThreadPool::instance() {
  static RenderThreadPool pool;
  return pool;
}

std::shared_ptr<RenderThread> RenderThreadPool::acquire(const RenderDeviceConfig& config) {
  std::scoped_lock lock(mutex_);
  for (auto& thread : threads_) {
    if (thread->config().shares_execution_thread_with(config)) {
      return thread;
    }
  }
  auto thread = std::make_shared<RenderThread>(config);
  if (auto r = thread->start(); !r) {
    log_error(r.error());
    return nullptr;
  }
  threads_.push_back(thread);
  return thread;
}

void RenderThreadPool::shutdown() {
  std::scoped_lock lock(mutex_);
  for (auto& thread : threads_) {
    thread->stop();
  }
  threads_.clear();
}

}  // namespace tamias
