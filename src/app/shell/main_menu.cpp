// 窗口最上面那行菜单。
//
// 排法按 FreeCAD / 主流 CAD 走：文件 · 编辑 · 视图 · 工具 · 窗口 · 帮助。
// 菜单栏本体挂在 RibbonBar 上（它和"品牌 + 快速工具 + 形态 / 卷起"那一行同属一块
// widget，见 RibbonBar::menu_bar），这里只负责往里填菜单。
//
// 两条约定：
// 1. 菜单项和功能区**共用同一批 QAction**——改名、换图标、灰掉都只做一次，
//    勾选态与快捷键也只有一份。所以这里 addAction 的多半是 main_window 里建好的动作。
// 2. "最近打开"和"窗口"是动态子菜单：每次弹出前照着当前状态重建（文件删了、
//    标签页开关了都立刻反映出来）。

#include "app/shell/main_window.h"

#include "app/shell/ribbon_bar.h"

#include <QAction>
#include <QActionGroup>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QUrl>

#include <array>

namespace tamias {
namespace {

// 在线手册（mkdocs 站点，见 mkdocs.yml 的 site_url）。
constexpr char kDocumentationUrl[] = "https://terry-chao.github.io/tamias/";

}  // namespace

void MainWindow::build_menu_bar() {
  if (ribbon_ == nullptr) {
    return;
  }
  menu_bar_ = ribbon_->menu_bar();
  if (menu_bar_ == nullptr) {
    return;
  }

  // ── 文件 ────────────────────────────────────────────────────────────────
  QMenu* file_menu = menu_bar_->addMenu(tr("&File"));
  file_menu->addAction(new_action_);
  file_menu->addAction(open_action_);
  file_menu->addAction(open_drawing_action_);
  recent_menu_ = file_menu->addMenu(tr("Recent Files"));
  connect(recent_menu_, &QMenu::aboutToShow, this, &MainWindow::refresh_recent_menu);
  file_menu->addSeparator();
  file_menu->addAction(save_action_);
  file_menu->addAction(save_as_action_);
  // 渲染场景快照：把当前视口写成 .trscn（只读清单，能当"这一刻长什么样"的证据）。
  export_scene_action_ = new QAction(tr("Save Render Scene Snapshot"), this);
  export_scene_action_->setToolTip(
      tr("Write the current view to a .trscn and open it as a read-only snapshot"));
  connect(export_scene_action_, &QAction::triggered, this,
          [this] { export_render_scene(); });
  file_menu->addAction(export_scene_action_);
  file_menu->addSeparator();
  close_tab_action_ = new QAction(tr("Close Tab"), this);
  close_tab_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
  connect(close_tab_action_, &QAction::triggered, this, [this] {
    if (tabs_->count() > 0) {
      close_tab(tabs_->currentIndex());
    }
  });
  file_menu->addAction(close_tab_action_);
  file_menu->addSeparator();
  file_menu->addAction(exit_action_);

  // 标签页之间来回切（Ctrl+Tab / Ctrl+PageDown 两套惯例都收）。这两个动作挂在主窗口上，
  // 快捷键不依赖"窗口菜单被弹出过"——菜单里的那一行是每次重建时再挂进去的。
  const auto step_tab = [this](int step) {
    const int count = tabs_->count();
    if (count <= 0) {
      return;
    }
    const int next = ((tabs_->currentIndex() + step) % count + count) % count;
    activate_open_document(next);
  };
  next_tab_action_ = new QAction(tr("Next Tab"), this);
  next_tab_action_->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Tab")),
                                  QKeySequence(QStringLiteral("Ctrl+PageDown"))});
  connect(next_tab_action_, &QAction::triggered, this, [this, step_tab] { step_tab(1); });
  addAction(next_tab_action_);
  prev_tab_action_ = new QAction(tr("Previous Tab"), this);
  prev_tab_action_->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
                                  QKeySequence(QStringLiteral("Ctrl+PageUp"))});
  connect(prev_tab_action_, &QAction::triggered, this, [this, step_tab] { step_tab(-1); });
  addAction(prev_tab_action_);

  // ── 编辑 ────────────────────────────────────────────────────────────────
  // 撤销 / 重做：顶栏上那两个图标按钮就是它们（RibbonBar 的快捷区），
  // 这里再列一遍是菜单该有的样子——和 FreeCAD 的 Edit 菜单一致。
  QMenu* edit_menu = menu_bar_->addMenu(tr("&Edit"));
  edit_menu->addAction(undo_action_);
  edit_menu->addAction(redo_action_);
  edit_menu->addSeparator();
  edit_menu->addAction(move_action_);
  edit_menu->addAction(copy_action_);
  edit_menu->addAction(rotate_action_);
  edit_menu->addAction(mirror_action_);
  edit_menu->addAction(array_action_);
  edit_menu->addSeparator();
  // 偏好设置按惯例在"编辑"下（FreeCAD 也是 Edit → Preferences）。
  edit_menu->addAction(settings_action_);

  // ── 视图 ────────────────────────────────────────────────────────────────
  QMenu* view_menu = menu_bar_->addMenu(tr("&View"));
  QMenu* display_menu = view_menu->addMenu(tr("&Display Mode"));
  display_menu->addAction(wireframe_action_);
  display_menu->addAction(shaded_action_);
  display_menu->addAction(realistic_action_);
  view_menu->addAction(xray_action_);
  view_menu->addSeparator();
  view_menu->addAction(frame_all_action_);
  view_menu->addSeparator();
  view_menu->addAction(grid_action_);
  view_menu->addAction(grid_settings_action_);
  view_menu->addAction(drawing_visible_action_);
  view_menu->addAction(trace_drawing_action_);
  view_menu->addSeparator();
  QMenu* annotations_menu = view_menu->addMenu(tr("Annotations"));
  annotations_menu->addAction(label_axis_action_);
  annotations_menu->addAction(label_level_action_);
  annotations_menu->addAction(label_dimension_action_);
  view_menu->addSeparator();
  // 面板：停靠面板（属性 / 绘制设置 / 贴图 / 句柄 / 计时 / 控制台 / 调试器）
  // 和视口右侧工具列（构件 / 楼层 / 图纸）都在这里开关。
  QMenu* panels_menu = view_menu->addMenu(tr("Panels"));
  panels_menu->addAction(property_toggle_);
  panels_menu->addAction(draw_toggle_);
  panels_menu->addAction(texture_toggle_);
  panels_menu->addAction(handle_toggle_);
  panels_menu->addAction(timing_toggle_);
  panels_menu->addAction(console_toggle_);
  panels_menu->addAction(debug_scene_action_);
  panels_menu->addAction(diagnostics_action_);
  panels_menu->addSeparator();
  panels_menu->addAction(components_action_);
  panels_menu->addAction(floors_action_);
  panels_menu->addAction(floor_views_action_);
  panels_menu->addAction(drawings_action_);
  view_menu->addSeparator();
  view_menu->addAction(home_action_);  // 回欢迎页（首页）

  // ── 工具 ────────────────────────────────────────────────────────────────
  // 放"管工具的工具"：插件、以及开发/排障用的一次性动作。
  QMenu* tools_menu = menu_bar_->addMenu(tr("&Tools"));
  tools_menu->addAction(manage_action_);
  tools_menu->addSeparator();
  tools_menu->addAction(debug_scene_action_);
  tools_menu->addAction(pin_render_action_);

  // ── 窗口 ────────────────────────────────────────────────────────────────
  // 内容每次弹出前重建：打开的文档就是这里的清单（当前那个打勾）。
  window_menu_ = menu_bar_->addMenu(tr("&Window"));
  connect(window_menu_, &QMenu::aboutToShow, this, &MainWindow::refresh_window_menu);

  // ── 帮助 ────────────────────────────────────────────────────────────────
  QMenu* help_menu = menu_bar_->addMenu(tr("&Help"));
  QAction* docs_action = new QAction(tr("Documentation"), this);
  docs_action->setToolTip(tr("Open the online manual"));
  connect(docs_action, &QAction::triggered, this,
          [] { QDesktopServices::openUrl(QUrl(QString::fromLatin1(kDocumentationUrl))); });
  help_menu->addAction(docs_action);
  help_menu->addSeparator();
  help_menu->addAction(diagnostics_action_);
  help_menu->addSeparator();
  QAction* about_menu_action = new QAction(tr("About Tamias"), this);
  connect(about_menu_action, &QAction::triggered, this, &MainWindow::open_about);
  help_menu->addAction(about_menu_action);
}

void MainWindow::refresh_recent_menu() {
  if (recent_menu_ == nullptr) {
    return;
  }
  recent_menu_->clear();

  const QVector<RecentFileItem>& items = recent_.items();
  if (items.isEmpty()) {
    QAction* empty = recent_menu_->addAction(tr("No recent files"));
    empty->setEnabled(false);
    return;
  }
  for (const RecentFileItem& item : items) {
    // 文件已经不在的条目照样列出来，但点不动——比"悄悄消失"更好解释。
    const bool exists = QFileInfo::exists(item.path);
    QAction* entry = recent_menu_->addAction(item.name.isEmpty() ? item.path : item.name);
    entry->setToolTip(item.path);
    entry->setEnabled(exists);
    if (exists) {
      connect(entry, &QAction::triggered, this,
              [this, path = item.path] { open_recent_path(path); });
    }
  }
  recent_menu_->addSeparator();
  QAction* clear = recent_menu_->addAction(tr("Clear Recent Files"));
  connect(clear, &QAction::triggered, this, [this] {
    const QVector<RecentFileItem> items = recent_.items();  // remove() 会改表，先拷一份
    for (const RecentFileItem& item : items) {
      recent_.remove(item.path);
    }
    refresh_home();
  });
}

void MainWindow::refresh_window_menu() {
  if (window_menu_ == nullptr) {
    return;
  }
  window_menu_->clear();

  if (next_tab_action_ != nullptr) {
    next_tab_action_->setEnabled(tabs_->count() > 1);
    window_menu_->addAction(next_tab_action_);
  }
  if (prev_tab_action_ != nullptr) {
    prev_tab_action_->setEnabled(tabs_->count() > 1);
    window_menu_->addAction(prev_tab_action_);
  }

  window_menu_->addSeparator();
  if (tabs_->count() == 0) {
    QAction* empty = window_menu_->addAction(tr("No document open"));
    empty->setEnabled(false);
    return;
  }
  for (int i = 0; i < tabs_->count(); ++i) {
    QAction* entry =
        window_menu_->addAction(QStringLiteral("%1  %2").arg(i + 1).arg(tabs_->tabText(i)));
    entry->setCheckable(true);
    entry->setChecked(i == tabs_->currentIndex());
    connect(entry, &QAction::triggered, this, [this, i] { activate_open_document(i); });
  }
}

void MainWindow::sync_document_actions() {
  DocumentViewport* vp = current_viewport();
  const bool has_document = vp != nullptr;
  const bool can_undo = vp != nullptr && vp->session().can_undo();
  const bool can_redo = vp != nullptr && vp->session().can_redo();

  // 只对文档有意义的命令：没有文档就灰掉。这是主流 CAD / 办公软件的做法
  // （FreeCAD 里没有文档时 Save / Undo 也是灰的），比"点了才弹提示"更早把话说清楚。
  // 倒角 / 圆角 / 文字注记也在此列：它们没文档时点下去是静默无效。
  const std::array<QAction*, 19> document_actions = {
      save_action_,       save_as_action_,     export_scene_action_, move_action_,
      copy_action_,       rotate_action_,      mirror_action_,       array_action_,
      fillet_action_,     chamfer_action_,     text_action_,         frame_all_action_,
      pin_render_action_, debug_scene_action_, components_action_,   floors_action_,
      floor_views_action_, drawings_action_,   close_tab_action_,
  };
  for (QAction* action : document_actions) {
    if (action != nullptr) {
      action->setEnabled(has_document);
    }
  }
  // 构件 / 草图工具整组一起灰：没有文档时它们无处落地。原先是靠 set_create_tool
  // 那句"请先打开文档"的提示，但图标一直是亮的，看着像可用。create_group_ 里就是
  // 功能区「绘制 / 建筑 / 结构」三组的全部工具，以后加新构件也自动跟着走。
  if (create_group_ != nullptr) {
    for (QAction* action : create_group_->actions()) {
      if (action != nullptr) {
        action->setEnabled(has_document);
      }
    }
  }
  // 首页永远可点：它就是回到欢迎页，和有没有文档无关。
  if (home_action_ != nullptr) {
    home_action_->setEnabled(true);
  }
  // "关闭标签页"看的是标签页而不是文档（关掉参考图纸页也算）。
  if (close_tab_action_ != nullptr) {
    close_tab_action_->setEnabled(tabs_ != nullptr && tabs_->count() > 0);
  }
  if (undo_action_ != nullptr) {
    undo_action_->setEnabled(can_undo);
  }
  if (redo_action_ != nullptr) {
    redo_action_->setEnabled(can_redo);
  }
  // 显示模式 / 轴网 / 标注这些开关也有各自的同步函数（它们要按活跃文档回填勾选态），
  // 但那些函数原来只在"切标签页 / 新建标签页"时被调用。"关掉最后一个文档 → 回首页"
  // 走的是 sync_document_actions（见 close_tab → show_home），所以在这里补一次：
  // 否则最后一份文档关掉以后，显示模式和轴网 / 标注按钮会留在可点状态。
  sync_render_mode_actions();
  sync_bim_actions();
}

}  // namespace tamias
