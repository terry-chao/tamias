#include "section_preview_widget.h"

#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr int kPadL = 12;
constexpr int kPadT = 28;
constexpr int kPadR = 52;
constexpr int kPadB = 36;
constexpr int kChipW = 72;
constexpr int kChipH = 22;

double clamp_positive(double v, double fallback) {
  return v > 1e-6 ? v : fallback;
}

QString format_value(double v, int decimals) {
  return QString::number(v, 'f', decimals);
}

}  // namespace

SectionPreviewWidget::SectionPreviewWidget(QWidget* parent) : QWidget(parent) {
  setMinimumSize(200, 180);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SectionPreviewWidget::set_section(const SectionPreviewSpec& spec,
                                       const std::vector<ParamSpec>& params) {
  spec_ = spec;
  params_ = params;
  values_.clear();
  for (const ParamSpec& p : params_) {
    values_[p.key.toStdString()] = p.def;
  }
  rebuild_editors();
  update();
}

void SectionPreviewWidget::set_value(const QString& key, double value) {
  values_[key.toStdString()] = value;
  for (DimEditor& dim : editors_) {
    if (dim.key != key) {
      continue;
    }
    const QSignalBlocker block(dim.spin);
    dim.spin->setValue(value);
    update_chip_text(dim);
  }
  layout_editors();
  update();
}

double SectionPreviewWidget::value(const QString& key, double fallback) const {
  const auto it = values_.find(key.toStdString());
  return it == values_.end() ? fallback : it->second;
}

std::unordered_map<std::string, double> SectionPreviewWidget::values() const {
  return values_;
}

const ParamSpec* SectionPreviewWidget::find_param(const QString& key) const {
  for (const ParamSpec& p : params_) {
    if (p.key == key) {
      return &p;
    }
  }
  return nullptr;
}

void SectionPreviewWidget::rebuild_editors() {
  for (DimEditor& dim : editors_) {
    delete dim.chip;
    delete dim.spin;
  }
  editors_.clear();
  editing_ = false;
  if (!spec_.is_valid()) {
    hide();
    return;
  }
  show();

  for (const QString& key : spec_.param_keys()) {
    DimEditor dim;
    dim.key = key;
    if (const ParamSpec* p = find_param(key)) {
      dim.param = *p;
    } else {
      dim.param.key = key;
      dim.param.def = 0.4;
      dim.param.min = 0.01;
      dim.param.max = 1.0e6;
      dim.param.step = 0.05;
      dim.param.decimals = 3;
    }
    dim.chip = new QPushButton(this);
    dim.chip->setCursor(Qt::PointingHandCursor);
    dim.chip->setFocusPolicy(Qt::NoFocus);
    dim.chip->setFixedSize(kChipW, kChipH);
    dim.chip->setToolTip(dim.param.label);
    dim.chip->setStyleSheet(
        QStringLiteral("QPushButton { background:#ffffff; border:1px solid #c4c7c5;"
                       " border-radius:3px; font-size:11px; color:#202124; }"
                       "QPushButton:hover { border-color:#1a73e8; color:#1a73e8; }"));
    dim.spin = new QDoubleSpinBox(this);
    dim.spin->setRange(dim.param.min, dim.param.max);
    dim.spin->setDecimals(dim.param.decimals);
    dim.spin->setSingleStep(dim.param.step);
    dim.spin->setKeyboardTracking(false);
    dim.spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    dim.spin->setFixedSize(kChipW + 8, kChipH);
    dim.spin->hide();
    dim.spin->setValue(value(key, dim.param.def));
    update_chip_text(dim);

    connect(dim.chip, &QPushButton::clicked, this, [this, key]() {
      for (DimEditor& e : editors_) {
        if (e.key == key) {
          begin_edit(e);
          break;
        }
      }
    });
    connect(dim.spin, &QDoubleSpinBox::valueChanged, this, [this, key](double v) {
      values_[key.toStdString()] = v;
      layout_editors();
      update();
      emit param_edited(key, v);
    });
    connect(dim.spin, &QDoubleSpinBox::editingFinished, this, [this, key]() {
      for (DimEditor& e : editors_) {
        if (e.key == key) {
          commit_edit(e);
          break;
        }
      }
    });
    editors_.push_back(dim);
  }
  layout_editors();
}

void SectionPreviewWidget::begin_edit(DimEditor& dim) {
  editing_ = true;
  dim.chip->hide();
  dim.spin->setGeometry(dim.chip->geometry().adjusted(0, 0, 8, 0));
  dim.spin->show();
  dim.spin->setFocus();
  dim.spin->selectAll();
}

void SectionPreviewWidget::commit_edit(DimEditor& dim) {
  values_[dim.key.toStdString()] = dim.spin->value();
  update_chip_text(dim);
  dim.spin->hide();
  dim.chip->show();
  editing_ = false;
  update();
}

void SectionPreviewWidget::update_chip_text(DimEditor& dim) {
  dim.chip->setText(format_value(value(dim.key, dim.param.def), dim.param.decimals));
}

QRectF SectionPreviewWidget::world_bounds() const {
  const auto v = [this](const QString& key, double fb) {
    return clamp_positive(value(key, fb), fb);
  };
  switch (spec_.kind) {
    case SectionPreviewKind::Circle: {
      const double d = v(spec_.diameter_key, 0.4);
      return {-d * 0.5, -d * 0.5, d, d};
    }
    case SectionPreviewKind::Tee:
    case SectionPreviewKind::IBeam: {
      const double fw = v(spec_.flange_width_key, 0.4);
      const double h = v(spec_.height_key, 0.5);
      return {-fw * 0.5, 0.0, fw, h};
    }
    case SectionPreviewKind::Rectangle:
    case SectionPreviewKind::Window:
    case SectionPreviewKind::Door:
    case SectionPreviewKind::Curtain:
    case SectionPreviewKind::HollowRect: {
      double w = spec_.horiz_key.isEmpty() ? 0.0 : v(spec_.horiz_key, 0.4);
      double h = spec_.vert_key.isEmpty() ? 0.0 : v(spec_.vert_key, 0.4);
      if (w <= 1e-6 && h > 1e-6) {
        w = std::max(h * 4.0, 0.8);  // 板：示意长度，厚度才是参数
      }
      if (h <= 1e-6 && w > 1e-6) {
        h = std::max(w * 0.25, 0.2);
      }
      return {0.0, 0.0, w, h};
    }
    default:
      return {0.0, 0.0, 1.0, 1.0};
  }
}

QTransform SectionPreviewWidget::world_transform() const {
  const QRectF world = world_bounds();
  const QRectF view = QRectF(rect()).adjusted(kPadL, kPadT, -kPadR, -kPadB);
  if (world.width() < 1e-9 || world.height() < 1e-9 || view.width() < 1.0 || view.height() < 1.0) {
    return {};
  }
  const double s = std::min(view.width() / world.width(), view.height() / world.height());
  QTransform t;
  t.translate(view.center().x(), view.center().y());
  t.scale(s, -s);
  t.translate(-world.center().x(), -world.center().y());
  return t;
}

QPointF SectionPreviewWidget::to_widget(QPointF world) const {
  return world_transform().map(world);
}

void SectionPreviewWidget::draw_h_dim(QPainter& painter, QPointF a, QPointF b, bool above) const {
  painter.save();
  painter.setPen(QPen(QColor(QStringLiteral("#5f6368")), 1.0));
  const QPointF wa = to_widget(a);
  const QPointF wb = to_widget(b);
  const double y = above ? std::min(wa.y(), wb.y()) - 10.0 : std::max(wa.y(), wb.y()) + 10.0;
  painter.drawLine(QPointF(wa.x(), wa.y()), QPointF(wa.x(), y + (above ? -4 : 4)));
  painter.drawLine(QPointF(wb.x(), wb.y()), QPointF(wb.x(), y + (above ? -4 : 4)));
  painter.drawLine(QPointF(wa.x(), y), QPointF(wb.x(), y));
  painter.restore();
}

void SectionPreviewWidget::draw_v_dim(QPainter& painter, QPointF a, QPointF b,
                                      const QString& /*key*/) const {
  painter.save();
  painter.setPen(QPen(QColor(QStringLiteral("#5f6368")), 1.0));
  const QPointF wa = to_widget(a);
  const QPointF wb = to_widget(b);
  const double x = std::max(wa.x(), wb.x()) + 10.0;
  painter.drawLine(QPointF(wa.x() + 2, wa.y()), QPointF(x + 4, wa.y()));
  painter.drawLine(QPointF(wb.x() + 2, wb.y()), QPointF(x + 4, wb.y()));
  painter.drawLine(QPointF(x, wa.y()), QPointF(x, wb.y()));
  painter.restore();
}

QRect SectionPreviewWidget::chip_rect_h(QPointF a, QPointF b, bool above) const {
  const QPointF wa = to_widget(a);
  const QPointF wb = to_widget(b);
  const double mx = (wa.x() + wb.x()) * 0.5;
  const double y = above ? std::min(wa.y(), wb.y()) - kChipH - 6
                         : std::max(wa.y(), wb.y()) + 14.0;
  return QRect(int(mx) - kChipW / 2, int(y), kChipW, kChipH);
}

QRect SectionPreviewWidget::chip_rect_v(QPointF a, QPointF b) const {
  const QPointF wa = to_widget(a);
  const QPointF wb = to_widget(b);
  const double x = std::max(wa.x(), wb.x()) + 14.0;
  const double my = (wa.y() + wb.y()) * 0.5;
  return QRect(int(x), int(my) - kChipH / 2, kChipW, kChipH);
}

void SectionPreviewWidget::layout_editors() {
  if (!spec_.is_valid() || editors_.empty()) {
    return;
  }
  const auto v = [this](const QString& key, double fb) {
    return clamp_positive(value(key, fb), fb);
  };
  auto place = [this](const QString& key, const QRect& r) {
    for (DimEditor& dim : editors_) {
      if (dim.key != key) {
        continue;
      }
      QRect clipped = r;
      clipped.moveLeft(std::clamp(clipped.x(), 0, std::max(0, width() - clipped.width())));
      clipped.moveTop(std::clamp(clipped.y(), 0, std::max(0, height() - clipped.height())));
      dim.chip->setGeometry(clipped);
      dim.spin->setGeometry(clipped.adjusted(0, 0, 8, 0));
      dim.chip->setVisible(!dim.spin->isVisible());
    }
  };

  switch (spec_.kind) {
    case SectionPreviewKind::Circle: {
      const double d = v(spec_.diameter_key, 0.4);
      const QPointF a(-d * 0.5, 0.0);
      const QPointF b(d * 0.5, 0.0);
      const QPointF wa = to_widget(a);
      const QPointF wb = to_widget(b);
      const double mx = (wa.x() + wb.x()) * 0.5;
      const double y = std::min(wa.y(), wb.y()) - kChipH - 8;
      place(spec_.diameter_key, QRect(int(mx) - kChipW / 2, int(y), kChipW, kChipH));
      break;
    }
    case SectionPreviewKind::Tee:
    case SectionPreviewKind::IBeam: {
      const double fw = v(spec_.flange_width_key, 0.4);
      const double wt = v(spec_.web_thickness_key, 0.2);
      const double h = v(spec_.height_key, 0.5);
      const double ft = v(spec_.flange_thickness_key, 0.1);
      place(spec_.flange_width_key, chip_rect_h({-fw * 0.5, h}, {fw * 0.5, h}, true));
      place(spec_.height_key, chip_rect_v({fw * 0.5, 0.0}, {fw * 0.5, h}));
      place(spec_.web_thickness_key, chip_rect_h({-wt * 0.5, 0.0}, {wt * 0.5, 0.0}, false));
      const double z = spec_.kind == SectionPreviewKind::Tee ? (h - ft * 0.5) : (ft * 0.5);
      const QPointF wa = to_widget({-fw * 0.5, z});
      place(spec_.flange_thickness_key,
            QRect(int(wa.x()) - kChipW - 4, int(wa.y()) - kChipH / 2, kChipW, kChipH));
      break;
    }
    case SectionPreviewKind::HollowRect: {
      const double th = v(spec_.horiz_key, 0.2);
      const double h = v(spec_.vert_key, 3.0);
      const double leaf = v(spec_.leaf_key, 0.08);
      place(spec_.horiz_key, chip_rect_h({0.0, 0.0}, {th, 0.0}, false));
      place(spec_.vert_key, chip_rect_v({th, 0.0}, {th, h}));
      place(spec_.leaf_key, chip_rect_h({0.0, h * 0.55}, {leaf, h * 0.55}, true));
      break;
    }
    case SectionPreviewKind::Rectangle:
    case SectionPreviewKind::Window:
    case SectionPreviewKind::Door:
    case SectionPreviewKind::Curtain: {
      const QRectF b = world_bounds();
      if (!spec_.horiz_key.isEmpty()) {
        place(spec_.horiz_key, chip_rect_h({b.left(), b.top()}, {b.right(), b.top()}, false));
      }
      if (!spec_.vert_key.isEmpty()) {
        place(spec_.vert_key, chip_rect_v({b.right(), b.top()}, {b.right(), b.bottom()}));
      }
      break;
    }
    default:
      break;
  }
}

void SectionPreviewWidget::draw_section(QPainter& painter) const {
  const auto v = [this](const QString& key, double fb) {
    return clamp_positive(value(key, fb), fb);
  };
  QPainterPath path;
  QPainterPath inner;
  switch (spec_.kind) {
    case SectionPreviewKind::Circle: {
      const double d = v(spec_.diameter_key, 0.4);
      path.addEllipse(QPointF(0.0, 0.0), d * 0.5, d * 0.5);
      break;
    }
    case SectionPreviewKind::Tee: {
      const double fw = v(spec_.flange_width_key, 0.4);
      const double wt = v(spec_.web_thickness_key, 0.2);
      const double h = v(spec_.height_key, 0.5);
      const double ft = std::min(v(spec_.flange_thickness_key, 0.1), h * 0.8);
      const double hw = fw * 0.5;
      const double ht = wt * 0.5;
      const double z_web = h - ft;
      path.moveTo(-ht, 0.0);
      path.lineTo(-ht, z_web);
      path.lineTo(-hw, z_web);
      path.lineTo(-hw, h);
      path.lineTo(hw, h);
      path.lineTo(hw, z_web);
      path.lineTo(ht, z_web);
      path.lineTo(ht, 0.0);
      path.closeSubpath();
      break;
    }
    case SectionPreviewKind::IBeam: {
      const double fw = v(spec_.flange_width_key, 0.4);
      const double wt = v(spec_.web_thickness_key, 0.2);
      const double h = v(spec_.height_key, 0.5);
      const double ft = std::min(v(spec_.flange_thickness_key, 0.1), h * 0.45);
      const double hw = fw * 0.5;
      const double ht = wt * 0.5;
      const double z_bot = ft;
      const double z_top = h - ft;
      path.moveTo(-hw, 0.0);
      path.lineTo(hw, 0.0);
      path.lineTo(hw, z_bot);
      path.lineTo(ht, z_bot);
      path.lineTo(ht, z_top);
      path.lineTo(hw, z_top);
      path.lineTo(hw, h);
      path.lineTo(-hw, h);
      path.lineTo(-hw, z_top);
      path.lineTo(-ht, z_top);
      path.lineTo(-ht, z_bot);
      path.lineTo(-hw, z_bot);
      path.closeSubpath();
      break;
    }
    case SectionPreviewKind::HollowRect: {
      const double th = v(spec_.horiz_key, 0.2);
      const double h = v(spec_.vert_key, 3.0);
      const double leaf = std::min(v(spec_.leaf_key, 0.08), th * 0.45);
      const double cap = std::min(0.1, h * 0.2);
      path.addRect(0.0, 0.0, th, h);
      inner.addRect(leaf, cap, std::max(th - 2.0 * leaf, 0.01), std::max(h - 2.0 * cap, 0.01));
      path = path.subtracted(inner);
      break;
    }
    case SectionPreviewKind::Rectangle:
    case SectionPreviewKind::Window:
    case SectionPreviewKind::Door:
    case SectionPreviewKind::Curtain: {
      const QRectF b = world_bounds();
      path.addRect(b);
      break;
    }
    default:
      break;
  }

  painter.setTransform(world_transform());
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(QColor(QStringLiteral("#c5d4e8")));
  painter.setPen(QPen(QColor(QStringLiteral("#3c4043")), 0.0));
  painter.drawPath(path);

  painter.setBrush(Qt::NoBrush);
  const QPen detail(QColor(QStringLiteral("#5f6368")), 0.0);
  painter.setPen(detail);
  if (spec_.kind == SectionPreviewKind::Window) {
    const QRectF b = world_bounds();
    painter.drawLine(QPointF(b.center().x(), b.top()), QPointF(b.center().x(), b.bottom()));
    painter.drawLine(QPointF(b.left(), b.center().y()), QPointF(b.right(), b.center().y()));
  } else if (spec_.kind == SectionPreviewKind::Door) {
    const QRectF b = world_bounds();
    painter.drawLine(QPointF(b.left() + b.width() * 0.08, b.top()),
                     QPointF(b.left() + b.width() * 0.08, b.bottom()));
    painter.setBrush(QColor(QStringLiteral("#3c4043")));
    painter.drawEllipse(QPointF(b.right() - b.width() * 0.12, b.center().y()), b.width() * 0.03,
                        b.width() * 0.03);
  } else if (spec_.kind == SectionPreviewKind::Curtain) {
    const QRectF b = world_bounds();
    painter.drawLine(QPointF(b.left() + b.width() / 3.0, b.top()),
                     QPointF(b.left() + b.width() / 3.0, b.bottom()));
    painter.drawLine(QPointF(b.left() + 2.0 * b.width() / 3.0, b.top()),
                     QPointF(b.left() + 2.0 * b.width() / 3.0, b.bottom()));
    painter.drawLine(QPointF(b.left(), b.center().y()), QPointF(b.right(), b.center().y()));
  } else if (spec_.kind == SectionPreviewKind::Circle) {
    const double d = v(spec_.diameter_key, 0.4);
    painter.drawLine(QPointF(-d * 0.5, 0.0), QPointF(d * 0.5, 0.0));
  }
  painter.setTransform(QTransform());

  switch (spec_.kind) {
    case SectionPreviewKind::Circle: {
      const double d = v(spec_.diameter_key, 0.4);
      draw_h_dim(painter, {-d * 0.5, 0.0}, {d * 0.5, 0.0}, true);
      break;
    }
    case SectionPreviewKind::Tee:
    case SectionPreviewKind::IBeam: {
      const double fw = v(spec_.flange_width_key, 0.4);
      const double wt = v(spec_.web_thickness_key, 0.2);
      const double h = v(spec_.height_key, 0.5);
      const double ft = v(spec_.flange_thickness_key, 0.1);
      draw_h_dim(painter, {-fw * 0.5, h}, {fw * 0.5, h}, true);
      draw_v_dim(painter, {fw * 0.5, 0.0}, {fw * 0.5, h}, spec_.height_key);
      draw_h_dim(painter, {-wt * 0.5, 0.0}, {wt * 0.5, 0.0}, false);
      const double z0 = spec_.kind == SectionPreviewKind::Tee ? (h - ft) : 0.0;
      const double z1 = spec_.kind == SectionPreviewKind::Tee ? h : ft;
      draw_v_dim(painter, {-fw * 0.5, z0}, {-fw * 0.5, z1}, spec_.flange_thickness_key);
      break;
    }
    case SectionPreviewKind::HollowRect: {
      const double th = v(spec_.horiz_key, 0.2);
      const double h = v(spec_.vert_key, 3.0);
      const double leaf = v(spec_.leaf_key, 0.08);
      draw_h_dim(painter, {0.0, 0.0}, {th, 0.0}, false);
      draw_v_dim(painter, {th, 0.0}, {th, h}, spec_.vert_key);
      draw_h_dim(painter, {0.0, h * 0.5}, {leaf, h * 0.5}, true);
      break;
    }
    case SectionPreviewKind::Rectangle:
    case SectionPreviewKind::Window:
    case SectionPreviewKind::Door:
    case SectionPreviewKind::Curtain: {
      const QRectF b = world_bounds();
      if (!spec_.horiz_key.isEmpty()) {
        draw_h_dim(painter, {b.left(), b.top()}, {b.right(), b.top()}, false);
      }
      if (!spec_.vert_key.isEmpty()) {
        draw_v_dim(painter, {b.right(), b.top()}, {b.right(), b.bottom()}, spec_.vert_key);
      }
      break;
    }
    default:
      break;
  }
}

void SectionPreviewWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(QStringLiteral("#dadce0")), 1.0));
  painter.setBrush(QColor(QStringLiteral("#f8f9fa")));
  painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);
  if (!spec_.is_valid()) {
    return;
  }
  draw_section(painter);
}

void SectionPreviewWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layout_editors();
}

}  // namespace tamias
