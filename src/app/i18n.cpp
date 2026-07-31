#include "i18n.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace tamias {
namespace {

QTranslator* app_translator() {
  static auto* translator = new QTranslator(QCoreApplication::instance());
  return translator;
}

QTranslator* qt_translator() {
  static auto* translator = new QTranslator(QCoreApplication::instance());
  return translator;
}

bool load_qt_base(QTranslator* translator, const QString& language) {
  const QString path = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
  const QStringList prefixes = {QStringLiteral("qtbase_"), QStringLiteral("qt_")};
  for (const QString& prefix : prefixes) {
    if (translator->load(prefix + language, path)) {
      return true;
    }
    // Fall back to language without country (zh_CN -> zh).
    const QString short_lang = language.section(QLatin1Char('_'), 0, 0);
    if (short_lang != language && translator->load(prefix + short_lang, path)) {
      return true;
    }
  }
  return false;
}

bool load_app(QTranslator* translator, const QString& language) {
  const QString name = QStringLiteral("tamias_") + language;
  return translator->load(QStringLiteral(":/i18n/") + name) ||
         translator->load(name, QStringLiteral(":/i18n"));
}

QString catalog_from_system() {
  if (QLocale::system().language() == QLocale::Chinese) {
    return QStringLiteral("zh_CN");
  }
  // Source language / fallback for unsupported OS locales.
  return QStringLiteral("en");
}

}  // namespace

QString system_ui_language() { return QStringLiteral("system"); }

QString default_ui_language() { return system_ui_language(); }

QString resolve_ui_language(const QString& language) {
  if (language.isEmpty() || language == system_ui_language()) {
    return catalog_from_system();
  }
  if (language.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0 ||
      language.startsWith(QStringLiteral("en_"), Qt::CaseInsensitive)) {
    return QStringLiteral("en");
  }
  if (language.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0 ||
      language.startsWith(QStringLiteral("zh_"), Qt::CaseInsensitive) ||
      language.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("zh_CN");
  }
  if (available_ui_languages().contains(language)) {
    return language;
  }
  return catalog_from_system();
}

QStringList available_ui_languages() {
  return {system_ui_language(), QStringLiteral("zh_CN"), QStringLiteral("en")};
}

QString ui_language_display_name(const QString& language) {
  if (language == system_ui_language()) {
    return QCoreApplication::translate("tamias::SettingsDialog", "System");
  }
  if (language == QStringLiteral("zh_CN")) {
    return QStringLiteral("中文");
  }
  if (language == QStringLiteral("en")) {
    return QStringLiteral("English");
  }
  return language;
}

bool apply_ui_language(const QString& language) {
  const bool follow_system =
      language.isEmpty() || language == system_ui_language();
  const QString lang = resolve_ui_language(language);

  auto* app_tr = app_translator();
  auto* qt_tr = qt_translator();
  QCoreApplication::removeTranslator(app_tr);
  QCoreApplication::removeTranslator(qt_tr);

  if (follow_system) {
    QLocale::setDefault(QLocale::system());
  } else if (lang.startsWith(QStringLiteral("zh"))) {
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
  } else {
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  }

  load_qt_base(qt_tr, lang);
  QCoreApplication::installTranslator(qt_tr);

  // Source strings are English — only install an app catalog for other locales.
  if (lang.startsWith(QStringLiteral("en"))) {
    return true;
  }

  if (!load_app(app_tr, lang)) {
    return false;
  }
  QCoreApplication::installTranslator(app_tr);
  return true;
}

}  // namespace tamias
