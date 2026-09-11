#include "texture_image.h"

#include <QByteArray>
#include <QFileInfo>
#include <QImage>
#include <cstring>

namespace tamias {

Result<TextureAsset> texture_from_qimage(const QImage& source, std::string name, TextureUsage usage,
                                         bool srgb, std::string source_path) {
  if (source.isNull() || source.width() <= 0 || source.height() <= 0) {
    return Err("texture image is empty");
  }
  QImage image = source.convertToFormat(QImage::Format_RGBA8888);
  TextureAsset asset{};
  asset.name = std::move(name);
  asset.source_path = std::move(source_path);
  asset.usage = usage;
  asset.width = static_cast<std::uint32_t>(image.width());
  asset.height = static_cast<std::uint32_t>(image.height());
  asset.srgb = srgb;
  asset.rgba.resize(static_cast<std::size_t>(asset.width) * asset.height * 4);
  for (std::uint32_t y = 0; y < asset.height; ++y) {
    std::memcpy(asset.rgba.data() + static_cast<std::size_t>(y) * asset.width * 4,
                image.constScanLine(static_cast<int>(y)),
                static_cast<std::size_t>(asset.width) * 4);
  }
  return asset;
}

Result<TextureAsset> load_texture_image(const QString& path, TextureUsage usage, bool srgb) {
  QImage image(path);
  if (image.isNull()) {
    return Err("failed to load image");
  }
  return texture_from_qimage(image, QFileInfo(path).completeBaseName().toStdString(), usage, srgb,
                             path.toStdString());
}

Result<TextureAsset> decode_texture_image(const QByteArray& bytes, std::string name,
                                          TextureUsage usage, bool srgb) {
  QImage image = QImage::fromData(bytes);
  if (image.isNull()) {
    return Err("failed to decode image bytes");
  }
  return texture_from_qimage(image, std::move(name), usage, srgb);
}

QPixmap texture_thumbnail(const TextureAsset& tex, QSize size) {
  const int w = static_cast<int>(tex.width);
  const int h = static_cast<int>(tex.height);
  const qsizetype expected = static_cast<qsizetype>(w) * h * 4;
  if (w <= 0 || h <= 0 || static_cast<qsizetype>(tex.rgba.size()) < expected) {
    return {};
  }
  const QImage qimage(tex.rgba.data(), w, h, w * 4, QImage::Format_RGBA8888);
  return QPixmap::fromImage(qimage.copy().scaled(size, Qt::KeepAspectRatio, Qt::FastTransformation));
}

}  // namespace tamias
