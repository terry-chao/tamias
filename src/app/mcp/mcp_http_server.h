#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <functional>

class QTcpServer;
class QTcpSocket;

namespace tamias {

struct McpHttpRequest {
  QByteArray method;
  QByteArray target;  // 含 query，按需自己 section
  QByteArray version;
  QHash<QByteArray, QByteArray> headers;  // 键一律小写
  QByteArray body;
};

struct McpHttpResponse {
  int status = 200;
  QByteArray content_type = QByteArrayLiteral("application/json");
  QByteArray body;
  QList<QByteArray> extra_headers;
};

// 手写的极简 HTTP/1.1，只够 MCP 用：POST + Content-Length、无 chunked、无 TLS。
//
// **为什么可以用 UI 线程直接跑**：QTcpServer / QTcpSocket 的信号本来就走创建它们的
// 线程的事件循环，所以 readyRead 里处理请求时我们已经在 UI 线程上，能直接读 Session，
// 不需要额外的队列或跨线程投递。代价是长命令会阻塞界面——和控制台脚本求值同一性质。
class McpHttpServer : public QObject {
  Q_OBJECT
 public:
  using Handler = std::function<McpHttpResponse(const McpHttpRequest&)>;

  explicit McpHttpServer(QObject* parent = nullptr);
  ~McpHttpServer() override;

  // port = 0 时让系统挑一个空闲端口，用 port() 取实际值。
  bool listen(quint16 port);
  void close();
  [[nodiscard]] bool is_listening() const;
  [[nodiscard]] quint16 port() const;

  void set_handler(Handler handler) { handler_ = std::move(handler); }

 private:
  void on_new_connection();
  void on_ready_read(QTcpSocket* socket);
  void on_disconnected(QTcpSocket* socket);
  void write_response(QTcpSocket* socket, const McpHttpResponse& response, bool keep_alive);

  QTcpServer* server_ = nullptr;
  Handler handler_;
  QHash<QTcpSocket*, QByteArray> buffers_;
};

}  // namespace tamias
