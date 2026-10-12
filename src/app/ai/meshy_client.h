#pragma once

#include "ai/meshy_protocol.h"

#include <QObject>
#include <QString>

#include <memory>
#include <string>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace tamias::ai {

// Meshy 的 API key 在凭据管理器里的槽位名。与 AI 面板的 `ai/api-key` 分开：
// 一个是对话模型、一个是生成服务，两个 key 互不相干，清一个不该影响另一个。
inline constexpr char kMeshyApiKeySlot[] = "meshy/api-key";

// Meshy 的**异步任务式**接口封装：建任务 → 轮询 → 把模型下载到本地文件。
//
// 与 AiClient 的三处不同，都来自"一次生成是分钟级"：
//  - 它不是一问一答，而是一个状态机（见 Stage），UI 订阅 stage_changed 就能画进度；
//  - 轮询用 QTimer，事件驱动，**不阻塞 UI 线程**（这也是 M3 不做后台线程的前提）；
//  - 结果落到**本地文件**再交给导入——[`ImportMeshCommand`] 和 GLB 的内嵌贴图都吃路径。
//
// 这个类不做的事：不碰 Document、不导入、不管面板。它只回答"文件在哪"或"为什么没成"。
// 协议细节（字段 / 端点 / 状态机判定）全在 src/ai/meshy_protocol.{h,cpp}，那边有单测。
class MeshyClient : public QObject {
  Q_OBJECT
 public:
  struct Config {
    QString base_url;  // 例如 https://api.meshy.ai/openapi/v2
    QString api_key;
  };

  enum class Stage {
    kIdle,         // 没在跑
    kCreating,     // 建任务
    kGenerating,   // 轮询等结果
    kDownloading,  // 下载模型
    kSucceeded,    // 本地文件就绪
    kFailed,       // 失败（网络 / 服务端 / 超时 / 格式不收）
    kCanceled,
  };

  explicit MeshyClient(QObject* parent = nullptr);
  ~MeshyClient() override;

  void set_config(Config config);
  [[nodiscard]] bool configured() const;
  [[nodiscard]] Stage stage() const { return stage_; }
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString task_id() const;
  // 上次成功下载的本地路径（失败 / 取消后保留，供 UI 重试导入）。
  [[nodiscard]] QString last_model_path() const { return last_model_path_; }

  // 丢一句提示词出去。默认从 preview（粗模）起步；成功回调给的是本地模型路径。
  void generate(const MeshyTextTo3dRequest& request);
  // 取消：停轮询、断在飞的请求、删掉半截的临时文件；之后不再有别的信号。
  void cancel();
  // 查剩余额度。与生成互不干扰（自己的回复指针）。生成期间也可以查。
  void fetch_balance();

 signals:
  void stage_changed(MeshyClient::Stage stage);
  // 生成阶段是服务端进度（0–100）；下载阶段是下载百分比。
  void progress_changed(int percent);
  // local_model_path 是下载好的本地文件；thumbnail_url 可能为空。
  void succeeded(const QString& local_model_path, const QString& thumbnail_url);
  void failed(const QString& message);
  void canceled();
  void balance_fetched(int balance);
  void balance_failed(const QString& message);

 private:
  void post_create();
  void on_create_finished();
  void poll_once();
  void on_task_finished();
  void start_download(const std::string& url);
  void on_download_ready();
  void on_download_finished();

  void enter_stage(Stage stage);
  void fail(const QString& message);
  [[nodiscard]] QString begin_download_file(const std::string& url);
  void discard_download_file();
  void reset_transient();

  Config config_;
  QNetworkAccessManager* network_ = nullptr;
  QNetworkReply* pending_ = nullptr;         // 建任务 / 查任务 / 下载共用（同一时刻只有一个）
  QNetworkReply* balance_reply_ = nullptr;
  QTimer* poll_timer_ = nullptr;
  std::unique_ptr<QFile> download_file_;
  QString download_path_;
  QString last_model_path_;
  QString thumbnail_url_;
  std::string task_id_;
  MeshyTextTo3dRequest request_;  // 建任务要在手，refine 那步还要用同一个 prompt
  Stage stage_ = Stage::kIdle;
  int poll_attempts_ = 0;
  int offline_retries_ = 0;  // 连续"连不上"的次数；超过上限才算失败
};

}  // namespace tamias::ai
