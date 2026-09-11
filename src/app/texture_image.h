#pragma once

#include "engine/core/result.h"
#include "engine/render/texture_asset.h"

#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>

class QByteArray;

namespace tamias {

Result<TextureAsset> texture_from_qimage(const QImage& image, std::string name, TextureUsage usage,
                                         bool srgb, std::string source_path = {});
Result<TextureAsset> load_texture_image(const QString& path, TextureUsage usage, bool srgb);
Result<TextureAsset> decode_texture_image(const QByteArray& bytes, std::string name,
                                          TextureUsage usage, bool srgb);
QPixmap texture_thumbnail(const TextureAsset& tex, QSize size);

}  // namespace tamias
