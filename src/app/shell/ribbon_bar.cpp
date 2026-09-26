#include "app/shell/ribbon_bar.h"

#include "app/shell/ribbon_float_window.h"
#include "app/shell/ribbon_group.h"
#include "app/shell/ribbon_page.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPixmap>
#include <QScreen>
#include <QSize>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>

namespace tamias {
namespace {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    switch (hints->colorScheme()) {
      case Qt::ColorScheme::Dark:
        return true;
      case Qt::ColorScheme::Light:
        return false;
      case Qt::ColorScheme::Unknown:
        break;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString ribbon_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#ribbonBar { background: #2b2d30; }"
        "QMenuBar#ribbonMenuBar {"
        "  background: #2b2d30; padding: 3px 4px; border-bottom: 1px solid #3c3f41;"
        "}"
        "QMenuBar#ribbonMenuBar::item {"
        "  background: transparent; padding: 4px 10px; color: #dcdcdc;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QMenuBar#ribbonMenuBar::item:selected { background: #3c3f41; color: #ffffff; }"
        "QMenuBar#ribbonMenuBar::item:pressed { background: #45494b; }"
        "#ribbonPages, #ribbonPage, #ribbonPageContent, #ribbonPageScroll {"
        "  background: #313338;"
        "}"
        "#ribbonSectionSep { background: #3c3f41; border: none; }"
        "QToolButton#ribbonCollapse {"
        "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
        "}"
        "QToolButton#ribbonCollapse:hover {"
        "  background: #3c3f41;"
        "}"
        "QToolButton#ribbonStyleButton {"
        "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
        "}"
        "QToolButton#ribbonStyleButton:hover { background: #3c3f41; }"
        "QToolButton#ribbonStyleButton::menu-indicator { image: none; width: 0; }"
        "QToolButton#ribbonButton {"
        "  background: transparent; border: none; border-radius: 4px;"
        "  color: #dcdcdc; padding: 4px 8px 2px 8px; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QToolButton#ribbonButton:hover { background: #3c3f41; }"
        "QToolButton#ribbonButton:checked, QToolButton#ribbonButton:pressed {"
        "  background: #45494b;"
        "}"
        // 只有图标的按钮用紧内边距；后面那条 ribbonLabelLess 管「不写字、旁边却有
        // 带字按钮」的情形——外框撑到和带字按钮一样高（4 + 图标 24 + 17 = 45，
        // 加样式表自带的 3px 正好 48），图标留在顶上那一排，和旁边的图标齐平。
        "QToolButton#ribbonButton[ribbonIconOnly=\"true\"] { padding: 2px 4px; }"
        "QToolButton#ribbonButton[ribbonLabelLess=\"true\"] { padding: 4px 4px 17px 4px; }"
        "#ribbonGroup:hover { background: #3a3d41; border-radius: 4px; }"
        "#ribbonDropIndicator { background: #6cb6ff; }"
        "#ribbonGroupTitle {"
        "  color: #8c8c8c; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "#ribbonGroupSep { background: #3c3f41; border: none; max-width: 1px; }"
        "QScrollArea#ribbonPageScroll { background: transparent; border: none; }");
  }

  return QStringLiteral(
      "#ribbonBar { background: #f7f7f7; }"
      "QMenuBar#ribbonMenuBar {"
      "  background: #f7f7f7; padding: 3px 4px; border-bottom: 1px solid #e6e6e6;"
      "}"
      "QMenuBar#ribbonMenuBar::item {"
      "  background: transparent; padding: 4px 10px; color: #222222;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QMenuBar#ribbonMenuBar::item:selected { background: #e8f2fb; color: #1f1f1f; }"
      "QMenuBar#ribbonMenuBar::item:pressed { background: #d7e6f8; }"
      "#ribbonPages, #ribbonPage, #ribbonPageContent, #ribbonPageScroll {"
      "  background: #f7f7f7;"
      "}"
      "#ribbonSectionSep { background: #d8d8d8; border: none; }"
      "QToolButton#ribbonCollapse {"
      "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
      "}"
      "QToolButton#ribbonCollapse:hover {"
      "  background: #ececec;"
      "}"
      "QToolButton#ribbonStyleButton {"
      "  background: transparent; border: none; border-radius: 4px; padding: 4px;"
      "}"
      "QToolButton#ribbonStyleButton:hover { background: #ececec; }"
      "QToolButton#ribbonStyleButton::menu-indicator { image: none; width: 0; }"
      "QToolButton#ribbonButton {"
      "  background: transparent; border: none; border-radius: 4px;"
      "  color: #333333; padding: 4px 8px 2px 8px; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QToolButton#ribbonButton:hover { background: #e8e8e8; }"
      "QToolButton#ribbonButton:checked, QToolButton#ribbonButton:pressed {"
      "  background: #dadada;"
      "}"
      "QToolButton#ribbonButton[ribbonIconOnly=\"true\"] { padding: 2px 4px; }"
      "QToolButton#ribbonButton[ribbonLabelLess=\"true\"] { padding: 4px 4px 17px 4px; }"
      "#ribbonGroup:hover { background: #ececec; border-radius: 4px; }"
      "#ribbonDropIndicator { background: #1a73e8; }"
      "#ribbonGroupTitle {"
      "  color: #6a6a6a; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "#ribbonGroupSep { background: #d8d8d8; border: none; max-width: 1px; }"
      "QScrollArea#ribbonPageScroll { background: transparent; border: none; }");
}

}  // namespace

RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("ribbonBar"));
  setAttribute(Qt::WA_StyledBackground, true);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  // 分组的拖出 / 拖回都落在这个窗口上。
  setAcceptDrops(true);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // 最上面（也是唯一）那一行：菜单 + 两端的角部 widget。
  //
  // 原来菜单下面还有一条 36px 的「工具行」：品牌 + 新建 / 打开 / 保存 / 撤销 / 重做 +
  // 样式 / 卷起。撤销 / 重做 现在住进工具带的「编辑」组，新建 / 打开 / 保存 本来在
  // 「文件」组里就有，那条行整条去掉了——省下来的 36px 全给工具带。样式 / 卷起两个小
  // 按钮改挂在这条菜单栏的右端（QMenuBar 的角部 widget），一行装下，不另占高度；
  // 品牌 logo 也一并从栏里撤了，菜单直接从「文件」开始。
  //
  // 菜单本体由宿主填（见 MainWindow::build_menu_bar），RibbonBar 只管这一行的高度和配色。
  menu_bar_ = new QMenuBar(this);
  menu_bar_->setObjectName(QStringLiteral("ribbonMenuBar"));
  menu_bar_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  // 菜单栏的空白处双击也卷起 / 展开：页签和工具行都没了，这一行是唯一「点哪儿都不
  // 触发命令」的地方，拿它当双击目标最稳（落在菜单 / 按钮上的双击由它们自己吃掉）。
  menu_bar_->installEventFilter(this);

  // 菜单栏右边角部：形态切换 + 卷起。放在菜单栏末端，一眼能看见，也不占工具带的地方。
  // 左边不放东西（原来这里挂品牌小图标，去掉之后菜单直接从「文件」开始）。
  style_button_ = new QToolButton(menu_bar_);
  style_button_->setObjectName(QStringLiteral("ribbonStyleButton"));
  style_button_->setAutoRaise(true);
  style_button_->setFocusPolicy(Qt::NoFocus);
  style_button_->setCursor(Qt::PointingHandCursor);
  style_button_->setIconSize(QSize(14, 14));
  style_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  style_button_->setPopupMode(QToolButton::InstantPopup);
  style_button_->setToolTip(tr("Ribbon style"));
  style_menu_ = new QMenu(style_button_);
  style_button_->setMenu(style_menu_);
  build_style_menu();

  collapse_button_ = new QToolButton(menu_bar_);
  collapse_button_->setObjectName(QStringLiteral("ribbonCollapse"));
  collapse_button_->setAutoRaise(true);
  collapse_button_->setFocusPolicy(Qt::NoFocus);
  collapse_button_->setCursor(Qt::PointingHandCursor);
  collapse_button_->setIconSize(QSize(12, 12));
  connect(collapse_button_, &QToolButton::clicked, this, &RibbonBar::toggle_collapsed);

  auto* corner_host = new QWidget(menu_bar_);
  auto* corner_layout = new QHBoxLayout(corner_host);
  corner_layout->setContentsMargins(0, 0, 6, 0);
  corner_layout->setSpacing(2);
  corner_layout->addWidget(style_button_, 0, Qt::AlignVCenter);
  corner_layout->addWidget(collapse_button_, 0, Qt::AlignVCenter);
  menu_bar_->setCornerWidget(corner_host, Qt::TopRightCorner);

  root->addWidget(menu_bar_);

  // 工具带本体：所有分区（开始 / 视图 / 插件页…）**同时**铺在这里，一个接一个。
  // 每段各自决定要几排（见 RibbonPage::apply_rows），段与段之间一条横线。
  pages_host_ = new QWidget(this);
  pages_host_->setObjectName(QStringLiteral("ribbonPages"));
  pages_layout_ = new QVBoxLayout(pages_host_);
  pages_layout_->setContentsMargins(0, 0, 0, 0);
  pages_layout_->setSpacing(0);

  root->addWidget(pages_host_);

  if (QStyleHints* hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      apply_theme();
    });
  }

  // 浮窗拖到哪儿了要记下来：moveEvent 一次拖动会连发，歇 400ms 再让宿主存一次。
  float_save_timer_ = new QTimer(this);
  float_save_timer_->setSingleShot(true);
  float_save_timer_->setInterval(400);
  connect(float_save_timer_, &QTimer::timeout, this, [this] { emit floating_groups_changed(); });

  update_collapse_button();
  update_style_actions();
  apply_theme();
}

RibbonPage* RibbonBar::add_page(const QString& title) {
  return add_page(title.trimmed().toCaseFolded(), title);
}

RibbonPage* RibbonBar::add_page(const QString& id, const QString& title) {
  if (RibbonPage* existing = find_page(id)) {
    return existing;
  }

  const int section = static_cast<int>(page_list_.size());
  if (section > 0) {
    // 分区之间的横线：两段工具之间要有一刀，不然「开始」的最后一组和「视图」的
    // 第一组看上去像同一段里的两组。
    auto* sep = new QFrame(pages_host_);
    sep->setObjectName(QStringLiteral("ribbonSectionSep"));
    sep->setFixedHeight(1);
    sep->setFrameShape(QFrame::NoFrame);
    sep->setAttribute(Qt::WA_StyledBackground, true);
    pages_layout_->addWidget(sep);
  }

  auto* page = new RibbonPage(pages_host_);
  page->set_page_id(id);
  page->set_section_title(title);
  page->set_section_tooltip(tr("Collapse the ribbon"));
  page->set_section_accent(section_accent(section));
  page_list_.push_back(page);
  pages_layout_->addWidget(page);
  pages_by_id_.insert(id, page);
  connect(page, &RibbonPage::group_added, this, &RibbonBar::install_group_hooks);
  // 页高变了要立刻转告外层：RibbonBar 的父窗口是 QMainWindow，
  // 菜单区高度取的是 RibbonBar 的 sizeHint，不 updateGeometry 就还按旧高度留位置。
  connect(page, &RibbonPage::rows_changed, this, [this] {
    if (pages_host_ != nullptr) {
      pages_host_->updateGeometry();
    }
    updateGeometry();
  });
  // 双击分区标题栏 = 卷起 / 展开（老 Ribbon 里是双击页签，页签没了就落在它身上）。
  connect(page, &RibbonPage::section_header_double_clicked, this,
          [this] { toggle_collapsed(); });
  page->set_display_mode(display_mode_);
  // 只有一段时不画分区标题栏：一个分区没有「跟谁区分」的问题，白占 22px。
  refresh_section_chrome();
  return page;
}

RibbonPage* RibbonBar::find_page(const QString& id) const { return pages_by_id_.value(id); }

void RibbonBar::set_collapsed(bool collapsed) {
  if (collapsed_ == collapsed) {
    return;
  }
  collapsed_ = collapsed;
  pages_host_->setVisible(!collapsed_);
  update_collapse_button();
  updateGeometry();
  emit collapsed_changed(collapsed_);
}

void RibbonBar::toggle_collapsed() { set_collapsed(!collapsed_); }

void RibbonBar::update_collapse_button() {
  collapse_button_->setArrowType(collapsed_ ? Qt::DownArrow : Qt::UpArrow);
  collapse_button_->setToolTip(collapsed_ ? tr("Expand the ribbon")
                                          : tr("Collapse the ribbon"));
}

void RibbonBar::apply_theme() {
  if (applying_theme_) {
    return;
  }
  applying_theme_ = true;
  setStyleSheet(ribbon_stylesheet(is_dark_theme()));
  // 分区主色是主题相关的（深色底上用亮一档的蓝 / 青绿），换主题要把颜色重发一遍。
  refresh_section_chrome();
  applying_theme_ = false;
}

// ==== 分区外观 ====

// 分区主色：第 1 段蓝、第 2 段青绿，再往后（插件页、更多分区）从一张小表里轮着取。
// 深色主题用亮一档的同一组色，保证在深底上也看得清。
QColor RibbonBar::section_accent(int index) const {
  static const QColor kLight[] = {
      QColor(0x1a, 0x73, 0xe8), QColor(0x0f, 0x9d, 0x8c),
      QColor(0xc2, 0x6b, 0x0d), QColor(0x7a, 0x4f, 0xc9),
  };
  static const QColor kDark[] = {
      QColor(0x6c, 0xb6, 0xff), QColor(0x3d, 0xc9, 0xb6),
      QColor(0xe0, 0x9a, 0x3d), QColor(0xa9, 0x8b, 0xea),
  };
  const int count = static_cast<int>(std::size(kLight));
  const int slot = ((index % count) + count) % count;
  return is_dark_theme() ? kDark[slot] : kLight[slot];
}

void RibbonBar::refresh_section_chrome() {
  // 只有一段分区时不画标题栏与色标：那是「没有第二个分区」的情况，画了反而像装饰。
  const bool many = page_list_.size() > 1;
  for (std::size_t i = 0; i < page_list_.size(); ++i) {
    RibbonPage* page = page_list_[i];
    if (page == nullptr) {
      continue;
    }
    page->set_section_accent(section_accent(static_cast<int>(i)));
    page->set_section_chrome_visible(many);
  }
}

// ==== 两种形态 ====

void RibbonBar::build_style_menu() {
  style_group_ = new QActionGroup(this);
  style_group_->setExclusive(true);
  style_text_action_ = style_menu_->addAction(tr("Icon + text"));
  style_text_action_->setCheckable(true);
  style_text_action_->setToolTip(tr("The ribbon as it is today: every tool with its name"));
  style_group_->addAction(style_text_action_);
  style_icons_action_ = style_menu_->addAction(tr("Icon only (hover shows the name)"));
  style_icons_action_->setCheckable(true);
  style_icons_action_->setToolTip(tr("FreeCAD-like: icons only, the name appears on hover"));
  style_group_->addAction(style_icons_action_);
  connect(style_text_action_, &QAction::triggered, this,
          [this] { set_display_mode(RibbonDisplayMode::IconWithText); });
  connect(style_icons_action_, &QAction::triggered, this,
          [this] { set_display_mode(RibbonDisplayMode::IconOnly); });
  // 拖乱了想回到出厂排布：把浮动的收回、分组按默认顺序摆。
  style_menu_->addSeparator();
  QAction* reset_action = style_menu_->addAction(tr("Reset ribbon layout"));
  reset_action->setToolTip(tr("Put every group back to its factory position and dock the "
                              "floating toolbars"));
  connect(reset_action, &QAction::triggered, this, &RibbonBar::reset_layout);
}

void RibbonBar::set_style_button_icon(const QIcon& icon) {
  if (style_button_ != nullptr) {
    style_button_->setIcon(icon);
  }
}

void RibbonBar::update_style_actions() {
  const bool icon_only = display_mode_ == RibbonDisplayMode::IconOnly;
  if (style_icons_action_ != nullptr) {
    style_icons_action_->setChecked(icon_only);
  }
  if (style_text_action_ != nullptr) {
    style_text_action_->setChecked(!icon_only);
  }
}

void RibbonBar::set_display_mode(RibbonDisplayMode mode) {
  display_mode_ = mode;
  for (RibbonPage* page : pages_by_id_) {
    if (page != nullptr) {
      page->set_display_mode(mode);
    }
  }
  update_style_actions();
  updateGeometry();
  emit display_mode_changed(mode);
}

// ==== 分组拖出 / 拖回 ====

void RibbonBar::install_group_hooks(RibbonGroup* group) {
  if (group == nullptr) {
    return;
  }
  group->set_display_mode(display_mode_);
  connect(group, &RibbonGroup::drag_dropped_outside, this,
          &RibbonBar::handle_group_dropped_outside);
}

RibbonPage* RibbonBar::page_of_group(RibbonGroup* group) const {
  if (group == nullptr) {
    return nullptr;
  }
  RibbonPage* page = find_page(group->page_id());
  if (page != nullptr && page->find_group(group->group_id()) == group) {
    return page;
  }
  return nullptr;
}

bool RibbonBar::over_ribbon(const QPoint& global_pos) const {
  return rect().contains(mapFromGlobal(global_pos));
}

void RibbonBar::clamp_to_screen(RibbonFloatWindow* window) {
  if (window == nullptr) {
    return;
  }
  QScreen* screen = QGuiApplication::screenAt(window->frameGeometry().center());
  if (screen == nullptr) {
    screen = QGuiApplication::primaryScreen();
  }
  if (screen == nullptr) {
    return;
  }
  const QRect avail = screen->availableGeometry();
  QRect frame = window->frameGeometry();
  const int max_x = (std::max)(avail.left(), avail.right() - frame.width() + 1);
  const int max_y = (std::max)(avail.top(), avail.bottom() - frame.height() + 1);
  frame.moveLeft(std::clamp(frame.left(), avail.left(), max_x));
  frame.moveTop(std::clamp(frame.top(), avail.top(), max_y));
  window->move(frame.topLeft());
}

RibbonFloatWindow* RibbonBar::float_group(RibbonGroup* group, RibbonPage* page) {
  if (group == nullptr || page == nullptr) {
    return nullptr;
  }
  FloatingEntry entry;
  entry.page = page;
  // 先摘下来，浮窗 addWidget 时才不会和旧布局打架；顺手记住原位，点「收回」能回到原处。
  entry.slot = page->detach_group(group);
  entry.window = new RibbonFloatWindow(group, window());
  floating_.insert(group, entry);
  group->set_floating(true);
  connect(entry.window, &RibbonFloatWindow::dock_requested, this,
          [this](RibbonGroup* target) {
            const auto it = floating_.find(target);
            if (it != floating_.end()) {
              dock_group(target, it->page, it->slot);
            }
          });
  // 浮窗被别的方式销毁（关主窗口等）时把登记清掉，别留悬空。
  connect(entry.window, &QObject::destroyed, this,
          [this, group](QObject* source) {
            const auto it = floating_.find(group);
            // 只清自己那一条：这一组可能已经被重新拖出来了（换了个浮窗）。
            if (it != floating_.end() && static_cast<QObject*>(it->window) == source) {
              floating_.remove(group);
            }
          });
  // 浮窗被拖到别处：节流后把新位置存下来（见构造函数里的 float_save_timer_）。
  connect(entry.window, &RibbonFloatWindow::moved, this, [this] {
    if (float_save_timer_ != nullptr) {
      float_save_timer_->start();
    }
  });
  return entry.window;
}

void RibbonBar::dock_group(RibbonGroup* group, RibbonPage* page, RibbonPage::Slot slot) {
  if (group == nullptr || page == nullptr) {
    return;
  }
  const auto it = floating_.find(group);
  if (it != floating_.end()) {
    RibbonFloatWindow* window = it->window;
    if (window != nullptr) {
      window->release_group();
      window->hide();
      window->deleteLater();
    }
    floating_.erase(it);
  }
  page->insert_group(group, slot);
  group->set_floating(false);
  group->show();
  emit floating_groups_changed();
  emit layout_changed();  // 组的落点变了：排/序号要记下来
}

void RibbonBar::handle_group_dropped_outside(RibbonGroup* group,
                                             const QPoint& group_top_left) {
  if (group == nullptr) {
    return;
  }
  // 松手时指针还在 Ribbon 上（多半是 Esc 取消）：什么都不用动。
  if (over_ribbon(QCursor::pos())) {
    return;
  }
  if (const auto it = floating_.find(group); it != floating_.end()) {
    it->window->move(group_top_left - it->window->group_offset());
    clamp_to_screen(it->window);
  } else if (RibbonPage* page = page_of_group(group)) {
    if (RibbonFloatWindow* window = float_group(group, page)) {
      window->move(group_top_left - window->group_offset());
      window->show();
      window->raise();
      clamp_to_screen(window);
    }
  }
  emit floating_groups_changed();
  emit layout_changed();  // 这一组离开了页面：页面上剩下的分组位置也要记
}

QStringList RibbonBar::floating_group_keys() const {
  QStringList keys;
  for (auto it = floating_.constBegin(); it != floating_.constEnd(); ++it) {
    const RibbonGroup* group = it.key();
    const FloatingEntry& entry = it.value();
    if (group == nullptr || entry.window == nullptr) {
      continue;
    }
    const QPoint pos = entry.window->pos();
    keys.push_back(QStringLiteral("%1|%2|%3|%4")
                       .arg(group->page_id(), group->group_id())
                       .arg(pos.x())
                       .arg(pos.y()));
  }
  return keys;
}

bool RibbonBar::restore_floating_group(const QString& page_id, const QString& group_id,
                                       const QPoint& window_pos) {
  RibbonPage* page = find_page(page_id);
  RibbonGroup* group = page != nullptr ? page->find_group(group_id) : nullptr;
  if (group == nullptr || floating_.contains(group)) {
    return false;
  }
  RibbonFloatWindow* window = float_group(group, page);
  if (window == nullptr) {
    return false;
  }
  window->move(window_pos);
  window->show();
  clamp_to_screen(window);
  return true;
}

// ==== 布局记忆 ====

std::vector<RibbonPage*> RibbonBar::pages_in_order() const {
  return page_list_;
}

QStringList RibbonBar::layout_keys() const {
  QStringList keys;
  for (RibbonPage* page : pages_in_order()) {
    if (page == nullptr) {
      continue;
    }
    for (const RibbonPage::Placement& placement : page->placements()) {
      if (placement.group == nullptr) {
        continue;
      }
      keys.push_back(QStringLiteral("%1|%2|%3|%4")
                         .arg(page->page_id(), placement.group->group_id())
                         .arg(placement.slot.row)
                         .arg(placement.slot.index));
    }
  }
  return keys;
}

bool RibbonBar::apply_layout(const QStringList& keys) {
  if (keys.isEmpty()) {
    return false;
  }
  const std::vector<RibbonPage*> pages = pages_in_order();
  // 先摘光再按记录插回。不摘光的话 insert_group 里的「自己原来在左边，序号要减一」
  // 修正会把回放顺序带偏（一次回放里每个组都会被挪一次）。
  // 摘之前留一份「每一页当前的组顺序」：记录里没提到的组要按这个顺序补回去，
  // 而不是按哈希表的任意顺序（那份顺序在摘光之后就没了）。
  std::vector<std::vector<RibbonGroup*>> page_groups;
  page_groups.reserve(pages.size());
  for (RibbonPage* page : pages) {
    std::vector<RibbonGroup*> before;
    for (const RibbonPage::Placement& placement : page->placements()) {
      before.push_back(placement.group);
    }
    for (RibbonGroup* group : page->all_groups()) {
      if (group != nullptr && std::find(before.begin(), before.end(), group) == before.end()) {
        before.push_back(group);
      }
    }
    page_groups.push_back(std::move(before));
    page->detach_all_groups();
  }
  int applied = 0;
  for (const QString& entry : keys) {
    const QStringList fields = entry.split(QLatin1Char('|'));
    if (fields.size() != 4) {
      continue;
    }
    RibbonPage* page = find_page(fields[0]);
    RibbonGroup* group = page != nullptr ? page->find_group(fields[1]) : nullptr;
    // 浮动出去的组由 floating_group_keys 负责，这里跳过（否则会把它从浮窗里拽出来）。
    if (group == nullptr || floating_.contains(group)) {
      continue;
    }
    bool row_ok = false;
    bool index_ok = false;
    const int row = fields[2].toInt(&row_ok);
    const int index = fields[3].toInt(&index_ok);
    if (!row_ok || !index_ok) {
      continue;
    }
    page->insert_group(group, RibbonPage::Slot{row, index});
    ++applied;
  }
  // 记录里没有的分组（新版本新增的、插件后加的）补在末尾：旧布局不该让新工具消失。
  for (std::size_t i = 0; i < pages.size(); ++i) {
    RibbonPage* page = pages[i];
    for (RibbonGroup* group : page_groups[i]) {
      if (group == nullptr || floating_.contains(group) || page->row_of(group) >= 0) {
        continue;
      }
      page->append_group(group);
    }
  }
  return applied > 0;
}

void RibbonBar::remember_default_layout() { default_layout_keys_ = layout_keys(); }

void RibbonBar::reset_layout() {
  // 先把漂在外面的都收回原位（收回会清掉浮动登记），再按默认布局重摆。
  std::vector<RibbonGroup*> floats;
  floats.reserve(static_cast<std::size_t>(floating_.size()));
  for (auto it = floating_.constBegin(); it != floating_.constEnd(); ++it) {
    floats.push_back(it.key());
  }
  for (RibbonGroup* group : floats) {
    const auto it = floating_.find(group);
    if (it != floating_.end()) {
      dock_group(group, it->page, it->slot);
    }
  }
  (void)apply_layout(default_layout_keys_);
  emit layout_changed();
  emit floating_groups_changed();
}

RibbonGroup* RibbonBar::group_for_mime(const QMimeData* mime) const {
  if (mime == nullptr || !mime->hasFormat(QString::fromLatin1(kRibbonGroupMimeType))) {
    return nullptr;
  }
  const QString payload =
      QString::fromUtf8(mime->data(QString::fromLatin1(kRibbonGroupMimeType)));
  const int split = payload.indexOf(QLatin1Char('|'));
  if (split <= 0) {
    return nullptr;
  }
  RibbonPage* page = find_page(payload.left(split));
  return page != nullptr ? page->find_group(payload.mid(split + 1)) : nullptr;
}

void RibbonBar::dragEnterEvent(QDragEnterEvent* event) {
  RibbonGroup* group = group_for_mime(event->mimeData());
  RibbonPage* page = group != nullptr ? page_of_group(group) : nullptr;
  if (page == nullptr) {
    event->ignore();
    return;
  }
  // 拖动期间把末尾的空排亮出来当落点：不然那条排没高度，鼠标进不去。
  page->set_drop_target_visible(true);
  event->acceptProposedAction();
}

void RibbonBar::dragMoveEvent(QDragMoveEvent* event) {
  RibbonGroup* group = group_for_mime(event->mimeData());
  // **别用光标位置去认页面**：空排要先撑高才存在，而撑高要等一次布局；
  // 光标稍微偏出 pages_ 的当前几何就会认不到页面，空排永远亮不起来。
  // 拖动的是哪一组，它属于哪一页本来就知道——页面身份由组决定，光标只用来选排和排内位置。
  RibbonPage* page = group != nullptr ? page_of_group(group) : nullptr;
  if (page == nullptr) {
    hide_drop_indicator();
    event->ignore();
    return;
  }
  page->set_drop_target_visible(true);
  const QPoint content_pos =
      page->content()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
  page->show_drop_indicator(page->drop_slot_at(content_pos));
  event->acceptProposedAction();
}

void RibbonBar::dragLeaveEvent(QDragLeaveEvent* event) {
  hide_drop_indicator();
  event->accept();
}

void RibbonBar::dropEvent(QDropEvent* event) {
  RibbonGroup* group = group_for_mime(event->mimeData());
  RibbonPage* page = group != nullptr ? page_of_group(group) : nullptr;
  if (page == nullptr) {
    hide_drop_indicator();
    event->ignore();
    return;
  }
  // 落点要**先算**：hide_drop_indicator() 会把空着的落点排收回去，收完就找不到那一排了。
  const QPoint content_pos =
      page->content()->mapFromGlobal(mapToGlobal(event->position().toPoint()));
  const RibbonPage::Slot slot = page->drop_slot_at(content_pos);
  hide_drop_indicator();
  dock_group(group, page, slot);
  event->acceptProposedAction();
}

void RibbonBar::hide_drop_indicator() {
  for (RibbonPage* page : pages_by_id_) {
    if (page != nullptr) {
      page->hide_drop_indicator();
      page->set_drop_target_visible(false);  // 没落下的空排收回去
    }
  }
}

void RibbonBar::changeEvent(QEvent* event) {
  if (!applying_theme_ &&
      (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)) {
    apply_theme();
  }
  QWidget::changeEvent(event);
}

bool RibbonBar::eventFilter(QObject* watched, QEvent* event) {
  // 菜单栏空白处双击 = 卷起 / 展开（见构造函数里为什么挑这一行）。
  if (watched == menu_bar_ && event->type() == QEvent::MouseButtonDblClick) {
    toggle_collapsed();
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace tamias
