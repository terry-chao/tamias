#pragma once

#include <cstdint>
#include <string>
#include <string_view>

class QWidget;

namespace tamias {

[[nodiscard]] std::int32_t show_plugin_dialog(QWidget* parent, std::int32_t kind,
                                              std::int32_t buttons, std::string_view spec,
                                              std::string& out);

}  // namespace tamias
