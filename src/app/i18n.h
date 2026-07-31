#pragma once

#include <QString>
#include <QStringList>

namespace tamias {

// Preference value meaning "follow the OS locale".
[[nodiscard]] QString system_ui_language();

// Default preference: system.
[[nodiscard]] QString default_ui_language();

// Maps a preference (system / zh_CN / en / …) to a concrete catalog code.
[[nodiscard]] QString resolve_ui_language(const QString& language);

[[nodiscard]] QStringList available_ui_languages();
[[nodiscard]] QString ui_language_display_name(const QString& language);

// Installs app + Qt base translators and sets the default QLocale.
// English uses source strings (no app .qm). Returns false if a non-English
// catalog failed to load.
bool apply_ui_language(const QString& language);

}  // namespace tamias
