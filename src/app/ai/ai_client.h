#pragma once

#include "mcp/json.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace tamias::ai {

// mcp::Json ↔ Qt JSON 的桥。工具 schema 直接来自门面，所以两边都要能转。
[[nodiscard]] QJsonValue to_qt(const mcp::Json& value);
[[nodiscard]] mcp::Json from_qt(const QJsonValue& value);

// OpenAI 的 chat 消息形状。assistant 带工具调用时 tool_calls 非空；
// role == "tool" 时必须给 tool_call_id。
struct ChatMessage {
  QString role;
  QString content;
  QJsonArray tool_calls;
  QString tool_call_id;
};

struct ToolCall {
  QString id;
  QString name;
  mcp::Json arguments;
};

struct ChatReply {
  QString content;
  std::vector<ToolCall> tool_calls;
  QString error;  // 非空 = 这一轮失败
};

// OpenAI 兼容的 /chat/completions 客户端。刻意只做这一种协议：OpenAI、DeepSeek、
// Ollama（/v1）、LM Studio、llama.cpp server 都是这个形状，一个实现全覆盖。
class AiClient : public QObject {
  Q_OBJECT
 public:
  struct Config {
    QString base_url;  // 例如 https://api.openai.com/v1 或 http://127.0.0.1:11434/v1
    QString api_key;   // 本地模型可以为空
    QString model;
  };

  explicit AiClient(QObject* parent = nullptr);

  void set_config(Config config) { config_ = std::move(config); }
  [[nodiscard]] bool configured() const;

  // 发一轮。同一时刻只允许一个在飞；结果通过 finished 回来。
  void send(const std::vector<ChatMessage>& messages, const QJsonArray& tools);
  void abort();
  [[nodiscard]] bool busy() const { return pending_ != nullptr; }

 signals:
  void finished(const ChatReply& reply);

 private:
  void on_reply_finished();
  [[nodiscard]] QByteArray build_body(const std::vector<ChatMessage>& messages,
                                      const QJsonArray& tools) const;

  QNetworkAccessManager* network_ = nullptr;
  QNetworkReply* pending_ = nullptr;
  Config config_;
};

}  // namespace tamias::ai
