#pragma once

#include "engine/graphics/graphics_backend.h"
#include "engine/render/rhi/rhi_blocklist.h"

#include <QStringList>

#include <optional>

namespace tamias {

// 启动相关的命令行开关。和现有用法共存：不带 - 前缀的参数仍然当文件路径。
//   --probe-rhi            只探测、打印报告、退出（IT / 部署脚本的体检入口）
//   --probe-rhi --json     报告用 JSON 输出
//   --gpu-backend=vulkan   指定后端（调试用；策略 > 命令行）
//   --safe-mode            只试最保守的后端，且关掉校验层
struct RhiCliOptions {
  bool probe = false;
  bool json = false;
  bool safe_mode = false;
  std::optional<GraphicsBackend> backend;
  // --render-view=<out.png>：把位置参数给的文档（.tdoc / .obj）离屏渲成一张 PNG 后退出。
  // 这是离屏渲染的第一个真实消费者，也是 IT / 脚本出图的入口。
  std::optional<QString> render_view_path;
  std::uint32_t render_width = 1600;   // --render-size=WxH
  std::uint32_t render_height = 1000;
  // --diagnostics-report=<out.txt>：把图形诊断报告写成文件后退出（IT / 脚本收集现场用）。
  std::optional<QString> diagnostics_report_path;
};

[[nodiscard]] RhiCliOptions parse_rhi_cli(const QStringList& arguments);

// 块名单：assets/rhi_blocklist.json（随包）。读不到 = 空名单，不算错——
// 「没有已知问题」本来就是常态。
[[nodiscard]] RhiBlocklist load_rhi_blocklist(QStringList* warnings);

// 策略：先看 %PROGRAMDATA%/tamias/rhi_policy.json（IT 下发），再看 exe 旁边的
// rhi_policy.json（便携部署 / 调试）。都没有就是空策略。
[[nodiscard]] RhiPolicy load_rhi_policy(QStringList* warnings);

}  // namespace tamias
