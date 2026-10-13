#pragma once

#include "engine/base/result.h"
#include "host/session.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

// 进程级初始化：注册建模内核 + 命令注册表（幂等，线程安全）。
// 服务端与桌面 / wasm 需要的是同一份注册，否则命令 id 解析不出来。
void ensure_server_initialized();

// 服务端的一个会话。**无 Qt、无 RHI、无窗口**：服务端只拥有「文档 + 命令 + 宽读」，
// 相机、拾取、渲染留在客户端（见 docs/DECISION-SERVER-LANGUAGE.md §3 的分工表）。
//
// 一个实例对应一个打开的文档；句柄的生命周期由调用方（服务壳）管理，不共享线程。
class SessionService {
 public:
  explicit SessionService(std::string document_name);
  ~SessionService();

  SessionService(const SessionService&) = delete;
  SessionService& operator=(const SessionService&) = delete;

  [[nodiscard]] Session& session() { return *session_; }

  // 窄写：命令名 + 与桌面控制台同格式的参数文本。
  Result<void> dispatch(std::string_view command, std::string_view args_text);

  // 内存文档 IO：按扩展名分派 .tdoc / .trscn / .obj。
  Result<void> load_bytes(std::string_view name, std::span<const std::uint8_t> bytes);

  // 宽读：只读快照，不返回内核对象。实体不存在时返回空串 / 空表，不抛异常。
  [[nodiscard]] std::string document_name() const;
  [[nodiscard]] std::vector<std::uint64_t> entity_ids() const;
  [[nodiscard]] std::string entity_name(std::uint64_t id) const;
  [[nodiscard]] std::string entity_kind(std::uint64_t id) const;

 private:
  std::unique_ptr<Session> session_;
};

}  // namespace tamias
