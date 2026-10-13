#include "app/shell/panel/meshy_panel.h"

#include "app/base/app_settings.h"
#include "app/base/secret_store.h"
#include "engine/base/log.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QShortcut>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>

namespace tamias {
namespace {

// 风格与面数都是「常用值 + 可自己敲」：服务端加了新风格不该逼我们发版。
QStringList art_style_presets() {
  return {QStringLiteral("realistic"), QStringLiteral("sculpture")};
}

QStringList polycount_presets() {
  return {QStringLiteral("5000"), QStringLiteral("10000"), QStringLiteral("30000"),
          QStringLiteral("60000")};
}

QToolButton* make_button(const QString& text, const QString& tip) {
  auto* button = new QToolButton;
  button->setText(text);
  button->setToolTip(tip);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return button;
}

}  // namespace

MeshyPanel::MeshyPanel(QWidget* parent) : QWidget(parent) {
  client_ = new ai::MeshyClient(this);

  prompt_ = new QPlainTextEdit(this);
  prompt_->setPlaceholderText(
      tr("Describe the object to generate, e.g. \"a wooden chair with four legs\""));
  prompt_->setFixedHeight(72);

  art_style_ = new QComboBox(this);
  art_style_->setEditable(true);
  art_style_->setInsertPolicy(QComboBox::NoInsert);
  art_style_->addItems(art_style_presets());
  art_style_->setToolTip(tr("Art style the service should aim for"));

  polycount_ = new QComboBox(this);
  polycount_->setEditable(true);
  polycount_->setInsertPolicy(QComboBox::NoInsert);
  polycount_->addItems(polycount_presets());
  polycount_->setCurrentText(QStringLiteral("30000"));
  polycount_->setToolTip(tr("Target triangle count for the generated mesh"));

  auto* options = new QHBoxLayout;
  options->setContentsMargins(0, 0, 0, 0);
  options->addWidget(new QLabel(tr("Style"), this));
  options->addWidget(art_style_);
  options->addSpacing(8);
  options->addWidget(new QLabel(tr("Triangles"), this));
  options->addWidget(polycount_);
  options->addStretch(1);

  progress_ = new QProgressBar(this);
  progress_->setRange(0, 100);
  progress_->setTextVisible(true);

  stage_label_ = new QLabel(this);
  balance_label_ = new QLabel(this);
  result_ = new QLabel(this);
  result_->setWordWrap(true);

  generate_button_ = make_button(tr("Generate"), tr("Start a generation (Ctrl+Enter)"));
  refine_button_ =
      make_button(tr("Refine"), tr("Spend another pass on the last preview result"));
  cancel_button_ = make_button(tr("Cancel"), tr("Stop waiting; the file is discarded"));
  balance_button_ = make_button(tr("Credits"), tr("Ask the service for the remaining credits"));
  settings_button_ = make_button(tr("Settings"), tr("Service address, model, API key"));

  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(balance_label_, 1);
  buttons->addWidget(balance_button_);
  buttons->addWidget(settings_button_);
  buttons->addWidget(cancel_button_);
  buttons->addWidget(refine_button_);
  buttons->addWidget(generate_button_);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);
  layout->addWidget(prompt_);
  layout->addLayout(options);
  layout->addWidget(progress_);
  layout->addWidget(stage_label_);
  layout->addWidget(result_);
  layout->addLayout(buttons);
  layout->addStretch(1);

  for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), prompt_);
    shortcut->setContext(Qt::WidgetShortcut);
    connect(shortcut, &QShortcut::activated, this, &MeshyPanel::submit);
  }
  connect(generate_button_, &QToolButton::clicked, this, &MeshyPanel::submit);
  connect(refine_button_, &QToolButton::clicked, this, &MeshyPanel::refine);
  connect(cancel_button_, &QToolButton::clicked, this, &MeshyPanel::cancel);
  connect(balance_button_, &QToolButton::clicked, this, &MeshyPanel::fetch_balance);
  connect(settings_button_, &QToolButton::clicked, this, &MeshyPanel::open_settings);

  connect(client_, &ai::MeshyClient::stage_changed, this, &MeshyPanel::show_stage);
  connect(client_, &ai::MeshyClient::progress_changed, this,
          [this](int percent) { progress_->setValue(percent); });
  connect(client_, &ai::MeshyClient::failed, this, [this](const QString& message) {
    progress_->setValue(0);
    log_error("Meshy: " + message.toStdString());
    note(tr("Failed: %1").arg(message));
  });
  connect(client_, &ai::MeshyClient::canceled, this, [this] {
    progress_->setValue(0);
    note(tr("Canceled. Nothing was added to the document."));
  });
  connect(client_, &ai::MeshyClient::succeeded, this,
          [this](const QString& path, const QString& thumbnail) {
            Q_UNUSED(thumbnail);
            // 名字取提示词前 40 字：属性面板 / 大纲里要能认出这是哪次生成的。
            const QString name = last_prompt_.left(40);
            const std::uint64_t node_id =
                import_handler_ ? import_handler_(path, name) : 0;
            if (node_id == 0) {
              note(tr("Generated, but it could not be added to the document: %1").arg(path));
              return;
            }
            // 生成是外部动作、且花了钱：留一行审计（和手点导入区分开）。
            log_info("Meshy: imported " + path.toStdString() + " as node " +
                     std::to_string(node_id) + " (one undo step)");
            set_busy(false);
            progress_->setValue(100);
            note(tr("Added to the document as node %1 (Ctrl+Z undoes it). File: %2")
                     .arg(node_id)
                     .arg(path));
          });
  connect(client_, &ai::MeshyClient::balance_fetched, this, [this](int balance) {
    balance_label_->setText(tr("Credits: %1").arg(balance));
  });
  connect(client_, &ai::MeshyClient::balance_failed, this, [this](const QString& message) {
    balance_label_->setText(tr("Credits: unavailable"));
    log_warn("Meshy: balance query failed: " + message.toStdString());
  });

  const auto& settings = AppSettings::instance();
  api_key_ = SecretStore::load(QLatin1String(ai::kMeshyApiKeySlot));
  sync_config();

  set_busy(false);
  show_stage(ai::MeshyClient::Stage::kIdle);
  note(tr("Write a prompt and press Generate. The model is downloaded, then added to the "
          "current document as one undoable step. Generation costs credits and takes a "
          "few minutes."));
}

void MeshyPanel::set_import_handler(ImportHandler handler) {
  import_handler_ = std::move(handler);
}

void MeshyPanel::sync_config() {
  const auto& settings = AppSettings::instance();
  QString base_url = settings.meshy_base_url().trimmed();
  if (base_url.isEmpty()) {
    base_url = QString::fromStdString(std::string(ai::kDefaultMeshyBaseUrl));
  }
  client_->set_config({base_url, api_key_});
}

void MeshyPanel::generate(const QString& prompt) {
  if (prompt.trimmed().isEmpty()) {
    return;
  }
  prompt_->setPlainText(prompt.trimmed());
  submit();
}

void MeshyPanel::submit() {
  if (client_->busy()) {
    return;
  }
  const QString text = prompt_->toPlainText().trimmed();
  if (text.isEmpty()) {
    note(tr("Write a prompt first."));
    return;
  }

  ai::MeshyTextTo3dRequest request;
  request.prompt = text.toStdString();
  request.art_style = art_style_->currentText().trimmed().toStdString();
  bool ok = false;
  const int polycount = polycount_->currentText().trimmed().toInt(&ok);
  if (ok && polycount > 0) {
    request.target_polycount = polycount;
  }
  request.ai_model = AppSettings::instance().meshy_model().trimmed().toStdString();

  last_prompt_ = text;
  preview_task_id_.clear();  // 新的一次 preview，之前那版的细化入口作废
  last_task_was_preview_ = true;
  set_busy(true);
  note(tr("Generating… this can take a few minutes."));
  client_->generate(request);
}

void MeshyPanel::refine() {
  if (client_->busy() || preview_task_id_.isEmpty()) {
    return;
  }
  ai::MeshyTextTo3dRequest request;
  request.prompt = last_prompt_.toStdString();
  request.mode = "refine";
  request.preview_task_id = preview_task_id_.toStdString();
  request.ai_model = AppSettings::instance().meshy_model().trimmed().toStdString();
  // 细化出来的那一版不再接受二次细化：要更好的话应该是重新生成一轮。
  preview_task_id_.clear();
  last_task_was_preview_ = false;
  set_busy(true);
  note(tr("Refining the last preview…"));
  client_->generate(request);
}

void MeshyPanel::cancel() {
  if (!client_->busy()) {
    return;
  }
  client_->cancel();
}

void MeshyPanel::fetch_balance() {
  balance_label_->setText(tr("Credits: checking…"));
  client_->fetch_balance();
}

void MeshyPanel::set_busy(bool busy) {
  generate_button_->setEnabled(!busy);
  cancel_button_->setEnabled(busy);
  // 细化只在「刚跑完一次 preview」时有意义，忙的时候一律按住。
  refine_button_->setEnabled(!busy && !preview_task_id_.isEmpty());
  prompt_->setReadOnly(busy);
}

void MeshyPanel::show_stage(ai::MeshyClient::Stage stage) {
  switch (stage) {
    case ai::MeshyClient::Stage::kIdle:
      stage_label_->setText(tr("Idle"));
      break;
    case ai::MeshyClient::Stage::kCreating:
      progress_->setValue(0);
      stage_label_->setText(tr("Creating the task…"));
      break;
    case ai::MeshyClient::Stage::kGenerating:
      stage_label_->setText(tr("Generating on Meshy…"));
      break;
    case ai::MeshyClient::Stage::kDownloading:
      stage_label_->setText(tr("Downloading the model…"));
      break;
    case ai::MeshyClient::Stage::kSucceeded:
      stage_label_->setText(tr("Done"));
      // 只有 preview 能被细化；细化那一版跑完不该再给入口。忙闲状态在导入完成后收。
      preview_task_id_ = last_task_was_preview_ ? client_->task_id() : QString();
      break;
    case ai::MeshyClient::Stage::kFailed:
      stage_label_->setText(tr("Failed"));
      set_busy(false);
      break;
    case ai::MeshyClient::Stage::kCanceled:
      stage_label_->setText(tr("Canceled"));
      set_busy(false);
      break;
  }
}

void MeshyPanel::note(const QString& text) { result_->setText(text); }

void MeshyPanel::open_settings() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("3D Generation Settings"));

  auto* base_url = new QLineEdit(AppSettings::instance().meshy_base_url(), &dialog);
  base_url->setPlaceholderText(
      QString::fromStdString(std::string(ai::kDefaultMeshyBaseUrl)));
  base_url->setToolTip(tr("Leave empty to use the default service address"));
  auto* model = new QLineEdit(AppSettings::instance().meshy_model(), &dialog);
  model->setPlaceholderText(tr("Leave empty for the service default model"));
  auto* key = new QLineEdit(api_key_, &dialog);
  key->setEchoMode(QLineEdit::Password);
  key->setPlaceholderText(tr("Leave empty to forget the saved key"));

  auto* form = new QFormLayout;
  form->addRow(tr("Service address"), base_url);
  form->addRow(tr("Model"), model);
  form->addRow(tr("API Key"), key);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* note_label = new QLabel(
      tr("Service address and model are remembered. The API key is saved to %1 so you "
         "don't have to retype it. This panel only drives text-to-3D; the key never goes "
         "into the document.")
          .arg(SecretStore::backend_name()),
      &dialog);
  note_label->setWordWrap(true);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(note_label);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  auto& settings = AppSettings::instance();
  settings.set_meshy_base_url(base_url->text().trimmed());
  settings.set_meshy_model(model->text().trimmed());
  settings.save();
  api_key_ = key->text().trimmed();

  // 留空 = 把存过的密钥删掉，不是存一个空串。
  QString key_error;
  const bool key_saved = SecretStore::save(QLatin1String(ai::kMeshyApiKeySlot), api_key_,
                                           &key_error);
  sync_config();
  if (key_saved) {
    note(tr("Settings updated."));
  } else {
    // 没写进去就只在本次会话生效：功能照用，但要让用户知道下次得重敲。
    note(tr("Settings updated, but the API key could not be saved (%1); it stays in this "
            "session only.")
             .arg(key_error));
  }
}

}  // namespace tamias
