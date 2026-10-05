#include "ui/qt/dialogs.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <vector>

namespace tac::qt {
namespace {

QString to_qstring(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString dialog_title(std::string_view title) {
  return title.empty() ? QStringLiteral("Tamias") : to_qstring(title);
}

QString qt_filter(const std::string& filter) {
  return filter.empty() ? QStringLiteral("All files (*.*)") : to_qstring(filter);
}

// 与既有插件对话框一致：只在 min/max 都给时才限制范围。
constexpr double kUnbounded = 1.0e9;

}  // namespace

QtDialogService::QtDialogService(QWidget* parent) : parent_(parent) {}

DialogResult QtDialogService::show_message(std::string_view title, std::string_view message,
                                           DialogButtons buttons) {
  QMessageBox box(parent_);
  box.setWindowTitle(dialog_title(title));
  box.setText(to_qstring(message));
  box.setIcon(QMessageBox::Information);
  switch (buttons) {
    case DialogButtons::OkCancel:
      box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
      box.setDefaultButton(QMessageBox::Ok);
      break;
    case DialogButtons::YesNo:
      box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      box.setDefaultButton(QMessageBox::Yes);
      break;
    case DialogButtons::YesNoCancel:
      box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      box.setDefaultButton(QMessageBox::Yes);
      break;
    case DialogButtons::Ok:
    default:
      box.setStandardButtons(QMessageBox::Ok);
      box.setDefaultButton(QMessageBox::Ok);
      break;
  }
  switch (box.exec()) {
    case QMessageBox::Ok:
      return DialogResult::Ok;
    case QMessageBox::Cancel:
      return DialogResult::Cancel;
    case QMessageBox::Yes:
      return DialogResult::Yes;
    case QMessageBox::No:
      return DialogResult::No;
    default:
      return DialogResult::None;
  }
}

std::optional<std::string> QtDialogService::prompt_string(std::string_view title,
                                                          std::string_view label,
                                                          std::string_view default_value) {
  bool ok = false;
  const QString value = QInputDialog::getText(parent_, dialog_title(title), to_qstring(label),
                                              QLineEdit::Normal, to_qstring(default_value), &ok);
  if (!ok) {
    return std::nullopt;
  }
  return value.toUtf8().toStdString();
}

std::optional<double> QtDialogService::prompt_number(std::string_view title,
                                                     std::string_view label,
                                                     double default_value,
                                                     std::optional<double> min,
                                                     std::optional<double> max) {
  const bool bounded = min.has_value() && max.has_value();
  bool ok = false;
  const double value =
      QInputDialog::getDouble(parent_, dialog_title(title), to_qstring(label), default_value,
                              bounded ? *min : -kUnbounded, bounded ? *max : kUnbounded, 4, &ok);
  if (!ok) {
    return std::nullopt;
  }
  return value;
}

bool QtDialogService::show_form(PromptForm& form) {
  if (form.fields().empty()) {
    return false;
  }

  QDialog dialog(parent_);
  dialog.setWindowTitle(dialog_title(form.title));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form_layout = new QFormLayout();
  layout->addLayout(form_layout);

  struct Editors {
    QLineEdit* text = nullptr;
    QDoubleSpinBox* number = nullptr;
    QCheckBox* flag = nullptr;
  };
  std::vector<Editors> editors;
  editors.reserve(form.fields().size());

  for (PromptField& field : form.fields()) {
    Editors editor;
    const QString label = to_qstring(field.label.empty() ? field.id : field.label);
    switch (field.kind) {
      case PromptFieldKind::Number:
        editor.number = new QDoubleSpinBox(&dialog);
        editor.number->setDecimals(4);
        editor.number->setRange(field.has_range ? field.min : -kUnbounded,
                                field.has_range ? field.max : kUnbounded);
        editor.number->setValue(field.number);
        form_layout->addRow(label, editor.number);
        break;
      case PromptFieldKind::Bool:
        editor.flag = new QCheckBox(label, &dialog);
        editor.flag->setChecked(field.flag);
        form_layout->addRow(editor.flag);
        break;
      case PromptFieldKind::String:
      default:
        editor.text = new QLineEdit(&dialog);
        editor.text->setText(to_qstring(field.text));
        form_layout->addRow(label, editor.text);
        break;
    }
    editors.push_back(editor);
  }

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) {
    return false;
  }

  for (std::size_t i = 0; i < form.fields().size(); ++i) {
    PromptField& field = form.fields()[i];
    const Editors& editor = editors[i];
    if (editor.number != nullptr) {
      field.number = editor.number->value();
    } else if (editor.flag != nullptr) {
      field.flag = editor.flag->isChecked();
    } else if (editor.text != nullptr) {
      field.text = editor.text->text().toUtf8().toStdString();
    }
  }
  return true;
}

std::optional<std::string> QtDialogService::open_file(const FileDialogRequest& request) {
  const QString path = QFileDialog::getOpenFileName(parent_, dialog_title(request.title),
                                                    QString(), qt_filter(request.filter));
  if (path.isEmpty()) {
    return std::nullopt;
  }
  return path.toUtf8().toStdString();
}

std::optional<std::string> QtDialogService::save_file(const FileDialogRequest& request) {
  const QString path = QFileDialog::getSaveFileName(parent_, dialog_title(request.title),
                                                    to_qstring(request.default_name),
                                                    qt_filter(request.filter));
  if (path.isEmpty()) {
    return std::nullopt;
  }
  return path.toUtf8().toStdString();
}

}  // namespace tac::qt
