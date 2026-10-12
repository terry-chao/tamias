#pragma once

// Meshy 的异步任务协议：**纯数据，没有 Qt、没有网络**。
// 这样它能在 tamias_tests 里被直接测（客户端那层只负责把这里的 JSON 发出去、
// 把回包喂回来），也方便以后换传输（sidecar / MCP 工具）时复用同一份解析。
//
// ⚠️ **字段名与端点以 https://docs.meshy.ai 为准。** 本文件按 v2 text-to-3d 的公开形状写，
// 服务地址来自设置项、不写死，所以换版本时只需要动这一个文件。
// 两个刻意的容错（都属于"宁可多问一轮，也不要误判"）：
//   - 不认识的状态一律当 Pending，绝不当成"完成"；
//   - 字段缺失 / 类型不符按缺省处理，只有建任务拿不到 id、查询拿不到 status 才算失败。

#include "engine/base/result.h"
#include "mcp/json.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace tamias::ai {

// 默认服务地址。设置里可以改成网关 / 代理（见 AppSettings::meshy_base_url）。
inline constexpr std::string_view kDefaultMeshyBaseUrl = "https://api.meshy.ai/openapi/v2";

// 任务状态。Meshy 用大写字符串，未知值当 Pending。
enum class MeshyTaskStatus { kPending, kInProgress, kSucceeded, kFailed, kCanceled };

[[nodiscard]] const char* meshy_status_name(MeshyTaskStatus status);
[[nodiscard]] MeshyTaskStatus meshy_status_from_string(std::string_view text);
// 轮询可以停了：成功 / 失败 / 取消都是终态。
[[nodiscard]] bool meshy_status_terminal(MeshyTaskStatus status);

// 生成的各通道下载地址。协议层不替调用方挑格式（Tamias 只吃 glb / obj），
// 但给一个兜底顺序，免得每个调用点都自己写一遍。
struct MeshyModelUrls {
  std::string glb;
  std::string obj;
  std::string fbx;
  std::string usdz;

  [[nodiscard]] bool empty() const;
  // 按 glb → obj → fbx → usdz 挑第一个非空的；全空返回空串。
  [[nodiscard]] std::string best_importable() const;
};

// 贴图通道。服务端可能给单对象，也可能给数组（一个任务一组贴图），解析时统一。
struct MeshyTextureUrls {
  std::string base_color;
  std::string metallic;
  std::string roughness;
  std::string normal;
};

struct MeshyTask {
  std::string id;
  MeshyTaskStatus status = MeshyTaskStatus::kPending;
  int progress = 0;  // 0–100
  MeshyModelUrls model_urls;
  MeshyTextureUrls texture_urls;
  std::string thumbnail_url;
  std::string error;              // 服务端给的失败原因（FAILED 时才有）
  int credits_consumed = -1;      // -1 = 回包里没说
};

// ---- 请求 ----

struct MeshyTextTo3dRequest {
  std::string prompt;
  // "preview" = 粗模，便宜快；"refine" = 精修，必须带 preview_task_id。
  // 默认 preview：先让人看到东西，再决定要不要花钱细化。
  std::string mode = "preview";
  std::string preview_task_id;
  std::string art_style = "realistic";
  std::string ai_model;  // 空 = 用服务端默认
  bool should_remesh = true;
  std::string topology = "triangle";
  int target_polycount = 30000;
  std::string texture_prompt;  // 空 = 不额外控制贴图
};

// 只放该 mode 用得到的字段：preview 带 preview_task_id、refine 不带 should_remesh 之外的
// 冲突项，免得服务端因为"这个模式下不认识的参数"整单拒绝。
[[nodiscard]] mcp::Json meshy_text_to_3d_body(const MeshyTextTo3dRequest& request);

// ---- 响应 ----

// 建任务的回包是 {"result": "<task id>"}。
[[nodiscard]] Result<std::string> meshy_parse_created_task_id(const mcp::Json& root);
// 查询任务的回包就是任务本体；没有 status 一律算失败（"不知道状态"不能当"在跑"）。
[[nodiscard]] Result<MeshyTask> meshy_parse_task(const mcp::Json& root);
// 额度回包 {"balance": 123}。
[[nodiscard]] Result<int> meshy_parse_balance(const mcp::Json& root);

// ---- 端点 ----

// base 与 path 之间只补一个斜杠，不关心 base 有没有结尾斜杠。
[[nodiscard]] std::string meshy_join(std::string_view base, std::string_view path);
// 建任务 / 查任务都在 base 下（默认 base 已含 /openapi/v2）。
[[nodiscard]] std::string meshy_text_to_3d_url(std::string_view base);
[[nodiscard]] std::string meshy_task_url(std::string_view base, std::string_view task_id);
// 额度在 openapi/v1 下（官方把两个版本分开）。所以从 base 里剥掉 /openapi/<ver> 再拼；
// base 里没有 /openapi 时退回"base + /balance"。
[[nodiscard]] std::string meshy_balance_url(std::string_view base);

}  // namespace tamias::ai
