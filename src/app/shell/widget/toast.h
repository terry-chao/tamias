#pragma once

#include <QString>
#include <QWidget>

class QTimer;
class QVariantAnimation;

namespace tamias {

// 提示语义：正常=绿、警告=黄、错误=红。
enum class ToastLevel { Info, Warning, Error };

// 短暂悬浮提示（toast）：不抢焦点、不用点击，飘几秒自己淡出。
// 常驻挂在宿主部件（主窗口中央区）上，水平居中、靠上显示；一行文字，长了
// 从中间省略；只画自己那块圆角，四角透出底下的画面；不吃鼠标事件，所以提示
// 浮着的时候底下该怎么点还怎么点。
class Toast final : public QWidget {
  Q_OBJECT

 public:
  // 在 host 上飘一条消息。重复调用复用同一块提示，只重新计时。
  static void show_message(QWidget* host, const QString& text,
                           ToastLevel level = ToastLevel::Info, int duration_ms = 2800);

  explicit Toast(QWidget* host);

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void popup(const QString& text, ToastLevel level, int duration_ms);
  void reposition();
  void fade_to(qreal to, int duration_ms);

  QString text_;  // 完整文案；显示时按可用的宽度省略
  ToastLevel level_ = ToastLevel::Info;
  qreal opacity_ = 1.0;
  QTimer* dismiss_timer_ = nullptr;
  QVariantAnimation* fade_ = nullptr;
};

}  // namespace tamias
