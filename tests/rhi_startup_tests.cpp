#include "engine/render/rhi/rhi_blocklist.h"
#include "engine/render/rhi/rhi_probe.h"
#include "engine/render/rhi/rhi_startup_decision.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tamias {
namespace {

// 假设备：只实现探测用得到的那几个方法，其余（Vulkan/GL 才用的）一律报 "unused"。
// 有了它，「Vulkan 失败 → OpenGL 成功」这种降级路径不用真 GPU 就能测。
class FakeDevice final : public RHIDevice {
 public:
  FakeDevice(GraphicsBackend backend, RhiGpuIdentity identity, bool submit_ok)
      : backend_(backend), identity_(std::move(identity)), submit_ok_(submit_ok) {}

  [[nodiscard]] GraphicsBackend backend() const override { return backend_; }
  [[nodiscard]] Mat4 clip_space_correction_matrix() const override { return Mat4::identity(); }
  [[nodiscard]] RhiGpuIdentity gpu_identity() const override { return identity_; }
  [[nodiscard]] Result<void> submit_noop() override {
    ++submit_calls;
    if (submit_ok_) {
      return {};
    }
    return Err("fake: queue submit failed");
  }
  void wait_idle() override { ++wait_calls; }

  Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc&) override {
    return Err("unused");
  }
  Result<std::unique_ptr<Texture>> create_texture(const TextureDesc&) override {
    return Err("unused");
  }
  Result<std::unique_ptr<ShaderModule>> create_shader_module(const ShaderModuleDesc&) override {
    return Err("unused");
  }
  Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc&) override {
    return Err("unused");
  }
  Result<std::unique_ptr<CommandList>> create_command_list() override { return Err("unused"); }
  Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc&) override {
    return Err("unused");
  }
  Result<std::unique_ptr<Fence>> create_fence() override { return Err("unused"); }
  Result<void> begin_frame(SwapChain&) override { return Err("unused"); }
  Result<void> execute(CommandList&) override { return Err("unused"); }
  Result<void> end_frame(SwapChain&) override { return Err("unused"); }

  int submit_calls = 0;
  int wait_calls = 0;

 private:
  GraphicsBackend backend_;
  RhiGpuIdentity identity_;
  bool submit_ok_;
};

RhiGpuIdentity make_identity(std::uint32_t vendor_id, std::uint32_t device_id,
                             std::string driver_version) {
  RhiGpuIdentity identity{};
  identity.os = rhi_current_os();
  identity.vendor_id = vendor_id;
  identity.device_id = device_id;
  identity.adapter_name = "Test Adapter";
  identity.driver_name = "Test";
  identity.driver_version = std::move(driver_version);
  return identity;
}

// ---------------------------------------------------------------- 版本比较

TEST(RhiVersionCompare, ComparesNumericallyPerSegment) {
  EXPECT_LT(compare_versions("24.9", "24.20"), 0);  // 字符串比较会搞反的那一对
  EXPECT_GT(compare_versions("24.20", "24.9"), 0);
  EXPECT_EQ(compare_versions("24", "24.0"), 0);      // 缺的段当 0
  EXPECT_EQ(compare_versions("24.0.0", "24"), 0);
  EXPECT_LT(compare_versions("1.4.321.0", "1.4.357.0"), 0);
  EXPECT_EQ(compare_versions("566.03", "566.3"), 0);  // 前导零不影响数值
  EXPECT_LT(compare_versions("24.20.0 beta", "24.20.1"), 0);
}

// ---------------------------------------------------------------- 块名单

TEST(RhiBlocklist, MatchesByVendorDeviceAndDriverRange) {
  const std::string text = R"({
  "version": 2,
  "entries": [
    { "id": "nvidia-old", "why": "old driver", "vendor": 4318, "device": 0,
      "driver_min": "500.0.0.0", "driver_max": "520.0.0.0", "skip": "vulkan" }
  ]
})";
  std::vector<std::string> warnings;
  const RhiBlocklist list = parse_rhi_blocklist(text, &warnings);
  ASSERT_EQ(list.entries.size(), 1u) << (warnings.empty() ? "" : warnings.front());
  EXPECT_TRUE(warnings.empty());
  EXPECT_EQ(list.version, 2);

  const RhiGpuIdentity hit = make_identity(0x10DE, 0x1234, "510.0.0.0");
  const RhiGpuIdentity too_new = make_identity(0x10DE, 0x1234, "530.0.0.0");
  const RhiGpuIdentity other_vendor = make_identity(0x8086, 0x1234, "510.0.0.0");
  ASSERT_NE(list.match(hit), nullptr);
  EXPECT_EQ(list.match(hit)->id, "nvidia-old");
  EXPECT_EQ(list.match(too_new), nullptr);
  EXPECT_EQ(list.match(other_vendor), nullptr);
  // 驱动版本报不出来的机器（比如 OpenGL）不该命中点名了版本的条目。
  EXPECT_EQ(list.match(make_identity(0x10DE, 0x1234, "")), nullptr);
}

TEST(RhiBlocklist, MatchesEnvironmentConditionsAndFirstEntryWins) {
  const std::string text = R"({
  "entries": [
    { "id": "rdp-intel", "why": "rdp", "vendor": 32902, "remote": true, "skip": "vulkan" },
    { "id": "intel-any", "why": "all intel", "vendor": 32902, "skip": "vulkan" }
  ]
})";
  const RhiBlocklist list = parse_rhi_blocklist(text);
  ASSERT_EQ(list.entries.size(), 2u);

  RhiGpuIdentity remote = make_identity(0x8086, 0, "1.0.0.0");
  remote.remote_session = true;
  ASSERT_NE(list.match(remote), nullptr);
  EXPECT_EQ(list.match(remote)->id, "rdp-intel");  // 顺序即优先级

  RhiGpuIdentity local = make_identity(0x8086, 0, "1.0.0.0");
  local.remote_session = false;
  ASSERT_NE(list.match(local), nullptr);
  EXPECT_EQ(list.match(local)->id, "intel-any");
}

TEST(RhiBlocklist, MatchesAdapterSubstringAndSoftwareFlag) {
  const std::string text = R"({
  "entries": [
    { "id": "llvmpipe", "why": "software", "adapter_contains": "llvmpipe", "skip": "vulkan" },
    { "id": "software", "why": "software", "software": true, "skip": "vulkan" }
  ]
})";
  const RhiBlocklist list = parse_rhi_blocklist(text);
  ASSERT_EQ(list.entries.size(), 2u);
  RhiGpuIdentity gpu = make_identity(0, 0, "");
  gpu.adapter_name = "Mesa LLVMpipe";
  gpu.software_renderer = true;
  ASSERT_NE(list.match(gpu), nullptr);
  EXPECT_EQ(list.match(gpu)->id, "llvmpipe");
  gpu.adapter_name = "Some Soft Renderer";
  ASSERT_NE(list.match(gpu), nullptr);
  EXPECT_EQ(list.match(gpu)->id, "software");
}

TEST(RhiBlocklist, SkipsBadLinesButKeepsTheRest) {
  const std::string text = R"({
  "entries": [
    { "id": "no-action", "vendor": 4318 },
    { "id": "bad-backend", "skip": "direct3d" },
    { "id": "good", "vendor": 4318, "skip": "vulkan" }
  ]
})";
  std::vector<std::string> warnings;
  const RhiBlocklist list = parse_rhi_blocklist(text, &warnings);
  EXPECT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries.front().id, "good");
  EXPECT_EQ(warnings.size(), 2u);  // 两行坏掉的都要报出来，不能悄悄吞
}

TEST(RhiBlocklist, ParsesForceActionAndPolicy) {
  const RhiBlocklist list = parse_rhi_blocklist(
      R"({ "entries": [ { "id": "x", "vendor": 4318, "force": "opengl" } ] })");
  ASSERT_EQ(list.entries.size(), 1u);
  EXPECT_EQ(list.entries.front().action, RhiBlockActionKind::ForceBackend);
  EXPECT_EQ(list.entries.front().backend, GraphicsBackend::OpenGL);

  const RhiPolicy policy =
      parse_rhi_policy(R"({ "force_backend": "opengl", "lock": true, "override_blocklist": true })");
  ASSERT_TRUE(policy.force_backend.has_value());
  EXPECT_EQ(*policy.force_backend, GraphicsBackend::OpenGL);
  EXPECT_TRUE(policy.lock);
  EXPECT_TRUE(policy.override_blocklist);

  std::vector<std::string> warnings;
  const RhiPolicy bad = parse_rhi_policy(R"({ "force_backend": "direct3d" })", &warnings);
  EXPECT_FALSE(bad.force_backend.has_value());  // 认不出就别猜
  EXPECT_EQ(warnings.size(), 1u);
}

// ---------------------------------------------------------------- 启动决策

TEST(RhiStartupDecision, PriorityChain) {
  RhiStartupInput input{};
  auto decision = decide_rhi_startup(input);
  ASSERT_EQ(decision.candidates.size(), 2u);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::Vulkan);  // 默认顺序
  EXPECT_EQ(decision.candidates[1], GraphicsBackend::OpenGL);

  input.last_good_backend = GraphicsBackend::OpenGL;
  decision = decide_rhi_startup(input);
  ASSERT_EQ(decision.candidates.size(), 2u);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::OpenGL);  // 上次可用优先
  EXPECT_EQ(decision.candidates[1], GraphicsBackend::Vulkan);

  input.last_good_trusted = false;  // 上次没干净退出 → 不信任它
  decision = decide_rhi_startup(input);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::Vulkan);

  input.cli_backend = GraphicsBackend::OpenGL;
  decision = decide_rhi_startup(input);
  ASSERT_EQ(decision.candidates.size(), 1u);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::OpenGL);

  input.policy.force_backend = GraphicsBackend::Vulkan;
  input.policy.lock = true;
  decision = decide_rhi_startup(input);
  ASSERT_EQ(decision.candidates.size(), 1u);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::Vulkan);  // 策略压过命令行
  EXPECT_TRUE(decision.policy_locked);

  input.cli_safe_mode = true;
  decision = decide_rhi_startup(input);
  ASSERT_EQ(decision.candidates.size(), 1u);
  EXPECT_EQ(decision.candidates[0], GraphicsBackend::OpenGL);  // 安全模式最高优先
  EXPECT_TRUE(decision.safe_mode);
}

// ---------------------------------------------------------------- 探测

TEST(RhiProbe, FallsBackWhenFirstBackendFails) {
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    if (info.backend == GraphicsBackend::Vulkan) {
      return Err("vkCreateInstance failed");
    }
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(0x8086, 0x1234, "31.0.0.0"), true));
  };
  RhiProbeOptions options{};
  const RhiProbeReport report = probe_rhi(options, factory);
  ASSERT_TRUE(report.chosen.has_value());
  EXPECT_EQ(*report.chosen, GraphicsBackend::OpenGL);
  ASSERT_EQ(report.attempts.size(), 2u);
  EXPECT_EQ(report.attempts[0].backend, GraphicsBackend::Vulkan);
  EXPECT_FALSE(report.attempts[0].ok);
  EXPECT_EQ(report.attempts[0].reason, "vkCreateInstance failed");
  EXPECT_TRUE(report.attempts[1].ok);
  EXPECT_NE(report.summary.find("OpenGL"), std::string::npos);
  EXPECT_NE(report.summary.find("Vulkan"), std::string::npos);  // 失败原因也要在里面
}

TEST(RhiProbe, ReportsNoBackendWhenAllFail) {
  const RhiDeviceFactory factory = [](const DeviceCreateInfo&) -> Result<std::unique_ptr<RHIDevice>> {
    return Err("no runtime");
  };
  const RhiProbeReport report = probe_rhi(RhiProbeOptions{}, factory);
  EXPECT_FALSE(report.chosen.has_value());
  EXPECT_EQ(report.attempts.size(), 2u);
  EXPECT_NE(report.summary.find("no usable"), std::string::npos);
}

TEST(RhiProbe, HonorsSubmitDepth) {
  int submits = 0;
  const RhiDeviceFactory factory = [&submits](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    auto device = std::make_unique<FakeDevice>(
        info.backend, make_identity(0x10DE, 0x1234, "566.0.3.0"),
        /*submit_ok=*/info.backend != GraphicsBackend::Vulkan);
    return std::unique_ptr<RHIDevice>(device.release());
  };
  (void)submits;

  RhiProbeOptions device_depth{};
  device_depth.depth = RhiProbeDepth::Device;
  device_depth.candidates = {GraphicsBackend::Vulkan};
  const RhiProbeReport shallow = probe_rhi(device_depth, factory);
  EXPECT_TRUE(shallow.chosen.has_value());  // 不提交就看不出队列坏没坏

  RhiProbeOptions submit_depth{};
  submit_depth.depth = RhiProbeDepth::Submit;
  submit_depth.candidates = {GraphicsBackend::Vulkan, GraphicsBackend::OpenGL};
  const RhiProbeReport deep = probe_rhi(submit_depth, factory);
  ASSERT_TRUE(deep.chosen.has_value());
  EXPECT_EQ(*deep.chosen, GraphicsBackend::OpenGL);  // 提交失败 → 降级
  ASSERT_EQ(deep.attempts.size(), 2u);
  EXPECT_EQ(deep.attempts[0].reason, "fake: queue submit failed");
}

TEST(RhiProbe, BlocklistSkipsBlockedBackend) {
  const RhiBlocklist blocklist = parse_rhi_blocklist(
      R"({ "entries": [ { "id": "nvidia-no-vk", "vendor": 4318, "skip": "vulkan" } ] })");
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    const std::uint32_t vendor = info.backend == GraphicsBackend::Vulkan ? 0x10DE : 0x8086;
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(vendor, 0, "1.0.0.0"), true));
  };
  RhiProbeOptions options{};
  options.blocklist = &blocklist;
  const RhiProbeReport report = probe_rhi(options, factory);
  ASSERT_TRUE(report.chosen.has_value());
  EXPECT_EQ(*report.chosen, GraphicsBackend::OpenGL);
  ASSERT_EQ(report.attempts.size(), 2u);
  EXPECT_EQ(report.attempts[0].matched_entry, "nvidia-no-vk");
  EXPECT_FALSE(report.attempts[0].ok);
}

TEST(RhiProbe, BlocklistForceRedirectsToAnotherBackend) {
  const RhiBlocklist blocklist = parse_rhi_blocklist(
      R"({ "entries": [ { "id": "nvidia-use-gl", "vendor": 4318, "force": "opengl" } ] })");
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    const std::uint32_t vendor = info.backend == GraphicsBackend::Vulkan ? 0x10DE : 0x8086;
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(vendor, 0, "1.0.0.0"), true));
  };
  RhiProbeOptions options{};
  options.blocklist = &blocklist;
  const RhiProbeReport report = probe_rhi(options, factory);
  ASSERT_TRUE(report.chosen.has_value());
  EXPECT_EQ(*report.chosen, GraphicsBackend::OpenGL);
  EXPECT_EQ(report.attempts[0].matched_entry, "nvidia-use-gl");
}

TEST(RhiProbe, PolicyOverrideIgnoresBlocklist) {
  const RhiBlocklist blocklist = parse_rhi_blocklist(
      R"({ "entries": [ { "id": "nvidia-no-vk", "vendor": 4318, "skip": "vulkan" } ] })");
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(0x10DE, 0, "1.0.0.0"), true));
  };
  RhiProbeOptions options{};
  options.blocklist = &blocklist;
  options.blocklist_override = true;  // IT 明确要求 → 听它的
  const RhiProbeReport report = probe_rhi(options, factory);
  ASSERT_TRUE(report.chosen.has_value());
  EXPECT_EQ(*report.chosen, GraphicsBackend::Vulkan);
}

// C 档（离屏画一个像素）在后端不支持离屏时，要如实报原因并继续降级，而不是崩。
TEST(RhiProbe, PixelDepthReportsMissingOffscreenSupport) {
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(0x10DE, 0x1234, "566.0.3.0"), true));
  };
  RhiProbeOptions options{};
  options.depth = RhiProbeDepth::Pixel;
  options.candidates = {GraphicsBackend::Vulkan, GraphicsBackend::OpenGL};
  const RhiProbeReport report = probe_rhi(options, factory);
  EXPECT_FALSE(report.chosen.has_value());  // 假设备不支持离屏 → 两条都失败
  ASSERT_EQ(report.attempts.size(), 2u);
  EXPECT_NE(report.attempts[0].reason.find("offscreen target"), std::string::npos);
  EXPECT_NE(report.summary.find("no usable"), std::string::npos);
}

TEST(RhiProbe, JsonReportMentionsChosenBackendAndAdapter) {
  const RhiDeviceFactory factory = [](const DeviceCreateInfo& info)
      -> Result<std::unique_ptr<RHIDevice>> {
    return std::unique_ptr<RHIDevice>(
        new FakeDevice(info.backend, make_identity(0x10DE, 0x2684, "566.0.3.0"), true));
  };
  RhiProbeOptions options{};
  options.candidates = {GraphicsBackend::Vulkan};
  const std::string json = probe_rhi(options, factory).to_json();
  EXPECT_NE(json.find("\"chosen\": \"Vulkan\""), std::string::npos);
  EXPECT_NE(json.find("Test Adapter"), std::string::npos);
  EXPECT_NE(json.find("566.0.3.0"), std::string::npos);
}

// ---------------------------------------------------------------- 指纹

TEST(RhiGpuIdentity, FingerprintChangesWithDriverAndDevice) {
  const RhiGpuIdentity base = make_identity(0x10DE, 0x2684, "566.0.3.0");
  const RhiGpuIdentity same = make_identity(0x10DE, 0x2684, "566.0.3.0");
  const RhiGpuIdentity new_driver = make_identity(0x10DE, 0x2684, "570.0.0.0");
  const RhiGpuIdentity new_device = make_identity(0x10DE, 0x9999, "566.0.3.0");
  EXPECT_EQ(rhi_gpu_fingerprint(base), rhi_gpu_fingerprint(same));
  EXPECT_NE(rhi_gpu_fingerprint(base), rhi_gpu_fingerprint(new_driver));
  EXPECT_NE(rhi_gpu_fingerprint(base), rhi_gpu_fingerprint(new_device));
}

}  // namespace
}  // namespace tamias
