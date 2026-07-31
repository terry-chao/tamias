#include "home_page.h"

#include <QCoreApplication>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

#include <functional>

namespace tamias {
namespace {

constexpr int kCardRadius = 12;

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

QString format_created_time(const QDateTime& dt) {
  return dt.isValid() ? QLocale().toString(dt.date(), QLocale::ShortFormat)
                      : QCoreApplication::translate("tamias::HomePage", "Unknown");
}

void apply_rounded_mask(QWidget* widget, int radius) {
  QPainterPath path;
  path.addRoundedRect(QRectF(widget->rect()), radius, radius);
  widget->setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void paint_rounded_card(QWidget* widget, bool missing) {
  QPainter p(widget);
  p.setRenderHint(QPainter::Antialiasing, true);
  const bool hot = widget->underMouse() || widget->hasFocus();
  const QColor bg = missing ? QColor(0x13, 0x13, 0x14)
                            : (hot ? QColor(0x1b, 0x1b, 0x1d) : QColor(0x16, 0x16, 0x17));
  const QColor border = hot ? QColor(0xe8, 0x8f, 0x4d) : QColor(0x29, 0x29, 0x2b);
  QPainterPath path;
  path.addRoundedRect(QRectF(widget->rect()).adjusted(1.0, 1.0, -1.0, -1.0),
                      kCardRadius - 1, kCardRadius - 1);
  p.fillPath(path, bg);
  p.setPen(QPen(border, 1.0));
  p.drawPath(path);
}

class CardBorderOverlay final : public QWidget {
 public:
  explicit CardBorderOverlay(QWidget* card) : QWidget(card), card_(card) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor border =
        (card_->underMouse() || card_->hasFocus()) ? QColor(0xe8, 0x8f, 0x4d)
                                                   : QColor(0x29, 0x29, 0x2b);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0),
                        kCardRadius - 1, kCardRadius - 1);
    p.setPen(QPen(border, 1.0));
    p.drawPath(path);
  }

 private:
  QWidget* card_ = nullptr;
};

class RecentCard final : public QFrame {
 public:
  using ActivationHandler = std::function<void(const QString&, bool)>;
  using RemoveHandler = std::function<void(const QString&)>;

  RecentCard(const RecentFileItem& item, bool exists, ActivationHandler on_activated,
             RemoveHandler on_removed, QWidget* parent = nullptr)
      : QFrame(parent),
        path_(item.path),
        exists_(exists),
        missing_(!exists),
        on_activated_(std::move(on_activated)),
        on_removed_(std::move(on_removed)) {
    setObjectName(QStringLiteral("recentCard"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setFrameShape(QFrame::NoFrame);
    setToolTip(item.path);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 16);
    root->setSpacing(0);

    auto* thumb = new QFrame(this);
    thumb->setObjectName(QStringLiteral("recentThumb"));
    thumb->setFixedHeight(112);
    auto* thumb_grid = new QGridLayout(thumb);
    thumb_grid->setContentsMargins(0, 0, 0, 0);
    thumb_grid->setSpacing(0);

    auto* preview = new QLabel(thumb);
    preview->setObjectName(QStringLiteral("recentPreview"));
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(112);
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QPixmap thumb_pix;
    if (!item.thumbnail_path.isEmpty()) {
      thumb_pix.load(item.thumbnail_path);
    }
    if (!thumb_pix.isNull()) {
      preview->setPixmap(thumb_pix.scaled(320, 112, Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation));
    } else {
      const QPixmap logo(QStringLiteral(":/branding/logo.png"));
      if (!logo.isNull()) {
        preview->setPixmap(logo.scaled(58, 58, Qt::KeepAspectRatio, Qt::SmoothTransformation));
      } else {
        preview->setText(QStringLiteral("3D"));
      }
    }
    thumb_grid->addWidget(preview, 0, 0);

    auto* chrome = new QWidget(thumb);
    auto* chrome_layout = new QHBoxLayout(chrome);
    chrome_layout->setContentsMargins(8, 8, 8, 8);

    auto* remove_btn = new QPushButton(QString(QChar(0x00d7)), chrome);
    remove_btn->setObjectName(QStringLiteral("recentRemove"));
    remove_btn->setFixedSize(24, 24);
    remove_btn->setCursor(Qt::PointingHandCursor);
    remove_btn->setFocusPolicy(Qt::NoFocus);
    remove_btn->setToolTip(
        QCoreApplication::translate("tamias::HomePage", "Remove from recent"));
    connect(remove_btn, &QPushButton::clicked, this, [this] {
      if (on_removed_) {
        on_removed_(path_);
      }
    });
    chrome_layout->addWidget(remove_btn, 0, Qt::AlignTop);

    chrome_layout->addStretch(1);
    auto* type = new QLabel(QFileInfo(item.path).suffix().toUpper(), chrome);
    type->setObjectName(QStringLiteral("fileType"));
    type->setAlignment(Qt::AlignCenter);
    type->setFixedWidth(44);
    type->setAttribute(Qt::WA_TransparentForMouseEvents);
    chrome_layout->addWidget(type, 0, Qt::AlignTop);
    thumb_grid->addWidget(chrome, 0, 0);
    root->addWidget(thumb);

    auto* name = new QLabel(item.name, this);
    name->setObjectName(QStringLiteral("recentName"));
    name->setWordWrap(true);
    name->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    name->setToolTip(item.path);
    name->setMinimumHeight(38);
    name->setContentsMargins(16, 14, 16, 0);
    root->addWidget(name);

    auto* location = new QLabel(QFileInfo(item.path).absolutePath(), this);
    location->setObjectName(QStringLiteral("recentPath"));
    location->setToolTip(item.path);
    location->setContentsMargins(16, 0, 16, 10);
    root->addWidget(location);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("cardDivider"));
    divider->setFixedHeight(1);
    root->addWidget(divider);

    auto* opened = new QLabel(
        QCoreApplication::translate("tamias::HomePage", "Opened  %1")
            .arg(format_opened_time(item.opened_at)),
        this);
    opened->setObjectName(QStringLiteral("recentMeta"));
    opened->setContentsMargins(16, 10, 16, 0);
    root->addWidget(opened);

    auto* created = new QLabel(
        QCoreApplication::translate("tamias::HomePage", "Created  %1")
            .arg(format_created_time(item.created_at)),
        this);
    created->setObjectName(QStringLiteral("recentMeta"));
    created->setContentsMargins(16, 3, 16, 0);
    root->addWidget(created);

    if (!exists) {
      auto* missing = new QLabel(
          QCoreApplication::translate("tamias::HomePage",
                                      "File unavailable · click to remove"),
          this);
      missing->setObjectName(QStringLiteral("recentMissing"));
      missing->setContentsMargins(16, 7, 16, 0);
      root->addWidget(missing);
    }

    border_overlay_ = new CardBorderOverlay(this);
    border_overlay_->setGeometry(rect());
    border_overlay_->raise();
  }

 protected:
  void paintEvent(QPaintEvent*) override { paint_rounded_card(this, missing_); }

  void resizeEvent(QResizeEvent* event) override {
    QFrame::resizeEvent(event);
    apply_rounded_mask(this, kCardRadius);
    border_overlay_->setGeometry(rect());
    border_overlay_->raise();
  }

  void enterEvent(QEnterEvent* event) override {
    QFrame::enterEvent(event);
    update();
    border_overlay_->update();
  }

  void leaveEvent(QEvent* event) override {
    QFrame::leaveEvent(event);
    update();
    border_overlay_->update();
  }

  void focusInEvent(QFocusEvent* event) override {
    QFrame::focusInEvent(event);
    update();
    border_overlay_->update();
  }

  void focusOutEvent(QFocusEvent* event) override {
    QFrame::focusOutEvent(event);
    update();
    border_overlay_->update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
      on_activated_(path_, exists_);
    }
    QFrame::mouseReleaseEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
      on_activated_(path_, exists_);
      return;
    }
    QFrame::keyPressEvent(event);
  }

 private:
  QString path_;
  bool exists_ = true;
  bool missing_ = false;
  CardBorderOverlay* border_overlay_ = nullptr;
  ActivationHandler on_activated_;
  RemoveHandler on_removed_;
};

class OpenDocCard final : public QFrame {
 public:
  using ActivationHandler = std::function<void(int)>;

  OpenDocCard(const OpenDocumentItem& item, ActivationHandler on_activated, QWidget* parent = nullptr)
      : QFrame(parent), index_(item.index), on_activated_(std::move(on_activated)) {
    setObjectName(QStringLiteral("openDocCard"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setFrameShape(QFrame::NoFrame);
    setToolTip(item.path.isEmpty() ? item.name : item.path);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(14);

    auto* badge =
        new QLabel(QCoreApplication::translate("tamias::HomePage", "OPEN"), this);
    badge->setObjectName(QStringLiteral("openBadge"));
    badge->setAlignment(Qt::AlignCenter);
    root->addWidget(badge, 0, Qt::AlignVCenter);

    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* name = new QLabel(item.name, this);
    name->setObjectName(QStringLiteral("recentName"));
    name->setWordWrap(true);
    text->addWidget(name);
    auto* subtitle = new QLabel(
        item.path.isEmpty()
            ? QCoreApplication::translate("tamias::HomePage", "Untitled document")
            : item.path,
        this);
    subtitle->setObjectName(QStringLiteral("recentPath"));
    subtitle->setWordWrap(true);
    text->addWidget(subtitle);
    root->addLayout(text, 1);

    auto* hint =
        new QLabel(QCoreApplication::translate("tamias::HomePage", "Continue →"), this);
    hint->setObjectName(QStringLiteral("openHint"));
    root->addWidget(hint, 0, Qt::AlignVCenter);
  }

 protected:
  void paintEvent(QPaintEvent*) override { paint_rounded_card(this, false); }

  void resizeEvent(QResizeEvent* event) override {
    QFrame::resizeEvent(event);
    apply_rounded_mask(this, kCardRadius);
  }

  void enterEvent(QEnterEvent* event) override {
    QFrame::enterEvent(event);
    update();
  }

  void leaveEvent(QEvent* event) override {
    QFrame::leaveEvent(event);
    update();
  }

  void focusInEvent(QFocusEvent* event) override {
    QFrame::focusInEvent(event);
    update();
  }

  void focusOutEvent(QFocusEvent* event) override {
    QFrame::focusOutEvent(event);
    update();
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
      on_activated_(index_);
    }
    QFrame::mouseReleaseEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
      on_activated_(index_);
      return;
    }
    QFrame::keyPressEvent(event);
  }

 private:
  int index_ = -1;
  ActivationHandler on_activated_;
};

}  // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("homePage"));
  setStyleSheet(QStringLiteral(
      "#homePage, #homeCanvas, #homeContent { background: #0c0c0d; }"
      "#hero { background: #171719; border: 1px solid #28282b; border-radius: 18px; }"
      "#heroEyebrow { color: #eaa76b; font-size: 11px; font-weight: 700; }"
      "#heroTitle { color: #faf7f2; font-size: 32px; font-weight: 700; }"
      "#heroTagline { color: #aaa6a1; font-size: 14px; }"
      "#homeSection { color: #f3efe9; font-size: 20px; font-weight: 700; }"
      "#homeSectionHint { color: #77736f; font-size: 12px; }"
      "#homeEmptyPanel { background: #151516; border: 1px solid #29292b;"
      "  border-radius: 12px; }"
      "#homeEmptyTitle { color: #eee9e2; font-size: 16px; font-weight: 600; }"
      "#homeEmpty { color: #85817d; font-size: 13px; }"
      "QPushButton#homeAction {"
      "  background: #e88f4d; color: #17110d; border: none;"
      "  padding: 12px 22px; font-weight: 700; border-radius: 8px;"
      "}"
      "QPushButton#homeAction:hover { background: #f2a163; }"
      "QPushButton#homeAction:pressed { background: #d98143; }"
      "QPushButton#homeActionSecondary {"
      "  background: #232325; color: #e9e5df;"
      "  border: 1px solid #38383b; padding: 11px 21px; border-radius: 8px;"
      "}"
      "QPushButton#homeActionSecondary:hover { background: #2d2d30; border-color: #505054; }"
      "#recentCard, #openDocCard {"
      "  background: transparent;"
      "  border: none;"
      "  min-width: 210px; min-height: 250px;"
      "}"
      "#openDocCard { min-height: 0; }"
      "QPushButton#recentRemove {"
      "  background: rgba(20, 20, 22, 180); color: #eee9e2;"
      "  border: 1px solid #444448; border-radius: 12px;"
      "  font-size: 14px; font-weight: 700; padding: 0;"
      "}"
      "QPushButton#recentRemove:hover {"
      "  background: #e88f4d; color: #17110d; border-color: #e88f4d;"
      "}"
      "#openBadge { background: #2a2118; color: #e88f4d; border: 1px solid #5a3f28;"
      "  border-radius: 4px; padding: 4px 8px; font-size: 10px; font-weight: 700; }"
      "#openHint { color: #e88f4d; font-size: 12px; font-weight: 600; }"
      "#recentThumb { background: #202022; border: none; }"
      "#recentPreview { background: transparent; }"
      "#fileType { background: #2b2b2e; color: #c9c4bd; border: 1px solid #414145;"
      "  border-radius: 4px; padding: 3px 5px; font-size: 10px; font-weight: 700; }"
      "#recentName { color: #f1ede7; font-size: 14px; font-weight: 600; }"
      "#recentPath { color: #77736f; font-size: 10px; }"
      "#cardDivider { background: #29292b; }"
      "#recentMeta { color: #99948e; font-size: 11px; }"
      "#recentMissing { color: #e8a064; font-size: 11px; }"));

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  root->addWidget(scroll);

  auto* canvas = new QWidget(scroll);
  canvas->setObjectName(QStringLiteral("homeCanvas"));
  scroll->setWidget(canvas);

  auto* canvas_layout = new QHBoxLayout(canvas);
  canvas_layout->setContentsMargins(0, 0, 0, 0);
  canvas_layout->addStretch(1);

  auto* content = new QWidget(canvas);
  content->setObjectName(QStringLiteral("homeContent"));
  content->setMaximumWidth(1180);
  content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  canvas_layout->addWidget(content, 1);
  canvas_layout->addStretch(1);

  auto* content_layout = new QVBoxLayout(content);
  content_layout->setContentsMargins(48, 38, 48, 52);
  content_layout->setSpacing(30);

  auto* hero = new QFrame(content);
  hero->setObjectName(QStringLiteral("hero"));
  hero->setMinimumHeight(202);
  auto* hero_layout = new QHBoxLayout(hero);
  hero_layout->setContentsMargins(44, 32, 42, 32);
  hero_layout->setSpacing(42);

  auto* hero_copy = new QVBoxLayout();
  hero_copy->setSpacing(7);
  auto* eyebrow = new QLabel(tr("WELCOME BACK"), hero);
  eyebrow->setObjectName(QStringLiteral("heroEyebrow"));
  hero_copy->addWidget(eyebrow);
  auto* title = new QLabel(tr("Ready to bring your next idea to life?"), hero);
  title->setObjectName(QStringLiteral("heroTitle"));
  hero_copy->addWidget(title);
  auto* tagline =
      new QLabel(tr("Pick up a recent model, open something new, or explore with a demo scene."),
                 hero);
  tagline->setObjectName(QStringLiteral("heroTagline"));
  tagline->setWordWrap(true);
  hero_copy->addWidget(tagline);
  hero_copy->addSpacing(9);

  auto* actions = new QHBoxLayout();
  actions->setSpacing(10);
  auto* open_btn = new QPushButton(tr("Open a model"), hero);
  open_btn->setObjectName(QStringLiteral("homeAction"));
  open_btn->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
  connect(open_btn, &QPushButton::clicked, this, &HomePage::openRequested);
  actions->addWidget(open_btn);
  auto* demo_btn = new QPushButton(tr("Try Alvin demo"), hero);
  demo_btn->setObjectName(QStringLiteral("homeActionSecondary"));
  connect(demo_btn, &QPushButton::clicked, this, &HomePage::newDemoRequested);
  actions->addWidget(demo_btn);
  auto* settings_btn = new QPushButton(tr("Settings"), hero);
  settings_btn->setObjectName(QStringLiteral("homeActionSecondary"));
  settings_btn->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
  connect(settings_btn, &QPushButton::clicked, this, &HomePage::settingsRequested);
  actions->addWidget(settings_btn);
  actions->addStretch(1);
  hero_copy->addLayout(actions);
  hero_layout->addLayout(hero_copy, 1);

  auto* logo = new QLabel(hero);
  logo->setFixedSize(126, 126);
  logo->setAlignment(Qt::AlignCenter);
  const QPixmap brand(QStringLiteral(":/branding/logo.png"));
  if (!brand.isNull()) {
    logo->setPixmap(brand.scaled(116, 116, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  hero_layout->addWidget(logo, 0, Qt::AlignCenter);
  content_layout->addWidget(hero);

  open_section_ = new QWidget(content);
  auto* open_section_layout = new QVBoxLayout(open_section_);
  open_section_layout->setContentsMargins(0, 0, 0, 0);
  open_section_layout->setSpacing(14);
  auto* open_row = new QHBoxLayout();
  auto* open_title = new QLabel(tr("Open now"), open_section_);
  open_title->setObjectName(QStringLiteral("homeSection"));
  open_row->addWidget(open_title);
  open_row->addStretch(1);
  auto* open_hint = new QLabel(tr("Click to return to a viewport"), open_section_);
  open_hint->setObjectName(QStringLiteral("homeSectionHint"));
  open_row->addWidget(open_hint, 0, Qt::AlignBottom);
  open_section_layout->addLayout(open_row);
  open_host_ = new QWidget(open_section_);
  open_layout_ = new QGridLayout(open_host_);
  open_layout_->setContentsMargins(0, 0, 0, 0);
  open_layout_->setHorizontalSpacing(14);
  open_layout_->setVerticalSpacing(12);
  open_section_layout->addWidget(open_host_);
  open_section_->setVisible(false);
  content_layout->addWidget(open_section_);

  auto* section_row = new QHBoxLayout();
  auto* section = new QLabel(tr("Jump back in"), content);
  section->setObjectName(QStringLiteral("homeSection"));
  section_row->addWidget(section);
  section_row->addStretch(1);
  auto* section_hint = new QLabel(tr("Recently opened"), content);
  section_hint->setObjectName(QStringLiteral("homeSectionHint"));
  section_row->addWidget(section_hint, 0, Qt::AlignBottom);
  content_layout->addLayout(section_row);

  cards_host_ = new QWidget(content);
  cards_layout_ = new QGridLayout(cards_host_);
  cards_layout_->setContentsMargins(0, 0, 0, 0);
  cards_layout_->setHorizontalSpacing(18);
  cards_layout_->setVerticalSpacing(18);
  content_layout->addWidget(cards_host_);

  empty_panel_ = new QFrame(content);
  empty_panel_->setObjectName(QStringLiteral("homeEmptyPanel"));
  auto* empty_layout = new QVBoxLayout(empty_panel_);
  empty_layout->setContentsMargins(28, 24, 28, 24);
  empty_layout->setSpacing(5);
  auto* empty_title = new QLabel(tr("A fresh start"), empty_panel_);
  empty_title->setObjectName(QStringLiteral("homeEmptyTitle"));
  empty_layout->addWidget(empty_title);
  empty_label_ =
      new QLabel(tr("Open a model and it will be ready for you here next time."), empty_panel_);
  empty_label_->setObjectName(QStringLiteral("homeEmpty"));
  empty_layout->addWidget(empty_label_);
  content_layout->addWidget(empty_panel_);
  content_layout->addStretch(1);
}

void HomePage::clear_layout(QGridLayout* layout) {
  if (!layout) {
    return;
  }
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* w = item->widget()) {
      w->deleteLater();
    }
    delete item;
  }
}

void HomePage::clear_cards() { clear_layout(cards_layout_); }

void HomePage::set_open_documents(const QVector<OpenDocumentItem>& items) {
  clear_layout(open_layout_);
  open_section_->setVisible(!items.isEmpty());
  constexpr int kColumns = 2;
  for (int column = 0; column < kColumns; ++column) {
    open_layout_->setColumnStretch(column, 1);
  }
  for (int i = 0; i < items.size(); ++i) {
    auto* card = new OpenDocCard(
        items[i],
        [this](int index) { emit openDocumentActivated(index); },
        open_host_);
    open_layout_->addWidget(card, i / kColumns, i % kColumns);
  }
}

void HomePage::refresh(const QVector<RecentFileItem>& items) {
  clear_cards();
  empty_panel_->setVisible(items.isEmpty());
  cards_host_->setVisible(!items.isEmpty());

  constexpr int kColumns = 4;
  for (int column = 0; column < kColumns; ++column) {
    cards_layout_->setColumnStretch(column, 1);
  }
  for (int i = 0; i < items.size() && i < kMaxRecentFiles; ++i) {
    const RecentFileItem& item = items[i];
    const bool exists = QFileInfo::exists(item.path);
    auto* card = new RecentCard(
        item, exists,
        [this](const QString& path, bool item_exists) {
          if (item_exists) {
            emit fileActivated(path);
          } else {
            emit missingFileActivated(path);
          }
        },
        [this](const QString& path) { emit recentRemoveRequested(path); },
        cards_host_);
    cards_layout_->addWidget(card, i / kColumns, i % kColumns);
  }
}

}  // namespace tamias
