#include "app/shell/toast.h"

#include "app/base/theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <QVariantAnimation>

namespace tamias {
namespace {

constexpr int kPadH = 18;           // 左右内边距
constexpr int kPadV = 11;           // 上下内边距
constexpr int kTopMargin = 56;      // 距宿主顶边的高度（工具条下方居中偏上）
constexpr int kSideMargin = 48;     // 距宿主左右的最小留白
constexpr int kMinWidth = 180;
constexpr int kMaxWidth = 720;      // 一行再长就不好看，超过从中间省略
constexpr int kFadeInMs = 120;
constexpr int kFadeOutMs = 320;
constexpr int kDefaultDurationMs = 2800;

// 一块提示的三种颜色：底色 + 描边 + 字色（字色按底色亮度取）。
struct LevelColors {
  QColor fill;
  QColor border;
  QColor text;
};

LevelColors level_colors(ToastLevel level) {
  const bool dark = is_dark_theme();
  QColor fill;
  switch (level) {
    case ToastLevel::Warning:
      fill = dark ? QColor(0xE3, 0xA8, 0x18) : QColor(0xF2, 0xB7, 0x1B);
      break;
    case ToastLevel::Error:
      fill = dark ? QColor(0xD8, 0x50, 0x48) : QColor(0xC6, 0x35, 0x2C);
      break;
    case ToastLevel::Info:
    default:
      fill = dark ? QColor(0x3F, 0xA8, 0x5A) : QColor(0x2E, 0x9E, 0x4C);
      break;
  }
  LevelColors colors;
  colors.fill = fill;
  colors.border = fill.darker(125);
  // 黄的底色亮，配深字；绿和红配白字。
  colors.text = fill.lightness() > 160 ? QColor(0x20, 0x21, 0x24) : QColor(0xFF, 0xFF, 0xFF);
  return colors;
}

}  // namespace

Toast::Toast(QWidget* host) : QWidget(host) {
  setObjectName(QStringLiteral("toast"));
  // 提示只是「看得见」：不吃鼠标事件、不抢焦点。
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setFocusPolicy(Qt::NoFocus);

  dismiss_timer_ = new QTimer(this);
  dismiss_timer_->setSingleShot(true);
  connect(dismiss_timer_, &QTimer::timeout, this, [this] { fade_to(0.0, kFadeOutMs); });

  // 淡入淡出自己算透明度再画：不用 QGraphicsOpacityEffect，那玩意会把整块
  // 部件渲染成一张不透明的图，圆角外会漏出一圈底色。
  fade_ = new QVariantAnimation(this);
  connect(fade_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    opacity_ = value.toReal();
    update();
  });
  connect(fade_, &QVariantAnimation::finished, this, [this] {
    if (opacity_ <= 0.0) {
      hide();
    }
  });

  host->installEventFilter(this);
  hide();
}

void Toast::show_message(QWidget* host, const QString& text, ToastLevel level, int duration_ms) {
  if (host == nullptr || text.isEmpty()) {
    return;
  }
  auto* toast = host->findChild<Toast*>(QString(), Qt::FindDirectChildrenOnly);
  if (toast == nullptr) {
    toast = new Toast(host);
  }
  toast->popup(text, level, duration_ms);
}

void Toast::popup(const QString& text, ToastLevel level, int duration_ms) {
  const bool was_visible = isVisible();
  fade_->stop();
  text_ = text;
  level_ = level;
  reposition();
  // 已经浮着的时候又保存了一次（连着按 Ctrl+S）：不重新淡入，只重新计时。
  opacity_ = was_visible ? 1.0 : 0.0;
  show();
  raise();
  if (!was_visible) {
    fade_to(1.0, kFadeInMs);
  }
  update();
  dismiss_timer_->start(duration_ms > 0 ? duration_ms : kDefaultDurationMs);
}

void Toast::reposition() {
  QWidget* host = parentWidget();
  if (host == nullptr) {
    return;
  }
  const int max_width = qBound(kMinWidth, host->width() - 2 * kSideMargin, kMaxWidth);
  const QFontMetrics metrics(font());
  // 一行文字：宽度贴着字走，长了从中间省略。
  const int width = qMin(metrics.horizontalAdvance(text_) + 2 * kPadH, max_width);
  resize(width, metrics.height() + 2 * kPadV);
  move(qMax(0, (host->width() - width) / 2), kTopMargin);
}

void Toast::fade_to(qreal to, int duration_ms) {
  fade_->stop();
  fade_->setDuration(duration_ms);
  fade_->setStartValue(opacity_);
  fade_->setEndValue(to);
  fade_->start();
}

void Toast::paintEvent(QPaintEvent*) {
  if (opacity_ <= 0.0) {
    return;
  }
  const LevelColors colors = level_colors(level_);
  QColor fill = colors.fill;
  fill.setAlphaF(opacity_);
  QColor border = colors.border;
  border.setAlphaF(opacity_);
  QColor text_color = colors.text;
  text_color.setAlphaF(opacity_);

  QPainter painter(this);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  // 画满整个矩形、方角：画布和首页都是浅色，圆角会把底下的浅色露出来，
  // 看着就像「四角发白」。方角实心就一点缝都不漏。
  painter.fillRect(rect(), fill);
  painter.setPen(QPen(border, 1.0));
  painter.drawRect(rect().adjusted(0, 0, -1, -1));

  painter.setFont(font());
  painter.setPen(text_color);
  painter.drawText(rect(), Qt::AlignCenter,
                   QFontMetrics(font()).elidedText(text_, Qt::ElideMiddle,
                                                   qMax(0, width() - 2 * kPadH)));
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
  if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
    reposition();
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace tamias
