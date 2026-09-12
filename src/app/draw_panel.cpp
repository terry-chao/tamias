#include "draw_panel.h"

#include "section_preview_widget.h"

#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace tamias {
namespace {

// std::unordered_map 没有 .value()（Qt API），用 find 取 spinbox。
QDoubleSpinBox* find_spin(const std::unordered_map<std::string, QDoubleSpinBox*>& map,
                           const std::string& key) {
  auto it = map.find(key);
  return it == map.end() ? nullptr : it->second;
}

QString discipline_label(Discipline d) {
  switch (d) {
    case Discipline::Architectural:
      return DrawPanel::tr("Architectural");
    case Discipline::Structural:
      return DrawPanel::tr("Structural");
    default:
      return {};
  }
}

QString discipline_style(Discipline d) {
  switch (d) {
    case Discipline::Architectural:
      return QStringLiteral("background:#e8f0fe;color:#1a73e8;padding:1px 6px;border-radius:8px;"
                            "font-size:11px;");
    case Discipline::Structural:
      return QStringLiteral("background:#fce8e6;color:#d93025;padding:1px 6px;border-radius:8px;"
                            "font-size:11px;");
    default:
      return {};
  }
}

}  // namespace

DrawPanel::DrawPanel(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(260);
  root_ = new QVBoxLayout(this);
  root_->setContentsMargins(8, 8, 8, 8);
  root_->setSpacing(6);
  set_component(ToolMode::None);
}

void DrawPanel::set_component(ToolMode mode) {
  if (current_mode_ == mode && spec_ != nullptr) {
    return;
  }
  current_mode_ = mode;
  spec_ = find_component_spec(mode);
  set_armed(false);
  rebuild_form();
}

void DrawPanel::set_armed(bool armed) {
  if (armed_ == armed) {
    return;
  }
  armed_ = armed;
  update_arm_button();
  if (!armed) {
    emit disarmed();
  }
}

void DrawPanel::clear_armed() {
  if (!armed_) {
    return;
  }
  armed_ = false;
  update_arm_button();
}

void DrawPanel::update_arm_button() {
  if (arm_button_ == nullptr) {
    return;
  }
  // 武装后按钮改成醒目的"结束绘制"：面板是显式可退出的模式，用户不必去猜 Esc/右键。
  if (armed_) {
    arm_button_->setText(tr("End Drawing"));
    arm_button_->setToolTip(tr("Leave the component tool and drop the pending command"));
    arm_button_->setStyleSheet(
        QStringLiteral("padding:6px;color:#d93025;border:1px solid #d93025;"));
  } else {
    arm_button_->setText(tr("Start Drawing"));
    arm_button_->setToolTip(tr("Arm the component tool, then pick points in the viewport"));
    arm_button_->setStyleSheet(QStringLiteral("padding:6px;"));
  }
}

void DrawPanel::rebuild_form() {
  if (content_) {
    content_->hide();
    content_->deleteLater();
    content_ = nullptr;
  }
  param_spins_.clear();
  sub_type_group_ = nullptr;
  sub_type_row_ = nullptr;
  section_label_ = nullptr;
  section_preview_ = nullptr;
  param_host_ = nullptr;
  param_form_ = nullptr;
  hint_label_ = nullptr;
  arm_button_ = nullptr;
  title_label_ = nullptr;
  discipline_label_ = nullptr;

  content_ = new QWidget(this);
  auto* column = new QVBoxLayout(content_);
  column->setContentsMargins(0, 0, 0, 0);
  column->setSpacing(8);
  root_->addWidget(content_);

  if (spec_ == nullptr) {
    auto* placeholder = new QLabel(tr("Select a component from the ribbon to start drawing"), content_);
    placeholder->setWordWrap(true);
    placeholder->setStyleSheet(QStringLiteral("color:#9aa0a6;"));
    column->addWidget(placeholder);
    column->addStretch(1);
    return;
  }

  // 标题 + 专业标签。
  auto* head = new QHBoxLayout();
  head->setSpacing(6);
  title_label_ = new QLabel(spec_->title, content_);
  title_label_->setStyleSheet(QStringLiteral("font-weight:600;font-size:14px;"));
  head->addWidget(title_label_);
  const QString disc = discipline_label(spec_->discipline);
  if (!disc.isEmpty()) {
    discipline_label_ = new QLabel(disc, content_);
    discipline_label_->setStyleSheet(discipline_style(spec_->discipline));
    head->addWidget(discipline_label_);
  }
  head->addStretch(1);
  column->addLayout(head);

  // 子类型选择。
  if (spec_->has_sub_types()) {
    auto* st_title = new QLabel(tr("Type"), content_);
    st_title->setStyleSheet(QStringLiteral("color:#9aa0a6;font-size:11px;"));
    column->addWidget(st_title);
    sub_type_row_ = new QWidget(content_);
    auto* st_layout = new QHBoxLayout(sub_type_row_);
    st_layout->setContentsMargins(0, 0, 0, 0);
    st_layout->setSpacing(4);
    sub_type_group_ = new QButtonGroup(this);
    sub_type_group_->setExclusive(true);
    const QString default_id =
        spec_->default_sub_type.isEmpty() ? spec_->sub_types.front().id : spec_->default_sub_type;
    current_sub_type_ = default_id;
    for (const auto& st : spec_->sub_types) {
      auto* btn = new QPushButton(st.label, sub_type_row_);
      btn->setCheckable(true);
      btn->setChecked(st.id == default_id);
      btn->setCursor(Qt::PointingHandCursor);
      sub_type_group_->addButton(btn);
      st_layout->addWidget(btn);
      connect(btn, &QPushButton::clicked, this, [this, id = st.id]() { on_sub_type_changed(id); });
    }
    st_layout->addStretch(1);
    column->addWidget(sub_type_row_);
  }

  section_label_ = new QLabel(tr("Section"), content_);
  section_label_->setStyleSheet(QStringLiteral("color:#9aa0a6;font-size:11px;"));
  column->addWidget(section_label_);
  section_preview_ = new SectionPreviewWidget(content_);
  section_preview_->setMinimumHeight(188);
  connect(section_preview_, &SectionPreviewWidget::param_edited, this,
          [this](const QString&, double) {
            if (armed_ && !suppress_rearm_) {
              gather_and_emit(true);
            }
          });
  column->addWidget(section_preview_);

  // 参数表单（子类型切换时整块重建，避免旧 label 叠字）。截面尺寸走预览标注。
  param_host_ = new QWidget(content_);
  column->addWidget(param_host_);
  rebuild_param_rows();

  // 提示 + 武装按钮。
  hint_label_ = new QLabel(spec_->pick_hint, content_);
  hint_label_->setWordWrap(true);
  hint_label_->setStyleSheet(QStringLiteral("color:#5f6368;font-size:11px;padding:2px 0;"));
  column->addWidget(hint_label_);

  arm_button_ = new QPushButton(tr("Start Drawing"), content_);
  arm_button_->setCursor(Qt::PointingHandCursor);
  connect(arm_button_, &QPushButton::clicked, this, &DrawPanel::arm_clicked);
  update_arm_button();
  column->addWidget(arm_button_);

  column->addStretch(1);
}

void DrawPanel::rebuild_param_rows() {
  if (param_host_ == nullptr || spec_ == nullptr) {
    return;
  }
  // 立即删掉旧控件。takeAt 只卸嵌套 layout 时 widget 仍挂在 host 上，切换子类型会叠字。
  const auto old_widgets =
      param_host_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
  for (QWidget* w : old_widgets) {
    delete w;
  }
  delete param_host_->layout();
  param_form_ = nullptr;
  param_spins_.clear();

  param_form_ = new QFormLayout(param_host_);
  param_form_->setContentsMargins(0, 0, 0, 0);
  param_form_->setSpacing(4);
  param_form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  param_form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  const SectionPreviewSpec section = spec_->section_for(current_sub_type_);
  const std::vector<ParamSpec> merged = spec_->merged_params(current_sub_type_);
  if (section_preview_ != nullptr) {
    section_preview_->set_section(section, merged);
  }
  const bool show_section = section.is_valid();
  if (section_label_ != nullptr) {
    section_label_->setVisible(show_section);
  }
  if (section_preview_ != nullptr) {
    section_preview_->setVisible(show_section);
  }

  auto add_param = [&](const ParamSpec& p) {
    if (spec_->is_section_key(p.key, current_sub_type_)) {
      return;
    }
    auto* spin = new QDoubleSpinBox(param_host_);
    spin->setRange(p.min, p.max);
    spin->setDecimals(p.decimals);
    spin->setSingleStep(p.step);
    spin->setKeyboardTracking(false);
    spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    spin->setValue(p.def);
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this](double) {
      if (armed_ && !suppress_rearm_) {
        gather_and_emit(true);
      }
    });
    param_form_->addRow(p.label, spin);
    param_spins_[p.key.toStdString()] = spin;
  };

  for (const ParamSpec& p : merged) {
    add_param(p);
  }
  param_host_->setVisible(param_form_->rowCount() > 0);
}

void DrawPanel::on_sub_type_changed(const QString& id) {
  if (current_sub_type_ == id) {
    return;
  }
  std::unordered_map<std::string, double> kept;
  if (section_preview_ != nullptr) {
    kept = section_preview_->values();
  }
  for (const auto& [key, spin] : param_spins_) {
    if (spin != nullptr) {
      kept[key] = spin->value();
    }
  }
  current_sub_type_ = id;
  rebuild_param_rows();
  suppress_rearm_ = true;
  for (const auto& [key, val] : kept) {
    const QString qkey = QString::fromStdString(key);
    if (section_preview_ != nullptr && spec_->is_section_key(qkey, current_sub_type_)) {
      section_preview_->set_value(qkey, val);
    }
    if (auto* spin = find_spin(param_spins_, key)) {
      QSignalBlocker b(spin);
      spin->setValue(val);
    }
  }
  suppress_rearm_ = false;
  if (armed_ && !suppress_rearm_) {
    gather_and_emit(true);
  }
}

CommandArgs DrawPanel::gather_args() const {
  CommandArgs args;
  if (spec_ == nullptr) {
    return args;
  }
  if (spec_->has_sub_types()) {
    args["sub_type"] = current_sub_type_.toStdString();
  }
  if (section_preview_ != nullptr) {
    for (const QString& key : section_preview_->spec().param_keys()) {
      args[key.toStdString()] = section_preview_->value(key);
    }
  }
  for (const auto& [key, spin] : param_spins_) {
    if (spin != nullptr) {
      args[key] = spin->value();
    }
  }
  return args;
}

void DrawPanel::gather_and_emit(bool arm) {
  emit armed_args(current_mode_, gather_args());
  if (arm) {
    armed_ = true;
    update_arm_button();
  }
}

void DrawPanel::arm_clicked() {
  if (spec_ == nullptr) {
    return;
  }
  if (armed_) {
    // 已在绘制中：再点一次表示结束，回到未武装状态并取消视口里的 pending。
    set_armed(false);
    return;
  }
  gather_and_emit(true);
}

}  // namespace tamias
