#pragma once

#include "app/mcp/mcp_http_server.h"
#include "app/mcp/mcp_policy.h"
#include "host/session_mcp_backend.h"
#include "mcp/mcp_server.h"

#include <QObject>
#include <QString>

#include <functional>

namespace tamias {

// 桌面壳里的 MCP 服务：把「协议门面 + 会话后端 + loopback HTTP + 发现文件」串起来。
//
// 与 PluginHost 一样，它跟着**当前活动视口**重新绑定：切页签、关文档都不需要重启服务。
// 没有活动文档时工具仍可列出，读写会返回「空表 / no active document」。
class McpService : public QObject {
  Q_OBJECT
 public:
  explicit McpService(QString server_version, QObject* parent = nullptr);
  ~McpService() override;

  // 绑定活动会话；session 为空表示停在欢迎页。after_edit 由壳提供（刷新视口）。
  void bind(Session* session, std::function<void()> after_edit);

  // port = 0 时自动选端口。成功后写发现文件（<AppData>/mcp.json）。
  bool start(quint16 port = 0);
  void stop();
  [[nodiscard]] bool running() const { return running_; }
  [[nodiscard]] quint16 port() const { return port_; }
  [[nodiscard]] QString endpoint() const;
  [[nodiscard]] QString token() const { return token_; }
  // 给自动化和「复制配置」用：Claude Desktop / Cursor 的一段 mcpServers 配置。
  [[nodiscard]] QString client_config_json() const;
  // 写操作策略。默认只读——外部客户端连上不会直接改文档。
  void set_policy(McpPolicy policy) { policy_ = policy; }
  [[nodiscard]] McpPolicy policy() const { return policy_; }
  // 打开 tamias_evaluate（全信任 C# 求值）。必须在 start() 之前调用，
  // 否则工具表里不会有它。
  void set_evaluator(std::function<Result<std::string>(std::string_view)> evaluator);

 signals:
  // 启动 / 停止 / 失败的消息，接到命令控制台面板上。
  void message(const QString& text);

 private:
  [[nodiscard]] McpHttpResponse handle_http(const McpHttpRequest& request);
  [[nodiscard]] bool authorized(const McpHttpRequest& request) const;
  // 执行前的闸门：读放行；写按策略拒 / 问 / 放。
  [[nodiscard]] mcp::McpToolGateDecision gate_tool(std::string_view tool,
                                                   const mcp::Json& args);
  [[nodiscard]] mcp::McpToolGateDecision ask_approval(std::string_view tool,
                                                      const mcp::Json& args);
  // 执行后的审计：工具名 + 目标文档 + 参数 + 结果，进命令控制台。
  void audit_tool(std::string_view tool, const mcp::Json& args,
                  const mcp::McpToolResult& result);
  void write_discovery_file();
  void remove_discovery_file();
  [[nodiscard]] static QString discovery_path();

  McpHttpServer http_;
  SessionMcpBackend backend_;
  mcp::McpServer server_;
  QString server_version_;
  QString token_;
  QString document_name_;
  quint16 port_ = 0;
  bool running_ = false;
  // 一个请求还在跑时（典型情况：审批对话框在等用户），再来的请求直接顶回去。
  bool busy_ = false;
  McpPolicy policy_ = McpPolicy::kReadOnly;
};

}  // namespace tamias
