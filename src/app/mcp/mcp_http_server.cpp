#include "app/mcp/mcp_http_server.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

namespace tamias {
namespace {

// 头部 32KB / 正文 8MB：MCP 消息（工具参数、特征树）远小于这个量级，
// 上限只是防止畸形请求把内存吃光。
constexpr qsizetype kMaxHeadBytes = 32 * 1024;
constexpr qsizetype kMaxBodyBytes = 8 * 1024 * 1024;

enum class ParseResult { kIncomplete, kComplete, kBad };

[[nodiscard]] QByteArray status_text(int status) {
  switch (status) {
    case 200:
      return QByteArrayLiteral("OK");
    case 202:
      return QByteArrayLiteral("Accepted");
    case 204:
      return QByteArrayLiteral("No Content");
    case 400:
      return QByteArrayLiteral("Bad Request");
    case 401:
      return QByteArrayLiteral("Unauthorized");
    case 403:
      return QByteArrayLiteral("Forbidden");
    case 404:
      return QByteArrayLiteral("Not Found");
    case 405:
      return QByteArrayLiteral("Method Not Allowed");
    case 413:
      return QByteArrayLiteral("Payload Too Large");
    case 503:
      return QByteArrayLiteral("Service Unavailable");
    default:
      return QByteArrayLiteral("Error");
  }
}

// 从缓冲区里取一条完整请求。取出后 buffer 里留下的就是下一条的字节（支持 keep-alive）。
[[nodiscard]] ParseResult parse_request(QByteArray& buffer, McpHttpRequest& request,
                                        QByteArray& error) {
  const qsizetype head_end = buffer.indexOf("\r\n\r\n");
  if (head_end < 0) {
    if (buffer.size() > kMaxHeadBytes) {
      error = QByteArrayLiteral("request head too large");
      return ParseResult::kBad;
    }
    return ParseResult::kIncomplete;
  }

  QList<QByteArray> lines = buffer.left(head_end).split('\n');
  for (QByteArray& line : lines) {
    if (line.endsWith('\r')) {
      line.chop(1);
    }
  }
  if (lines.isEmpty()) {
    error = QByteArrayLiteral("empty request");
    return ParseResult::kBad;
  }

  const QList<QByteArray> request_line = lines.first().split(' ');
  if (request_line.size() != 3) {
    error = QByteArrayLiteral("malformed request line");
    return ParseResult::kBad;
  }
  request.method = request_line[0];
  request.target = request_line[1];
  request.version = request_line[2];

  request.headers.clear();
  for (qsizetype i = 1; i < lines.size(); ++i) {
    if (lines[i].isEmpty()) {
      continue;
    }
    const qsizetype colon = lines[i].indexOf(':');
    if (colon <= 0) {
      error = QByteArrayLiteral("malformed header");
      return ParseResult::kBad;
    }
    request.headers.insert(lines[i].left(colon).trimmed().toLower(),
                           lines[i].mid(colon + 1).trimmed());
  }

  if (const QByteArray transfer = request.headers.value("transfer-encoding");
      !transfer.isEmpty() && transfer.toLower() != "identity") {
    error = QByteArrayLiteral("chunked transfer encoding is not supported");
    return ParseResult::kBad;
  }

  qsizetype content_length = 0;
  if (const QByteArray raw_length = request.headers.value("content-length");
      !raw_length.isEmpty()) {
    bool ok = false;
    content_length = raw_length.toLongLong(&ok);
    if (!ok || content_length < 0) {
      error = QByteArrayLiteral("invalid Content-Length");
      return ParseResult::kBad;
    }
    if (content_length > kMaxBodyBytes) {
      error = QByteArrayLiteral("request body too large");
      return ParseResult::kBad;
    }
  }

  const qsizetype total = head_end + 4 + content_length;
  if (buffer.size() < total) {
    return ParseResult::kIncomplete;
  }
  request.body = buffer.mid(head_end + 4, content_length);
  buffer.remove(0, total);
  return ParseResult::kComplete;
}

[[nodiscard]] bool wants_keep_alive(const McpHttpRequest& request) {
  const QByteArray connection = request.headers.value("connection").toLower();
  if (connection == "close") {
    return false;
  }
  if (connection == "keep-alive") {
    return true;
  }
  return request.version == "HTTP/1.1";
}

}  // namespace

McpHttpServer::McpHttpServer(QObject* parent) : QObject(parent) {}

McpHttpServer::~McpHttpServer() { close(); }

bool McpHttpServer::listen(quint16 port) {
  if (server_ != nullptr) {
    return is_listening();
  }
  // 不挂 parent：生命周期由 close() 显式管理（stop 之后立刻释放，不等事件循环）。
  auto* server = new QTcpServer();
  connect(server, &QTcpServer::newConnection, this, &McpHttpServer::on_new_connection);
  // 只绑回环：MCP 是给本机的 AI 客户端用的，不对局域网开放。
  if (!server->listen(QHostAddress::LocalHost, port)) {
    delete server;
    return false;
  }
  server_ = server;
  return true;
}

void McpHttpServer::close() {
  buffers_.clear();
  if (server_ == nullptr) {
    return;
  }
  server_->close();
  delete server_;  // 已建立的 socket 是它的子对象，一并销毁
  server_ = nullptr;
}

bool McpHttpServer::is_listening() const {
  return server_ != nullptr && server_->isListening();
}

quint16 McpHttpServer::port() const {
  return server_ == nullptr ? 0 : server_->serverPort();
}

void McpHttpServer::on_new_connection() {
  while (QTcpSocket* socket = server_->nextPendingConnection()) {
    buffers_.insert(socket, QByteArray());
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { on_ready_read(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] { on_disconnected(socket); });
  }
}

void McpHttpServer::on_ready_read(QTcpSocket* socket) {
  QByteArray& buffer = buffers_[socket];
  buffer += socket->readAll();

  while (true) {
    McpHttpRequest request;
    QByteArray error;
    const ParseResult result = parse_request(buffer, request, error);
    if (result == ParseResult::kIncomplete) {
      return;
    }
    if (result == ParseResult::kBad) {
      McpHttpResponse response;
      response.status = 400;
      response.content_type = QByteArrayLiteral("text/plain; charset=utf-8");
      response.body = error;
      write_response(socket, response, false);
      socket->disconnectFromHost();
      return;
    }

    McpHttpResponse response;
    if (handler_) {
      response = handler_(request);
    } else {
      response.status = 503;
      response.content_type = QByteArrayLiteral("text/plain; charset=utf-8");
      response.body = QByteArrayLiteral("MCP handler not installed");
    }
    const bool keep_alive = wants_keep_alive(request);
    write_response(socket, response, keep_alive);
    if (!keep_alive) {
      socket->disconnectFromHost();
      return;
    }
  }
}

void McpHttpServer::on_disconnected(QTcpSocket* socket) {
  buffers_.remove(socket);
  socket->deleteLater();
}

void McpHttpServer::write_response(QTcpSocket* socket, const McpHttpResponse& response,
                                   bool keep_alive) {
  QByteArray head;
  head += "HTTP/1.1 ";
  head += QByteArray::number(response.status);
  head += ' ';
  head += status_text(response.status);
  head += "\r\nContent-Type: ";
  head += response.content_type;
  head += "\r\nContent-Length: ";
  head += QByteArray::number(static_cast<qint64>(response.body.size()));
  head += "\r\nCache-Control: no-store\r\nConnection: ";
  head += keep_alive ? "keep-alive" : "close";
  head += "\r\n";
  for (const QByteArray& extra : response.extra_headers) {
    head += extra;
    head += "\r\n";
  }
  head += "\r\n";

  socket->write(head);
  socket->write(response.body);
  socket->flush();
}

}  // namespace tamias
