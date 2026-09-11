#pragma once

#include "section_preview_kind.h"

#include <QString>
#include <vector>

namespace tamias {

// 截面预览如何画、哪些参数画在标注上。
struct SectionPreviewSpec {
  SectionPreviewKind kind = SectionPreviewKind::None;
  QString horiz_key;      // 矩形水平向：width / thickness / length
  QString vert_key;       // 矩形竖直向：height / depth / width
  QString diameter_key;   // 圆：diameter
  QString flange_width_key = QStringLiteral("flange_width");
  QString web_thickness_key = QStringLiteral("web_thickness");
  QString height_key = QStringLiteral("height");
  QString flange_thickness_key = QStringLiteral("flange_thickness");
  QString leaf_key = QStringLiteral("leaf");

  [[nodiscard]] bool is_valid() const { return kind != SectionPreviewKind::None; }

  [[nodiscard]] std::vector<QString> param_keys() const {
    std::vector<QString> keys;
    auto push = [&](const QString& k) {
      if (!k.isEmpty()) {
        keys.push_back(k);
      }
    };
    switch (kind) {
      case SectionPreviewKind::None:
        break;
      case SectionPreviewKind::Rectangle:
      case SectionPreviewKind::Window:
      case SectionPreviewKind::Door:
      case SectionPreviewKind::Curtain:
        push(horiz_key);
        push(vert_key);
        break;
      case SectionPreviewKind::Circle:
        push(diameter_key);
        break;
      case SectionPreviewKind::Tee:
      case SectionPreviewKind::IBeam:
        push(flange_width_key);
        push(web_thickness_key);
        push(height_key);
        push(flange_thickness_key);
        break;
      case SectionPreviewKind::HollowRect:
        push(horiz_key);
        push(vert_key);
        push(leaf_key);
        break;
    }
    return keys;
  }

  [[nodiscard]] bool has_key(const QString& key) const {
    if (key.isEmpty()) {
      return false;
    }
    for (const QString& k : param_keys()) {
      if (k == key) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace tamias
