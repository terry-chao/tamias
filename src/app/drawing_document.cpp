#include "drawing_document.h"

#include "engine/drawing/dxf_reader.h"

#include "qt_path.h"

#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QObject>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>
#include <QTransform>

#if defined(TAMIAS_HAVE_QT_PDF)
#include <QPdfDocument>
#endif

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <map>
#include <utility>

namespace tamias {
namespace {

constexpr int kThumbnailWidth = 320;
constexpr int kThumbnailHeight = 180;
constexpr double kPi = 3.14159265358979323846;
// 缩略图/画布底色，和 3D 视口的深色底一致。
const QColor kCanvasDark(30, 33, 39);
const QColor kCanvasLight(246, 246, 248);

bool has_extension(const QString& path, const QStringList& extensions) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  return !suffix.isEmpty() && extensions.contains(suffix);
}

// DXF 文本编码：先按 UTF-8，出现替换字符再退回系统 ANSI 代码页
// （中文 Windows 上是 GBK/CP936，多数国产 CAD 出的 DXF 正是它）。
QString decode_dxf_text(const std::string& raw) {
  if (raw.empty()) {
    return {};
  }
  const QString utf8 = QString::fromUtf8(raw.data(), static_cast<int>(raw.size()));
  if (!utf8.contains(QChar::ReplacementCharacter)) {
    return utf8;
  }
  const QString local = QString::fromLocal8Bit(raw.data(), static_cast<int>(raw.size()));
  return local.contains(QChar::ReplacementCharacter) ? utf8 : local;
}

QColor to_qcolor(Vec3 c) {
  const auto channel = [](float v) {
    return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255.f));
  };
  return QColor(channel(c.x), channel(c.y), channel(c.z));
}

// 近白线在浅色底上看不见，浅底模式把它翻成黑。
QColor adapt_to_background(QColor color, bool dark_background) {
  if (dark_background) {
    return color;
  }
  if (color.red() > 230 && color.green() > 230 && color.blue() > 230) {
    return QColor(20, 20, 20);
  }
  return color;
}

// 位图/SVG 是 Y 向下的，世界是 Y 向上的：在页矩形里做一次竖直翻转。
void paint_flipped(QPainter& painter, const QRectF& rect,
                   const std::function<void(QPainter&, const QRectF&)>& body) {
  painter.save();
  painter.translate(rect.left(), rect.bottom());
  painter.scale(1.0, -1.0);
  body(painter, QRectF(0.0, 0.0, rect.width(), rect.height()));
  painter.restore();
}

}  // namespace

bool DrawingDocument::is_drawing_path(const QString& path) {
  static const QStringList kExtensions{
      QStringLiteral("pdf"), QStringLiteral("dxf"),  QStringLiteral("svg"),
      QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"),
      QStringLiteral("bmp"), QStringLiteral("tif"),  QStringLiteral("tiff"),
      QStringLiteral("gif"), QStringLiteral("webp")};
  return has_extension(path, kExtensions);
}

QString DrawingDocument::file_dialog_filter() {
  return QObject::tr(
      "Drawings (*.pdf *.dxf *.svg *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.webp);;"
      "PDF (*.pdf);;DXF (*.dxf);;Vector (*.svg);;Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.webp)");
}

std::unique_ptr<DrawingDocument> DrawingDocument::open(const QString& path, QString& error) {
  std::unique_ptr<DrawingDocument> document(new DrawingDocument());
  try {
    if (!document->load(path, error)) {
      return nullptr;
    }
  } catch (const std::exception& e) {
    // 兜底：路径编码/文件系统异常不该把主程序带走。
    error = QObject::tr("Cannot open drawing: %1").arg(QString::fromUtf8(e.what()));
    return nullptr;
  }
  return document;
}

DrawingDocument::~DrawingDocument() = default;

bool DrawingDocument::load(const QString& path, QString& error) {
  const QFileInfo info(path);
  path_ = info.absoluteFilePath();
  title_ = info.fileName();
  const QString suffix = info.suffix().toLower();

  bool ok = false;
  if (suffix == QStringLiteral("dxf")) {
    ok = load_dxf(path_, error);
  } else if (suffix == QStringLiteral("svg")) {
    ok = load_svg(path_, error);
  } else if (suffix == QStringLiteral("pdf")) {
    ok = load_pdf(path_, error);
  } else {
    ok = load_image(path_, error);
  }
  if (!ok) {
    return false;
  }
  layer_hidden_.assign(static_cast<std::size_t>(layer_count()), false);
  return true;
}

bool DrawingDocument::load_image(const QString& path, QString& error) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  image_ = reader.read();
  if (image_.isNull()) {
    error = QObject::tr("Cannot read image: %1").arg(reader.errorString());
    return false;
  }
  kind_ = Kind::Raster;
  kind_label_ = QObject::tr("Image");
  page_count_ = 1;
  page_size_ = QSizeF(image_.width(), image_.height());
  detail_ = QObject::tr("%1 × %2 px").arg(image_.width()).arg(image_.height());
  return true;
}

bool DrawingDocument::load_svg(const QString& path, QString& error) {
  svg_ = std::make_unique<QSvgRenderer>(path);
  if (!svg_->isValid()) {
    error = QObject::tr("Cannot read SVG: %1").arg(QFileInfo(path).fileName());
    svg_.reset();
    return false;
  }
  QSize size = svg_->defaultSize();
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
    size = QSize(1000, 1000);  // 没有 viewBox 也没有宽高的 SVG
  }
  kind_ = Kind::Svg;
  kind_label_ = QStringLiteral("SVG");
  page_count_ = 1;
  page_size_ = QSizeF(size.width(), size.height());
  detail_ = QObject::tr("%1 × %2 units").arg(size.width()).arg(size.height());
  return true;
}

bool DrawingDocument::load_dxf(const QString& path, QString& error) {
  // 必须走 qstring_to_path：Windows 上 std::filesystem::path(std::string) 按 ANSI
  // 代码页解释窄字符串，中文路径会抛 std::system_error（见 engine/core/fs_utf8.h）。
  auto loaded = tamias::load_dxf(qstring_to_path(path));
  if (!loaded) {
    error = QString::fromStdString(loaded.error());
    return false;
  }
  drawing_ = std::move(*loaded);
  kind_ = Kind::Dxf;
  kind_label_ = QStringLiteral("DXF");
  page_count_ = 1;

  Aabb2 bounds = drawing_.bounds();
  if (!bounds.valid() || bounds.width() <= 0.f || bounds.height() <= 0.f) {
    error = QObject::tr("DXF has no measurable extent.");
    return false;
  }
  page_size_ = QSizeF(bounds.width(), bounds.height());
  build_dxf_batches();

  detail_ = QObject::tr("%1 curves · %2 texts · %3 layers")
                .arg(drawing_.paths().size())
                .arg(drawing_.texts().size())
                .arg(drawing_.layers().size());
  if (drawing_.unsupported_entity_count() > 0) {
    detail_ += QObject::tr(" · %1 unsupported").arg(drawing_.unsupported_entity_count());
  }
  return true;
}

bool DrawingDocument::load_pdf(const QString& path, QString& error) {
#if defined(TAMIAS_HAVE_QT_PDF)
  pdf_ = std::make_unique<QPdfDocument>();
  (void)pdf_->load(path);
  if (pdf_->pageCount() <= 0) {
    error = QObject::tr("Cannot read PDF: %1").arg(path);
    pdf_.reset();
    return false;
  }
  kind_ = Kind::Pdf;
  kind_label_ = QStringLiteral("PDF");
  page_count_ = std::max(1, pdf_->pageCount());
  page_size_ = pdf_->pagePointSize(0);
  detail_ = QObject::tr("%1 page(s)").arg(page_count_);
  return true;
#else
  (void)path;
  error = QObject::tr(
      "PDF viewing needs the Qt PDF module (Qt6::Pdf), which is not part of this build.\n"
      "Export the drawing to DXF, SVG or an image, or rebuild with Qt6::Pdf available.");
  return false;
#endif
}

void DrawingDocument::build_dxf_batches() {
  // 同一 (图层, 颜色) 合成一个 QPainterPath，画的时候一次描边。
  struct Key {
    int layer = -1;
    QRgb rgb = 0;
    bool operator<(const Key& other) const {
      return layer != other.layer ? layer < other.layer : rgb < other.rgb;
    }
  };
  std::map<Key, std::size_t> index;
  const auto batch_for = [&](int layer, QColor color) -> PathBatch& {
    const Key key{layer, color.rgb()};
    auto it = index.find(key);
    if (it == index.end()) {
      PathBatch batch;
      batch.layer = layer;
      batch.color = color;
      dxf_batches_.push_back(std::move(batch));
      it = index.emplace(key, dxf_batches_.size() - 1).first;
    }
    return dxf_batches_[it->second];
  };

  for (const DrawingPath& path : drawing_.paths()) {
    if (path.points.size() < 2) {
      continue;
    }
    QPainterPath painter_path;
    painter_path.moveTo(path.points.front().x, path.points.front().y);
    for (std::size_t i = 1; i < path.points.size(); ++i) {
      painter_path.lineTo(path.points[i].x, path.points[i].y);
    }
    if (path.closed) {
      painter_path.closeSubpath();
    }
    batch_for(static_cast<int>(path.layer), to_qcolor(path.color)).paths.addPath(painter_path);
  }

  // 文字用字形轮廓，保证任意缩放都是矢量。
  QFont font;
  font.setPixelSize(100);
  for (const DrawingText& text : drawing_.texts()) {
    const QString content = decode_dxf_text(text.text);
    if (content.isEmpty() || !(text.height > 0.f)) {
      continue;
    }
    QPainterPath glyphs;
    glyphs.addText(QPointF(0.0, 0.0), font, content);
    if (glyphs.isEmpty()) {
      continue;
    }
    // DXF 字高 ≈ 大写字母高，约 0.7 em。
    const double scale = static_cast<double>(text.height) / 70.0;
    const double angle = static_cast<double>(text.rotation_deg) * kPi / 180.0;
    const double cs = std::cos(angle);
    const double sn = std::sin(angle);
    // 世界 Y 向上：旋转 + 竖直镜像一起写进矩阵（见 docs/DRAWING.md）。
    const QTransform place(cs * scale, sn * scale, sn * scale, -cs * scale, text.position.x,
                           text.position.y);
    PathBatch& batch = batch_for(static_cast<int>(text.layer), to_qcolor(text.color));
    batch.paths.addPath(place.map(glyphs));
  }
}

QRectF DrawingDocument::page_rect(int page) const {
  if (kind_ == Kind::Pdf) {
#if defined(TAMIAS_HAVE_QT_PDF)
    const QSizeF size = pdf_ ? pdf_->pagePointSize(std::clamp(page, 0, page_count_ - 1))
                             : page_size_;
    return QRectF(0.0, 0.0, size.width(), size.height());
#endif
  }
  (void)page;
  return QRectF(0.0, 0.0, page_size_.width(), page_size_.height());
}

int DrawingDocument::layer_count() const {
  return static_cast<int>(drawing_.layers().size());
}

QString DrawingDocument::layer_name(int index) const {
  const auto& layers = drawing_.layers();
  if (index < 0 || index >= static_cast<int>(layers.size())) {
    return {};
  }
  return decode_dxf_text(layers[static_cast<std::size_t>(index)].name);
}

QColor DrawingDocument::layer_color(int index) const {
  const auto& layers = drawing_.layers();
  if (index < 0 || index >= static_cast<int>(layers.size())) {
    return QColor(255, 255, 255);
  }
  return to_qcolor(layers[static_cast<std::size_t>(index)].color);
}

bool DrawingDocument::layer_visible(int index) const {
  if (index < 0 || index >= static_cast<int>(layer_hidden_.size())) {
    return true;
  }
  return !layer_hidden_[static_cast<std::size_t>(index)];
}

void DrawingDocument::set_layer_visible(int index, bool visible) {
  if (index < 0 || index >= static_cast<int>(layer_hidden_.size())) {
    return;
  }
  layer_hidden_[static_cast<std::size_t>(index)] = !visible;
}

void DrawingDocument::paint_page(QPainter& painter, int page, double device_scale) const {
  // DXF 本来就是 Y 向上的世界坐标，直接画；位图/SVG/PDF 是 Y 向下的，翻一次。
  if (kind_ == Kind::Dxf) {
    paint_dxf(painter);
    return;
  }
  const QRectF rect = page_rect(page);
  paint_flipped(painter, rect, [&](QPainter& p, const QRectF& target) {
    paint_page_content(p, page, device_scale, target);
  });
}

void DrawingDocument::paint_page_content(QPainter& painter, int page, double device_scale,
                                         const QRectF& target) const {
  switch (kind_) {
    case Kind::Dxf:
      paint_dxf(painter);
      return;
    case Kind::Svg:
      if (svg_) {
        // 矢量：QSvgRenderer 按当前变换直接画，放大不糊。
        svg_->render(&painter, target);
      }
      return;
    case Kind::Raster:
      if (!image_.isNull()) {
        painter.drawImage(target, image_);
      }
      return;
    case Kind::Pdf:
#if defined(TAMIAS_HAVE_QT_PDF)
      if (!pdf_) {
        return;
      }
      {
        const int clamped = std::clamp(page, 0, std::max(0, page_count_ - 1));
        const QSizeF size = pdf_->pagePointSize(clamped);
        const double scale = std::clamp(device_scale, 0.05, 20.0);
        QSize target(std::max(1, static_cast<int>(std::lround(size.width() * scale))),
                     std::max(1, static_cast<int>(std::lround(size.height() * scale))));
        // 单页纹理上限，防止一张大图把显存/内存打爆。
        constexpr int kMaxRasterEdge = 8192;
        if (target.width() > kMaxRasterEdge || target.height() > kMaxRasterEdge) {
          target.scale(kMaxRasterEdge, kMaxRasterEdge, Qt::KeepAspectRatio);
        }
        if (pdf_cached_image_.isNull() || pdf_cached_page_ != clamped ||
            pdf_cached_size_ != target) {
          QImage rendered(target, QImage::Format_RGBA8888);
          rendered.fill(Qt::white);
          pdf_->render(&rendered, target, clamped);
          pdf_cached_image_ = std::move(rendered);
          pdf_cached_page_ = clamped;
          pdf_cached_size_ = target;
        }
        painter.drawImage(target, pdf_cached_image_);
      }
#endif
      return;
  }
}

void DrawingDocument::paint_dxf(QPainter& painter) const {
  for (const PathBatch& batch : dxf_batches_) {
    if (!layer_visible(batch.layer)) {
      continue;
    }
    QPen pen(adapt_to_background(batch.color, dark_background_));
    // 发丝线：纸面/屏幕上恒为 1px，不随缩放变粗。
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(batch.paths);
  }
}

QImage DrawingDocument::render_thumbnail(QSize size) const {
  if (size.width() <= 0 || size.height() <= 0) {
    size = QSize(kThumbnailWidth, kThumbnailHeight);
  }
  QImage image(size, QImage::Format_RGB32);
  image.fill(dark_background_ ? kCanvasDark : kCanvasLight);
  const QRectF page = page_rect(0);
  if (page.width() <= 0.0 || page.height() <= 0.0) {
    return image;
  }
  const double scale_x = (size.width() - 8.0) / page.width();
  const double scale_y = (size.height() - 8.0) / page.height();
  const double scale = std::max(1e-6, std::min(scale_x, scale_y));

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.translate(size.width() * 0.5, size.height() * 0.5);
  painter.scale(scale, -scale);
  painter.translate(-page.center().x(), -page.center().y());
  paint_page(painter, 0, scale);
  painter.end();
  return image;
}

}  // namespace tamias
