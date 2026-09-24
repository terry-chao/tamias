#include "app/shell/graphics_diagnostics_dialog.h"

#include "app/base/log_buffer.h"
#include "app/base/rhi_diagnostics.h"
#include "tamias_version.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QString>
#include <QSysInfo>
#include <QVBoxLayout>

#include <string>

namespace tamias {

GraphicsDiagnosticsDialog::GraphicsDiagnosticsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Graphics Diagnostics"));
  resize(760, 560);

  report_view_ = new QPlainTextEdit(this);
  report_view_->setReadOnly(true);
  report_view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  report_view_->setPlainText(build_report_text());

  auto* buttons = new QDialogButtonBox(this);
  auto* copy_button = buttons->addButton(tr("Copy to Clipboard"), QDialogButtonBox::ActionRole);
  auto* save_button = buttons->addButton(tr("Save Report…"), QDialogButtonBox::ActionRole);
  buttons->addButton(QDialogButtonBox::Close);
  connect(copy_button, &QPushButton::clicked, this, &GraphicsDiagnosticsDialog::copy_to_clipboard);
  connect(save_button, &QPushButton::clicked, this, &GraphicsDiagnosticsDialog::save_to_file);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(report_view_);
  layout->addWidget(buttons);
}

QString GraphicsDiagnosticsDialog::build_report_text() const {
  // 和 --diagnostics-report=file 输出同一份文本（见 rhi_diagnostics.h）：协议一处维护，
  // 客户复制的那段和 IT 脚本收集的那份永远一致。
  QString text = QStringLiteral("Qt %1 / %2\n\n")
                     .arg(QString::fromLatin1(qVersion()), QSysInfo::prettyProductName());
  text += QString::fromStdString(build_diagnostics_report());
  return text;
}

void GraphicsDiagnosticsDialog::copy_to_clipboard() {
  if (QClipboard* clipboard = QApplication::clipboard(); clipboard != nullptr) {
    clipboard->setText(report_view_->toPlainText());
  }
}

void GraphicsDiagnosticsDialog::save_to_file() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Save Diagnostics Report"),
                                                   QStringLiteral("tamias-graphics.txt"),
                                                   tr("Text files (*.txt)"));
  if (path.isEmpty()) {
    return;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  file.write(report_view_->toPlainText().toUtf8());
  file.commit();
}

}  // namespace tamias
