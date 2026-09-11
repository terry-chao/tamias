#include "render_runtime.h"

#include "engine/core/executable_directory.h"
#include "engine/core/log.h"
#include "engine/math/math.h"
#include "engine/render/ibl.h"
#include "engine/render/gpu_instance.h"
#include "engine/render/scene_graph.h"
#include "engine/render/texture_mips.h"
#if defined(TAMIAS_HAS_RHI_WEBGL)
#include "engine/render/rhi/webgl/webgl_shaders.h"
#endif
#if defined(TAMIAS_HAS_RHI_WEBGPU)
#include "engine/render/rhi/webgpu/webgpu_shaders.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

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

namespace {

std::filesystem::path resolve_shader_path(const std::filesystem::path& name) {
  const auto exe_dir = executable_directory() / "shaders" / name;
  if (std::filesystem::exists(exe_dir)) {
    return exe_dir;
  }
  const auto cwd = std::filesystem::current_path() / "shaders" / name;
  if (std::filesystem::exists(cwd)) {
    return cwd;
  }
  const auto out_dir = std::filesystem::path(TAMIAS_SHADER_DIR) / name;
  if (std::filesystem::exists(out_dir)) {
    return out_dir;
  }
  return std::filesystem::path(TAMIAS_SHADER_SOURCE_DIR) / name;
}

// 世界坐标轴线段（X 红 / Y 绿 / Z 蓝，LineList 拓扑）。
MeshCpu make_axes_mesh(float length = 1.0f) {
  MeshCpu mesh;
  const auto add_line = [&](Vec3 a, Vec3 b, Vec3 color) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    Vertex va{};
    va.position = a;
    va.normal = {0.f, 0.f, 1.f};
    va.color = color;
    Vertex vb = va;
    vb.position = b;
    mesh.vertices.push_back(va);
    mesh.vertices.push_back(vb);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
  };
  add_line({0, 0, 0}, {length, 0, 0}, {1, 0, 0});  // X 红
  add_line({0, 0, 0}, {0, length, 0}, {0, 1, 0});  // Y 绿
  add_line({0, 0, 0}, {0, 0, length}, {0, 0, 1});  // Z 蓝
  recompute_bounds(mesh);
  return mesh;
}

// 全屏三角形（顶点已是 NDC 坐标，覆盖整个屏幕）。
MeshCpu make_fullscreen_triangle() {
  MeshCpu mesh;
  const Vec3 verts[3] = {{-1.f, -1.f, 0.f}, {3.f, -1.f, 0.f}, {-1.f, 3.f, 0.f}};
  for (const auto& p : verts) {
    Vertex v{};
    v.position = p;
    v.normal = {0.f, 0.f, 1.f};
    mesh.vertices.push_back(v);
  }
  mesh.indices = {0, 1, 2};
  recompute_bounds(mesh);
  return mesh;
}

// 地面网格四边形（XZ 平面 y=-0.01 的大四边形，相机锚定；网格线由 shader 程序化生成）。
MeshCpu make_grid_quad(float extent = 2000.f) {
  MeshCpu mesh;
  const Vec3 verts[4] = {
      {-extent, 0.f, -extent},
      {extent, 0.f, -extent},
      {extent, 0.f, extent},
      {-extent, 0.f, extent},
  };
  for (const auto& p : verts) {
    Vertex v{};
    v.position = p;
    v.normal = {0.f, 1.f, 0.f};
    mesh.vertices.push_back(v);
  }
  mesh.indices = {0, 1, 2, 0, 2, 3};
  recompute_bounds(mesh);
  return mesh;
}

// 预览线（单元线：原点 → +Z，白色顶点色；实际颜色由 push constant 决定）。
MeshCpu make_preview_line_mesh() {
  MeshCpu mesh;
  const Vec3 white{1.f, 1.f, 1.f};
  Vertex a{};
  a.position = {0.f, 0.f, 0.f};
  a.normal = {0.f, 1.f, 0.f};
  a.color = white;
  Vertex b = a;
  b.position = {0.f, 0.f, 1.f};
  mesh.vertices.push_back(a);
  mesh.vertices.push_back(b);
  mesh.indices = {0, 1};
  recompute_bounds(mesh);
  return mesh;
}

// 把 CPU 网格上传成 GPU 网格（顶点 + 索引 buffer）。
Result<GpuMesh> create_gpu_mesh(RHIDevice& device, MeshCpu mesh) {
  BufferDesc vb{};
  vb.size = mesh.vertices.size() * sizeof(Vertex);
  vb.usage = BufferDesc::Usage::Vertex;
  vb.host_visible = true;
  auto vbuf = device.create_buffer(vb);
  if (!vbuf) {
    return Err(vbuf.error());
  }
  if (auto w = (*vbuf)->write(0, std::as_bytes(std::span(mesh.vertices))); !w) {
    return Err(w.error());
  }
  BufferDesc ib{};
  ib.size = mesh.indices.size() * sizeof(std::uint32_t);
  ib.usage = BufferDesc::Usage::Index;
  ib.host_visible = true;
  auto ibuf = device.create_buffer(ib);
  if (!ibuf) {
    return Err(ibuf.error());
  }
  if (auto w = (*ibuf)->write(0, std::as_bytes(std::span(mesh.indices))); !w) {
    return Err(w.error());
  }
  GpuMesh gpu;
  gpu.vertex_buffer = std::move(*vbuf);
  gpu.index_buffer = std::move(*ibuf);
  gpu.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  gpu.bounds = mesh.bounds;
  gpu.line_list = mesh.line_list;
  gpu.has_texcoord = mesh.has_texcoord;
  gpu.gpu_bytes = mesh.vertices.size() * sizeof(Vertex) +
                  mesh.indices.size() * sizeof(std::uint32_t);
  return gpu;
}

}  // namespace

RenderThread::RenderThread(RenderDeviceConfig config) : config_(config) {}

RenderThread::~RenderThread() { stop(); }

void RenderThread::post(std::function<void()> task) {
  if (config_.synchronous) {
    task();
    return;
  }
  {
    std::scoped_lock lock(mutex_);
    tasks_.push(std::move(task));
  }
  cv_.notify_one();
}

void RenderThread::drain_once() {
  std::vector<std::function<void()>> tasks;
  std::vector<std::uint64_t> dirty_channels;
  {
    std::unique_lock lock(mutex_);
    while (!tasks_.empty()) {
      tasks.push_back(std::move(tasks_.front()));
      tasks_.pop();
    }
  }
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
    std::optional<FrameSubmission> frame;
    {
      std::scoped_lock lock(mutex_);
      auto it = channels_.find(id);
      if (it != channels_.end() && it->second.latest) {
        channel = &it->second;
        frame = *it->second.latest;
      }
    }
    if (!channel || !frame) {
      continue;
    }
    if (auto r = draw_channel(id, *channel, *frame); !r) {
      log_warn(r.error());
    }
  }
}

void RenderThread::pump() { drain_once(); }

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
  if (!config_.synchronous) {
    thread_ = std::thread([this] { thread_main(); });
  }
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
  if (device_) {
    device_->wait_idle();
  }
  channels_.clear();
  meshes_.clear();
  asset_to_gpu_.clear();
  resident_.clear();
  texture_resident_.clear();
  pending_evicted_texture_ids_.clear();
  lod_box_mesh_ = GpuMesh{};
  lod_box_gpu_id_ = 0;
  textures_.clear();
  texture_asset_to_gpu_.clear();
  default_texture_.reset();
  default_normal_.reset();
  default_orm_.reset();
  ibl_irradiance_.reset();
  ibl_prefilter_.reset();
  ibl_brdf_lut_.reset();
  instance_buffer_.reset();
  instance_write_offset_ = 0;
  shaded_pipeline_.reset();
  wire_pipeline_.reset();
  line_pipeline_.reset();
  entity_line_pipeline_.reset();
  blend_pipeline_.reset();
  sky_pipeline_.reset();
  grid_pipeline_.reset();
  axes_mesh_ = GpuMesh{};
  sky_mesh_ = GpuMesh{};
  grid_mesh_ = GpuMesh{};
  preview_line_mesh_ = GpuMesh{};
  vs_.reset();
  fs_.reset();
  sky_vs_.reset();
  sky_fs_.reset();
  grid_vs_.reset();
  grid_fs_.reset();
  if (device_) {
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

Result<std::uint64_t> RenderThread::upload_mesh_on_thread(std::uint64_t asset_id, MeshCpu mesh) {
  if (asset_id == 0 || mesh.vertices.empty() || mesh.indices.empty() || device_ == nullptr) {
    return Err("upload_mesh: empty mesh");
  }
  if (auto cached = resident_.lookup(asset_id)) {
    return *cached;
  }
  if (const auto it = asset_to_gpu_.find(asset_id); it != asset_to_gpu_.end()) {
    resident_.insert(asset_id, it->second, meshes_[it->second].gpu_bytes);
    return it->second;
  }
  auto gpu = create_gpu_mesh(*device_, std::move(mesh));
  if (!gpu) {
    return Err(gpu.error());
  }
  const std::uint64_t bytes = gpu->gpu_bytes;
  const auto id = next_mesh_id_++;
  meshes_.emplace(id, std::move(*gpu));
  asset_to_gpu_[asset_id] = id;
  resident_.insert(asset_id, id, bytes);
  evict_resident();
  return id;
}

void RenderThread::evict_resident() {
  for (const std::uint64_t asset_id : resident_.over_budget_assets()) {
    const auto gpu_it = asset_to_gpu_.find(asset_id);
    if (gpu_it != asset_to_gpu_.end()) {
      meshes_.erase(gpu_it->second);
      asset_to_gpu_.erase(gpu_it);
    }
    resident_.erase(asset_id);
  }
}

void RenderThread::evict_texture_resident() {
  std::vector<std::uint64_t> evicted;
  for (const std::uint64_t asset_id : texture_resident_.over_budget_assets()) {
    const auto gpu_it = texture_asset_to_gpu_.find(asset_id);
    if (gpu_it != texture_asset_to_gpu_.end()) {
      textures_.erase(gpu_it->second);
      texture_asset_to_gpu_.erase(gpu_it);
    }
    texture_resident_.erase(asset_id);
    evicted.push_back(asset_id);
  }
  if (!evicted.empty()) {
    std::scoped_lock lock(mutex_);
    pending_evicted_texture_ids_.insert(pending_evicted_texture_ids_.end(), evicted.begin(),
                                        evicted.end());
  }
}

std::vector<std::uint64_t> RenderThread::take_evicted_texture_ids() {
  std::scoped_lock lock(mutex_);
  std::vector<std::uint64_t> out;
  out.swap(pending_evicted_texture_ids_);
  return out;
}

Result<std::uint64_t> RenderThread::upload_mesh(std::uint64_t asset_id, MeshCpu mesh) {
  auto promise = std::make_shared<std::promise<Result<std::uint64_t>>>();
  auto future = promise->get_future();
  post([this, mesh = std::move(mesh), promise, asset_id]() mutable {
    promise->set_value(upload_mesh_on_thread(asset_id, std::move(mesh)));
  });
  return future.get();
}

void RenderThread::request_upload_mesh(std::uint64_t asset_id, MeshCpu mesh) {
  if (asset_id == 0 || mesh.vertices.empty()) {
    return;
  }
  post([this, mesh = std::move(mesh), asset_id]() mutable {
    (void)upload_mesh_on_thread(asset_id, std::move(mesh));
  });
}

std::vector<LodRequest> RenderThread::take_lod_requests() {
  std::scoped_lock lock(mutex_);
  std::vector<LodRequest> out;
  out.swap(pending_lod_requests_);
  return out;
}

RenderFrameStats RenderThread::last_stats() const {
  std::scoped_lock lock(mutex_);
  return last_stats_;
}

Result<std::uint64_t> RenderThread::upload_texture(std::uint64_t asset_id, TextureAsset asset) {
  auto promise = std::make_shared<std::promise<Result<std::uint64_t>>>();
  auto future = promise->get_future();
  post([this, asset = std::move(asset), promise, asset_id]() mutable {
      // 幂等：同一资产同一 generation 只上传一次；replace 后 generation 变了则重建。
      if (auto it = texture_asset_to_gpu_.find(asset_id); it != texture_asset_to_gpu_.end()) {
        auto gpu_it = textures_.find(it->second);
        if (gpu_it != textures_.end() && gpu_it->second.generation == asset.generation) {
          if (!texture_resident_.lookup(asset_id)) {
            texture_resident_.insert(asset_id, it->second, gpu_it->second.gpu_bytes);
          }
          promise->set_value(it->second);
          return;
        }
        if (gpu_it != textures_.end()) {
          textures_.erase(gpu_it);
        }
        texture_asset_to_gpu_.erase(it);
        texture_resident_.erase(asset_id);
      }
      const int first_r = asset.rgba.empty() ? 0 : static_cast<int>(asset.rgba[0]);
      const int first_g = asset.rgba.size() > 1 ? static_cast<int>(asset.rgba[1]) : 0;
      const int first_b = asset.rgba.size() > 2 ? static_cast<int>(asset.rgba[2]) : 0;
      const int first_a = asset.rgba.size() > 3 ? static_cast<int>(asset.rgba[3]) : 0;
      log_info("upload_texture: id=" + std::to_string(asset_id) + " gen=" +
               std::to_string(asset.generation) + " " + std::to_string(asset.width) + "x" +
               std::to_string(asset.height) + " first_rgba=" + std::to_string(first_r) + "," +
               std::to_string(first_g) + "," + std::to_string(first_b) + "," +
               std::to_string(first_a));
      std::vector<std::vector<std::uint8_t>> mips;
      const std::size_t expected =
          static_cast<std::size_t>(asset.width) * static_cast<std::size_t>(asset.height) * 4u;
      if (!asset.rgba.empty() && asset.rgba.size() >= expected && asset.width > 0 &&
          asset.height > 0) {
        mips = build_texture_mips(asset);
      } else if (!asset.rgba.empty()) {
        mips.push_back(asset.rgba);
      }
      TextureDesc desc{};
      desc.width = asset.width;
      desc.height = asset.height;
      desc.format = asset.srgb ? TextureDesc::Format::R8G8B8A8_SRGB
                               : TextureDesc::Format::R8G8B8A8_UNORM;
      desc.usage = TextureDesc::Usage::Sampled;
      desc.mip_levels = std::max(1u, static_cast<std::uint32_t>(mips.size()));
      auto tex = device_->create_texture(desc);
      if (!tex) {
        promise->set_value(Err(tex.error()));
        return;
      }
      std::uint64_t gpu_bytes = 0;
      if (mips.empty()) {
        if (auto w = (*tex)->write(0, std::as_bytes(std::span(asset.rgba))); !w) {
          promise->set_value(Err(w.error()));
          return;
        }
      } else {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(mips.size()); ++i) {
          if (auto w = (*tex)->write_subresource(i, 0, std::as_bytes(std::span(mips[i]))); !w) {
            promise->set_value(Err(w.error()));
            return;
          }
          gpu_bytes += mips[i].size();
        }
      }
      const auto id = next_texture_id_++;
      textures_.emplace(id, GpuTexture{std::move(*tex), asset.generation, gpu_bytes});
      texture_asset_to_gpu_[asset_id] = id;
      texture_resident_.insert(asset_id, id, gpu_bytes);
      evict_texture_resident();
      promise->set_value(id);
    });
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
  ChannelState& channel = it->second;
  const std::uint32_t width = std::max(1u, w);
  const std::uint32_t height = std::max(1u, h);
  const bool same_canvas =
      channel.window.canvas_selector == window.canvas_selector ||
      (channel.window.canvas_selector != nullptr && window.canvas_selector != nullptr &&
       std::strcmp(channel.window.canvas_selector, window.canvas_selector) == 0);
  const bool same_window = channel.window.hwnd == window.hwnd &&
                           channel.window.display == window.display &&
                           channel.window.window == window.window && same_canvas;
  if (same_window && channel.width == width && channel.height == height) {
    return;
  }
  channel.window = window;
  channel.width = width;
  channel.height = height;
  channel.needs_recreate = true;
  cv_.notify_one();
}

Result<void> RenderThread::ensure_pipelines() {
  if (shaded_pipeline_ && wire_pipeline_ && line_pipeline_ && entity_line_pipeline_ &&
      blend_pipeline_ && sky_pipeline_ && grid_pipeline_ &&
      axes_mesh_.index_buffer && sky_mesh_.index_buffer && grid_mesh_.index_buffer &&
      preview_line_mesh_.index_buffer && lod_box_mesh_.index_buffer && default_texture_ && default_normal_ &&
      default_orm_ &&
      ibl_irradiance_ && ibl_prefilter_ && ibl_brdf_lut_) {
    return {};
  }

  // Keep SPIR-V / GLSL alive for the create_shader_module span lifetime.
  std::vector<std::uint32_t> vs_spirv;
  std::vector<std::uint32_t> fs_spirv;
  std::string vs_glsl;
  std::string fs_glsl;
  std::string vs_wgsl;
  std::string fs_wgsl;

  const bool opengl = device_->backend() == GraphicsBackend::OpenGL;
  const bool webgl = device_->backend() == GraphicsBackend::WebGL;
  const bool webgpu = device_->backend() == GraphicsBackend::WebGPU;

  ShaderModuleDesc vs_desc{};
  vs_desc.stage = ShaderStage::Vertex;
  vs_desc.entry = "main";
  ShaderModuleDesc fs_desc{};
  fs_desc.stage = ShaderStage::Fragment;
  fs_desc.entry = "main";

  if (webgl) {
#if defined(TAMIAS_HAS_RHI_WEBGL)
    vs_glsl.assign(webgl_shaders::mesh_vert());
    fs_glsl.assign(webgl_shaders::mesh_frag());
    vs_desc.language = ShaderLanguage::Glsl;
    vs_desc.glsl = std::span<const char>(vs_glsl.data(), vs_glsl.size());
    fs_desc.language = ShaderLanguage::Glsl;
    fs_desc.glsl = std::span<const char>(fs_glsl.data(), fs_glsl.size());
#else
    return Err("WebGL shaders were not compiled into this binary");
#endif
  } else if (webgpu) {
#if defined(TAMIAS_HAS_RHI_WEBGPU)
    vs_wgsl.assign(webgpu_shaders::mesh_vert());
    fs_wgsl.assign(webgpu_shaders::mesh_frag());
    vs_desc.language = ShaderLanguage::Wgsl;
    vs_desc.wgsl = std::span<const char>(vs_wgsl.data(), vs_wgsl.size());
    fs_desc.language = ShaderLanguage::Wgsl;
    fs_desc.wgsl = std::span<const char>(fs_wgsl.data(), fs_wgsl.size());
#else
    return Err("WebGPU shaders were not compiled into this binary");
#endif
  } else {
    const char* vs_name = opengl ? "mesh.vert.gl.spv" : "mesh.vert.spv";
    const char* fs_name = opengl ? "mesh.frag.gl.spv" : "mesh.frag.spv";
    auto vs_words = load_spirv_file(resolve_shader_path(vs_name).string());
    if (!vs_words) {
      return Err(vs_words.error());
    }
    auto fs_words = load_spirv_file(resolve_shader_path(fs_name).string());
    if (!fs_words) {
      return Err(fs_words.error());
    }
    vs_spirv = std::move(*vs_words);
    fs_spirv = std::move(*fs_words);
    vs_desc.language = ShaderLanguage::Spirv;
    vs_desc.spirv = vs_spirv;
    fs_desc.language = ShaderLanguage::Spirv;
    fs_desc.spirv = fs_spirv;
  }

  auto vs = device_->create_shader_module(vs_desc);
  if (!vs) {
    return Err(vs.error());
  }
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
  shaded.instanced = true;
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

  PipelineDesc blend = shaded;
  blend.blend = true;
  blend.depth_write = false;
  auto pblend = device_->create_pipeline(blend);
  if (!pblend) {
    return Err(pblend.error());
  }
  blend_pipeline_ = std::move(*pblend);

  // 天空管线（全屏三角形，深度测试关、不写深度）。
  std::vector<std::uint32_t> sky_vs_spirv;
  std::vector<std::uint32_t> sky_fs_spirv;
  std::string sky_vs_glsl;
  std::string sky_fs_glsl;
  std::string sky_vs_wgsl;
  std::string sky_fs_wgsl;
  ShaderModuleDesc sky_vs_desc{};
  sky_vs_desc.stage = ShaderStage::Vertex;
  sky_vs_desc.entry = "main";
  ShaderModuleDesc sky_fs_desc{};
  sky_fs_desc.stage = ShaderStage::Fragment;
  sky_fs_desc.entry = "main";
  if (webgl) {
#if defined(TAMIAS_HAS_RHI_WEBGL)
    sky_vs_glsl.assign(webgl_shaders::sky_vert());
    sky_fs_glsl.assign(webgl_shaders::sky_frag());
    sky_vs_desc.language = ShaderLanguage::Glsl;
    sky_vs_desc.glsl = std::span<const char>(sky_vs_glsl.data(), sky_vs_glsl.size());
    sky_fs_desc.language = ShaderLanguage::Glsl;
    sky_fs_desc.glsl = std::span<const char>(sky_fs_glsl.data(), sky_fs_glsl.size());
#else
    return Err("WebGL shaders were not compiled into this binary");
#endif
  } else if (webgpu) {
#if defined(TAMIAS_HAS_RHI_WEBGPU)
    sky_vs_wgsl.assign(webgpu_shaders::sky_vert());
    sky_fs_wgsl.assign(webgpu_shaders::sky_frag());
    sky_vs_desc.language = ShaderLanguage::Wgsl;
    sky_vs_desc.wgsl = std::span<const char>(sky_vs_wgsl.data(), sky_vs_wgsl.size());
    sky_fs_desc.language = ShaderLanguage::Wgsl;
    sky_fs_desc.wgsl = std::span<const char>(sky_fs_wgsl.data(), sky_fs_wgsl.size());
#else
    return Err("WebGPU shaders were not compiled into this binary");
#endif
  } else {
    const char* sky_vs_name = opengl ? "sky.vert.gl.spv" : "sky.vert.spv";
    const char* sky_fs_name = opengl ? "sky.frag.gl.spv" : "sky.frag.spv";
    auto sky_vs_words = load_spirv_file(resolve_shader_path(sky_vs_name).string());
    if (!sky_vs_words) {
      return Err(sky_vs_words.error());
    }
    auto sky_fs_words = load_spirv_file(resolve_shader_path(sky_fs_name).string());
    if (!sky_fs_words) {
      return Err(sky_fs_words.error());
    }
    sky_vs_spirv = std::move(*sky_vs_words);
    sky_fs_spirv = std::move(*sky_fs_words);
    sky_vs_desc.language = ShaderLanguage::Spirv;
    sky_vs_desc.spirv = sky_vs_spirv;
    sky_fs_desc.language = ShaderLanguage::Spirv;
    sky_fs_desc.spirv = sky_fs_spirv;
  }

  auto sky_vs = device_->create_shader_module(sky_vs_desc);
  if (!sky_vs) {
    return Err(sky_vs.error());
  }
  auto sky_fs = device_->create_shader_module(sky_fs_desc);
  if (!sky_fs) {
    return Err(sky_fs.error());
  }
  sky_vs_ = std::move(*sky_vs);
  sky_fs_ = std::move(*sky_fs);

  PipelineDesc sky{};
  sky.vertex_shader = sky_vs_.get();
  sky.fragment_shader = sky_fs_.get();
  sky.depth_test = false;
  auto psky = device_->create_pipeline(sky);
  if (!psky) {
    return Err(psky.error());
  }
  sky_pipeline_ = std::move(*psky);

  // 地面网格管线（网格 shader；测深度但不写深度，不遮挡地下的模型）。
  std::vector<std::uint32_t> grid_vs_spirv;
  std::vector<std::uint32_t> grid_fs_spirv;
  std::string grid_vs_glsl;
  std::string grid_fs_glsl;
  std::string grid_vs_wgsl;
  std::string grid_fs_wgsl;
  ShaderModuleDesc grid_vs_desc{};
  grid_vs_desc.stage = ShaderStage::Vertex;
  grid_vs_desc.entry = "main";
  ShaderModuleDesc grid_fs_desc{};
  grid_fs_desc.stage = ShaderStage::Fragment;
  grid_fs_desc.entry = "main";
  if (webgl) {
#if defined(TAMIAS_HAS_RHI_WEBGL)
    grid_vs_glsl.assign(webgl_shaders::grid_vert());
    grid_fs_glsl.assign(webgl_shaders::grid_frag());
    grid_vs_desc.language = ShaderLanguage::Glsl;
    grid_vs_desc.glsl = std::span<const char>(grid_vs_glsl.data(), grid_vs_glsl.size());
    grid_fs_desc.language = ShaderLanguage::Glsl;
    grid_fs_desc.glsl = std::span<const char>(grid_fs_glsl.data(), grid_fs_glsl.size());
#else
    return Err("WebGL shaders were not compiled into this binary");
#endif
  } else if (webgpu) {
#if defined(TAMIAS_HAS_RHI_WEBGPU)
    grid_vs_wgsl.assign(webgpu_shaders::grid_vert());
    grid_fs_wgsl.assign(webgpu_shaders::grid_frag());
    grid_vs_desc.language = ShaderLanguage::Wgsl;
    grid_vs_desc.wgsl = std::span<const char>(grid_vs_wgsl.data(), grid_vs_wgsl.size());
    grid_fs_desc.language = ShaderLanguage::Wgsl;
    grid_fs_desc.wgsl = std::span<const char>(grid_fs_wgsl.data(), grid_fs_wgsl.size());
#else
    return Err("WebGPU shaders were not compiled into this binary");
#endif
  } else {
    const char* grid_vs_name = opengl ? "grid.vert.gl.spv" : "grid.vert.spv";
    const char* grid_fs_name = opengl ? "grid.frag.gl.spv" : "grid.frag.spv";
    auto grid_vs_words = load_spirv_file(resolve_shader_path(grid_vs_name).string());
    if (!grid_vs_words) {
      return Err(grid_vs_words.error());
    }
    auto grid_fs_words = load_spirv_file(resolve_shader_path(grid_fs_name).string());
    if (!grid_fs_words) {
      return Err(grid_fs_words.error());
    }
    grid_vs_spirv = std::move(*grid_vs_words);
    grid_fs_spirv = std::move(*grid_fs_words);
    grid_vs_desc.language = ShaderLanguage::Spirv;
    grid_vs_desc.spirv = grid_vs_spirv;
    grid_fs_desc.language = ShaderLanguage::Spirv;
    grid_fs_desc.spirv = grid_fs_spirv;
  }

  auto grid_vs = device_->create_shader_module(grid_vs_desc);
  if (!grid_vs) {
    return Err(grid_vs.error());
  }
  auto grid_fs = device_->create_shader_module(grid_fs_desc);
  if (!grid_fs) {
    return Err(grid_fs.error());
  }
  grid_vs_ = std::move(*grid_vs);
  grid_fs_ = std::move(*grid_fs);

  PipelineDesc grid{};
  grid.vertex_shader = grid_vs_.get();
  grid.fragment_shader = grid_fs_.get();
  grid.depth_test = true;
  grid.depth_write = false;
  auto pgrid = device_->create_pipeline(grid);
  if (!pgrid) {
    return Err(pgrid.error());
  }
  grid_pipeline_ = std::move(*pgrid);

  // 坐标轴线管线：LineList + 关深度测试，让轴始终可见。
  PipelineDesc line = shaded;
  line.topology = PrimitiveTopology::LineList;
  line.depth_test = false;
  auto pl = device_->create_pipeline(line);
  if (!pl) {
    return Err(pl.error());
  }
  line_pipeline_ = std::move(*pl);

  // 草图实体线：LineList + 开深度，看起来是线而不是挤出的小圆柱。
  PipelineDesc entity_line = shaded;
  entity_line.topology = PrimitiveTopology::LineList;
  entity_line.depth_test = true;
  entity_line.depth_write = true;
  auto pel = device_->create_pipeline(entity_line);
  if (!pel) {
    return Err(pel.error());
  }
  entity_line_pipeline_ = std::move(*pel);

  // 上传环境网格（天空 / 地面网格 / 坐标轴）。
  auto sky_mesh = create_gpu_mesh(*device_, make_fullscreen_triangle());
  if (!sky_mesh) {
    return Err(sky_mesh.error());
  }
  sky_mesh_ = std::move(*sky_mesh);

  auto grid_mesh = create_gpu_mesh(*device_, make_grid_quad());
  if (!grid_mesh) {
    return Err(grid_mesh.error());
  }
  grid_mesh_ = std::move(*grid_mesh);

  auto axes_mesh = create_gpu_mesh(*device_, make_axes_mesh());
  if (!axes_mesh) {
    return Err(axes_mesh.error());
  }
  axes_mesh_ = std::move(*axes_mesh);

  auto preview_line_mesh = create_gpu_mesh(*device_, make_preview_line_mesh());
  if (!preview_line_mesh) {
    return Err(preview_line_mesh.error());
  }
  preview_line_mesh_ = std::move(*preview_line_mesh);

  auto lod_box = create_gpu_mesh(*device_, make_box_mesh(1.f, 1.f, 1.f));
  if (!lod_box) {
    return Err(lod_box.error());
  }
  lod_box_mesh_ = std::move(*lod_box);
  lod_box_gpu_id_ = next_mesh_id_++;

  // 默认 1×1 白纹理：无贴图材质也须绑定合法纹理（Vulkan 描述符要求），采样结果为白色。
  if (!default_texture_) {
    TextureDesc td{};
    td.width = 1;
    td.height = 1;
    td.format = TextureDesc::Format::R8G8B8A8_SRGB;
    td.usage = TextureDesc::Usage::Sampled;
    auto tex = device_->create_texture(td);
    if (!tex) {
      return Err(tex.error());
    }
    const std::byte white[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
    if (auto w = (*tex)->write(0, std::span<const std::byte>{white, 4}); !w) {
      return Err(w.error());
    }
    default_texture_ = std::move(*tex);
  }

  if (!default_normal_) {
    TextureDesc td{};
    td.width = 1;
    td.height = 1;
    td.format = TextureDesc::Format::R8G8B8A8_UNORM;
    td.usage = TextureDesc::Usage::Sampled;
    auto tex = device_->create_texture(td);
    if (!tex) {
      return Err(tex.error());
    }
    const std::byte flat[4] = {std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}};
    if (auto w = (*tex)->write(0, std::span<const std::byte>{flat, 4}); !w) {
      return Err(w.error());
    }
    default_normal_ = std::move(*tex);
  }

  if (!default_orm_) {
    TextureDesc td{};
    td.width = 1;
    td.height = 1;
    td.format = TextureDesc::Format::R8G8B8A8_UNORM;
    td.usage = TextureDesc::Usage::Sampled;
    auto tex = device_->create_texture(td);
    if (!tex) {
      return Err(tex.error());
    }
    const std::byte orm[4] = {std::byte{255}, std::byte{153}, std::byte{0}, std::byte{255}};
    if (auto w = (*tex)->write(0, std::span<const std::byte>{orm, 4}); !w) {
      return Err(w.error());
    }
    default_orm_ = std::move(*tex);
  }

  if (!ibl_irradiance_ || !ibl_prefilter_ || !ibl_brdf_lut_) {
    const IblCpu ibl = bake_studio_ibl();
    ibl_max_mip_ = ibl.prefilter_mips > 1 ? static_cast<float>(ibl.prefilter_mips - 1) : 1.f;

    auto make_cube_desc = [](std::uint32_t size, std::uint32_t mips) {
      TextureDesc td{};
      td.width = size;
      td.height = size;
      td.format = TextureDesc::Format::R16G16B16A16_SFLOAT;
      td.usage = TextureDesc::Usage::Sampled;
      td.dimension = TextureDesc::Dimension::Cube;
      td.mip_levels = std::max(1u, mips);
      td.address_mode = TextureDesc::AddressMode::Clamp;
      return td;
    };

    auto irr_tex = device_->create_texture(make_cube_desc(ibl.irradiance_size, 1));
    if (!irr_tex) {
      return Err(irr_tex.error());
    }
    for (int face = 0; face < 6; ++face) {
      if (auto w = (*irr_tex)->write_subresource(
              0, static_cast<std::uint32_t>(face),
              std::as_bytes(std::span(ibl.irradiance_faces[face])));
          !w) {
        return Err(w.error());
      }
    }
    ibl_irradiance_ = std::move(*irr_tex);

    auto pre_tex =
        device_->create_texture(make_cube_desc(ibl.prefilter_size, ibl.prefilter_mips));
    if (!pre_tex) {
      return Err(pre_tex.error());
    }
    for (int face = 0; face < 6; ++face) {
      for (std::uint32_t mip = 0; mip < ibl.prefilter_mips; ++mip) {
        if (auto w = (*pre_tex)->write_subresource(
                mip, static_cast<std::uint32_t>(face),
                std::as_bytes(std::span(ibl.prefilter_faces[face][mip])));
            !w) {
          return Err(w.error());
        }
      }
    }
    ibl_prefilter_ = std::move(*pre_tex);

    TextureDesc lut{};
    lut.width = ibl.lut_size;
    lut.height = ibl.lut_size;
    lut.format = TextureDesc::Format::R16G16_SFLOAT;
    lut.usage = TextureDesc::Usage::Sampled;
    lut.address_mode = TextureDesc::AddressMode::Clamp;
    auto lut_tex = device_->create_texture(lut);
    if (!lut_tex) {
      return Err(lut_tex.error());
    }
    if (auto w = (*lut_tex)->write(0, std::as_bytes(std::span(ibl.brdf_lut))); !w) {
      return Err(w.error());
    }
    ibl_brdf_lut_ = std::move(*lut_tex);
  }

  return {};
}

Result<void> RenderThread::draw_channel(std::uint64_t, ChannelState& channel,
                                       const FrameSubmission& frame) {
  if (channel.width == 0 || channel.height == 0 || !channel.window.valid()) {
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

  if (auto r = device_->begin_frame(*channel.swap_chain); !r) {
    channel.needs_recreate = true;
    return r;
  }

  channel.command_list->begin();
  channel.command_list->begin_render_pass(*channel.swap_chain, frame.clear_color, 1.f);
  channel.command_list->set_viewport(0.f, 0.f, static_cast<float>(channel.swap_chain->width()),
                                     static_cast<float>(channel.swap_chain->height()));
  channel.command_list->set_scissor(0, 0, channel.swap_chain->width(), channel.swap_chain->height());
  const Mat4 clip = device_->clip_space_correction_matrix();
  const Mat4 view_proj = clip * frame.proj * frame.view;
  instance_write_offset_ = 0;
  {
    const auto estimated = (std::max)(
        static_cast<std::uint64_t>((frame.items.size() + 4096u) * sizeof(GpuInstance)),
        static_cast<std::uint64_t>(1024 * 1024));
    if (!instance_buffer_ || instance_buffer_->desc().size < estimated) {
      BufferDesc d{};
      d.size = estimated;
      d.usage = BufferDesc::Usage::Vertex;
      d.host_visible = true;
      if (auto created = device_->create_buffer(d)) {
        instance_buffer_ = std::move(*created);
      }
    }
  }

  auto bind_mesh_sets = [&]() {
    if (default_texture_) {
      channel.command_list->set_texture(*default_texture_, kTextureSlotAlbedo);
    }
    if (default_normal_) {
      channel.command_list->set_texture(*default_normal_, kTextureSlotNormal);
    }
    if (default_orm_) {
      channel.command_list->set_texture(*default_orm_, kTextureSlotOrm);
    }
    if (ibl_irradiance_) {
      channel.command_list->set_texture(*ibl_irradiance_, kTextureSlotIblIrradiance);
    }
    if (ibl_prefilter_) {
      channel.command_list->set_texture(*ibl_prefilter_, kTextureSlotIblPrefilter);
    }
    if (ibl_brdf_lut_) {
      channel.command_list->set_texture(*ibl_brdf_lut_, kTextureSlotIblBrdfLut);
    }
  };
  auto upload_instances = [&](std::span<const GpuInstance> insts) -> bool {
    if (insts.empty() || !instance_buffer_) {
      return false;
    }
    const auto bytes = static_cast<std::uint64_t>(insts.size_bytes());
    if (instance_write_offset_ + bytes > instance_buffer_->desc().size) {
      return false;
    }
    if (auto w = instance_buffer_->write(instance_write_offset_, std::as_bytes(insts)); !w) {
      return false;
    }
    channel.command_list->set_instance_buffer(*instance_buffer_, instance_write_offset_);
    instance_write_offset_ += bytes;
    return true;
  };
  auto bind_identity_instance = [&]() {
    const GpuInstance inst =
        make_gpu_instance(Mat4::identity(), Vec3{1.f, 1.f, 1.f}, 1.f, 0.6f, 0.f, false);
    upload_instances(std::span<const GpuInstance>{&inst, 1});
  };
  // 天空：采样 IBL cubemap，与模型环境倒影同源。
  if (sky_pipeline_ && sky_mesh_.index_buffer && ibl_prefilter_) {
    PushConstants pc{};
    Mat4 cam_to_world = invert_affine(frame.view);
    cam_to_world(0, 3) = cam_to_world(1, 3) = cam_to_world(2, 3) = 0.f;
    pc.model = cam_to_world;
    pc.color[0] = 1.f / std::max(std::fabs(frame.proj(0, 0)), 1e-4f);
    pc.color[1] = 1.f / std::max(std::fabs(frame.proj(1, 1)), 1e-4f);
    pc.lighting[0] = 1.f;
    channel.command_list->set_pipeline(*sky_pipeline_);
    channel.command_list->set_texture(*ibl_prefilter_, kTextureSlotIblPrefilter);
    channel.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
    channel.command_list->set_vertex_buffer(*sky_mesh_.vertex_buffer);
    channel.command_list->set_index_buffer(*sky_mesh_.index_buffer);
    DrawIndexedDesc sd{};
    sd.index_count = sky_mesh_.index_count;
    channel.command_list->draw_indexed(sd);
  }

  // 无光照线条（mode 3）：地面网格 + 坐标轴。
  auto draw_lines = [&](PipelineState& pipeline, const GpuMesh& mesh) {
    PushConstants pc{};
    pc.mvp = view_proj;
    pc.model = Mat4::identity();
    pc.color[0] = pc.color[1] = pc.color[2] = pc.color[3] = 1.f;
    pc.eye_pos_mode[3] = 3.f;
    channel.command_list->set_pipeline(pipeline);
    // 网格 shader 静态引用贴图描述符，即使 mode==3 不采样也须绑定合法资源。
    bind_mesh_sets();
    channel.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
    channel.command_list->set_vertex_buffer(*mesh.vertex_buffer);
    channel.command_list->set_index_buffer(*mesh.index_buffer);
    bind_identity_instance();
    DrawIndexedDesc d{};
    d.index_count = mesh.index_count;
    channel.command_list->draw_indexed(d);
  };

  // 地面网格（相机锚定四边形 + 网格 shader；测深度不写深度）。
  if (grid_pipeline_ && grid_mesh_.index_buffer) {
    constexpr float kGridQuadExtent = 2000.f;
    const float view_scale = std::max(frame.view_distance, 1.f);
    // Fade ends at 32× view distance; grow the camera-anchored quad so it still covers it.
    const float extent_scale = std::max(view_scale * 32.f / kGridQuadExtent, 1.f);
    const Mat4 grid_model =
        translate({frame.eye_position.x, 0.0f, frame.eye_position.z}) *
        scale({extent_scale, 1.f, extent_scale});
    PushConstants pc{};
    pc.mvp = view_proj * grid_model;
    pc.model = grid_model;
    pc.eye_pos_mode[0] = frame.eye_position.x;
    pc.eye_pos_mode[1] = frame.eye_position.y;
    pc.eye_pos_mode[2] = frame.eye_position.z;
    pc.eye_pos_mode[3] = view_scale;
    channel.command_list->set_pipeline(*grid_pipeline_);
    channel.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
    channel.command_list->set_vertex_buffer(*grid_mesh_.vertex_buffer);
    channel.command_list->set_index_buffer(*grid_mesh_.index_buffer);
    DrawIndexedDesc d{};
    d.index_count = grid_mesh_.index_count;
    channel.command_list->draw_indexed(d);
  }

  // 模型：留存渲染场景图 + 脏标记增量同步。代次未变时不重建树，
  // 只让 RecordCommands 访问者从留存树录制 draw 命令（视锥剔除 / 隐藏集
  // 在录制时按节点判断）。代次变了但没有脏 id（clear / 整档恢复等）走整树重建兜底。
  const float mode_value = frame.mode == RenderMode::Wireframe  ? 0.f
                           : frame.mode == RenderMode::Realistic ? 2.f
                                                                 : 1.f;

  if (!channel.scene_root) {
    channel.scene_root = build_scene_graph(frame.items, &channel.scene_nodes);
    channel.scene_generation = frame.scene_generation;
  } else if (frame.scene_generation != channel.scene_generation) {
    if (frame.scene_dirty_ids.empty()) {
      channel.scene_root = build_scene_graph(frame.items, &channel.scene_nodes);
    } else if (auto* root = dynamic_cast<GroupNode*>(channel.scene_root.get())) {
      update_scene_graph(*root, channel.scene_nodes, frame.items, frame.scene_dirty_ids);
    } else {
      channel.scene_root = build_scene_graph(frame.items, &channel.scene_nodes);
    }
    channel.scene_generation = frame.scene_generation;
  }

  if (channel.scene_root) {
    // Vulkan 要求先有 pipeline layout 才能 bind descriptor set；mesh.frag 静态引用
    // 全部 5 个 set（着色模式不采样 IBL 也必须绑合法资源）。
    if (shaded_pipeline_) {
      channel.command_list->set_pipeline(*shaded_pipeline_);
    }
    bind_mesh_sets();
    std::unordered_set<std::uint64_t> hidden(frame.hidden_node_ids.begin(),
                                             frame.hidden_node_ids.end());
    const Frustum frustum = Frustum::from_view_proj(frame.proj * frame.view);
    SceneGraphDrawContext ctx{};
    ctx.command_list = channel.command_list.get();
    ctx.view_proj = &view_proj;
    ctx.frustum = &frustum;
    ctx.hidden_nodes = &hidden;
    ctx.eye_position = frame.eye_position;
    ctx.fovy = frame.fovy;
    ctx.framebuffer_height = static_cast<float>(std::max(frame.height, 1u));
    ctx.mode_value = mode_value;
    ctx.shaded_pipeline = shaded_pipeline_.get();
    ctx.wire_pipeline = wire_pipeline_.get();
    ctx.entity_line_pipeline = entity_line_pipeline_.get();
    ctx.blend_pipeline = blend_pipeline_.get();
    ctx.default_texture = default_texture_.get();
    ctx.default_normal = default_normal_.get();
    ctx.default_orm = default_orm_.get();
    ctx.exposure = 1.f;
    ctx.key_light_intensity = 0.45f;
    ctx.ibl_max_mip = ibl_max_mip_;
    ctx.meshes = &meshes_;
    ctx.asset_to_gpu = &asset_to_gpu_;
    ctx.textures = &textures_;
    ctx.texture_asset_to_gpu = &texture_asset_to_gpu_;
    ctx.texture_diag_logged = &logged_texture_diag_;
    ctx.grow_instance_buffer = [&](std::uint64_t bytes) -> Buffer* {
      if (instance_buffer_ && instance_buffer_->desc().size >= bytes) {
        return instance_buffer_.get();
      }
      if (instance_write_offset_ != 0) {
        return instance_buffer_.get();
      }
      BufferDesc d{};
      d.size = (std::max)(bytes, static_cast<std::uint64_t>(256 * sizeof(GpuInstance)));
      d.usage = BufferDesc::Usage::Vertex;
      d.host_visible = true;
      auto created = device_->create_buffer(d);
      if (!created) {
        return instance_buffer_.get();
      }
      instance_buffer_ = std::move(*created);
      return instance_buffer_.get();
    };
    ctx.instance_buffer = instance_buffer_.get();
    ctx.instance_write_offset = &instance_write_offset_;
    std::vector<LodRequest> lod_requests;
    RenderFrameStats stats{};
    ctx.lod_sets = &frame.lod_sets;
    ctx.lod_hysteresis = &channel.lod_by_node;
    ctx.lod_requests = &lod_requests;
    ctx.lod_box_mesh = lod_box_mesh_.index_buffer ? &lod_box_mesh_ : nullptr;
    ctx.lod_box_gpu_id = lod_box_gpu_id_;
    ctx.stats = &stats;
    RecordCommands visitor(ctx);
    channel.scene_root->accept(visitor);
    if (mode_value > 1.5f && blend_pipeline_) {
      ctx.transparent_pass = true;
      channel.command_list->set_pipeline(*blend_pipeline_);
      bind_mesh_sets();
      RecordCommands trans(ctx);
      channel.scene_root->accept(trans);
    }
    stats.gpu_mesh_bytes = resident_.bytes();
    stats.lod_requests = static_cast<std::uint32_t>(lod_requests.size());
    {
      std::scoped_lock lock(mutex_);
      last_stats_ = stats;
      pending_lod_requests_.insert(pending_lod_requests_.end(), lod_requests.begin(),
                                   lod_requests.end());
    }
  }

  // 坐标轴（深度测试关，始终可见）。
  if (frame.show_axes && line_pipeline_ && axes_mesh_.index_buffer) {
    draw_lines(*line_pipeline_, axes_mesh_);
  }

  // 预览线（建墙 / 草图曲线 / 贝塞尔控制多边形与控制点 / 网格捕捉，深度测试关）。
  if (line_pipeline_ && preview_line_mesh_.index_buffer) {
    const bool has_curve = frame.preview_polyline.size() >= 2;
    const bool has_controls = frame.preview_control_polyline.size() >= 2;
    const bool has_points = !frame.preview_points.empty();
    const bool has_grips = !frame.grip_points.empty();
    const bool has_snap = frame.snap_point.has_value();
    const bool has_debug = frame.debug_line_segments.size() >= 2;
    const bool has_vertex = frame.debug_vertex.has_value();
    if (has_curve || has_controls || has_points || has_grips || has_snap || has_debug ||
        has_vertex) {
      channel.command_list->set_pipeline(*line_pipeline_);
      bind_mesh_sets();
      channel.command_list->set_vertex_buffer(*preview_line_mesh_.vertex_buffer);
      channel.command_list->set_index_buffer(*preview_line_mesh_.index_buffer);

      auto draw_segment = [&](Vec3 start, Vec3 end, float r, float g, float b) {
        const Vec3 d = end - start;
        const float seg_len = length(d);
        if (seg_len < 1e-6f) {
          return;
        }
        const Mat4 model = segment_model(start, end);
        PushConstants pc{};
        pc.mvp = view_proj;
        pc.model = Mat4::identity();
        pc.color[0] = r;
        pc.color[1] = g;
        pc.color[2] = b;
        pc.color[3] = 1.f;
        pc.eye_pos_mode[3] = 3.f;  // mode 3 = 无光照线条
        channel.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
        const GpuInstance inst =
            make_gpu_instance(model, Vec3{1.f, 1.f, 1.f}, 1.f, 0.6f, 0.f, false);
        upload_instances(std::span<const GpuInstance>{&inst, 1});
        DrawIndexedDesc pd{};
        pd.index_count = preview_line_mesh_.index_count;
        channel.command_list->draw_indexed(pd);
      };

      auto draw_polyline = [&](const std::vector<Vec3>& pts, float r, float g, float b) {
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
          draw_segment(pts[i], pts[i + 1], r, g, b);
        }
      };

      auto draw_dashed_polyline = [&](const std::vector<Vec3>& pts, float r, float g, float b) {
        constexpr float kDash = 0.10f;
        constexpr float kGap = 0.06f;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
          const Vec3 a = pts[i];
          const Vec3 bpt = pts[i + 1];
          const Vec3 d = bpt - a;
          const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
          if (len < 1e-6f) {
            continue;
          }
          const Vec3 dir = d * (1.f / len);
          float t = 0.f;
          while (t < len) {
            const float t1 = (std::min)(t + kDash, len);
            draw_segment(a + dir * t, a + dir * t1, r, g, b);
            t = t1 + kGap;
          }
        }
      };

      auto marker_half = [&](Vec3 p) {
        const float dist = std::sqrt((p.x - frame.eye_position.x) * (p.x - frame.eye_position.x) +
                                     (p.y - frame.eye_position.y) * (p.y - frame.eye_position.y) +
                                     (p.z - frame.eye_position.z) * (p.z - frame.eye_position.z));
        return std::clamp(dist * 0.010f, 0.035f, 0.22f);
      };

      auto draw_square = [&](Vec3 p, float half, float r, float g, float b) {
        const Vec3 a{p.x - half, p.y, p.z - half};
        const Vec3 bpt{p.x + half, p.y, p.z - half};
        const Vec3 c{p.x + half, p.y, p.z + half};
        const Vec3 d{p.x - half, p.y, p.z + half};
        draw_segment(a, bpt, r, g, b);
        draw_segment(bpt, c, r, g, b);
        draw_segment(c, d, r, g, b);
        draw_segment(d, a, r, g, b);
      };

      auto draw_diamond = [&](Vec3 p, float half, float r, float g, float b) {
        const Vec3 n{p.x, p.y, p.z - half};
        const Vec3 e{p.x + half, p.y, p.z};
        const Vec3 s{p.x, p.y, p.z + half};
        const Vec3 w{p.x - half, p.y, p.z};
        draw_segment(n, e, r, g, b);
        draw_segment(e, s, r, g, b);
        draw_segment(s, w, r, g, b);
        draw_segment(w, n, r, g, b);
      };

      auto draw_cross = [&](Vec3 p, float half, float r, float g, float b) {
        draw_segment({p.x - half, p.y, p.z}, {p.x + half, p.y, p.z}, r, g, b);
        draw_segment({p.x, p.y, p.z - half}, {p.x, p.y, p.z + half}, r, g, b);
      };

      if (has_controls) {
        draw_dashed_polyline(frame.preview_control_polyline, 1.00f, 0.78f, 0.28f);
      }
      if (has_curve) {
        draw_polyline(frame.preview_polyline, 0.22f, 0.86f, 1.00f);
      }
      for (std::size_t i = 0; i < frame.preview_points.size(); ++i) {
        const Vec3 p = frame.preview_points[i];
        const float half = marker_half(p);
        const bool endpoint = (i == 0 || i + 1 == frame.preview_points.size());
        if (endpoint) {
          draw_square(p, half, 1.00f, 0.88f, 0.30f);
        } else {
          draw_diamond(p, half * 0.85f, 1.00f, 0.72f, 0.22f);
        }
      }
      for (const Vec3& p : frame.grip_points) {
        draw_square(p, marker_half(p) * 0.85f, 0.35f, 0.78f, 1.00f);
      }
      if (has_snap) {
        const Vec3 p = *frame.snap_point;
        draw_cross(p, marker_half(p), 0.35f, 0.95f, 0.55f);
      }
      if (has_debug) {
        for (std::size_t i = 0; i + 1 < frame.debug_line_segments.size(); i += 2) {
          draw_segment(frame.debug_line_segments[i], frame.debug_line_segments[i + 1], 1.00f,
                       0.92f, 0.18f);
        }
      }
      if (has_vertex) {
        const DebugVertexOverlay& v = *frame.debug_vertex;
        const Vec3 p = v.world;
        const float half = marker_half(p) * 1.35f;
        auto basis_from_normal = [](Vec3 n) {
          const Vec3 nn = normalize(n);
          const Vec3 ref = std::fabs(nn.y) < 0.9f ? Vec3{0.f, 1.f, 0.f} : Vec3{1.f, 0.f, 0.f};
          const Vec3 t = normalize(cross(ref, nn));
          const Vec3 b = cross(nn, t);
          return std::pair<Vec3, Vec3>{t, b};
        };
        const auto [tangent, bitangent] = basis_from_normal(v.normal);
        // 位置：洋红菱形 + RGB 短轴，和夹点/包围盒区分开。
        draw_diamond(p, half, 1.00f, 0.22f, 0.85f);
        draw_segment(p, p + Vec3{half, 0.f, 0.f}, 1.00f, 0.25f, 0.25f);
        draw_segment(p, p + Vec3{0.f, half, 0.f}, 0.25f, 1.00f, 0.30f);
        draw_segment(p, p + Vec3{0.f, 0.f, half}, 0.30f, 0.45f, 1.00f);
        // 顶点色：内侧小菱形。
        const float cr = std::clamp(v.color.x, 0.f, 1.f);
        const float cg = std::clamp(v.color.y, 0.f, 1.f);
        const float cb = std::clamp(v.color.z, 0.f, 1.f);
        draw_diamond(p, half * 0.45f, cr, cg, cb);
        // 法线：青色箭头。
        const Vec3 n = normalize(v.normal);
        if (length(n) > 1e-4f) {
          const float nlen = half * 4.2f;
          const Vec3 tip = p + n * nlen;
          draw_segment(p, tip, 0.20f, 0.95f, 1.00f);
          const Vec3 back = tip - n * (nlen * 0.18f);
          draw_segment(tip, back + tangent * (nlen * 0.10f), 0.20f, 0.95f, 1.00f);
          draw_segment(tip, back - tangent * (nlen * 0.10f), 0.20f, 0.95f, 1.00f);
        }
        // UV：切平面上红(U)/绿(V)刻度，长度随 uv 分量变长。
        const float u = std::clamp(v.uv.x, 0.f, 2.f);
        const float vv = std::clamp(v.uv.y, 0.f, 2.f);
        const float u_len = half * (0.55f + 1.15f * u);
        const float v_len = half * (0.55f + 1.15f * vv);
        draw_segment(p, p + tangent * u_len, 1.00f, 0.18f, 0.18f);
        draw_segment(p, p + bitangent * v_len, 0.18f, 1.00f, 0.28f);
        const Vec3 uv_color_pos = p + tangent * (half * 0.35f) + bitangent * (half * 0.35f);
        draw_square(uv_color_pos, half * 0.22f, u * 0.5f, vv * 0.5f, 0.15f);
      }
    }
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
      std::optional<FrameSubmission> frame;
      {
        std::scoped_lock lock(mutex_);
        auto it = channels_.find(id);
        if (it != channels_.end() && it->second.latest) {
          channel = &it->second;
          // 拷一份再画：submit_frame 会在无锁期间替换 latest，直接引用会 vector 越界。
          frame = *it->second.latest;
        }
      }
      if (!channel || !frame) {
        continue;
      }
      if (auto r = draw_channel(id, *channel, *frame); !r) {
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
