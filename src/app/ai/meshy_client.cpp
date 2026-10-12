#include "app/ai/meshy_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <string>
#include <utility>

namespace tamias::ai {
namespace {

// 轮询间隔与总时长上限：生成通常一到几分钟。间隔短了浪费额度查询次数，
// 长了用户觉得卡住；上限保证"服务端一直 IN_PROGRESS"不会把界面永久挂住。
constexpr int kPollIntervalMs = 3000;
constexpr int kMaxPollAttempts = 200;  // ≈10 分钟
// 轮询时"连不上"不算失败：家用网络抖一下很正常，连续几次才认。建任务时不重试
// （用户刚点下去，失败要立刻告诉他）。
constexpr int kMaxOfflineRetries = 3;

QString clipped(const QString& text, int limit) {
  return text.size() <= limit ? text : text.left(limit) + QStringLiteral("…");
}

// 服务端可能给 fbx / usdz —— Tamias 的 loader 只吃 glb / obj。与其下完再报"格式不支持"，
// 不如当场说清楚。URL 带查询串（签名链接）时只看路径部分。
bool url_looks_importable(const std::string& url) {
  const QString path = QUrl(QString::fromStdString(url)).path();
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix.isEmpty()) {
    return true;  // 没后缀的签名链接不拦，交给 loader 判断
  }
  static const QStringList kRejected = {QStringLiteral("fbx"), QStringLiteral("usdz"),
                                         QStringLiteral("stl"), QStringLiteral("ply")};
  return !kRejected.contains(suffix);
}

QString local_file_name_for(const std::string& url, const std::string& task_id) {
  QString name = QString::fromStdString(task_id);
  if (name.isEmpty()) {
    name = QStringLiteral("task");
  }
  // 任务 id 里的分隔符进了文件名不好看也不好找，统一换成连字符。
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
  QString suffix = QFileInfo(QUrl(QString::fromStdString(url)).path()).suffix().toLower();
  if (suffix.isEmpty()) {
    suffix = QStringLiteral("glb");
  }
  return name + QLatin1Char('.') + suffix;
}

}  // namespace

MeshyClient::MeshyClient(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      poll_timer_(new QTimer(this)) {
  poll_timer_->setInterval(kPollIntervalMs);
  connect(poll_timer_, &QTimer::timeout, this, &MeshyClient::poll_once);
}

MeshyClient::~MeshyClient() = default;

void MeshyClient::set_config(Config config) { config_ = std::move(config); }

bool MeshyClient::configured() const {
  return !config_.base_url.trimmed().isEmpty() && !config_.api_key.trimmed().isEmpty();
}

bool MeshyClient::busy() const {
  return stage_ == Stage::kCreating || stage_ == Stage::kGenerating ||
         stage_ == Stage::kDownloading;
}

QString MeshyClient::task_id() const { return QString::fromStdString(task_id_); }

void MeshyClient::enter_stage(Stage stage) {
  if (stage_ == stage) {
    return;
  }
  stage_ = stage;
  emit stage_changed(stage_);
}

void MeshyClient::reset_transient() {
  poll_timer_->stop();
  if (pending_ != nullptr) {
    QNetworkReply* reply = pending_;
    pending_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
  }
  discard_download_file();
}

void MeshyClient::discard_download_file() {
  if (download_file_ != nullptr) {
    download_file_->close();
    download_file_.reset();
  }
  if (!download_path_.isEmpty()) {
    QFile::remove(download_path_);
    download_path_.clear();
  }
}

void MeshyClient::generate(const MeshyTextTo3dRequest& request) {
  if (busy()) {
    return;  // 一次一个任务；UI 侧按钮此时应当是灰的
  }
  if (!configured()) {
    // 用队列回调，保证「generate 之后才可能收到信号」，调用方不会重入。
    QTimer::singleShot(0, this, [this] {
      fail(tr("Meshy is not configured: open Settings and fill in the service address "
              "and API key."));
    });
    return;
  }
  reset_transient();
  task_id_.clear();
  thumbnail_url_.clear();
  poll_attempts_ = 0;
  offline_retries_ = 0;
  request_ = request;
  post_create();
}

void MeshyClient::post_create() {
  enter_stage(Stage::kCreating);
  emit progress_changed(0);

  QNetworkRequest wire{
      QUrl(QString::fromStdString(meshy_text_to_3d_url(config_.base_url.trimmed().toStdString())))};
  wire.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  wire.setRawHeader("Authorization", "Bearer " + config_.api_key.trimmed().toUtf8());
  const std::string body = meshy_text_to_3d_body(request_).dump();
  pending_ = network_->post(wire, QByteArray::fromStdString(body));
  connect(pending_, &QNetworkReply::finished, this, &MeshyClient::on_create_finished);
}

void MeshyClient::on_create_finished() {
  QNetworkReply* reply = pending_;
  if (reply == nullptr) {
    return;  // 已经取消
  }
  pending_ = nullptr;
  reply->deleteLater();

  const QByteArray payload = reply->readAll();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 0) {
    fail(tr("Cannot reach Meshy: %1").arg(reply->errorString()));
    return;
  }
  if (status < 200 || status >= 300) {
    fail(tr("Meshy returned %1: %2").arg(status).arg(clipped(QString::fromUtf8(payload), 300)));
    return;
  }
  const auto root = mcp::Json::parse(std::string(payload.constData(), payload.size()));
  if (!root) {
    fail(tr("Meshy returned something that is not JSON."));
    return;
  }
  const auto id = meshy_parse_created_task_id(*root);
  if (!id) {
    fail(QString::fromStdString(id.error()));
    return;
  }
  task_id_ = *id;
  enter_stage(Stage::kGenerating);
  poll_timer_->start();
}

void MeshyClient::poll_once() {
  if (task_id_.empty()) {
    return;
  }
  if (poll_attempts_ >= kMaxPollAttempts) {
    fail(tr("Giving up after %1 minutes: the task never finished. It may still be running "
            "on Meshy — check the dashboard before generating again.")
             .arg((kPollIntervalMs / 1000 * kMaxPollAttempts) / 60));
    return;
  }
  ++poll_attempts_;
  QNetworkRequest wire{QUrl(QString::fromStdString(
      meshy_task_url(config_.base_url.trimmed().toStdString(), task_id_)))};
  wire.setRawHeader("Authorization", "Bearer " + config_.api_key.trimmed().toUtf8());
  pending_ = network_->get(wire);
  connect(pending_, &QNetworkReply::finished, this, &MeshyClient::on_task_finished);
}

void MeshyClient::on_task_finished() {
  QNetworkReply* reply = pending_;
  if (reply == nullptr) {
    return;
  }
  pending_ = nullptr;
  reply->deleteLater();

  const QByteArray payload = reply->readAll();
  const int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (http_status == 0) {
    // 网络抖动：继续等下一轮，连续失败到上限才认输。
    if (++offline_retries_ > kMaxOfflineRetries) {
      fail(tr("Lost contact with Meshy: %1").arg(reply->errorString()));
    }
    return;
  }
  offline_retries_ = 0;
  if (http_status < 200 || http_status >= 300) {
    fail(tr("Meshy returned %1: %2").arg(http_status).arg(clipped(QString::fromUtf8(payload), 300)));
    return;
  }

  const auto root = mcp::Json::parse(std::string(payload.constData(), payload.size()));
  if (!root) {
    fail(tr("Meshy returned something that is not JSON."));
    return;
  }
  const auto task = meshy_parse_task(*root);
  if (!task) {
    fail(QString::fromStdString(task.error()));
    return;
  }
  emit progress_changed(task->progress);
  if (!task->thumbnail_url.empty()) {
    thumbnail_url_ = QString::fromStdString(task->thumbnail_url);
  }
  if (!meshy_status_terminal(task->status)) {
    return;  // 继续轮询
  }
  poll_timer_->stop();
  if (task->status == MeshyTaskStatus::kFailed) {
    fail(task->error.empty() ? tr("Meshy reported the task as failed.")
                             : QString::fromStdString(task->error));
    return;
  }
  if (task->status == MeshyTaskStatus::kCanceled) {
    reset_transient();
    enter_stage(Stage::kCanceled);
    emit canceled();
    return;
  }

  const std::string url = task->model_urls.best_importable();
  if (url.empty()) {
    fail(tr("Meshy finished but returned no model file."));
    return;
  }
  if (!url_looks_importable(url)) {
    fail(tr("Meshy only returned a format Tamias cannot import (fbx / usdz / stl). "
            "Ask for a GLB, or open the file in another tool and export it as .glb."));
    return;
  }
  start_download(url);
}

QString MeshyClient::begin_download_file(const std::string& url) {
  QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/meshy");
  if (!QDir().mkpath(root)) {
    root = QDir::tempPath();  // 落盘位置失败就退到临时目录，不让整条流程断在这
  }
  const QString path = root + QLatin1Char('/') + local_file_name_for(url, task_id_);
  auto file = std::make_unique<QFile>(path);
  if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  download_file_ = std::move(file);
  download_path_ = path;
  return path;
}

void MeshyClient::start_download(const std::string& url) {
  const QString path = begin_download_file(url);
  if (path.isEmpty()) {
    fail(tr("Cannot write the downloaded model to disk."));
    return;
  }
  enter_stage(Stage::kDownloading);
  emit progress_changed(0);

  QNetworkRequest wire{QUrl(QString::fromStdString(url))};
  pending_ = network_->get(wire);
  connect(pending_, &QNetworkReply::readyRead, this, &MeshyClient::on_download_ready);
  connect(pending_, &QNetworkReply::downloadProgress, this,
          [this](qint64 received, qint64 total) {
            if (total > 0) {
              emit progress_changed(static_cast<int>(received * 100 / total));
            }
          });
  connect(pending_, &QNetworkReply::finished, this, &MeshyClient::on_download_finished);
}

void MeshyClient::on_download_ready() {
  if (pending_ == nullptr || download_file_ == nullptr) {
    return;
  }
  // 边收边写：模型可能几十 MB，不留一份在内存里。
  download_file_->write(pending_->readAll());
}

void MeshyClient::on_download_finished() {
  QNetworkReply* reply = pending_;
  if (reply == nullptr) {
    return;
  }
  pending_ = nullptr;
  reply->deleteLater();

  if (download_file_ != nullptr) {
    download_file_->write(reply->readAll());
    download_file_->close();
    download_file_.reset();
  }
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (reply->error() != QNetworkReply::NoError || (status != 0 && (status < 200 || status >= 300))) {
    const QString detail = reply->errorString();
    discard_download_file();
    fail(tr("Download failed: %1").arg(detail));
    return;
  }
  if (download_path_.isEmpty()) {
    fail(tr("Download finished but no file was written."));
    return;
  }

  last_model_path_ = download_path_;
  download_path_.clear();
  enter_stage(Stage::kSucceeded);
  emit progress_changed(100);
  emit succeeded(last_model_path_, thumbnail_url_);
}

void MeshyClient::fail(const QString& message) {
  reset_transient();
  enter_stage(Stage::kFailed);
  emit failed(message);
}

void MeshyClient::cancel() {
  if (!busy()) {
    return;
  }
  reset_transient();
  enter_stage(Stage::kCanceled);
  emit canceled();
}

void MeshyClient::fetch_balance() {
  if (balance_reply_ != nullptr) {
    return;  // 一次一个
  }
  if (!configured()) {
    QTimer::singleShot(0, this, [this] {
      emit balance_failed(tr("Meshy is not configured yet."));
    });
    return;
  }
  QNetworkRequest wire{
      QUrl(QString::fromStdString(meshy_balance_url(config_.base_url.trimmed().toStdString())))};
  wire.setRawHeader("Authorization", "Bearer " + config_.api_key.trimmed().toUtf8());
  balance_reply_ = network_->get(wire);
  connect(balance_reply_, &QNetworkReply::finished, this, [this] {
    QNetworkReply* reply = balance_reply_;
    balance_reply_ = nullptr;
    if (reply == nullptr) {
      return;
    }
    reply->deleteLater();
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
      emit balance_failed(tr("Cannot reach Meshy: %1").arg(reply->errorString()));
      return;
    }
    const auto root = mcp::Json::parse(std::string(payload.constData(), payload.size()));
    const auto balance = root ? meshy_parse_balance(*root) : Result<int>(Err("not JSON"));
    if (!balance) {
      emit balance_failed(tr("Cannot read the credit balance (HTTP %1).").arg(status));
      return;
    }
    emit balance_fetched(*balance);
  });
}

}  // namespace tamias::ai
