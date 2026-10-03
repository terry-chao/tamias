#pragma once

#include "app/ai/ai_client.h"
#include "host/session_mcp_backend.h"

#include <QJsonArray>
#include <QWidget>

#include <vector>

class QLabel;
class QPlainTextEdit;
class QToolButton;

namespace tamias {

// 内置对话面板：进程内的 AI 助手，工具直接调 SessionMcpBackend。
// 不走 MCP 传输（那是给外部客户端用的），所以没有发现文件 / 端口 / token 这些东西。
//
// 和外部客户端的关键区别：**这里不过写策略闸门**。面板是人自己在敲，和命令控制台
// 同一性质；`--mcp-policy` 约束的是外部 AI 客户端，不能反过来限制用户自己。
class AiPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit AiPanel(QWidget* parent = nullptr);

  // 跟当前活动文档走；nullptr = 停在欢迎页。
  void bind(Session* session);
  // 直接把一句话送进去（命令行 --ai-prompt / 以后的脚本自动化用）。
  void submit_text(const QString& text);
  // 等文档绑定好再发——启动时文档是异步打开的，立刻发会撞上 no active document。
  void submit_text_when_ready(const QString& text);

 private:
  void submit();
  void request_next();
  void handle_reply(const ai::ChatReply& reply);
  void run_tool_calls(const std::vector<ai::ToolCall>& calls);
  void append(const QString& who, const QString& text);
  void open_settings();
  void set_busy(bool busy);
  void refresh_status();

  SessionMcpBackend backend_;
  ai::AiClient* client_ = nullptr;
  QPlainTextEdit* transcript_ = nullptr;
  QPlainTextEdit* input_ = nullptr;
  QToolButton* send_button_ = nullptr;
  QToolButton* stop_button_ = nullptr;
  QToolButton* settings_button_ = nullptr;
  QLabel* status_ = nullptr;
  QJsonArray tool_schema_;  // 工具表是静态的，构造时抓一次
  std::vector<ai::ChatMessage> history_;
  int rounds_ = 0;
  bool busy_ = false;
  // 本次会话用的密钥。落盘在 SecretStore（Windows = 凭据管理器），不写进 QSettings。
  QString api_key_;
  QTimer* ready_timer_ = nullptr;
  QString pending_text_;
  int ready_ticks_ = 0;
};

}  // namespace tamias
