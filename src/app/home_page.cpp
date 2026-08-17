#include "home_page.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QStyleHints>
#include <QVBoxLayout>

#include <algorithm>

namespace tamias {

namespace {

constexpr int kThumbW = 200;
constexpr int kThumbH = 112;
constexpr int kGridW = 216;
constexpr int kGridH = 168;

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

QString home_page_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#homePage { background: #1e1f22; }"
        "#homeHeader {"
        "  background: #2b2d30; border-bottom: 1px solid #3c3f41;"
        "}"
        "#homeBrandTitle {"
        "  color: #efefef; font-size: 16px; font-weight: 700;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "#homeBrandSub {"
        "  color: #8c8c8c; font-size: 11px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QPushButton#homeAction {"
        "  background: #3d8bd4; color: #ffffff; border: 1px solid #3477b8;"
        "  padding: 6px 14px; border-radius: 2px; font-size: 12px; font-weight: 600;"
        "}"
        "QPushButton#homeAction:hover { background: #4a97db; }"
        "QPushButton#homeActionSecondary {"
        "  background: #3c3f41; color: #dcdcdc; border: 1px solid #4e5254;"
        "  padding: 6px 14px; border-radius: 2px; font-size: 12px;"
        "}"
        "QPushButton#homeActionSecondary:hover { background: #45494b; }"
        "#homeBody { background: #1e1f22; }"
        "#homeSection {"
        "  color: #cfcfcf; font-size: 12px; font-weight: 700;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "#homeSectionHint { color: #7a7a7a; font-size: 11px; }"
        "#homeOpenChip {"
        "  background: #2b2d30; color: #d6d6d6; border: 1px solid #3c3f41;"
        "  border-radius: 2px; padding: 6px 10px; text-align: left;"
        "}"
        "#homeOpenChip:hover { border-color: #3d8bd4; background: #323437; }"
        "QListWidget#recentGallery {"
        "  background: #1e1f22; border: none; outline: none; padding: 4px;"
        "}"
        "QListWidget#recentGallery::item {"
        "  color: #d8d8d8; background: #2b2d30; border: 1px solid #3c3f41;"
        "  border-radius: 2px; padding: 6px; margin: 4px;"
        "}"
        "QListWidget#recentGallery::item:hover {"
        "  border-color: #3d8bd4; background: #323437;"
        "}"
        "QListWidget#recentGallery::item:selected {"
        "  border-color: #3d8bd4; background: #2a3848; color: #ffffff;"
        "}"
        "#homeEmptyTitle { color: #b0b0b0; font-size: 13px; font-weight: 600; }"
        "#homeEmpty { color: #808080; font-size: 12px; }"
        "#homeVersion { color: #6e6e6e; font-size: 10px; }");
  }

  return QStringLiteral(
      "#homePage { background: #f2f2f2; }"
      "#homeHeader {"
      "  background: #ffffff; border-bottom: 1px solid #c8c8c8;"
      "}"
      "#homeBrandTitle {"
      "  color: #1f1f1f; font-size: 16px; font-weight: 700;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "#homeBrandSub {"
      "  color: #6a6a6a; font-size: 11px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QPushButton#homeAction {"
      "  background: #2a6fb0; color: #ffffff; border: 1px solid #235f96;"
      "  padding: 6px 14px; border-radius: 2px; font-size: 12px; font-weight: 600;"
      "}"
      "QPushButton#homeAction:hover { background: #3480c4; }"
      "QPushButton#homeActionSecondary {"
      "  background: #ffffff; color: #2a2a2a; border: 1px solid #bdbdbd;"
      "  padding: 6px 14px; border-radius: 2px; font-size: 12px;"
      "}"
      "QPushButton#homeActionSecondary:hover { background: #f7f7f7; }"
      "#homeBody { background: #f2f2f2; }"
      "#homeSection {"
      "  color: #2a2a2a; font-size: 12px; font-weight: 700;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "#homeSectionHint { color: #6a6a6a; font-size: 11px; }"
      "#homeOpenChip {"
      "  background: #ffffff; color: #2a2a2a; border: 1px solid #c8c8c8;"
      "  border-radius: 2px; padding: 6px 10px; text-align: left;"
      "}"
      "#homeOpenChip:hover { border-color: #2a6fb0; background: #f5f8fb; }"
      "QListWidget#recentGallery {"
      "  background: #f2f2f2; border: none; outline: none; padding: 4px;"
      "}"
      "QListWidget#recentGallery::item {"
      "  color: #2a2a2a; background: #ffffff; border: 1px solid #c8c8c8;"
      "  border-radius: 2px; padding: 6px; margin: 4px;"
      "}"
      "QListWidget#recentGallery::item:hover {"
      "  border-color: #2a6fb0; background: #f7fafc;"
      "}"
      "QListWidget#recentGallery::item:selected {"
      "  border-color: #2a6fb0; background: #e8f2fb; color: #1f1f1f;"
      "}"
      "#homeEmptyTitle { color: #3a3a3a; font-size: 13px; font-weight: 600; }"
      "#homeEmpty { color: #6a6a6a; font-size: 12px; }"
      "#homeVersion { color: #7a7a7a; font-size: 10px; }");
}

QString format_opened_time(const QDateTime& dt) {
  if (!dt.isValid()) {
    return QCoreApplication::translate("tamias::HomePage", "Unknown");
  }
  const QDate today = QDate::currentDate();
  if (dt.date() == today) {
    return QCoreApplication::translate("tamias::HomePage", "Today, %1")
        .arg(dt.time().toString(QStringLiteral("HH:mm")));
  }
  if (dt.date() == today.addDays(-1)) {
    return QCoreApplication::translate("tamias::HomePage", "Yesterday, %1")
        .arg(dt.time().toString(QStringLiteral("HH:mm")));
  }
  return QLocale().toString(dt.date(), QLocale::ShortFormat);
}

QIcon make_thumb_icon(const QString& thumbnail_path) {
  QPixmap src;
  if (!thumbnail_path.isEmpty()) {
    src.load(thumbnail_path);
  }
  if (src.isNull()) {
    src = QPixmap(QStringLiteral(":/branding/logo.png"));
    if (src.isNull()) {
      QPixmap blank(kThumbW, kThumbH);
      blank.fill(QColor(40, 42, 45));
      return QIcon(blank);
    }
    QPixmap canvas(kThumbW, kThumbH);
    canvas.fill(QColor(45, 47, 50));
    const QPixmap logo =
        src.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.drawPixmap((kThumbW - logo.width()) / 2, (kThumbH - logo.height()) / 2, logo);
    painter.end();
    return QIcon(canvas);
  }

  const QPixmap fitted =
      src.scaled(kThumbW, kThumbH, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const int x = std::max(0, (fitted.width() - kThumbW) / 2);
  const int y = std::max(0, (fitted.height() - kThumbH) / 2);
  return QIcon(fitted.copy(x, y, kThumbW, kThumbH));
}

}  // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("homePage"));
  apply_theme();
  if (QStyleHints* hints = QGuiApplication::styleHints()) {
    connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
      apply_theme();
    });
  }

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  header_ = new QFrame(this);
  header_->setObjectName(QStringLiteral("homeHeader"));
  header_->setFixedHeight(64);
  auto* header_layout = new QHBoxLayout(header_);
  header_layout->setContentsMargins(16, 10, 16, 10);
  header_layout->setSpacing(12);

  auto* logo = new QLabel(header_);
  logo->setFixedSize(36, 36);
  logo->setAlignment(Qt::AlignCenter);
  const QPixmap brand(QStringLiteral(":/branding/logo.png"));
  if (!brand.isNull()) {
    logo->setPixmap(brand.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  header_layout->addWidget(logo, 0, Qt::AlignVCenter);

  auto* brand_text = new QVBoxLayout();
  brand_text->setSpacing(0);
  brand_text->setContentsMargins(0, 0, 0, 0);
  auto* brand_title = new QLabel(QStringLiteral("Tamias"), header_);
  brand_title->setObjectName(QStringLiteral("homeBrandTitle"));
  brand_text->addWidget(brand_title);
  auto* brand_sub = new QLabel(tr("CAD Viewer"), header_);
  brand_sub->setObjectName(QStringLiteral("homeBrandSub"));
  brand_text->addWidget(brand_sub);
  header_layout->addLayout(brand_text);
  header_layout->addSpacing(20);

  auto* open_btn = new QPushButton(tr("Open…"), header_);
  open_btn->setObjectName(QStringLiteral("homeAction"));
  open_btn->setCursor(Qt::PointingHandCursor);
  open_btn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  connect(open_btn, &QPushButton::clicked, this, &HomePage::openRequested);
  header_layout->addWidget(open_btn, 0, Qt::AlignVCenter);

  auto* settings_btn = new QPushButton(tr("Settings"), header_);
  settings_btn->setObjectName(QStringLiteral("homeActionSecondary"));
  settings_btn->setCursor(Qt::PointingHandCursor);
  settings_btn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  connect(settings_btn, &QPushButton::clicked, this, &HomePage::settingsRequested);
  header_layout->addWidget(settings_btn, 0, Qt::AlignVCenter);

  header_layout->addStretch(1);
  version_label_ = new QLabel(
      tr("Version %1").arg(QCoreApplication::applicationVersion()), header_);
  version_label_->setObjectName(QStringLiteral("homeVersion"));
  header_layout->addWidget(version_label_, 0, Qt::AlignVCenter);
  root->addWidget(header_);

  auto* body = new QWidget(this);
  body->setObjectName(QStringLiteral("homeBody"));
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(18, 14, 18, 16);
  body_layout->setSpacing(12);

  open_section_ = new QWidget(body);
  auto* open_section_layout = new QVBoxLayout(open_section_);
  open_section_layout->setContentsMargins(0, 0, 0, 0);
  open_section_layout->setSpacing(6);
  auto* open_title = new QLabel(tr("Open Documents"), open_section_);
  open_title->setObjectName(QStringLiteral("homeSection"));
  open_section_layout->addWidget(open_title);
  open_host_ = new QWidget(open_section_);
  open_host_->setLayout(new QHBoxLayout());
  open_host_->layout()->setContentsMargins(0, 0, 0, 0);
  static_cast<QHBoxLayout*>(open_host_->layout())->setSpacing(8);
  open_section_layout->addWidget(open_host_);
  open_section_->setVisible(false);
  body_layout->addWidget(open_section_);

  auto* section_row = new QHBoxLayout();
  auto* section = new QLabel(tr("Recent Files"), body);
  section->setObjectName(QStringLiteral("homeSection"));
  section_row->addWidget(section);
  section_row->addStretch(1);
  auto* section_hint = new QLabel(tr("Click to open · Right-click for more"), body);
  section_hint->setObjectName(QStringLiteral("homeSectionHint"));
  section_row->addWidget(section_hint);
  body_layout->addLayout(section_row);

  recent_list_ = new QListWidget(body);
  recent_list_->setObjectName(QStringLiteral("recentGallery"));
  recent_list_->setViewMode(QListView::IconMode);
  recent_list_->setIconSize(QSize(kThumbW, kThumbH));
  recent_list_->setGridSize(QSize(kGridW, kGridH));
  recent_list_->setResizeMode(QListView::Adjust);
  recent_list_->setMovement(QListView::Static);
  recent_list_->setSpacing(8);
  recent_list_->setWordWrap(true);
  recent_list_->setUniformItemSizes(true);
  recent_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  recent_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recent_list_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(recent_list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (!item) {
      return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    const bool exists = item->data(Qt::UserRole + 1).toBool();
    if (exists) {
      emit fileActivated(path);
    } else {
      emit missingFileActivated(path);
    }
  });
  connect(recent_list_, &QListWidget::customContextMenuRequested, this,
          &HomePage::show_recent_menu);
  body_layout->addWidget(recent_list_, 1);

  empty_panel_ = new QWidget(body);
  auto* empty_layout = new QVBoxLayout(empty_panel_);
  empty_layout->setContentsMargins(8, 24, 8, 8);
  empty_layout->setSpacing(4);
  auto* empty_title = new QLabel(tr("No recent files"), empty_panel_);
  empty_title->setObjectName(QStringLiteral("homeEmptyTitle"));
  empty_layout->addWidget(empty_title);
  empty_label_ = new QLabel(
      tr("Use Open above to load OBJ / GLB / STEP / TDOC files."), empty_panel_);
  empty_label_->setObjectName(QStringLiteral("homeEmpty"));
  empty_label_->setWordWrap(true);
  empty_layout->addWidget(empty_label_);
  empty_layout->addStretch(1);
  empty_panel_->setVisible(false);
  body_layout->addWidget(empty_panel_, 1);

  root->addWidget(body, 1);
}

void HomePage::apply_theme() {
  if (applying_theme_) {
    return;
  }
  applying_theme_ = true;
  setStyleSheet(home_page_stylesheet(is_dark_theme()));
  update();
  for (QWidget* child : findChildren<QWidget*>()) {
    child->update();
  }
  applying_theme_ = false;
}

void HomePage::changeEvent(QEvent* event) {
  if (!applying_theme_ &&
      (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange)) {
    apply_theme();
  }
  QWidget::changeEvent(event);
}

void HomePage::show_recent_menu(const QPoint& pos) {
  QListWidgetItem* item = recent_list_->itemAt(pos);
  if (!item) {
    return;
  }
  const QString path = item->data(Qt::UserRole).toString();
  const bool exists = item->data(Qt::UserRole + 1).toBool();

  QMenu menu(this);
  QAction* open_act = menu.addAction(tr("Open"));
  open_act->setEnabled(exists);
  QAction* remove_act = menu.addAction(tr("Remove from recent"));
  QAction* chosen = menu.exec(recent_list_->mapToGlobal(pos));
  if (chosen == open_act) {
    emit fileActivated(path);
  } else if (chosen == remove_act) {
    emit recentRemoveRequested(path);
  }
}

void HomePage::rebuild_open_docs() {
  QLayout* layout = open_host_->layout();
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }

  open_section_->setVisible(!open_docs_.isEmpty());
  for (const OpenDocumentItem& doc : open_docs_) {
    auto* chip = new QPushButton(doc.name, open_host_);
    chip->setObjectName(QStringLiteral("homeOpenChip"));
    chip->setCursor(Qt::PointingHandCursor);
    chip->setToolTip(doc.path.isEmpty() ? doc.name : doc.path);
    const int index = doc.index;
    connect(chip, &QPushButton::clicked, this, [this, index] {
      emit openDocumentActivated(index);
    });
    layout->addWidget(chip);
  }
  static_cast<QHBoxLayout*>(layout)->addStretch(1);
}

void HomePage::set_open_documents(const QVector<OpenDocumentItem>& items) {
  open_docs_ = items;
  rebuild_open_docs();
}

void HomePage::refresh(const QVector<RecentFileItem>& items) {
  recent_list_->clear();
  const bool empty = items.isEmpty();
  empty_panel_->setVisible(empty);
  recent_list_->setVisible(!empty);

  const int count = std::min(static_cast<int>(items.size()), kMaxRecentFiles);
  for (int i = 0; i < count; ++i) {
    const RecentFileItem& item = items[i];
    const bool exists = QFileInfo::exists(item.path);
    const QString ext = QFileInfo(item.path).suffix().toUpper();
    const QString subtitle = exists ? format_opened_time(item.opened_at)
                                    : tr("Missing");
    auto* list_item = new QListWidgetItem(
        make_thumb_icon(item.thumbnail_path),
        item.name + QLatin1Char('\n') + ext + QStringLiteral(" · ") + subtitle,
        recent_list_);
    list_item->setData(Qt::UserRole, item.path);
    list_item->setData(Qt::UserRole + 1, exists);
    list_item->setToolTip(item.path);
    list_item->setSizeHint(QSize(kGridW - 8, kGridH - 8));
    if (!exists) {
      list_item->setForeground(QBrush(QColor(0xd4, 0xa0, 0x17)));
    }
  }
}

}  // namespace tamias
