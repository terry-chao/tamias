#include "about_dialog.h"

#include "tamias_version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QString>
#include <QStyleHints>
#include <QVBoxLayout>

namespace tamias {
namespace {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    if (hints->colorScheme() == Qt::ColorScheme::Dark) {
      return true;
    }
    if (hints->colorScheme() == Qt::ColorScheme::Light) {
      return false;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString dialog_stylesheet(bool dark) {
  const QString text = dark ? QStringLiteral("#e8e8e8")
                            : QStringLiteral("#202020");
  const QString muted = dark ? QStringLiteral("#a8adb3")
                             : QStringLiteral("#666666");
  const QString bg = dark ? QStringLiteral("#303030")
                          : QStringLiteral("#f3f3f3");
  return QStringLiteral(
             "#aboutDialog { background: %1; color: %2; }"
             "#aboutTitle { font-size: 22px; font-weight: 600; color: %2; }"
             "#aboutVersion { font-size: 13px; color: %3; }"
             "#aboutBody, #aboutTech { color: %3; }"
             "#aboutHeading { font-size: 12px; font-weight: 600; color: %2; }"
             "QDialogButtonBox QPushButton { min-width: 80px; padding: 5px 14px; }")
      .arg(bg, text, muted);
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("About Tamias"));
  setObjectName(QStringLiteral("aboutDialog"));
  setModal(true);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  resize(460, 420);
  setMinimumSize(400, 360);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 14);
  root->setSpacing(12);

  auto* header = new QHBoxLayout();
  header->setSpacing(14);
  auto* logo = new QLabel(this);
  logo->setFixedSize(64, 64);
  logo->setAlignment(Qt::AlignCenter);
  const QPixmap brand(QStringLiteral(":/branding/logo.png"));
  if (!brand.isNull()) {
    logo->setPixmap(
        brand.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  }
  header->addWidget(logo, 0, Qt::AlignTop);

  auto* titles = new QVBoxLayout();
  titles->setSpacing(4);
  titles->setContentsMargins(0, 4, 0, 0);
  auto* title = new QLabel(QStringLiteral("Tamias"), this);
  title->setObjectName(QStringLiteral("aboutTitle"));
  titles->addWidget(title);

  auto* version = new QLabel(
      tr("Version %1").arg(QStringLiteral(TAMIAS_VERSION_FULL)), this);
  version->setObjectName(QStringLiteral("aboutVersion"));
  version->setTextInteractionFlags(Qt::TextSelectableByMouse);
  titles->addWidget(version);

  auto* author = new QLabel(tr("Author: %1").arg(QStringLiteral("Terry")), this);
  author->setObjectName(QStringLiteral("aboutBody"));
  titles->addWidget(author);
  header->addLayout(titles, 1);
  root->addLayout(header);

  auto* blurb = new QLabel(
      tr("A geometry viewer and parametric modeling kernel spanning MCAD and BIM."),
      this);
  blurb->setObjectName(QStringLiteral("aboutBody"));
  blurb->setWordWrap(true);
  root->addWidget(blurb);

  auto* tech_heading = new QLabel(tr("Built with"), this);
  tech_heading->setObjectName(QStringLiteral("aboutHeading"));
  root->addWidget(tech_heading);

  const QString tech_lines =
      QStringLiteral("Qt %1\n"
                     "C++23\n"
                     "Vulkan / OpenGL\n"
                     "Open CASCADE Technology (OCCT)\n"
                     "IfcOpenShell\n"
                     ".NET (C# plugins)\n"
                     "CMake / vcpkg")
          .arg(QString::fromLatin1(qVersion()));
  auto* tech = new QLabel(tech_lines, this);
  tech->setObjectName(QStringLiteral("aboutTech"));
  tech->setTextInteractionFlags(Qt::TextSelectableByMouse);
  root->addWidget(tech);
  root->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  root->addWidget(buttons);

  apply_stylesheet();
}

void AboutDialog::apply_stylesheet() {
  if (applying_stylesheet_) {
    return;
  }
  applying_stylesheet_ = true;
  setStyleSheet(dialog_stylesheet(is_dark_theme()));
  applying_stylesheet_ = false;
}

void AboutDialog::changeEvent(QEvent* event) {
  if (!applying_stylesheet_ &&
      (event->type() == QEvent::ThemeChange ||
       event->type() == QEvent::PaletteChange)) {
    apply_stylesheet();
  }
  QDialog::changeEvent(event);
}

}  // namespace tamias
