#include "app/mcp/mcp_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QWidget>

#include <filesystem>
#include <utility>

namespace tamias {
namespace {

[[nodiscard]] McpHttpResponse json_response(int status, QByteArray body) {
  McpHttpResponse response;
  response.status = status;
  response.content_type = QByteArrayLiteral("application/json");
  response.body = std::move(body);
  return response;
}

[[nodiscard]] McpHttpResponse error_response(int status, QByteArray message) {
  mcp::Json error = mcp::Json::object({{"error", mcp::Json::string(message.toStdString())}});
  return json_response(status, QByteArray::fromStdString(error.dump()));
}

// 128 位随机 token。它不是密码学边界（网线到不了这里），但足以挡住本机其它进程
// 误连上就改文档。
[[nodiscard]] QString generate_token() {
  QByteArray bytes(16, 0);
  for (int i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
  }
  return QString::fromLatin1(bytes.toHex());
}

// 解析不了就不带 id —— 但至少让客户端看到一条 JSON-RPC 错误，而不是网络错误。
[[nodiscard]] QByteArray jsonrpc_error(const QByteArray& request, int code,
                                       const QString& message) {
  mcp::Json id = mcp::Json::null();
  const std::string_view body(request.constData(), static_cast<std::size_t>(request.size()));
  if (const auto parsed = mcp::Json::parse(body); parsed && parsed->is_object()) {
    if (const mcp::Json* raw = parsed->find("id"); raw != nullptr) {
      id = *raw;
    }
  }
  mcp::Json error = mcp::Json::object({
      {"jsonrpc", mcp::Json::string("2.0")},
      {"id", std::move(id)},
      {"error", mcp::Json::object({
          {"code", mcp::Json::integer(code)},
          {"message", mcp::Json::string(message.toStdString())},
      })},
  });
  return QByteArray::fromStdString(error.dump());
}

[[nodiscard]] QString clipped(const QString& text, int limit) {
  return text.size() <= limit ? text : text.left(limit) + QStringLiteral("…");
}

// 检索资源是跟着可执行文件走的（POST_BUILD 会把 resources/rag 拷到 exe 旁）。
std::filesystem::path application_directory() {
  const QString directory = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
  return std::filesystem::path(directory.toStdWString());
#else
  return std::filesystem::path(directory.toStdString());
#endif
}

}  // namespace

McpService::McpService(QString server_version, QObject* parent)
    : QObject(parent),
      backend_(nullptr),
      server_(composite_, "tamias", server_version.toStdString()),
      server_version_(std::move(server_version)) {
  compose_backends();
  server_.set_tool_gate(
      [this](std::string_view tool, const mcp::Json& args) { return gate_tool(tool, args); });
  server_.set_tool_observer([this](std::string_view tool, const mcp::Json& args,
                                   const mcp::McpToolResult& result) {
    audit_tool(tool, args, result);
  });
}

McpService::~McpService() { stop(); }

void McpService::compose_backends() {
  std::string error;
  if (!composite_.add(backend_, &error)) {
    // 空组合加第一个后端不可能重名；真发生了说明工具表里有重复项，是 bug。
    index_status_ = tr("MCP: 装配失败 —— %1").arg(QString::fromStdString(error));
    return;
  }

  std::string alias_warning;
  rag_index_ = rag::load_from_application(application_directory(), &error, &alias_warning);
  if (!rag_index_) {
    index_status_ = tr("MCP: 没有检索索引，tamias_search_docs 不可用 —— %1")
                        .arg(QString::fromStdString(error));
    return;
  }

  docs_backend_.emplace(*rag_index_, rag::build_git_rev());
  std::string clash;
  if (!composite_.add(*docs_backend_, &clash)) {
    docs_backend_.reset();
    index_status_ = tr("MCP: 检索后端与文档后端重名，已跳过 —— %1")
                        .arg(QString::fromStdString(clash));
    return;
  }

  index_status_ = tr("MCP: 检索索引已载入 %1 块（rev %2）")
                      .arg(rag_index_->chunks().size())
                      .arg(QString::fromStdString(rag_index_->git_rev()));
  if (!alias_warning.empty()) {
    index_status_ += tr("；别名表没读到：%1").arg(QString::fromStdString(alias_warning));
  }
}

void McpService::bind(Session* session, std::function<void()> after_edit) {
  backend_.set_session(session);
  backend_.set_after_edit(std::move(after_edit));
  document_name_ =
      session == nullptr ? QString() : QString::fromStdString(session->document().name());
  if (running_) {
    write_discovery_file();  // 发现文件里的 document 字段跟着活动文档走
  }
}

void McpService::set_evaluator(
    std::function<Result<std::string>(std::string_view)> evaluator) {
  backend_.set_evaluate(std::move(evaluator));
}

bool McpService::start(quint16 port) {
  if (running_) {
    return true;
  }
  token_ = generate_token();
  http_.set_handler([this](const McpHttpRequest& request) { return handle_http(request); });
  if (!http_.listen(port)) {
    token_.clear();
    emit message(tr("MCP: 无法在 127.0.0.1:%1 上监听").arg(port));
    return false;
  }
  port_ = http_.port();
  running_ = true;
  if (!index_status_.isEmpty()) {
    emit message(index_status_);  // 构造期还没有连接，索引状态攒到这里报
  }
  write_discovery_file();
  emit message(tr("MCP: 已监听 %1（写策略 %2）")
                   .arg(endpoint(), QString::fromLatin1(mcp_policy_name(policy_))));
  // 形态 B 的客户端配置；形态 A（直连 HTTP）的客户端仍可直接用 endpoint + token。
  emit message(tr("MCP: 客户端配置 %1").arg(client_config_json()));
  return true;
}

void McpService::stop() {
  if (!running_) {
    return;
  }
  http_.close();
  running_ = false;
  port_ = 0;
  token_.clear();
  remove_discovery_file();
  emit message(tr("MCP: 已停止"));
}

QString McpService::endpoint() const {
  return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port_);
}

QString McpService::client_config_json() const {
  // 形态 B：客户端拉起 tamias-mcp，由它连回进程内的桥。所以配置是 stdio 形状的
  // command/args，而不是直连的 url/headers。
#if defined(Q_OS_WIN)
  const QString command =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tamias-mcp.exe"));
#else
  const QString command =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tamias-mcp"));
#endif
  mcp::Json config = mcp::Json::object({
      {"mcpServers",
       mcp::Json::object(
           {{"tamias",
             mcp::Json::object({
                 {"command", mcp::Json::string(command.toStdString())},
                 {"args", mcp::Json::array()},
             })}})},
  });
  return QString::fromStdString(config.dump());
}

McpHttpResponse McpService::handle_http(const McpHttpRequest& request) {
  QByteArray target = request.target;
  if (const qsizetype query = target.indexOf('?'); query >= 0) {
    target = target.left(query);
  }

  // 只有浏览器会发 Origin；本机客户端（Claude Desktop / Cursor）不带。这道检查是
  // DNS rebinding 的防线：网页里的脚本即使能连到 127.0.0.1，Origin 也对不上。
  const QByteArray origin = request.headers.value("origin").toLower();
  if (!origin.isEmpty() && origin != QByteArrayLiteral("null") &&
      !origin.startsWith(QByteArrayLiteral("http://127.0.0.1")) &&
      !origin.startsWith(QByteArrayLiteral("http://localhost"))) {
    return error_response(403, QByteArrayLiteral("origin not allowed"));
  }

  if (!authorized(request)) {
    McpHttpResponse response = error_response(401, QByteArrayLiteral("missing or invalid token"));
    response.extra_headers.push_back(QByteArrayLiteral("WWW-Authenticate: Bearer"));
    return response;
  }

  if (request.method == "GET" && target == "/health") {
    mcp::Json health = mcp::Json::object({
        {"ok", mcp::Json::boolean(true)},
        {"document", mcp::Json::string(document_name_.toStdString())},
    });
    return json_response(200, QByteArray::fromStdString(health.dump()));
  }

  if (target != QByteArrayLiteral("/mcp")) {
    return error_response(404, QByteArrayLiteral("not found"));
  }
  if (request.method != "POST") {
    // 本服务不主动推送消息，所以没有 SSE 流可开；POST 足够表达请求/响应。
    McpHttpResponse response = error_response(405, QByteArrayLiteral("use POST"));
    response.extra_headers.push_back(QByteArrayLiteral("Allow: POST"));
    return response;
  }

  const std::string_view body(request.body.constData(),
                              static_cast<std::size_t>(request.body.size()));

  if (busy_) {
    // 已经有请求在跑——通常是审批对话框正等用户点确认。回 JSON-RPC error
    // （而不是 HTTP 错误），客户端才会把它当成一次失败的调用并稍后重试。
    return json_response(200, jsonrpc_error(request.body, mcp::McpServer::kInternalError,
                                            QStringLiteral("another MCP request is still "
                                                           "running; wait for it to finish")));
  }
  busy_ = true;
  struct BusyGuard {
    bool& flag;
    ~BusyGuard() { flag = false; }
  } busy_guard{busy_};

  const std::optional<mcp::Json> message = server_.handle_text(body);
  if (!message) {
    // 通知（没有 id）：Streamable HTTP 约定用 202 + 空体回答。
    return McpHttpResponse{202, QByteArrayLiteral("application/json"), QByteArray(), {}};
  }
  return json_response(200, QByteArray::fromStdString(message->dump()));
}

mcp::McpToolGateDecision McpService::gate_tool(std::string_view tool, const mcp::Json& args) {
  if (!SessionMcpBackend::mutates(tool)) {
    return {};  // 读工具永远放行
  }
  switch (policy_) {
    case McpPolicy::kAuto:
      return {};
    case McpPolicy::kReadOnly:
      return {false,
              "写操作被拒绝：Tamias 当前是只读策略。要让 AI 改文档，"
              "用 --mcp-policy=ask（每次确认）或 --mcp-policy=auto 启动。"};
    case McpPolicy::kAsk:
      return ask_approval(tool, args);
  }
  return {};
}

mcp::McpToolGateDecision McpService::ask_approval(std::string_view tool, const mcp::Json& args) {
  auto* parent = qobject_cast<QWidget*>(this->parent());
  const QString detail = clipped(QString::fromStdString(args.dump()), 600);
  // 默认按钮是 No：误点回车不该放行一次写操作。
  const auto answer = QMessageBox::question(
      parent, tr("AI 想修改文档"),
      tr("AI 请求执行写操作：\n\n%1\n%2\n\n允许这一次吗？")
          .arg(QString::fromStdString(std::string(tool)), detail),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (answer == QMessageBox::Yes) {
    return {};
  }
  return {false, "用户在 Tamias 里拒绝了这次写操作。"};
}

void McpService::audit_tool(std::string_view tool, const mcp::Json& args,
                            const mcp::McpToolResult& result) {
  const QString document = document_name_.isEmpty() ? QStringLiteral("-") : document_name_;
  QString line =
      QStringLiteral("[MCP] %1 | 文档 %2 | %3 | %4")
          .arg(QString::fromStdString(std::string(tool)), document,
               clipped(QString::fromStdString(args.dump()), 200),
               result.is_error ? QStringLiteral("失败") : QStringLiteral("成功"));
  if (result.is_error) {
    line += QStringLiteral("：") + clipped(QString::fromStdString(result.text), 160);
  }
  emit message(line);
}

bool McpService::authorized(const McpHttpRequest& request) const {
  if (token_.isEmpty()) {
    return true;
  }
  const QByteArray expected = token_.toLatin1();
  const QByteArray authorization = request.headers.value("authorization");
  if (authorization.left(7).toLower() == QByteArrayLiteral("bearer ") &&
      authorization.mid(7).trimmed() == expected) {
    return true;
  }
  return request.headers.value("x-tamias-token") == expected;
}

QString McpService::discovery_path() {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return QDir(base).filePath(QStringLiteral("mcp.json"));
}

void McpService::write_discovery_file() {
  const QString path = discovery_path();
  if (path.isEmpty()) {
    return;
  }
  QDir().mkpath(QFileInfo(path).absolutePath());

  const mcp::Json root = mcp::Json::object({
      {"pid", mcp::Json::integer(static_cast<std::int64_t>(QCoreApplication::applicationPid()))},
      {"port", mcp::Json::integer(port_)},
      {"token", mcp::Json::string(token_.toStdString())},
      {"endpoint", mcp::Json::string(endpoint().toStdString())},
      {"transport", mcp::Json::string("streamable-http")},
      {"document", mcp::Json::string(document_name_.toStdString())},
      {"startedAt", mcp::Json::string(
                        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString())},
  });

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    emit message(tr("MCP: 写发现文件失败 %1").arg(path));
    return;
  }
  file.write(QByteArray::fromStdString(root.dump()));
  if (!file.commit()) {
    emit message(tr("MCP: 提交发现文件失败 %1").arg(path));
  }
}

void McpService::remove_discovery_file() {
  const QString path = discovery_path();
  if (path.isEmpty()) {
    return;
  }
  // 多开时后启动的实例会覆盖 mcp.json；关掉先启动的那个时不能把别人的条目删掉。
  QFile file(path);
  if (file.open(QIODevice::ReadOnly)) {
    if (const auto parsed = mcp::Json::parse(file.readAll().toStdString()); parsed) {
      if (const mcp::Json* pid = parsed->find("pid");
          pid != nullptr && pid->is_number() &&
          pid->as_int() != static_cast<std::int64_t>(QCoreApplication::applicationPid())) {
        return;
      }
    }
  }
  QFile::remove(path);
}

}  // namespace tamias
