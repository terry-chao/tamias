#include "app/ai/ai_client.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace tamias::ai {
namespace {

QString clipped(const QString& text, int limit) {
  return text.size() <= limit ? text : text.left(limit) + QStringLiteral("…");
}

// base_url 允许带或不带结尾斜杠，也允许用户直接把完整路径填进来。
QUrl completion_url(const QString& base_url) {
  QString text = base_url.trimmed();
  while (text.endsWith(QLatin1Char('/'))) {
    text.chop(1);
  }
  if (!text.endsWith(QStringLiteral("/chat/completions"))) {
    text += QStringLiteral("/chat/completions");
  }
  return QUrl(text);
}

}  // namespace

QJsonValue to_qt(const mcp::Json& value) {
  switch (value.type()) {
    case mcp::Json::Type::kNull:
      return QJsonValue(QJsonValue::Null);
    case mcp::Json::Type::kBool:
      return QJsonValue(value.as_bool());
    case mcp::Json::Type::kInt:
      return QJsonValue(static_cast<qint64>(value.as_int()));
    case mcp::Json::Type::kDouble:
      return QJsonValue(value.as_double());
    case mcp::Json::Type::kString:
      return QJsonValue(QString::fromStdString(value.as_string()));
    case mcp::Json::Type::kArray: {
      QJsonArray array;
      for (const mcp::Json& item : value.items()) {
        array.append(to_qt(item));
      }
      return array;
    }
    case mcp::Json::Type::kObject: {
      QJsonObject object;
      for (const auto& [key, member] : value.members()) {
        object.insert(QString::fromStdString(key), to_qt(member));
      }
      return object;
    }
  }
  return QJsonValue(QJsonValue::Null);
}

mcp::Json from_qt(const QJsonValue& value) {
  if (value.isObject()) {
    mcp::Json object = mcp::Json::object();
    const QJsonObject source = value.toObject();
    for (auto it = source.begin(); it != source.end(); ++it) {
      object.set(it.key().toStdString(), from_qt(it.value()));
    }
    return object;
  }
  if (value.isArray()) {
    mcp::Json array = mcp::Json::array();
    const QJsonArray source = value.toArray();
    for (const QJsonValue& item : source) {
      array.push_back(from_qt(item));
    }
    return array;
  }
  // 注意先判 double 之前要判整数意图：JSON 里 1 和 1.0 都可能是 double。
  if (value.isDouble()) {
    const double number = value.toDouble();
    if (number == static_cast<double>(static_cast<qint64>(number))) {
      return mcp::Json::integer(static_cast<std::int64_t>(number));
    }
    return mcp::Json::number(number);
  }
  if (value.isBool()) {
    return mcp::Json::boolean(value.toBool());
  }
  if (value.isString()) {
    return mcp::Json::string(value.toString().toStdString());
  }
  return mcp::Json::null();
}

AiClient::AiClient(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {}

bool AiClient::configured() const {
  return !config_.base_url.trimmed().isEmpty() && !config_.model.trimmed().isEmpty();
}

void AiClient::abort() {
  if (pending_ != nullptr) {
    pending_->abort();  // 会触发 finished，由它统一收尾
  }
}

QByteArray AiClient::build_body(const std::vector<ChatMessage>& messages,
                                const QJsonArray& tools) const {
  QJsonArray wire_messages;
  for (const ChatMessage& message : messages) {
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    object.insert(QStringLiteral("content"), message.content);
    if (!message.tool_calls.isEmpty()) {
      object.insert(QStringLiteral("tool_calls"), message.tool_calls);
    }
    if (!message.tool_call_id.isEmpty()) {
      object.insert(QStringLiteral("tool_call_id"), message.tool_call_id);
    }
    wire_messages.append(object);
  }

  QJsonObject body;
  body.insert(QStringLiteral("model"), config_.model.trimmed());
  body.insert(QStringLiteral("messages"), wire_messages);
  if (!tools.isEmpty()) {
    body.insert(QStringLiteral("tools"), tools);
    body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
  }
  return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void AiClient::send(const std::vector<ChatMessage>& messages, const QJsonArray& tools) {
  if (pending_ != nullptr) {
    return;  // 一次一轮；调用方自己不并发
  }
  ChatReply immediate;
  if (!configured()) {
    immediate.error =
        tr("No model configured yet: open Settings in the AI panel and fill in the "
           "service address and model name.");
  } else {
    QNetworkRequest request(completion_url(config_.base_url));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!config_.api_key.trimmed().isEmpty()) {
      request.setRawHeader("Authorization", "Bearer " + config_.api_key.trimmed().toUtf8());
    }
    pending_ = network_->post(request, build_body(messages, tools));
    connect(pending_, &QNetworkReply::finished, this, &AiClient::on_reply_finished);
    return;
  }
  // 用队列回调，保证「send 之后才有可能收到 finished」这个时序，调用方不会重入。
  QTimer::singleShot(0, this, [this, immediate] { emit finished(immediate); });
}

void AiClient::on_reply_finished() {
  QNetworkReply* reply = pending_;
  if (reply == nullptr) {
    return;
  }
  pending_ = nullptr;
  reply->deleteLater();

  ChatReply result;
  const QByteArray body = reply->readAll();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (status == 0) {
    result.error = tr("Cannot reach %1: %2").arg(config_.base_url, reply->errorString());
    emit finished(result);
    return;
  }
  if (status < 200 || status >= 300) {
    result.error = tr("Service returned %1: %2")
                       .arg(status)
                       .arg(clipped(QString::fromUtf8(body), 400));
    emit finished(result);
    return;
  }

  QJsonParseError parse_error{};
  const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    result.error =
        tr("Response is not valid JSON: %1").arg(clipped(QString::fromUtf8(body), 200));
    emit finished(result);
    return;
  }

  const QJsonObject root = document.object();
  if (root.contains(QStringLiteral("error"))) {
    result.error = tr("Service error: %1").arg(clipped(
        QString::fromUtf8(QJsonDocument(root.value(QStringLiteral("error")).toObject())
                              .toJson(QJsonDocument::Compact)),
        300));
    emit finished(result);
    return;
  }

  const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
  if (choices.isEmpty()) {
    result.error = tr("Response contains no choices.");
    emit finished(result);
    return;
  }

  const QJsonObject message =
      choices.first().toObject().value(QStringLiteral("message")).toObject();
  result.content = message.value(QStringLiteral("content")).toString();
  for (const QJsonValue& entry : message.value(QStringLiteral("tool_calls")).toArray()) {
    const QJsonObject call = entry.toObject();
    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    ToolCall tool_call;
    tool_call.id = call.value(QStringLiteral("id")).toString();
    tool_call.name = function.value(QStringLiteral("name")).toString();
    // arguments 是**字符串包着的 JSON**，这是 OpenAI 的约定。
    const QString arguments = function.value(QStringLiteral("arguments")).toString();
    if (const auto parsed = mcp::Json::parse(arguments.toStdString()); parsed) {
      tool_call.arguments = *parsed;
    } else {
      tool_call.arguments = mcp::Json::object();
    }
    result.tool_calls.push_back(std::move(tool_call));
  }

  if (result.content.isEmpty() && result.tool_calls.empty()) {
    result.error = tr("The model returned no content.");
  }
  emit finished(result);
}

}  // namespace tamias::ai
