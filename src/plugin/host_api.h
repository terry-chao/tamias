#pragma once

#include <cstdint>

namespace tamias {

inline constexpr int kHostApiVersion = 8;

// C ABI for C# / native plugins. Layout must match plugin-sdk/csharp/Tamias.Api/HostApi.cs.
// Append fields only; bump kHostApiVersion when the layout changes.
struct HostApi {
  std::int32_t abi_version = kHostApiVersion;
  void* context = nullptr;

  void (*log)(void* context, std::int32_t level, const char* utf8) = nullptr;
  std::int32_t (*document_name)(void* context, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*entity_count)(void* context) = nullptr;
  std::int32_t (*entity_id_at)(void* context, std::int32_t index, std::uint64_t* out_id) = nullptr;
  std::int32_t (*entity_kind)(void* context, std::uint64_t id, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*entity_name)(void* context, std::uint64_t id, char* utf8, std::int32_t cap) = nullptr;
  std::int32_t (*selection_count)(void* context) = nullptr;
  std::int32_t (*selection_id_at)(void* context, std::int32_t index, std::uint64_t* out_id) = nullptr;
  std::int32_t (*dispatch)(void* context, const char* command, const char* args_utf8) = nullptr;
  std::int32_t (*register_command)(void* context, const char* id, const char* title,
                                   const char* tooltip, const char* page_id,
                                   const char* group_id, const char* icon_path,
                                   std::int32_t order, std::int32_t flags) = nullptr;
  std::int32_t (*register_plugin)(void* context, const char* id, const char* title,
                                  const char* author, const char* version,
                                  const char* release_date, const char* description,
                                  const char* homepage_url, const char* icon_path,
                                  std::int32_t flags) = nullptr;
  std::int32_t (*begin_point_input)(void* context, std::uint64_t request_id,
                                    std::int32_t min_points, std::int32_t max_points,
                                    std::int32_t flags, float work_plane_y,
                                    std::int32_t preview_kind,
                                    const char* preview_curve_kind,
                                    const char* filter_kind) = nullptr;
  std::int32_t (*cancel_point_input)(void* context, std::uint64_t request_id) = nullptr;
  std::int32_t (*set_selection)(void* context, const std::uint64_t* ids,
                                std::int32_t count) = nullptr;
  std::int32_t (*show_dialog)(void* context, std::int32_t kind, std::int32_t buttons,
                              const char* spec_utf8, char* out_utf8,
                              std::int32_t cap) = nullptr;

  // ── v6：宽读 = 特征树 + 参数 ──────────────────────────────────────────────
  // 「窄写、宽读」：写仍然只有 dispatch，但读要给全，否则脚本只能猜 feature_id。
  // 这里全是只读快照，不暴露 FeatureModel / TopoDS_Shape / 场景图指针。
  //
  // 特征按下标取（顺序 = 特征树的拓扑序，依赖在前）。
  // 参数按**名字**升序取（模型里是 unordered_map，排序后插件按下标遍历才是确定的）。
  std::int32_t (*entity_feature_count)(void* context, std::uint64_t entity_id) = nullptr;
  std::int32_t (*entity_feature_at)(void* context, std::uint64_t entity_id, std::int32_t index,
                                    std::uint64_t* out_id, std::int32_t* out_kind,
                                    std::int32_t* out_input_count,
                                    std::int32_t* out_param_count) = nullptr;
  // 第 index 个上游特征 id（依赖边）。失败返回 -1。
  std::int32_t (*feature_input_at)(void* context, std::uint64_t entity_id,
                                   std::uint64_t feature_id, std::int32_t index,
                                   std::uint64_t* out_input_id) = nullptr;
  std::int32_t (*feature_param_count)(void* context, std::uint64_t entity_id,
                                      std::uint64_t feature_id) = nullptr;
  // name_utf8 可以为空（只取值）；out_value 可以为空（只取名字）。
  std::int32_t (*feature_param_at)(void* context, std::uint64_t entity_id,
                                   std::uint64_t feature_id, std::int32_t index,
                                   char* name_utf8, std::int32_t cap,
                                   double* out_value) = nullptr;

  // ── v7：事务 ──────────────────────────────────────────────────────────────
  // 脚本一次改 N 个参数，不该在用户面前留下 N 步撤销。
  // begin 与 commit 之间成功执行的命令合成**一条**撤销记录；abort 逆序撤销这一段，
  // 文档回到 begin 时的样子，栈里不留记录。
  //
  // 不能嵌套；事务里不能武装交互式命令（点齐的时刻由鼠标决定，不在事务窗口里）。
  // 忘了 commit 不会把后面的编辑吞掉：插件命令返回时宿主会回滚并记一条日志。
  std::int32_t (*begin_transaction)(void* context, const char* name_utf8) = nullptr;
  std::int32_t (*commit_transaction)(void* context) = nullptr;
  // 返回回滚的命令条数（0 = 空事务）；失败 -1。
  std::int32_t (*abort_transaction)(void* context) = nullptr;

  // ── v8：扩展重载 ──────────────────────────────────────────────────────────
  // 把一个扩展连同它登记的命令一起摘掉（命令列表变短了，壳会重建 Ribbon）。
  // 重载就是「先摘旧的、再装新的」——不摘的话命令 id 会撞。
  std::int32_t (*unregister_plugin)(void* context, const char* plugin_id) = nullptr;
};

}  // namespace tamias
