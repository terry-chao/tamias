#pragma once

#include "engine/drawing/drawing.h"
#include "engine/drawing/dwf_reader.h"

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <memory>
#include <vector>

class QPainter;
class QSvgRenderer;
#if defined(TAMIAS_HAVE_QT_PDF)
class QPdfDocument;
#endif

namespace tamias {

// 一张"参考图纸"：图片 / SVG / DXF / DWF(DWFx) / PDF 统一成可画的页。
//
// 坐标约定：世界坐标 = 图纸坐标，**Y 向上**（和 DXF/工程图一致）。
// 绘制时 painter 上已经设好「世界 → 设备」的变换（含 Y 翻转），
// 本类负责把内容摆进 (0,0)-(w,h) 的页矩形里。
class DrawingDocument {
 public:
  ~DrawingDocument();

  // 按扩展名判断是不是图纸类文件（含 PDF / 位图 / SVG / DXF）。
  [[nodiscard]] static bool is_drawing_path(const QString& path);
  [[nodiscard]] static QString file_dialog_filter();

  // 打开失败返回 nullptr 并填 error（已翻译好的用户可读文本）。
  static std::unique_ptr<DrawingDocument> open(const QString& path, QString& error);

  [[nodiscard]] const QString& path() const { return path_; }
  [[nodiscard]] QString title() const { return title_; }
  // "DXF" / "PDF" / "SVG" / "图片"
  [[nodiscard]] QString kind_label() const { return kind_label_; }
  // 状态栏补充信息：图元数 / 图层数 / 跳过的图元。
  [[nodiscard]] QString detail_text() const { return detail_; }
  [[nodiscard]] int page_count() const { return page_count_; }
  [[nodiscard]] QRectF page_rect(int page) const;
  // DXF 大坐标归一化时被减掉的原点，显示绝对坐标要加回去。
  [[nodiscard]] Vec2 world_origin() const { return drawing_.world_origin(); }
  [[nodiscard]] int layer_count() const;
  [[nodiscard]] QString layer_name(int index) const;
  [[nodiscard]] QColor layer_color(int index) const;
  [[nodiscard]] bool layer_visible(int index) const;
  void set_layer_visible(int index, bool visible);
  // 图纸自己声明的单位换算（DXF 的 $INSUNITS，图纸单位 → 米）；没写单位返回 0，
  // 调用方（三维底图摆放）按模型范围适配，不要瞎猜毫米。
  [[nodiscard]] double declared_unit_scale() const {
    return drawing_.insunits() > 0 ? drawing_.unit_scale_to_meter() : 0.0;
  }
  // 深色底（默认，和 CAD 一致）还是浅色底；影响近白线的对比处理。
  void set_dark_background(bool dark) { dark_background_ = dark; }
  [[nodiscard]] bool dark_background() const { return dark_background_; }

  // painter 已设好世界→设备变换；device_scale = 每世界单位多少设备像素（PDF 光栅化用）。
  void paint_page(QPainter& painter, int page, double device_scale) const;

  // 三维底图用：把某一页光栅化成 RGBA8（**背景透明**），线条按背景取色
  //（深色视口 → 浅色线）。max_edge 限制最长边的像素数（还会按总像素数再收一次，
  // 免得一张超长图纸把显存吃光）。这一页没有可画内容时返回空图。
  [[nodiscard]] QImage render_page_rgba(int page, int max_edge, bool dark_background) const;

  [[nodiscard]] QImage render_thumbnail(QSize size) const;

 private:
  DrawingDocument() = default;

  enum class Kind { Raster, Svg, Dxf, Dwf, Pdf };

  // 矢量图按「页 + 图层 + 颜色」合成一批路径，画的时候按页与图层可见性过滤。
  struct PathBatch {
    int layer = -1;
    int page = 0;
    QColor color;
    QPainterPath paths;
  };

  bool load(const QString& path, QString& error);
  bool load_image(const QString& path, QString& error);
  bool load_svg(const QString& path, QString& error);
  bool load_dxf(const QString& path, QString& error);
  bool load_dwf(const QString& path, QString& error);
  bool load_pdf(const QString& path, QString& error);

  void build_vector_batches();
  void paint_vector(QPainter& painter, int page, bool dark_background) const;
  void paint_page_with_theme(QPainter& painter, int page, double device_scale,
                             bool dark_background) const;
  void paint_page_content(QPainter& painter, int page, double device_scale,
                          const QRectF& target, bool dark_background) const;
  // SVG 按目标像素尺寸光栅化后缓存：QSvgRenderer 直接在镜像（Y 翻转）的 painter
  // 上渲染会挂住，所以先在它自己的坐标系里画成图，再当位图翻转贴上去。
  [[nodiscard]] const QImage& svg_raster(const QSize& size) const;

  QString path_;
  QString title_;
  QString kind_label_;
  QString detail_;
  Kind kind_ = Kind::Raster;
  int page_count_ = 1;
  QSizeF page_size_{1.0, 1.0};

  // 位图页（图片，或 PDF 当前渲染结果）
  QImage image_;
  std::unique_ptr<QSvgRenderer> svg_;
  Drawing drawing_;
  // 矢量图的页划分（DXF 只有一页；DWFx 一页一个 .fpage）。
  std::vector<DwfPage> pages_;
  std::vector<PathBatch> dxf_batches_;
  std::vector<bool> layer_hidden_;
  bool dark_background_ = true;

  // PDF：按需光栅化，缓存最近一次 (页, 尺寸)。
  mutable int pdf_cached_page_ = -1;
  mutable QSize pdf_cached_size_;
  mutable QImage pdf_cached_image_;
#if defined(TAMIAS_HAVE_QT_PDF)
  std::unique_ptr<QPdfDocument> pdf_;
#endif
};

}  // namespace tamias
