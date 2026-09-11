#pragma once

#include "engine/render/render_types.h"
#include "engine/render/rhi/device.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace tamias {

// CPU 侧 CommandList：记下 pipeline / draw_indexed，供场景调试器重跑 RecordCommands。
class RecordingCommandList final : public CommandList {
 public:
  struct Draw {
    DrawIndexedDesc desc{};
    PipelineState* pipeline = nullptr;
  };

  void begin() override {}
  void end() override {}
  void begin_render_pass(SwapChain&, const float[4], float) override {}
  void end_render_pass() override {}
  void set_pipeline(PipelineState& pipeline) override { current_pipeline_ = &pipeline; }
  void set_vertex_buffer(Buffer&, std::uint64_t) override { ++vertex_binds; }
  void set_instance_buffer(Buffer&, std::uint64_t) override { ++instance_binds; }
  void set_index_buffer(Buffer&, std::uint64_t) override { ++index_binds; }
  void set_push_constants(std::span<const std::byte> data) override {
    PushConstants pc{};
    const std::size_t n = (std::min)(data.size(), sizeof(pc));
    std::memcpy(&pc, data.data(), n);
    push_constants.push_back(pc);
  }
  void set_texture(Texture&, std::uint32_t) override { ++texture_binds; }
  void draw_indexed(const DrawIndexedDesc& desc) override {
    draws.push_back(Draw{desc, current_pipeline_});
  }
  void set_viewport(float, float, float, float, float, float) override {}
  void set_scissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

  std::vector<Draw> draws;
  std::vector<PushConstants> push_constants;
  int texture_binds = 0;
  int vertex_binds = 0;
  int instance_binds = 0;
  int index_binds = 0;

 private:
  PipelineState* current_pipeline_ = nullptr;
};

}  // namespace tamias
