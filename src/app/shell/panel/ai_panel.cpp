#include "app/shell/panel/ai_panel.h"

#include "app/base/app_settings.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace tamias {
namespace {

// 一轮对话最多让模型连着调几轮工具，防止它在「读-想-读」里转圈。
constexpr int kMaxToolRounds = 8;

// 系统提示要交代三件事：先读后写、写只走工具、armed 是怎么回事。
const char* const kSystemPrompt = R"(你是 Tamias（跨 MCAD / BIM 的参数化建模软件）里的助手，可以直接读写用户当前打开的文档。

规则：
1. 先了解现状再动手：tamias_document_info / tamias_list_entities / tamias_get_features。
2. 所有改动都必须通过工具完成。不要声称"我已经改好了"——只有工具返回成功才算改。
3. 创建几何时如果工具返回 armed=true，表示工具已经架好、等用户在视口里点位置。这时要告诉用户去点，不要自己编坐标。
4. 尺寸单位是米。参数名以 tamias_get_features 读到的为准，不要猜。
5. 一次只做用户要求的事。回答用中文，简短，不要长篇大论。)";

QToolButton* make_button(const QString& text, const QString& tip) {
  auto* button = new QToolButton;
  button->setText(text);
  button->setToolTip(tip);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  return button;
}

// AI 设置里的候选项。两个框都可编辑：这些只是常用值，用户能直接敲自己的。
QStringList endpoint_presets() {
  return {
      QStringLiteral("https://api.deepseek.com"),
      QStringLiteral("https://api.deepseek.com/v1"),
      QStringLiteral("https://api.openai.com/v1"),
      QStringLiteral("https://api.anthropic.com/v1"),
      QStringLiteral("http://127.0.0.1:11434/v1"),
      QStringLiteral("http://127.0.0.1:1234/v1"),
  };
}

QStringList model_presets() {
  return {
      QStringLiteral("deepseek-flash"),
      QStringLiteral("deepseek-v4-pro"),
      QStringLiteral("claude-sonnet-4-5"),
      QStringLiteral("claude-opus-4-1"),
      QStringLiteral("gpt-4o-mini"),
      QStringLiteral("qwen2.5:7b"),
  };
}

const QString& default_endpoint() {
  static const QString url = QStringLiteral("https://api.deepseek.com");
  return url;
}

const QString& default_model() {
  static const QString model = QStringLiteral("deepseek-flash");
  return model;
}

}  // namespace

AiPanel::AiPanel(QWidget* parent) : QWidget(parent) {
  client_ = new ai::AiClient(this);

  transcript_ = new QPlainTextEdit(this);
  transcript_->setReadOnly(true);
  transcript_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  transcript_->setPlaceholderText(
      tr("Try asking what is in this document, or where to create a wall."));

  input_ = new QPlainTextEdit(this);
  input_->setPlaceholderText(tr("Ctrl+Enter to send"));
  input_->setFixedHeight(72);

  send_button_ = make_button(tr("Send"), tr("Send (Ctrl+Enter)"));
  stop_button_ = make_button(tr("Stop"), tr("Abort this request"));
  settings_button_ = make_button(tr("Settings"), tr("Service address, model, API key"));
  status_ = new QLabel(this);

  auto* buttons = new QHBoxLayout;
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->addWidget(status_, 1);
  buttons->addWidget(settings_button_);
  buttons->addWidget(stop_button_);
  buttons->addWidget(send_button_);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  layout->setSpacing(6);
  layout->addWidget(transcript_, 1);
  layout->addWidget(input_);
  layout->addLayout(buttons);

  for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
    auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), input_);
    shortcut->setContext(Qt::WidgetShortcut);
    connect(shortcut, &QShortcut::activated, this, &AiPanel::submit);
  }
  connect(send_button_, &QToolButton::clicked, this, &AiPanel::submit);
  connect(stop_button_, &QToolButton::clicked, this, [this] { client_->abort(); });
  connect(settings_button_, &QToolButton::clicked, this, &AiPanel::open_settings);
  connect(client_, &ai::AiClient::finished, this, &AiPanel::handle_reply);

  // 工具表只抓一次：面板不挂逃生舱，所以永远是那 13 个结构化工具。
  for (const mcp::McpTool& tool : backend_.tools()) {
    tool_schema_.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"),
         QJsonObject{
             {QStringLiteral("name"), QString::fromStdString(tool.name)},
             {QStringLiteral("description"), QString::fromStdString(tool.description)},
             {QStringLiteral("parameters"), ai::to_qt(tool.input_schema)},
         }},
    });
  }

  const auto& settings = AppSettings::instance();
  client_->set_config(ai::AiClient::Config{settings.ai_base_url(), QString(), settings.ai_model()});
  stop_button_->setEnabled(false);
  refresh_status();
  append(tr("System"),
         tr("Chat ready. Edits take effect right away and Ctrl+Z undoes them. "
            "Use Settings to fill in the service address and model first."));
}

void AiPanel::bind(Session* session) {
  backend_.set_session(session);
  // 换了文档就重开一段对话：上一段的实体 id 在新文档里没有意义。
  if (!history_.empty()) {
    history_.clear();
    append(tr("System"), tr("Document changed: the conversation context was reset."));
  }
}

void AiPanel::append(const QString& who, const QString& text) {
  transcript_->appendPlainText(who + QStringLiteral("：") + text);
  transcript_->verticalScrollBar()->setValue(transcript_->verticalScrollBar()->maximum());
}

void AiPanel::set_busy(bool busy) {
  busy_ = busy;
  send_button_->setEnabled(!busy);
  stop_button_->setEnabled(busy);
  input_->setReadOnly(busy);
  refresh_status();
}

void AiPanel::refresh_status() {
  const QString model = AppSettings::instance().ai_model();
  if (busy_) {
    status_->setText(tr("Requesting..."));
  } else if (model.isEmpty()) {
    status_->setText(tr("No model configured"));
  } else {
    status_->setText(tr("Model %1").arg(model));
  }
}

void AiPanel::submit() {
  if (busy_) {
    return;
  }
  const QString text = input_->toPlainText().trimmed();
  if (text.isEmpty()) {
    return;
  }
  input_->clear();
  append(tr("You"), text);
  history_.push_back(ai::ChatMessage{QStringLiteral("user"), text, {}, {}});
  rounds_ = 0;
  request_next();
}

void AiPanel::submit_text(const QString& text) {
  if (busy_ || text.trimmed().isEmpty()) {
    return;
  }
  input_->setPlainText(text);
  submit();
}

void AiPanel::submit_text_when_ready(const QString& text) {
  if (text.trimmed().isEmpty()) {
    return;
  }
  if (backend_.has_document()) {
    submit_text(text);
    return;
  }
  pending_text_ = text;
  ready_ticks_ = 0;
  if (ready_timer_ == nullptr) {
    ready_timer_ = new QTimer(this);
    ready_timer_->setInterval(200);
    connect(ready_timer_, &QTimer::timeout, this, [this] {
      if (pending_text_.isEmpty()) {
        ready_timer_->stop();
        return;
      }
      // 最多等 30 秒；超时就照发，让模型看到 no active document，
      // 比默默什么都不做要好排查。
      if (!backend_.has_document() && ++ready_ticks_ <= 150) {
        return;
      }
      const QString text = pending_text_;
      pending_text_.clear();
      ready_timer_->stop();
      submit_text(text);
    });
  }
  ready_timer_->start();
}

void AiPanel::request_next() {
  if (++rounds_ > kMaxToolRounds) {
    append(tr("System"),
           tr("Stopped after %1 tool rounds, the limit.").arg(kMaxToolRounds));
    set_busy(false);
    return;
  }
  std::vector<ai::ChatMessage> messages;
  messages.push_back(
      ai::ChatMessage{QStringLiteral("system"), QString::fromUtf8(kSystemPrompt), {}, {}});
  messages.insert(messages.end(), history_.begin(), history_.end());
  set_busy(true);
  client_->send(messages, tool_schema_);
}

void AiPanel::handle_reply(const ai::ChatReply& reply) {
  if (!reply.error.isEmpty()) {
    append(tr("System"), reply.error);
    set_busy(false);
    return;
  }

  // 把这一轮 assistant 消息（含工具调用）原样记进历史，下一步要发回去。
  ai::ChatMessage assistant;
  assistant.role = QStringLiteral("assistant");
  assistant.content = reply.content;
  if (!reply.tool_calls.empty()) {
    QJsonArray calls;
    for (const ai::ToolCall& call : reply.tool_calls) {
      calls.append(QJsonObject{
          {QStringLiteral("id"), call.id},
          {QStringLiteral("type"), QStringLiteral("function")},
          {QStringLiteral("function"),
           QJsonObject{
               {QStringLiteral("name"), call.name},
               // arguments 必须是字符串包着的 JSON —— 这是 OpenAI 的约定。
               {QStringLiteral("arguments"),
                QString::fromStdString(call.arguments.dump())},
           }},
      });
    }
    assistant.tool_calls = calls;
  }
  history_.push_back(assistant);

  if (!reply.content.isEmpty()) {
    append(tr("Assistant"), reply.content);
  }
  if (reply.tool_calls.empty()) {
    set_busy(false);
    return;
  }

  run_tool_calls(reply.tool_calls);
  request_next();
}

void AiPanel::run_tool_calls(const std::vector<ai::ToolCall>& calls) {
  for (const ai::ToolCall& call : calls) {
    // 直接调后端，不过协议层也不过写策略闸门——这里是人自己在敲。
    const mcp::McpToolResult result =
        backend_.call_tool(call.name.toStdString(), call.arguments);
    append(tr("Tool"), QStringLiteral("%1 %2").arg(
                           call.name, result.is_error ? tr("failed") : tr("done")));
    if (result.is_error) {
      append(tr("Tool"), QString::fromStdString(result.text));
    }

    ai::ChatMessage tool;
    tool.role = QStringLiteral("tool");
    tool.tool_call_id = call.id;
    tool.content = QString::fromStdString(result.text);
    history_.push_back(std::move(tool));
  }
}

void AiPanel::open_settings() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("AI Settings"));

  // 服务地址 / 模型名都是「可编辑下拉」：点开有常用值，也能直接敲自己的。
  auto make_combo = [&dialog](const QStringList& presets, const QString& stored,
                              const QString& fallback) {
    auto* combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItems(presets);
    combo->setCurrentText(stored.trimmed().isEmpty() ? fallback : stored.trimmed());
    return combo;
  };
  auto* base_url = make_combo(endpoint_presets(), AppSettings::instance().ai_base_url(),
                              default_endpoint());
  base_url->setToolTip(
      tr("Root of an OpenAI-compatible API, e.g. https://api.openai.com/v1"));
  auto* model = make_combo(model_presets(), AppSettings::instance().ai_model(),
                           default_model());
  model->setToolTip(tr("Model name exactly as the service expects it"));
  auto* key = new QLineEdit(api_key_, &dialog);
  key->setEchoMode(QLineEdit::Password);
  key->setPlaceholderText(tr("Local models can leave this empty; the key stays in this session"));

  auto* form = new QFormLayout;
  form->addRow(tr("Service address"), base_url);
  form->addRow(tr("Model"), model);
  form->addRow(tr("API Key"), key);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* note = new QLabel(
      tr("Service address and model are remembered; the API key stays in this session "
         "only and is never written to disk. Any OpenAI-compatible endpoint works "
         "(OpenAI / DeepSeek / Claude / Ollama / LM Studio ...)."),
      &dialog);
  note->setWordWrap(true);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addLayout(form);
  layout->addWidget(note);
  layout->addWidget(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  auto& settings = AppSettings::instance();
  settings.set_ai_base_url(base_url->currentText().trimmed());
  settings.set_ai_model(model->currentText().trimmed());
  settings.save();
  api_key_ = key->text();

  client_->set_config(ai::AiClient::Config{settings.ai_base_url(), api_key_, settings.ai_model()});
  refresh_status();
  append(tr("System"), tr("Settings updated."));
}

}  // namespace tamias
