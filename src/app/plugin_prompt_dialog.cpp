#include "plugin_prompt_dialog.h"

#include "plugin/plugin_dialog_kind.h"
#include "plugin/plugin_prompt_field_kind.h"
#include "plugin/plugin_prompt_spec.h"

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

#include <utility>
#include <vector>

namespace tamias {
namespace {

QString utf8(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

std::int32_t show_message(QWidget* parent, const PluginPromptSpec& spec, std::int32_t buttons) {
  QMessageBox box(parent);
  box.setWindowTitle(spec.title.empty() ? QStringLiteral("Tamias") : utf8(spec.title));
  box.setText(utf8(spec.value.empty() ? spec.label : spec.value));
  box.setIcon(QMessageBox::Information);
  switch (buttons) {
    case 1:
      box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
      box.setDefaultButton(QMessageBox::Ok);
      break;
    case 2:
      box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      box.setDefaultButton(QMessageBox::Yes);
      break;
    case 3:
      box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
      box.setDefaultButton(QMessageBox::Yes);
      break;
    default:
      box.setStandardButtons(QMessageBox::Ok);
      box.setDefaultButton(QMessageBox::Ok);
      break;
  }
  switch (box.exec()) {
    case QMessageBox::Ok:
      return 1;
    case QMessageBox::Cancel:
      return 2;
    case QMessageBox::Yes:
      return 3;
    case QMessageBox::No:
      return 4;
    default:
      return 2;
  }
}

std::int32_t show_string(QWidget* parent, const PluginPromptSpec& spec, std::string& out) {
  bool ok = false;
  const QString value = QInputDialog::getText(
      parent, utf8(spec.title), utf8(spec.label), QLineEdit::Normal, utf8(spec.value), &ok);
  if (!ok) {
    return 1;
  }
  out = value.toUtf8().toStdString();
  return 0;
}

std::int32_t show_number(QWidget* parent, const PluginPromptSpec& spec, std::string& out) {
  bool ok = false;
  const double min = spec.has_min ? spec.min : -1.0e9;
  const double max = spec.has_max ? spec.max : 1.0e9;
  const double value = QInputDialog::getDouble(
      parent, utf8(spec.title), utf8(spec.label), spec.number, min, max, 4, &ok);
  if (!ok) {
    return 1;
  }
  out = QString::number(value, 'g', 17).toStdString();
  return 0;
}

QString qt_filter(const std::string& filter) {
  return filter.empty() ? QStringLiteral("All files (*.*)") : utf8(filter);
}

std::int32_t show_open(QWidget* parent, const PluginPromptSpec& spec, std::string& out) {
  const QString path = QFileDialog::getOpenFileName(parent, utf8(spec.title), QString(),
                                                    qt_filter(spec.filter));
  if (path.isEmpty()) {
    return 1;
  }
  out = path.toUtf8().toStdString();
  return 0;
}

std::int32_t show_save(QWidget* parent, const PluginPromptSpec& spec, std::string& out) {
  const QString path = QFileDialog::getSaveFileName(
      parent, utf8(spec.title), utf8(spec.default_name), qt_filter(spec.filter));
  if (path.isEmpty()) {
    return 1;
  }
  out = path.toUtf8().toStdString();
  return 0;
}

std::int32_t show_form(QWidget* parent, PluginPromptSpec spec, std::string& out) {
  if (spec.fields.empty()) {
    return -1;
  }
  QDialog dialog(parent);
  dialog.setWindowTitle(spec.title.empty() ? QStringLiteral("Tamias") : utf8(spec.title));
  auto* layout = new QVBoxLayout(&dialog);
  auto* form = new QFormLayout();
  layout->addLayout(form);

  struct Editors {
    QLineEdit* text = nullptr;
    QDoubleSpinBox* number = nullptr;
    QCheckBox* flag = nullptr;
  };
  std::vector<Editors> editors;
  editors.reserve(spec.fields.size());
  for (auto& field : spec.fields) {
    Editors editor;
    const QString label = utf8(field.label.empty() ? field.id : field.label);
    if (field.kind == PluginPromptFieldKind::Number) {
      editor.number = new QDoubleSpinBox(&dialog);
      editor.number->setDecimals(4);
      editor.number->setRange(field.has_range ? field.min : -1.0e9,
                              field.has_range ? field.max : 1.0e9);
      editor.number->setValue(field.number);
      form->addRow(label, editor.number);
    } else if (field.kind == PluginPromptFieldKind::Bool) {
      editor.flag = new QCheckBox(label, &dialog);
      editor.flag->setChecked(field.flag);
      form->addRow(editor.flag);
    } else {
      editor.text = new QLineEdit(&dialog);
      editor.text->setText(utf8(field.text));
      form->addRow(label, editor.text);
    }
    editors.push_back(editor);
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) {
    return 1;
  }
  for (std::size_t i = 0; i < spec.fields.size(); ++i) {
    auto& field = spec.fields[i];
    const auto& editor = editors[i];
    if (editor.number != nullptr) {
      field.number = editor.number->value();
    } else if (editor.flag != nullptr) {
      field.flag = editor.flag->isChecked();
    } else if (editor.text != nullptr) {
      field.text = editor.text->text().toUtf8().toStdString();
    }
  }
  out = serialize_plugin_form_values(spec);
  return 0;
}

}  // namespace

std::int32_t show_plugin_dialog(QWidget* parent, std::int32_t kind, std::int32_t buttons,
                                std::string_view spec_text, std::string& out) {
  auto parsed = parse_plugin_prompt_spec(spec_text);
  if (!parsed) {
    return -1;
  }
  switch (static_cast<PluginDialogKind>(kind)) {
    case PluginDialogKind::Message:
      return show_message(parent, *parsed, buttons);
    case PluginDialogKind::PromptString:
      return show_string(parent, *parsed, out);
    case PluginDialogKind::PromptNumber:
      return show_number(parent, *parsed, out);
    case PluginDialogKind::OpenFile:
      return show_open(parent, *parsed, out);
    case PluginDialogKind::SaveFile:
      return show_save(parent, *parsed, out);
    case PluginDialogKind::Form:
      return show_form(parent, std::move(*parsed), out);
  }
  return -1;
}

}  // namespace tamias
