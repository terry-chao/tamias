#include "app/viewport/canvas/document_viewport.h"

#include "app/base/app_settings.h"
#include "app/base/qt_path.h"
#include "bim/host_geometry.h"
#include "bim/wall_size.h"
#include "command/edit/edit_entity_grip_command.h"
#include "command/import/import_texture_command.h"
#include "command/import/import_drawing_command.h"
#include "command/edit/replace_texture_command.h"
#include "command/edit/update_material_command.h"
#include "command/edit/update_grid_command.h"
#include "command/edit/update_storeys_command.h"
#include "app/bim/components/component_specs.h"
#include "bim/grid_dimensions.h"
#include "bim/length_text.h"
#include "engine/base/log.h"
#include "engine/document/picking.h"
#include "engine/render/text/font_fallback.h"
#include "engine/render/text/text_layout.h"
#include "engine/render/text/text_quad_builder.h"
#include "engine/math/grid.h"
#include "engine/modeling/feature/curve_geom.h"
#include "engine/modeling/feature/feature.h"
#include "engine/profile/timing_scope.h"
#include "entity/core/entity.h"
#include "entity/core/entity_grip.h"
#include "entity/core/entity_storey.h"

#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QFileInfo>
#include <QIcon>
#include <QInputDialog>
#include <QLineEdit>
#include <QKeyEvent>
#include <QKeySequence>
#include <QHBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#endif

namespace tamias {

namespace {

// 字体从哪来：仓库里的 assets/fonts（开发）→ exe 旁边的 assets/fonts（部署）→ 系统字体。
// 一个都没有时返回空，文字通路整体关掉——不崩、不画、在日志里说一声。
std::vector<std::filesystem::path> app_font_dirs() {
  std::vector<std::filesystem::path> dirs;
  const auto add = [&dirs](const std::filesystem::path& dir) {
    std::error_code ec;
    if (!dir.empty() && std::filesystem::is_directory(dir, ec)) {
      dirs.push_back(dir);
    }
  };
#if defined(TAMIAS_SOURCE_DIR)
  add(std::filesystem::path(TAMIAS_SOURCE_DIR) / "assets" / "fonts");
#endif
  add(qstring_to_path(QCoreApplication::applicationDirPath()) / "assets" / "fonts");
  for (const std::filesystem::path& dir : default_font_dirs()) {
    add(dir);
  }
  return dirs;
}

// 轴线点选的屏幕容差：轴是细线，得给点手抖的余量；比这个远就不算点中。
constexpr float kGridPickPixels = 8.f;
// 底图光栅化最长边（像素）：4096 够看清平面图上的墙线，又不会把显存吃满
//（DrawingDocument::render_page_rgba 里还会按总像素数再收一次）。
constexpr int kDrawingRasterEdge = 4096;
// 底图整体不透明度：透明底上的线稿压一点亮度，和模型分得开。
constexpr float kDrawingOverlayOpacity = 0.85f;

// 图纸页范围（图纸坐标，Y 向上）→ Aabb2。位图 / SVG / PDF 也走这里：它们的
// 页坐标是 Y 向下的像素/点，但只用宽高，方向由摆放矩阵统一处理。
Aabb2 page_bounds_of(const DrawingDocument& document, int page) {
  const QRectF rect = document.page_rect(page);
  Aabb2 box{};
  if (rect.width() > 0.0 && rect.height() > 0.0) {
    box.expand(static_cast<float>(rect.left()), static_cast<float>(rect.top()));
    box.expand(static_cast<float>(rect.right()), static_cast<float>(rect.bottom()));
  }
  return box;
}

}  // namespace

class DocumentViewport::NativeSurface final : public QWidget {
 public:
  explicit NativeSurface(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
  }

  QPaintEngine* paintEngine() const override { return nullptr; }

 protected:
  // Native HWND children eat Win32 mouse messages; forward them to the viewport
  // so orbit / pan / pick keep working over the Vulkan surface.
  bool event(QEvent* e) override {
    switch (e->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
      case QEvent::Wheel:
        if (parentWidget()) {
          return QCoreApplication::sendEvent(parentWidget(), e);
        }
        break;
      default:
        break;
    }
    return QWidget::event(e);
  }
};

DocumentViewport::DocumentViewport(std::shared_ptr<Document> document,
                                   std::shared_ptr<RenderThread> render_thread, QWidget* parent)
    : QWidget(parent),
      session_(std::make_unique<Session>(std::move(document))),
      document_(&session_->document()),
      command_system_(session_->command_system()),
      camera_(session_->camera().camera()),
      render_thread_(std::move(render_thread)) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Match Vulkan clear so any uncovered edge never flashes pure black.
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(36, 46, 61));
  setPalette(pal);

  // 命令回显：内核执行完一条命令就发一行等价 C# 调用（见 host/command_echo.h）。
  // 视口只做转发，文本怎么显示由控制台面板决定。
  session_->set_listener([this](HostEvent event, std::string_view message) {
    if (event == HostEvent::ConsoleMessage) {
      emit console_message(QString::fromUtf8(message.data(), static_cast<int>(message.size())));
    }
  });

  // Vulkan draws into a native child surface; this parent stays a normal Qt
  // widget so overlays (view cube) can paint and receive clicks on top.
  // Keep the surface in a layout so it tracks the viewport size from the first
  // show — manual setGeometry alone often leaves a tiny HWND at (0,0) until the
  // user resizes/interacts.
  surface_ = new NativeSurface(this);
  // 左：三维区域；右：工具列（不悬浮，从上到下占满，右侧再往外才是停靠面板）。
  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(surface_, 1);

  tool_panel_ = new ViewportToolPanel(this);
  tool_panel_->set_viewport(this);
  root->addWidget(tool_panel_, 0);

  view_cube_ = new ViewCubeWidget(this);
  connect(view_cube_, &ViewCubeWidget::face_clicked, this, &DocumentViewport::on_view_cube_face);
  connect(view_cube_, &ViewCubeWidget::corner_clicked, this, &DocumentViewport::on_view_cube_corner);
  connect(view_cube_, &ViewCubeWidget::orbit_dragged, this, [this](float dyaw, float dpitch) {
    stop_view_animation();
    if (plan_view_) {
      set_plan_view(false, false);
    }
    camera_.orbit(dyaw, dpitch);
    request_redraw();
  });

  connect(tool_panel_, &ViewportToolPanel::plan_view_toggled, this,
          [this](bool plan) { set_plan_view(plan); });
  connect(tool_panel_, &ViewportToolPanel::frame_all_clicked, this,
          &DocumentViewport::frame_scene);
  connect(tool_panel_, &ViewportToolPanel::layout_changed, this, [this] {
    layout_overlays();
    request_redraw();  // 三维区域宽度变了，宽高比要重算
  });
  // 图纸管理页双击 / 点「打开」：视口自己开不了标签页，转给主窗口去开。
  connect(tool_panel_, &ViewportToolPanel::drawing_open_requested, this,
          &DocumentViewport::drawing_open_requested);

  view_anim_timer_ = new QTimer(this);
  view_anim_timer_->setInterval(16);
  connect(view_anim_timer_, &QTimer::timeout, this, &DocumentViewport::on_view_anim_tick);

  // 楼层变了（切层 / 改标高 / 改层高 / 撤销回旧楼层）以后，武装中的构件命令必须
  // 按新楼层重画一遍，见 sync_armed_placement。
  connect(this, &DocumentViewport::document_changed, this,
          &DocumentViewport::sync_armed_placement);

  coord_label_ = new QLabel(this);
  coord_label_->setObjectName(QStringLiteral("coordReadout"));
  coord_label_->setAttribute(Qt::WA_NativeWindow);
  coord_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  coord_label_->setStyleSheet(QStringLiteral(
      "QLabel#coordReadout {"
      "  background: rgba(20, 22, 26, 180);"
      "  color: #d8d4ce;"
      "  border: 1px solid #3a3c42;"
      "  border-radius: 6px;"
      "  padding: 6px 10px;"
      "  font-family: Consolas, 'Cascadia Mono', monospace;"
      "  font-size: 12px;"
      "}"));

  box_select_overlay_ = new BoxSelectOverlay(this);

  camera_.frame_aabb(document_->bounds().valid() ? document_->bounds()
                                                 : Aabb{{-1, -1, -1}, {1, 1, 1}});
  rebuild_bvh();
  sync_view_cube();
  sync_coord_readout();
}

DocumentViewport::~DocumentViewport() {
  cancel_plugin_point_input();
  // Tear down the swapchain/surface while the native HWND is still valid, and
  // wait for the render thread so it cannot draw into a destroyed window.
  alive_ = false;
  channel_.reset();
  render_thread_.reset();
  destroy_gl_surface();
}

void DocumentViewport::set_render_mode(RenderMode mode) {
  mode_ = mode;
  request_redraw();
}

void DocumentViewport::set_xray(bool on) {
  if (xray_ == on) {
    return;
  }
  xray_ = on;
  request_redraw();
}

ViewportState DocumentViewport::capture_viewport_state() const {
  ViewportState state;
  state.target = camera_.target();
  state.distance = camera_.distance();
  state.yaw = camera_.yaw();
  state.pitch = camera_.pitch();
  state.fovy = camera_.fovy();
  state.znear = camera_.znear();
  state.zfar = camera_.zfar();
  state.render_mode = static_cast<ViewRenderMode>(mode_);
  state.xray = xray_ ? kXrayOpacity : 0.f;
  return state;
}

RenderScene::View DocumentViewport::capture_render_scene_view() const {
  RenderScene::View view;
  const qreal dpr = devicePixelRatioF();
  const int sw = surface_ != nullptr ? surface_->width() : std::max(1, width());
  const int sh = surface_ != nullptr ? surface_->height() : std::max(1, height());
  view.width = static_cast<std::uint32_t>(std::max(1, static_cast<int>(sw * dpr)));
  view.height = static_cast<std::uint32_t>(std::max(1, static_cast<int>(sh * dpr)));
  const float aspect =
      static_cast<float>(view.width) / static_cast<float>(std::max(1u, view.height));
  view.view = camera_.view_matrix();
  view.proj = camera_.proj_matrix(aspect);
  view.eye_position = camera_.eye_position();
  view.target = camera_.target();
  view.view_distance = camera_.distance();
  view.yaw = camera_.yaw();
  view.pitch = camera_.pitch();
  view.fovy = camera_.fovy();
  view.znear = camera_.znear();
  view.zfar = camera_.zfar();
  view.mode = mode_;
  view.xray = xray_ ? kXrayOpacity : 0.f;
  view.orthographic = camera_.orthographic();
  return view;
}

std::vector<std::uint64_t> DocumentViewport::capture_hidden_node_ids() const {
  std::vector<std::uint64_t> hidden;
  for (const auto& item : document_->render_items()) {
    if (!node_visible_in_view(item.node_id)) {
      hidden.push_back(item.node_id);
    }
  }
  std::sort(hidden.begin(), hidden.end());
  hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
  return hidden;
}

RenderScene DocumentViewport::capture_debug_scene() const {
  RenderScene scene = document_->capture_render_scene(capture_render_scene_view());
  scene.hidden_node_ids = capture_hidden_node_ids();
  if (render_thread_ != nullptr) {
    scene.debug_graph.lod_by_node = render_thread_->last_lod_by_node();
  }
  return scene;
}

void DocumentViewport::apply_viewport_state(const ViewportState& state) {
  stop_view_animation();
  camera_.set_target(state.target);
  camera_.set_distance(state.distance);
  camera_.set_yaw_pitch(state.yaw, state.pitch);
  camera_.set_fovy(state.fovy);
  mode_ = static_cast<RenderMode>(state.render_mode);
  xray_ = state.xray > 0.f;
  sync_view_cube();
  // Do not submit here if the widget is not yet laid out — an early present at the
  // wrong size can leave the swapchain stuck drawing into a corner of the window.
  if (isVisible() && surface_ && surface_->width() >= 2 && surface_->height() >= 2) {
    request_redraw();
  }
}

void DocumentViewport::frame_scene() {
  stop_view_animation();
  // 打开的是某一层的视图时，"适应窗口"框的是这一层（和双击楼层时同一块盒子）；
  // 只有全局视图才框整个模型。
  if (floor_view_.has_value()) {
    refresh_floors();
    const Aabb floor_box =
        floor_view_.has_value() ? floor_view_box(*floor_view_) : Aabb{};
    if (floor_box.valid()) {
      camera_.frame_aabb(floor_box);
      request_redraw();
      return;
    }
  }
  camera_.frame_aabb(document_->bounds());
  request_redraw();
}

Aabb DocumentViewport::floor_view_box(std::size_t floor_index) const {
  Aabb box = document_->bounds();
  if (!box.valid() || floor_index >= floors_.size()) {
    return Aabb{};
  }
  // 平面视图下高度不参与投影，把框压到这一层只是为了框住这一层的平面范围。
  box.min.y = floors_[floor_index].y_min;
  box.max.y = floors_[floor_index].y_max;
  return box;
}

void DocumentViewport::frame_node(std::uint64_t node_id) {
  if (document_ == nullptr || node_id == 0) {
    return;
  }
  const SceneNode* node = document_->scene().find(node_id);
  if (node == nullptr || !node->world_bounds.valid()) {
    return;
  }
  stop_view_animation();
  camera_.frame_aabb(node->world_bounds);
  request_redraw();
}

void DocumentViewport::request_redraw() {
  if (!alive_) {
    return;
  }
  sync_view_cube();
  sync_coord_readout();
  submit_current_frame();
}

void DocumentViewport::layout_overlays() {
  // 工具列刚改过宽度的话，先把布局跑完再摆叠加层，否则 ViewCube 会用旧的三维区尺寸。
  if (QLayout* box = layout()) {
    box->activate();
  }
  if (surface_) {
    surface_->lower();
    // Force HWND creation after layout so the first Vulkan present sees the
    // real client extent, not a default 0x0 / stub size.
    if (width() > 1 && height() > 1) {
      (void)surface_->winId();
    }
  }
  constexpr int kMargin = 12;
  // 叠加层只贴在三维区域上，不盖到右侧工具列。
  const QRect area = surface_ ? surface_->geometry() : rect();
  if (view_cube_) {
    view_cube_->move(area.x() + area.width() - view_cube_->width() - kMargin,
                     area.y() + kMargin);
    view_cube_->raise();
  }
  if (coord_label_) {
    coord_label_->adjustSize();
    coord_label_->move(area.x() + kMargin, area.y() + area.height() - coord_label_->height() - kMargin);
    coord_label_->raise();
  }
  if (box_select_overlay_ && box_select_overlay_->isVisible()) {
    box_select_overlay_->raise();
  }
}

void DocumentViewport::sync_view_cube() {
  if (view_cube_) {
    view_cube_->set_orientation(camera_.yaw(), camera_.pitch());
  }
}

QSize DocumentViewport::scene_area_size() const {
  const int w = surface_ != nullptr ? surface_->width() : width();
  const int h = surface_ != nullptr ? surface_->height() : height();
  return {std::max(1, w), std::max(1, h)};
}

// 光标 → 世界射线。渲染只发生在三维区域里，所以 NDC 与宽高比都按三维区算：
// 用整块视口的宽（含右侧工具列）会把光标横向拉伸，画面越靠右偏得越多。
Ray DocumentViewport::ray_at(const QPoint& pos) const {
  const QSize area = scene_area_size();
  const float w = static_cast<float>(area.width());
  const float h = static_cast<float>(area.height());
  return camera_ray(camera_, w / h, static_cast<float>(pos.x()), static_cast<float>(pos.y()), w,
                    h);
}

Vec3 DocumentViewport::cursor_world_position(const QPoint& pos) const {
  const Ray ray = ray_at(pos);

  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        return node_visible_in_view(id);
      })) {
    return ray.origin + ray.direction * hit->t;
  }

  // Fall back to the horizontal plane through the camera target.
  const float plane_y = camera_.target().y;
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      return ray.origin + ray.direction * t;
    }
  }
  return ray.origin + ray.direction * camera_.distance();
}

void DocumentViewport::sync_coord_readout() {
  if (!coord_label_) {
    return;
  }
  QString text;
  if (!has_cursor_) {
    const Vec3 target = camera_.target();
    text = tr("X %1  Y %2  Z %3")
               .arg(target.x, 0, 'f', 3)
               .arg(target.y, 0, 'f', 3)
               .arg(target.z, 0, 'f', 3);
  } else {
    const Vec3 p = grid_snap_active() ? cursor_ground_position(last_mouse_)
                                      : cursor_world_position(last_mouse_);
    text = tr("X %1  Y %2  Z %3")
               .arg(p.x, 0, 'f', 3)
               .arg(p.y, 0, 'f', 3)
               .arg(p.z, 0, 'f', 3);
  }
  if (render_thread_) {
    const RenderFrameStats stats = render_thread_->last_stats();
    text += tr("\ndraw %1  tri %2  gpu %3MB  tess %4")
                .arg(stats.draws)
                .arg(stats.triangles)
                .arg(static_cast<double>(stats.gpu_mesh_bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(document_->pending_tessellate_count() + stats.lod_requests);
  }
  coord_label_->setText(text);
  layout_overlays();
}

void DocumentViewport::stop_view_animation() {
  if (view_anim_timer_ && view_anim_timer_->isActive()) {
    view_anim_timer_->stop();
  }
}

void DocumentViewport::start_view_animation(float target_yaw, float target_pitch,
                                            bool finish_orthographic) {
  constexpr float kPi = 3.141592654f;
  anim_finish_orthographic_ = finish_orthographic;
  anim_from_yaw_ = camera_.yaw();
  anim_from_pitch_ = camera_.pitch();
  anim_to_yaw_ = target_yaw;
  anim_to_pitch_ = std::clamp(target_pitch, -kHalfPi, kHalfPi);

  // Shortest yaw arc so e.g. Front↔Back never spins the long way.
  float delta = anim_to_yaw_ - anim_from_yaw_;
  while (delta > kPi) {
    delta -= 2.f * kPi;
  }
  while (delta < -kPi) {
    delta += 2.f * kPi;
  }
  anim_yaw_delta_ = delta;

  if (std::abs(anim_yaw_delta_) < 1e-4f && std::abs(anim_to_pitch_ - anim_from_pitch_) < 1e-4f) {
    camera_.set_yaw_pitch(anim_to_yaw_, anim_to_pitch_);
    if (anim_finish_orthographic_) {
      camera_.set_orthographic(true);
      anim_finish_orthographic_ = false;
    }
    stop_view_animation();
    request_redraw();
    return;
  }

  view_anim_clock_.restart();
  view_anim_timer_->start();
  this->on_view_anim_tick();
}

void DocumentViewport::on_view_anim_tick() {
  constexpr int kDurationMs = 280;
  const float t_raw =
      std::clamp(static_cast<float>(view_anim_clock_.elapsed()) / static_cast<float>(kDurationMs),
                 0.f, 1.f);
  // Ease-out cubic.
  const float t = 1.f - (1.f - t_raw) * (1.f - t_raw) * (1.f - t_raw);
  const float yaw = anim_from_yaw_ + anim_yaw_delta_ * t;
  const float pitch = anim_from_pitch_ + (anim_to_pitch_ - anim_from_pitch_) * t;
  camera_.set_yaw_pitch(yaw, pitch);
  request_redraw();

  if (t_raw >= 1.f) {
    camera_.set_yaw_pitch(anim_to_yaw_, anim_to_pitch_);
    if (anim_finish_orthographic_) {
      camera_.set_orthographic(true);
      anim_finish_orthographic_ = false;
    }
    stop_view_animation();
    request_redraw();
  }
}

void DocumentViewport::on_view_cube_face(ViewCubeFace face) {
  float yaw = camera_.yaw();
  float pitch = 0.f;
  switch (face) {
    case ViewCubeFace::Front:
      yaw = 0.f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Back:
      yaw = 3.141592654f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Left:
      yaw = -1.570796327f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Right:
      yaw = 1.570796327f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Top:
      pitch = kHalfPi;
      break;
    case ViewCubeFace::Bottom:
      pitch = -kHalfPi;
      break;
  }
  if (plan_view_ && face != ViewCubeFace::Top) {
    set_plan_view(false, false);
  }
  start_view_animation(yaw, pitch);
}

void DocumentViewport::on_view_cube_corner(ViewCubeCorner corner) {
  // Eye along the chosen cube corner (Front=+Z, Right=+X, Top=+Y).
  Vec3 dir{};
  switch (corner) {
    case ViewCubeCorner::RightTopFront:
      dir = {1.f, 1.f, 1.f};
      break;
    case ViewCubeCorner::LeftTopFront:
      dir = {-1.f, 1.f, 1.f};
      break;
    case ViewCubeCorner::RightTopBack:
      dir = {1.f, 1.f, -1.f};
      break;
    case ViewCubeCorner::LeftTopBack:
      dir = {-1.f, 1.f, -1.f};
      break;
    case ViewCubeCorner::RightBottomFront:
      dir = {1.f, -1.f, 1.f};
      break;
    case ViewCubeCorner::LeftBottomFront:
      dir = {-1.f, -1.f, 1.f};
      break;
    case ViewCubeCorner::RightBottomBack:
      dir = {1.f, -1.f, -1.f};
      break;
    case ViewCubeCorner::LeftBottomBack:
      dir = {-1.f, -1.f, -1.f};
      break;
  }
  const Vec3 d = normalize(dir);
  const float pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
  const float yaw = std::atan2(d.x, d.z);
  if (plan_view_) {
    set_plan_view(false, false);
  }
  start_view_animation(yaw, pitch);
}

NativeWindowHandle DocumentViewport::native_handle() const {
  NativeWindowHandle handle{};
#if defined(_WIN32)
  if (gl_hwnd_) {
    handle.hwnd = gl_hwnd_;
  } else {
    handle.hwnd = reinterpret_cast<void*>(surface_ ? surface_->winId() : winId());
  }
#else
  if (auto* ni = QGuiApplication::platformNativeInterface()) {
    handle.display = ni->nativeResourceForIntegration("display");
  }
  handle.window = static_cast<std::uint64_t>(surface_ ? surface_->winId() : winId());
#endif
  return handle;
}

void DocumentViewport::destroy_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_) {
    destroy_opengl_surface_hwnd(gl_hwnd_);
    gl_hwnd_ = nullptr;
  }
#endif
}

void DocumentViewport::ensure_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_ || !surface_) {
    return;
  }
  // 用**本次实际用的后端**（可能因为探测降级而不同于用户偏好）：GL 子窗口只在实际走
  // OpenGL 时才需要，否则会画到一个没有 GL 表面的窗口上。
  if (AppSettings::instance().resolved_backend() != GraphicsBackend::OpenGL) {
    return;
  }
  // Force the Qt native child to exist, then create our GL HWND on the UI thread.
  const WId parent_id = surface_->winId();
  if (!parent_id) {
    return;
  }
  const auto dpr = devicePixelRatioF();
  const int w = std::max(1, static_cast<int>(surface_->width() * dpr));
  const int h = std::max(1, static_cast<int>(surface_->height() * dpr));
  gl_hwnd_ = create_opengl_surface_hwnd(reinterpret_cast<void*>(parent_id), w, h);
  if (!gl_hwnd_) {
    log_error("Failed to create OpenGL surface HWND on UI thread");
  }
#endif
}

void DocumentViewport::ensure_channel() {
  if (!alive_ || channel_) {
    return;
  }
  if (!render_thread_) {
    const RenderDeviceConfig config = AppSettings::instance().render_device_config();
    render_thread_ = RenderThreadPool::instance().acquire(config);
  }
  if (!render_thread_) {
    log_error("failed to acquire RenderThread");
    return;
  }
  channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
}

void DocumentViewport::rebuild_bvh() { bvh_.build(*document_); }

void DocumentViewport::submit_current_frame() {
  TAMIAS_TIMING_SCOPE("submit_current_frame", TimingCategory::Render);
  if (!alive_) {
    return;
  }
  ensure_channel();
  if (!channel_ || !surface_) {
    return;
  }
  // Skip until the surface has a real laid-out size; a 1x1 present on first
  // show leaves a black window until the next resize.
  if (surface_->width() < 2 || surface_->height() < 2) {
    return;
  }
  if (render_thread_) {
    for (const std::uint64_t id : document_->apply_completed_tess_jobs()) {
      if (const MeshAsset* asset = document_->mesh(id);
          asset != nullptr && !asset->cpu.vertices.empty()) {
        render_thread_->request_upload_mesh(id, asset->cpu);
      }
    }
    for (const LodRequest& req : render_thread_->take_lod_requests()) {
      document_->enqueue_lod_request(req);
    }
  }
  ensure_gl_surface();
  const auto dpr = devicePixelRatioF();
  const auto w = static_cast<std::uint32_t>(surface_->width() * dpr);
  const auto h = static_cast<std::uint32_t>(surface_->height() * dpr);
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_) {
    resize_opengl_surface_hwnd(gl_hwnd_, static_cast<int>(w), static_cast<int>(h));
  }
#endif
  channel_->resize(native_handle(), w, h);
  resync_textures();

  FrameSubmission frame{};
  frame.window = native_handle();
  frame.width = w;
  frame.height = h;
  const float aspect = static_cast<float>(w) / static_cast<float>(h);
  frame.view = camera_.view_matrix();
  frame.proj = camera_.proj_matrix(aspect);
  frame.eye_position = camera_.eye_position();
  frame.view_distance = camera_.distance();
  frame.fovy = camera_.fovy();
  frame.mode = mode_;
  frame.xray = xray_ ? kXrayOpacity : 0.f;
  refresh_floors();
  // 留存场景图的同步源必须是全量清单（无视锥剔除）；剔除/可见性过滤移到渲染
  // 线程录制时按节点判断，树本身保持完整。
  frame.items = document_->render_items();
  frame.lod_sets = document_->tess_cache().snapshot();
  std::vector<std::uint64_t> hidden;
  hidden.reserve(frame.items.size());
  for (const auto& item : frame.items) {
    if (!node_visible_in_view(item.node_id)) {
      hidden.push_back(item.node_id);
    }
  }
  frame.hidden_node_ids = std::move(hidden);
  if (debug_isolate_node_) {
    for (const auto& item : frame.items) {
      if (item.node_id != *debug_isolate_node_) {
        frame.hidden_node_ids.push_back(item.node_id);
      }
    }
  }
  frame.scene_generation = document_->scene().generation();
  frame.scene_dirty_ids = document_->scene().dirty_since(last_submitted_scene_generation_);
  last_submitted_scene_generation_ = frame.scene_generation;
  const Vec3 cursor = has_cursor_ ? cursor_ground_position(last_mouse_) : Vec3{};
  if (plugin_point_input_.active()) {
    std::vector<Vec3> controls;
    controls.reserve(plugin_point_input_.points().size() + 1);
    for (const PluginPickPoint& point : plugin_point_input_.points()) {
      controls.push_back(point.position);
    }
    if (has_cursor_ &&
        (controls.empty() || length(controls.back() - cursor) >= 1e-4f)) {
      controls.push_back(cursor);
    }
    frame.preview_control_polyline = controls;
    frame.preview_points = controls;
    if (controls.size() >= 2 && plugin_point_input_.preview_kind() != 0) {
      const int kind = plugin_point_input_.preview_kind();
      const std::string& curve = plugin_point_input_.preview_curve_kind();
      if (kind == 1) {
        if (curve == "nurbs") {
          frame.preview_polyline = sample_nurbs(controls, {});
        } else if (curve == "bspline") {
          frame.preview_polyline = sample_bspline(controls);
        } else if (curve == "bezier") {
          frame.preview_polyline = sample_bezier(controls);
        } else {
          frame.preview_polyline = controls;
        }
      } else if (kind == 2 || kind == 7) {
        frame.preview_polyline = {controls.front(), controls.back()};
      } else if (kind == 3) {
        frame.preview_polyline = controls;
      } else if (kind == 4 || kind == 8) {
        frame.preview_polyline = sample_rect_xz(controls.front(), controls.back());
      } else if (kind == 5) {
        frame.preview_polyline =
            sample_circle_xz(controls.front(), length(controls.back() - controls.front()));
      } else if (kind == 6) {
        if (controls.size() >= 3) {
          frame.preview_polyline = sample_arc_3pt(controls[0], controls[1], controls.back());
        } else {
          frame.preview_polyline = {controls.front(), controls.back()};
        }
      } else {
        frame.preview_polyline = controls;
      }
    }
  } else {
    if (is_opening_placement_tool() && command_system_.has_pending() && has_cursor_) {
      update_opening_hover(last_mouse_);
    }
    frame.preview_polyline = command_system_.preview_polyline(cursor);
    frame.preview_control_polyline = command_system_.preview_control_polyline(cursor);
    frame.preview_points = command_system_.preview_points(cursor);
  }
  if (grid_snap_active() && has_cursor_ && is_on_grid_xz(cursor)) {
    frame.snap_point = cursor;
  }
  // 放置中的轴网：整张幽灵跟着光标走，落位前就能看出它要放在哪。
  const float grid_y = grid_plane_y();
  if (grid_visible_ && !document_->bim().grid().empty()) {
    // 轴网画在当前楼层的标高上：平面视图里它正好落在工作面上。
    append_axis_segments(document_->bim().grid().axes(), frame.grid_line_segments);
    for (Vec3& point : frame.grid_line_segments) {
      point.y = grid_y;
    }
    // 选中的轴线单独出一份：画在普通轴线上面的琥珀色，框选/点选后看得见选了什么。
    for (const GridAxis& axis : document_->bim().grid().axes()) {
      if (!axis.selected || axis.length() <= 0.0) {
        continue;
      }
      frame.grid_selected_segments.push_back(axis.start_point());
      frame.grid_selected_segments.push_back(axis.end_point());
    }
    for (Vec3& point : frame.grid_selected_segments) {
      point.y = grid_y;
    }
  }
  // 派生标注（轴号 / 标高 / 尺寸链）：按类别开关逐条决定，见 docs/TEXT.md §4.4。
  append_text_annotations(frame);
  if (pending_grid_ && has_cursor_) {
    const Vec3 drop = plan_position_at_storey(last_mouse_);
    append_axis_segments(ghost_grid_axes(last_mouse_), frame.grid_preview_segments);
    for (Vec3& point : frame.grid_preview_segments) {
      point.y = grid_y;
    }
    // 锚点（生成行里的原点）画个方块：落位后它正好压在鼠标下。
    frame.preview_points.push_back(Vec3{drop.x, grid_y, drop.z});
  }
  // 参考图纸底图：贴在标高上的线稿，挡在它前面的构件会遮住它。
  submit_drawing_overlays(frame);
  fill_grip_overlay(frame);
  fill_debug_overlay(frame);
  channel_->submit(std::move(frame));
}

void DocumentViewport::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  layout_overlays();
  ensure_channel();
  request_redraw();
  // Qt finishes applying the native child size after showEvent returns; present
  // once more on the next event-loop tick so the first frame matches the HWND.
  QTimer::singleShot(0, this, [this] {
    if (!alive_) {
      return;
    }
    layout_overlays();
    request_redraw();
  });
}

void DocumentViewport::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layout_overlays();
  request_redraw();
}

void DocumentViewport::mousePressEvent(QMouseEvent* event) {
  setFocus();  // 让视口能收到按键（[ ] 改选中对象的参数）
  last_mouse_ = event->pos();
  press_mouse_ = event->pos();
  has_cursor_ = true;
  press_hit_ = 0;
  if (event->button() == Qt::LeftButton) {
    if (text_placement_) {
      commit_text_placement(event->pos());
      text_press_consumed_ = true;
      return;
    }
    if (pending_grid_) {
      commit_grid_placement(event->pos());
      grid_press_consumed_ = true;  // 抬起时别把这一下当成选择点击 / 清空选择
      return;
    }
    if (plugin_point_input_.active()) {
      plugin_input_press_ = true;
      Vec3 point = cursor_ground_position(event->pos());
      std::uint64_t picked = 0;
      if (plugin_point_input_.pick_entities()) {
        const Ray ray = ray_at(event->pos());
        if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
              return node_visible_in_view(id);
            })) {
          picked = hit->node_id;
          point = ray.origin + ray.direction * hit->t;
        }
      }
      if (plugin_point_input_.entities_only()) {
        if (picked == 0) {
          return;
        }
        const std::string& filter = plugin_point_input_.filter_kind();
        if (!filter.empty()) {
          const Entity* entity = document_->entity(picked);
          if (entity == nullptr || entity_kind_name(entity->kind()) != filter) {
            return;
          }
        }
      }
      plugin_point_input_.add_point({point, picked});
      request_redraw();
      return;
    }
    if (command_system_.has_pending()) {
      if (session_->tool_mode() == ToolMode::Slab && !plan_view_) {
        refuse_slab_outside_plan(true);
        set_tool(ToolMode::None);
        return;
      }
      plugin_input_press_ = session_->tool_mode() == ToolMode::None;
      Vec3 point = cursor_ground_position(event->pos());
      std::uint64_t picked = 0;
      if (is_opening_placement_tool()) {
        if (auto wall = pick_wall_at(event->pos())) {
          picked = wall->first;
          point = wall->second;
        } else {
          emit status_message(session_->tool_mode() == ToolMode::Door
                                  ? tr("Click a wall to place the door")
                                  : tr("Click a wall to place the window"));
          request_redraw();
          return;
        }
      }
      auto r = command_system_.feed_point(point, picked);  // 喂交互点给 pending 命令
      if (finish_pending_if_done(r)) {
        return;
      }
      request_redraw();
      return;
    }
    // 文字注记压在最上面：先看点到的是不是它。
    if (const std::uint64_t text_id = pick_text_annotation_at(event->pos()); text_id != 0) {
      select_text(text_id);
      text_press_consumed_ = true;
      return;
    }
    // 点到别处：放掉注记选择（和轴网选择一个道理）。
    select_text(0);
    press_hit_ = pick_node_at(event->pos());
    EntityGrip grip{};
    if (pick_grip_at(event->pos(), grip)) {
      Entity* entity = document_->entity(grip.entity_id);
      if (entity != nullptr) {
        gripping_ = true;
        active_grip_ = grip;
        grip_from_model_ = entity->model;
        grip_from_transform_ = entity->local_transform;
        grip_from_world_ = grip.world;
        clear_grip_preview();
        return;
      }
    }
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (press_hit_ == 0) {
      // 没点中构件：再看看是不是点在轴线上（轴网优先级在构件之后——平面图里墙压在轴上）。
      if (const std::uint64_t axis = pick_grid_axis_at(event->pos()); axis != 0) {
        select_grid_axis(axis, shift);
        grid_press_consumed_ = true;
        return;
      }
      if (!shift) {
        Grid& grid = document_->bim().grid();
        if (grid.has_selection()) {
          grid.clear_selection();  // 点空白：轴网选择当场清掉（构件选择在抬起时清）
          request_redraw();
        }
      }
      return;
    }
    const SceneNode* node = document_->scene().find(press_hit_);
    const bool already = node != nullptr && node->selected;
    if (!shift) {
      document_->bim().grid().clear_selection();  // 选构件 = 放掉轴网选择
    }
    if (shift) {
      if (already) {
        session_->deselect(press_hit_);
      } else {
        session_->select(press_hit_);
      }
    } else if (!already) {
      session_->clear_selection();
      session_->select(press_hit_);
    }
    emit selection_changed();
    request_redraw();
    return;
  }
  if (event->button() == Qt::MiddleButton) {
    stop_view_animation();
    mmb_nav_ = true;
    return;
  }
  if (event->button() == Qt::RightButton) {
    stop_view_animation();
    panning_ = true;
  }
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPoint delta = event->pos() - last_mouse_;
  last_mouse_ = event->pos();
  has_cursor_ = true;
  if (mmb_nav_ && (event->buttons() & Qt::MiddleButton)) {
    if (plan_view_ || (event->modifiers() & Qt::ShiftModifier)) {
      session_->camera().pan(-delta.x(), delta.y());
    } else {
      session_->camera().orbit(-delta.x(), delta.y());
    }
    request_redraw();
    return;
  }
  if (panning_) {
    session_->camera().pan(-delta.x(), delta.y());
    request_redraw();
    return;
  }
  if (gripping_ && (event->buttons() & Qt::LeftButton)) {
    apply_grip_at(event->pos());
    return;
  }
  if (pending_grid_) {
    sync_coord_readout();
    request_redraw();  // 幽灵轴网跟着光标走
    return;
  }
  if (plugin_point_input_.active() || command_system_.has_pending()) {
    update_opening_hover(event->pos());
    request_redraw();  // 更新网格捕捉点与预览线
    return;
  }
  if (event->buttons() & Qt::LeftButton) {
    if (!box_selecting_ && press_hit_ == 0 &&
        (event->pos() - press_mouse_).manhattanLength() >= 4) {
      box_selecting_ = true;
    }
    if (box_selecting_) {
      update_box_select_rect(event->pos());
      return;
    }
  }
  EntityGrip hover{};
  if (pick_grip_at(event->pos(), hover)) {
    setCursor(Qt::SizeAllCursor);
  } else {
    unsetCursor();
  }
  sync_coord_readout();
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    // 框选优先：拖动可能正好从一根轴线上起手，按下那一下的"选中轴线"不该吃掉框选。
    if (box_selecting_) {
      finish_box_select(event->pos(), (event->modifiers() & Qt::ShiftModifier) != 0);
    } else if (!grid_press_consumed_ && !text_press_consumed_) {
      // 已交给轴网（落位 / 选轴）或文字注记（选中 / 放置）的那一下，不再当选择点击。
      if (plugin_input_press_) {
        plugin_input_press_ = false;
      } else if (gripping_) {
        commit_grip_drag();
        gripping_ = false;
      } else if (session_->tool_mode() == ToolMode::None &&
                 (event->pos() - press_mouse_).manhattanLength() < 4 && press_hit_ == 0 &&
                 (event->modifiers() & Qt::ShiftModifier) == 0) {
        session_->clear_selection();
        document_->bim().grid().clear_selection();
        request_redraw();
        emit selection_changed();
      }
    }
    grid_press_consumed_ = false;
    text_press_consumed_ = false;
    box_selecting_ = false;
    if (box_select_overlay_) {
      box_select_overlay_->hide_box();
    }
  }
  if (event->button() == Qt::MiddleButton) {
    mmb_nav_ = false;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = false;
    if ((event->pos() - press_mouse_).manhattanLength() < 4) {
      if (pending_grid_) {
        cancel_grid_placement();
        return;
      }
      if (plugin_point_input_.active()) {
        plugin_point_input_.cancel();
        request_redraw();
        return;
      }
      if (session_->tool_mode() == ToolMode::None &&
          command_system_.has_pending()) {
        if (command_system_.accepts_confirm()) {
          if (!finish_pending_if_done(command_system_.confirm())) {
            command_system_.cancel();
            request_redraw();
          }
        } else {
          command_system_.cancel();
          request_redraw();
        }
        return;
      }
      if (session_->tool_mode() != ToolMode::None) {
        // 折线 / 贝塞尔：右键先提交已点的顶点，再退出工具。
        if (command_system_.accepts_confirm()) {
          const Result<bool> done = command_system_.confirm();
          if (done && *done) {
            resync_all_meshes();
            rebuild_bvh();
            emit document_changed();
          }
        }
        set_tool(ToolMode::None);
        return;
      }
      // 右键的菜单跟着光标下的东西走：构件 > 轴线 > 当前选中（构件）。
      const std::uint64_t hit = pick_node_at(event->pos());
      const std::uint64_t axis_hit = pick_grid_axis_at(event->pos());
      if (const std::uint64_t text_hit = pick_text_annotation_at(event->pos()); text_hit != 0) {
        select_text(text_hit);
        show_entity_context_menu(mapToGlobal(event->pos()));
      } else if (hit != 0 && document_->entity(hit) != nullptr) {
        document_->bim().grid().clear_selection();
        session_->clear_selection();
        session_->select(hit);
        request_redraw();
        emit selection_changed();
        show_entity_context_menu(mapToGlobal(event->pos()));
      } else if (axis_hit != 0) {
        if (!document_->bim().grid().axis_selected(axis_hit)) {
          select_grid_axis(axis_hit, /*additive=*/false);
        }
        show_grid_context_menu(mapToGlobal(event->pos()));
      } else if (document_->selected_entity() != nullptr) {
        show_entity_context_menu(mapToGlobal(event->pos()));
      }
    }
  }
}

void DocumentViewport::mouseDoubleClickEvent(QMouseEvent* event) {
  // 双击注记 = 就地改文字（先于命令确认，不然会被当成"完成当前命令"）。
  if (event->button() == Qt::LeftButton) {
    if (const std::uint64_t text_id = pick_text_annotation_at(event->pos()); text_id != 0) {
      select_text(text_id);
      edit_text_annotation(text_id);
      return;
    }
  }
  if (event->button() == Qt::LeftButton && plugin_point_input_.active() &&
      plugin_point_input_.accepts_confirm()) {
    plugin_point_input_.confirm();
    request_redraw();
    return;
  }
  if (event->button() != Qt::LeftButton || !command_system_.accepts_confirm()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  last_mouse_ = event->pos();
  (void)finish_pending_if_done(command_system_.confirm());
}

void DocumentViewport::wheelEvent(QWheelEvent* event) {
  stop_view_animation();
  const float steps = event->angleDelta().y() / 120.f;
  const float factor = std::pow(0.9f, steps);
  const QPoint pos = event->position().toPoint();
  last_mouse_ = pos;
  has_cursor_ = true;

  if (AppSettings::instance().zoom_to_mouse_position()) {
    // Zoom toward the cursor: sample the world point first, dolly, then shift
    // the orbit target so that point stays fixed in screen space.
    const Vec3 focus = cursor_world_position(pos);
    session_->camera().dolly_to_focus(factor, focus);
  } else {
    session_->camera().dolly(factor);
  }
  request_redraw();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event) {
  if (plugin_point_input_.active()) {
    if (event->key() == Qt::Key_Escape) {
      plugin_point_input_.cancel();
      request_redraw();
      return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        plugin_point_input_.accepts_confirm()) {
      plugin_point_input_.confirm();
      request_redraw();
      return;
    }
  }
  switch (event->key()) {
    case Qt::Key_Escape:
      if (text_placement_) {
        cancel_text_placement();
        break;
      }
      cancel_tool();
      break;
    case Qt::Key_Delete:
      if (session_->tool_mode() == ToolMode::None &&
          !command_system_.has_pending()) {
        delete_selected();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (pending_grid_) {
        // 和左键同义：落位在光标处（预览在哪儿就放哪儿）。
        if (has_cursor_) {
          commit_grid_placement(last_mouse_);
        }
        break;
      }
      if (command_system_.accepts_confirm()) {
        (void)finish_pending_if_done(command_system_.confirm());
      }
      break;
    case Qt::Key_BracketLeft:   // '[' 减参数
      adjust_selected_param(-0.05);
      break;
    case Qt::Key_BracketRight:  // ']' 加参数
      adjust_selected_param(0.05);
      break;
    default:
      QWidget::keyPressEvent(event);
      break;
  }
}

void DocumentViewport::refuse_slab_outside_plan(bool popup) {
  const QString text = tr("Slabs can only be drawn in plan view");
  emit status_message(text);
  if (popup) {
    QMessageBox::warning(this, tr("Slab"), text);
  }
}

void DocumentViewport::set_tool(ToolMode mode) {
  if (mode != ToolMode::None) {
    clear_grid_placement();  // 换工具 = 放弃这一步放置
  }
  unsetCursor();
  session_->set_tool(mode);
  cancel_plugin_point_input();
  command_system_.cancel();
  if (mode != ToolMode::None) {
    setFocus();
    // 有绘制规格的构件（墙/梁/柱/板/门/窗/结构墙/基础/幕墙）走面板武装，
    // 点 icon 不立即绘制；其余（草图）沿用立即 dispatch。
    if (find_component_spec(mode) == nullptr) {
      dispatch_tool_command(mode);
    }
  }
  request_redraw();
  emit tool_mode_changed(mode);
}

void DocumentViewport::arm_create(ToolMode mode, const CommandArgs& args) {
  if (mode == ToolMode::Slab && !plan_view_) {
    refuse_slab_outside_plan(true);
    emit tool_mode_changed(session_->tool_mode());
    return;
  }
  clear_grid_placement();
  session_->set_tool(mode);
  cancel_plugin_point_input();
  command_system_.cancel();
  setFocus();
  const ComponentSpec* spec = find_component_spec(mode);
  if (spec == nullptr) {
    log_error("arm_create: no component spec for tool");
    return;
  }
  dispatch_armed_component(spec->command.toStdString(), args);
  last_arm_mode_ = mode;
  last_arm_args_ = args;
  request_redraw();
  emit tool_mode_changed(mode);
}

void DocumentViewport::rearm_tool() {
  const ToolMode mode = session_->tool_mode();
  if (mode == ToolMode::None) {
    return;
  }
  if (find_component_spec(mode) != nullptr && last_arm_mode_ == mode) {
    // 面板构件：用上次武装参数重新 dispatch。
    command_system_.cancel();
    const ComponentSpec* spec = find_component_spec(mode);
    dispatch_armed_component(spec->command.toStdString(), last_arm_args_);
  } else {
    dispatch_tool_command(mode);
  }
}

// 武装一个构件命令，并记下"这一刻是照哪一层摆的"（见 armed_placement.h）。
void DocumentViewport::dispatch_armed_component(const std::string& command,
                                                const CommandArgs& args) {
  if (auto r = session_->dispatch(command, args); !r) {
    log_error(r.error());
    armed_placement_ = {};
    return;
  }
  // 非交互命令 dispatch 就执行完了，没有"武装着等点"的状态；只有交互式命令
  // 会留在 pending 里，才有必要记住它的楼层。
  armed_placement_ = command_system_.has_pending() ? capture_armed_placement(*document_)
                                                    : ArmedPlacement{};
}

void DocumentViewport::sync_armed_placement() {
  if (!command_system_.has_pending()) {
    armed_placement_ = {};  // 命令已经点齐执行（或取消了）：没有要跟的东西
    return;
  }
  const ToolMode mode = session_->tool_mode();
  if (find_component_spec(mode) == nullptr || last_arm_mode_ != mode) {
    return;  // 草图 / 插件点输入不吃楼层，别打断正在画的那一笔
  }
  if (!armed_placement_stale(armed_placement_, *document_)) {
    return;
  }
  // 按当前楼层重新武装：标高 / 工作面与楼层归属都从新的楼层重新取一遍，
  // 画出来的构件才既落在这一层的标高上、又归这一层。
  command_system_.cancel();
  const ComponentSpec* spec = find_component_spec(mode);
  if (spec == nullptr) {
    return;
  }
  dispatch_armed_component(spec->command.toStdString(), last_arm_args_);
}

void DocumentViewport::dispatch_tool_command(ToolMode mode) {
  if (mode == ToolMode::Wall) {
    if (auto r = session_->dispatch("create_wall",
                                          {{"thickness", kDefaultWallThickness},
                                           {"height", kDefaultWallHeight}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Beam) {
    if (auto r = session_->dispatch("create_beam",
                                          {{"width", 0.3}, {"depth", 0.5}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Column) {
    if (auto r = session_->dispatch("create_column", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Slab) {
    // 不传 elevation：按当前楼层层高落在本层顶（顶板）。
    if (auto r = session_->dispatch("create_slab", {{"thickness", 0.2}}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Door) {
    if (auto r = session_->dispatch("create_door", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Window) {
    if (auto r = session_->dispatch("create_window", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Line) {
    if (auto r = session_->dispatch("create_line", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Polyline) {
    if (auto r = session_->dispatch("create_polyline", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Circle) {
    if (auto r = session_->dispatch("create_circle", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Arc) {
    if (auto r = session_->dispatch("create_arc", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Bezier) {
    if (auto r = session_->dispatch("create_bezier", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::BSpline) {
    if (auto r = session_->dispatch("create_bspline", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Rectangle) {
    if (auto r = session_->dispatch("create_rectangle", {}); !r) {
      log_error(r.error());
    }
  }
}

bool DocumentViewport::finish_pending_if_done(const Result<bool>& done) {
  if (!done) {
    log_error(done.error());
    if (session_->tool_mode() != ToolMode::None) {
      rearm_tool();
    }
    request_redraw();
    return true;
  }
  if (*done) {
    resync_all_meshes();
    rebuild_bvh();
    emit document_changed();
    // 画完一个实体后继续同一绘制命令，直到 Esc / 右键退出。
    if (session_->tool_mode() != ToolMode::None) {
      rearm_tool();
    }
    request_redraw();
    return true;
  }
  return false;
}

void DocumentViewport::cancel_tool() {
  unsetCursor();
  if (pending_grid_) {
    cancel_grid_placement();
  } else if (plugin_point_input_.active()) {
    cancel_plugin_point_input();
    request_redraw();
  } else if (session_->tool_mode() == ToolMode::None &&
             command_system_.has_pending()) {
    command_system_.cancel();
    request_redraw();
  } else if (session_->tool_mode() != ToolMode::None && command_system_.drag_started()) {
    command_system_.cancel();
    dispatch_tool_command(session_->tool_mode());  // 取消当前放置，仍留在工具
    request_redraw();
  } else if (session_->tool_mode() != ToolMode::None) {
    set_tool(ToolMode::None);  // 退出工具
  }
}

void DocumentViewport::adjust_selected_param(double delta) {
  Entity* entity = document_->selected_entity();
  if (entity == nullptr) {
    return;
  }
  // 找可编辑的轮廓特征：RectProfile / PolygonProfile 的 width，或 CircleProfile 的 radius。
  std::uint64_t feature_id = 0;
  std::string param_name;
  double current = 0.0;
  for (const auto& f : entity->model.features()) {
    if (f.kind == FeatureKind::RectProfile || f.kind == FeatureKind::PolygonProfile) {
      feature_id = f.id;
      param_name = "width";
      current = entity->model.param(f.id, "width", 0.0);
      break;
    }
    if (f.kind == FeatureKind::CircleProfile || f.kind == FeatureKind::CircleWire) {
      feature_id = f.id;
      param_name = "radius";
      current = entity->model.param(f.id, "radius", 0.0);
      break;
    }
  }
  if (feature_id == 0) {
    return;
  }
  run_command("set_param",
              {{"entity_id", static_cast<std::int64_t>(entity->id)},
               {"feature_id", static_cast<std::int64_t>(feature_id)},
               {"param_name", param_name},
               {"value", current + delta}});
}

void DocumentViewport::run_command(const std::string& name, const CommandArgs& args,
                                   bool notify) {
  if (auto r = session_->dispatch(name, args); r) {
    resync_all_meshes();
    document_->recompute_scene();
    rebuild_bvh();
    request_redraw();
    if (notify) {
      emit document_changed();
    }
  } else {
    log_error(r.error());
  }
}

void DocumentViewport::refresh_after_edit() {
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
  emit selection_changed();
}

void DocumentViewport::notify_selection_changed() {
  request_redraw();
  emit selection_changed();
}

Result<void> DocumentViewport::begin_plugin_point_input(
    PluginPointInputRequest request, PluginHost::PointInputCompletion completion) {
  set_tool(ToolMode::None);
  setFocus();
  auto started = plugin_point_input_.begin(
      std::move(request),
      [this, completion = std::move(completion)](
          std::vector<PluginPickPoint> points, bool cancelled) mutable {
        emit plugin_point_input_changed(false);
        completion(std::move(points), cancelled);
        request_redraw();
      });
  if (started) {
    emit plugin_point_input_changed(true);
    request_redraw();
  }
  return started;
}

void DocumentViewport::cancel_plugin_point_input(std::uint64_t request_id) {
  plugin_point_input_.cancel(request_id);
  request_redraw();
}

void DocumentViewport::set_entity_param(std::uint64_t entity_id, std::uint64_t feature_id,
                                        const std::string& param_name, double value) {
  // 属性面板自己已经在 spinbox 里展示了新值，无需再通知刷新（避免重建丢焦点）。
  run_command("set_param",
              {{"entity_id", static_cast<std::int64_t>(entity_id)},
               {"feature_id", static_cast<std::int64_t>(feature_id)},
               {"param_name", param_name},
               {"value", value}},
              /*notify=*/false);
}

void DocumentViewport::set_entity_material(std::uint64_t entity_id, const Material& material) {
  // 纯视觉：材质只影响渲染，不重建网格/场景/BVH，仅重绘一帧。
  // 属性面板自己已展示新值，无需 notify 刷新（避免重建丢焦点）。
  if (auto r = session_->dispatch(
          "set_material",
          {{"entity_id", static_cast<std::int64_t>(entity_id)},
           {"material_id", static_cast<std::int64_t>(material.id)},
           {"name", material.name},
           {"base_color", material.base_color},
           {"roughness", static_cast<double>(material.roughness)},
           {"metallic", static_cast<double>(material.metallic)},
           {"opacity", static_cast<double>(material.opacity)},
           {"albedo_texture_id", static_cast<std::int64_t>(material.albedo_texture_id)},
           {"normal_texture_id", static_cast<std::int64_t>(material.normal_texture_id)},
           {"orm_texture_id", static_cast<std::int64_t>(material.orm_texture_id)},
           {"tex_scale_x", static_cast<double>(material.tex.scale.x)},
           {"tex_scale_y", static_cast<double>(material.tex.scale.y)},
           {"tex_offset_x", static_cast<double>(material.tex.offset.x)},
           {"tex_offset_y", static_cast<double>(material.tex.offset.y)},
           {"tex_rotation", static_cast<double>(material.tex.rotation)},
           {"tex_world_scale", static_cast<double>(material.tex.world_scale)}});
      r) {
    request_redraw();
  } else {
    log_error(r.error());
  }
}

std::uint64_t DocumentViewport::import_texture(TextureAsset asset) {
  auto cmd = std::make_unique<ImportTextureCommand>(*document_, std::move(asset));
  ImportTextureCommand* raw = cmd.get();
  if (auto r = raw->execute(); !r) {
    log_error(r.error());
    return 0;
  }
  const std::uint64_t id = raw->texture_id();
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
  emit document_changed();
  return id;
}

void DocumentViewport::replace_texture(std::uint64_t id, TextureAsset asset) {
  auto cmd = std::make_unique<ReplaceTextureCommand>(*document_, id, std::move(asset));
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
  emit document_changed();
}

void DocumentViewport::update_library_material(const Material& material) {
  auto cmd = std::make_unique<UpdateMaterialCommand>(*document_, material);
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
}

void DocumentViewport::create_storey(const std::string& name, double elevation) {
  run_command("create_storey", {{"name", name}, {"elevation", elevation}});
}

void DocumentViewport::set_active_storey(std::uint64_t storey_id) {
  document_->set_active_storey(storey_id);
  request_redraw();
  emit document_changed();
}

void DocumentViewport::set_entity_location(std::uint64_t entity_id, std::uint64_t storey_id,
                                           double elevation_offset) {
  run_command("set_location",
              {{"entity_id", static_cast<std::int64_t>(entity_id)},
               {"storey_id", static_cast<std::int64_t>(storey_id)},
               {"elevation_offset", elevation_offset}},
              /*notify=*/false);
}

void DocumentViewport::fillet_selected(double radius) {
  const Entity* e = document_->selected_entity();
  if (e == nullptr) {
    log_error("fillet: no entity selected");
    return;
  }
  run_command("fillet", {{"entity_id", static_cast<std::int64_t>(e->id)},
                         {"radius", radius},
                         {"edge", static_cast<std::int64_t>(0)}});
}

void DocumentViewport::chamfer_selected(double distance) {
  const Entity* e = document_->selected_entity();
  if (e == nullptr) {
    log_error("chamfer: no entity selected");
    return;
  }
  run_command("chamfer", {{"entity_id", static_cast<std::int64_t>(e->id)},
                          {"distance", distance},
                          {"edge", static_cast<std::int64_t>(0)}});
}

void DocumentViewport::delete_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  for (const std::uint64_t id : ids) {
    if (document_->entity(id) == nullptr) {
      continue;
    }
    run_command("delete_entity", {{"entity_id", static_cast<std::int64_t>(id)}});
  }
  // 选中的轴线一次删掉：留表 = 原来的表去掉选中项，仍然一步撤销。
  if (document_->bim().grid().has_selection()) {
    std::vector<GridAxis> keep;
    for (const GridAxis& axis : document_->bim().grid().axes()) {
      if (!axis.selected) {
        keep.push_back(axis);
      }
    }
    apply_grid_settings(std::move(keep));
  }
  // 选中的文字注记（一步撤销，删完清掉选中）。
  if (selected_text_id_ != 0) {
    run_command("delete_text", {{"text_id", static_cast<std::int64_t>(selected_text_id_)}});
    select_text(0);
  }
  emit selection_changed();
}

std::uint64_t DocumentViewport::pick_node_at(const QPoint& pos) const {
  const Ray ray = ray_at(pos);
  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        return node_visible_in_view(id);
      })) {
    return hit->node_id;
  }
  return 0;
}

float DocumentViewport::grid_plane_y() const {
  return static_cast<float>(
      document_->bim().storey_elevation(document_->bim().active_storey_id()));
}

std::uint64_t DocumentViewport::pick_grid_axis_at(const QPoint& pos) const {
  if (!grid_visible_ || document_->bim().grid().empty()) {
    return 0;
  }
  const QSize area = scene_area_size();
  return pick_grid_axis_on_screen(document_->bim().grid().axes(), view_proj(),
                                  static_cast<float>(area.width()),
                                  static_cast<float>(area.height()), grid_plane_y(),
                                  static_cast<float>(pos.x()), static_cast<float>(pos.y()),
                                  kGridPickPixels);
}

// 轴网不进实体表，所以它的选中态存在 BIM 层（GridAxis::selected），这里只做语义：
// 点一根 = 换成只选它；Shift 点 = 加选 / 减选。选轴网时把实体选择放掉，反之亦然，
// 免得删除键同时打到两拨东西。
void DocumentViewport::select_grid_axis(std::uint64_t axis_id, bool additive) {
  Grid& grid = document_->bim().grid();
  if (additive) {
    if (grid.axis_selected(axis_id)) {
      grid.deselect(axis_id);
    } else {
      grid.select(axis_id);
    }
  } else {
    session_->clear_selection();
    grid.clear_selection();
    grid.select(axis_id);
  }
  request_redraw();
  emit selection_changed();
}

void DocumentViewport::show_grid_context_menu(const QPoint& global_pos) {
  const std::vector<std::uint64_t> ids = document_->bim().grid().selected_ids();
  if (ids.empty()) {
    return;
  }
  QMenu menu(this);
  QAction* delete_act =
      menu.addAction(tr("Delete %1 axes").arg(static_cast<int>(ids.size())));
  delete_act->setShortcut(QKeySequence::Delete);
  if (menu.exec(global_pos) == delete_act) {
    delete_selected();
  }
}

std::optional<std::pair<std::uint64_t, Vec3>> DocumentViewport::pick_wall_at(
    const QPoint& pos) const {
  const Ray ray = ray_at(pos);
  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        if (!node_visible_in_view(id)) {
          return false;
        }
        const Entity* entity = document_->entity(id);
        return entity != nullptr && is_wall_host(*entity);
      })) {
    return std::pair<std::uint64_t, Vec3>{hit->node_id, ray.origin + ray.direction * hit->t};
  }
  return std::nullopt;
}

bool DocumentViewport::is_opening_placement_tool() const {
  return session_->tool_mode() == ToolMode::Window || session_->tool_mode() == ToolMode::Door;
}

void DocumentViewport::update_opening_hover(const QPoint& pos) {
  if (!is_opening_placement_tool() || !command_system_.has_pending()) {
    return;
  }
  if (auto wall = pick_wall_at(pos)) {
    command_system_.hover(wall->second, wall->first);
    setCursor(Qt::CrossCursor);
  } else {
    command_system_.hover(cursor_ground_position(pos), 0);
    setCursor(Qt::ForbiddenCursor);
  }
}

void DocumentViewport::show_entity_context_menu(const QPoint& global_pos) {
  // 选中的是文字注记：给它的菜单（编辑 / 删除），和构件菜单不是一个。
  if (selected_text_id_ != 0) {
    QMenu text_menu(this);
    QAction* edit_act = text_menu.addAction(tr("Edit Text…"));
    QAction* delete_act = text_menu.addAction(tr("Delete"));
    delete_act->setShortcut(QKeySequence::Delete);
    QAction* chosen = text_menu.exec(global_pos);
    if (chosen == edit_act) {
      edit_text_annotation(selected_text_id_);
    } else if (chosen == delete_act) {
      delete_selected();
    }
    return;
  }
  if (document_->selected_entity() == nullptr) {
    return;
  }
  QMenu menu(this);
  QAction* hide_act = menu.addAction(tr("Hide"));
  QAction* isolate_act = menu.addAction(tr("Isolate"));
  menu.addSeparator();
  QAction* delete_act = menu.addAction(tr("Delete"));
  delete_act->setShortcut(QKeySequence::Delete);
  QAction* chosen = menu.exec(global_pos);
  if (chosen == hide_act) {
    hide_selected();
  } else if (chosen == isolate_act) {
    isolate_selected();
  } else if (chosen == delete_act) {
    delete_selected();
  }
}

void DocumentViewport::resync_all_meshes() {
  if (!render_thread_) {
    return;
  }
  for (const auto& [unused, asset] : document_->meshes()) {
    (void)unused;
    if (asset.cpu.vertices.empty() || asset.cpu.indices.empty()) {
      continue;
    }
    render_thread_->request_upload_mesh(asset.id, asset.cpu);
  }
}

void DocumentViewport::resync_textures() {
  if (!render_thread_) {
    return;
  }
  auto forget_evicted = [this]() {
    for (const std::uint64_t id : render_thread_->take_evicted_texture_ids()) {
      // 字形图集也走同一条 LRU：被逐出就下次提交前重新上传。
      if (id == kTextAtlasTextureAssetId) {
        text_atlas_texture_id_ = 0;
        text_atlas_generation_ = 0;
        continue;
      }
      // 底图贴图也走同一条 LRU：被逐出就下次提交前重新光栅化 + 上传。
      if (id >= kDrawingTextureAssetIdBase) {
        for (auto& [unused, underlay] : drawing_underlays_) {
          (void)unused;
          if (underlay.texture_asset_id == id) {
            underlay.texture_id = 0;
            underlay.needs_upload = true;
          }
        }
        continue;
      }
      uploaded_textures_.erase(id);
    }
  };
  forget_evicted();
  for (const auto& [id, asset] : document_->textures()) {
    if (auto it = uploaded_textures_.find(id);
        it != uploaded_textures_.end() && it->second == asset.generation) {
      continue;
    }
    if (auto gpu = render_thread_->upload_texture(id, asset); !gpu) {
      log_error(gpu.error());
    } else {
      uploaded_textures_[id] = asset.generation;
    }
  }
  forget_evicted();
  sync_drawing_underlays();
}

void DocumentViewport::undo() {
  if (!session_->can_undo()) {
    return;
  }
  session_->undo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

void DocumentViewport::redo() {
  if (!session_->can_redo()) {
    return;
  }
  session_->redo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

bool DocumentViewport::grid_snap_active() const {
  if (plugin_point_input_.active()) {
    return plugin_point_input_.grid_snap();
  }
  if (session_->tool_mode() == ToolMode::None) {
    return command_system_.has_pending();
  }
  return session_->tool_mode() != ToolMode::None && session_->tool_mode() != ToolMode::Door &&
         session_->tool_mode() != ToolMode::Window;
}

Vec3 DocumentViewport::cursor_ground_position(const QPoint& pos) const {
  const Ray ray = ray_at(pos);
  Vec3 hit = ray.origin + ray.direction * camera_.distance();
  // 与当前工作面求交（地面 y=0；画板时是板的标高，避免透视下点偏）。
  const float plane_y = plugin_point_input_.active()
                            ? plugin_point_input_.work_plane_y()
                            : command_system_.work_plane_y();
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      hit = ray.origin + ray.direction * t;
    }
  }
  if (grid_snap_active()) {
    return snapped_ground_position(pos);
  }
  return hit;
}

Vec3 DocumentViewport::snapped_ground_position(const QPoint& pos) const {
  const Ray ray = ray_at(pos);
  Vec3 hit = ray.origin + ray.direction * camera_.distance();
  const float plane_y = plugin_point_input_.active()
                            ? plugin_point_input_.work_plane_y()
                            : command_system_.work_plane_y();
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      hit = ray.origin + ray.direction * t;
    }
  }
  const float dist = length(hit - camera_.eye_position());
  const float radius = grid_snap_world_radius(dist, camera_.fovy(),
                                             static_cast<float>(scene_area_size().height()));
  return snap_to_grid_xz_if_near(hit, radius);
}

Mat4 DocumentViewport::view_proj() const {
  const QSize area = scene_area_size();
  return camera_.proj_matrix(static_cast<float>(area.width()) /
                             static_cast<float>(area.height())) *
         camera_.view_matrix();
}

void DocumentViewport::update_box_select_rect(const QPoint& pos) {
  if (box_select_overlay_ == nullptr) {
    return;
  }
  const bool crossing = pos.x() < press_mouse_.x();
  box_select_overlay_->set_box(QRect(press_mouse_, pos), crossing);
}

void DocumentViewport::finish_box_select(const QPoint& pos, bool additive) {
  if (box_select_overlay_ != nullptr) {
    box_select_overlay_->hide_box();
  }
  if ((pos - press_mouse_).manhattanLength() < 4) {
    return;
  }
  const bool crossing = pos.x() < press_mouse_.x();
  const QSize area = scene_area_size();
  const std::vector<std::uint64_t> ids = nodes_in_screen_rect(
      *document_, view_proj(), static_cast<float>(area.width()), static_cast<float>(area.height()),
      static_cast<float>(press_mouse_.x()), static_cast<float>(press_mouse_.y()),
      static_cast<float>(pos.x()), static_cast<float>(pos.y()), crossing);
  // 轴网不进实体表，得单独框一次：轴线的投影线段跟框相交（crossing）/ 整段在框内（window）。
  const std::vector<std::uint64_t> axis_ids =
      grid_visible_ ? grid_axes_in_screen_rect(
                          document_->bim().grid().axes(), view_proj(),
                          static_cast<float>(area.width()), static_cast<float>(area.height()),
                          grid_plane_y(), static_cast<float>(press_mouse_.x()),
                          static_cast<float>(press_mouse_.y()), static_cast<float>(pos.x()),
                          static_cast<float>(pos.y()), crossing)
                    : std::vector<std::uint64_t>{};
  Grid& grid = document_->bim().grid();
  if (!additive) {
    session_->clear_selection();
    grid.clear_selection();
  }
  for (const std::uint64_t id : ids) {
    if (node_visible_in_view(id)) {
      session_->select(id);
    }
  }
  for (const std::uint64_t id : axis_ids) {
    grid.select(id);
  }
  request_redraw();
  emit selection_changed();
}

void DocumentViewport::set_plan_view(bool plan, bool restore_perspective) {
  const bool had_floor_view = floor_view_.has_value();
  const bool had_floor_filter = !hidden_floors_.empty();
  apply_plan_view(plan, restore_perspective, /*animate=*/true);
  // 在某一层的视图里 2D ⇄ 3D 换的只是看法，不是"离开这一层"：楼层过滤、楼层的
  // 高亮都留在原地。回到全局三维走 open_global_view（楼层管理页第一行）/ 全部显示。
  if (had_floor_view && !floor_view_.has_value()) {
    emit view_changed();
  }
  if (had_floor_filter && hidden_floors_.empty()) {
    emit visibility_changed();
  }
}

void DocumentViewport::apply_plan_view(bool plan, bool restore_perspective, bool animate) {
  // 三维不吃掉楼层视图：当前打开的是某一层时，这里是"这一层的三维"——只留这一层的
  // 过滤照旧，相机的目标点 / 距离也是 open_floor_view 框这一层时定下的。
  if (plan_view_ != plan) {
    if (plan) {
      persp_yaw_ = camera_.yaw();
      persp_pitch_ = camera_.pitch();
      plan_view_ = true;
      if (animate) {
        start_view_animation(0.f, kHalfPi, true);
      } else {
        stop_view_animation();
        camera_.set_yaw_pitch(0.f, kHalfPi);
        camera_.set_orthographic(true);
      }
    } else {
      plan_view_ = false;
      camera_.set_orthographic(false);
      if (restore_perspective) {
        if (animate) {
          start_view_animation(persp_yaw_, persp_pitch_);
        } else {
          stop_view_animation();
          camera_.set_yaw_pitch(persp_yaw_, persp_pitch_);
        }
      }
    }
  }
  if (tool_panel_) {
    tool_panel_->set_plan_view(plan_view_);
  }
  if (!plan_view_ && session_->tool_mode() == ToolMode::Slab) {
    set_tool(ToolMode::None);
    refuse_slab_outside_plan(false);
  }
  request_redraw();
}

// 全局三维：所有楼层都在、透视、框住整个模型。楼层管理页的第一行双击走这里。
void DocumentViewport::open_global_view() {
  refresh_floors();
  const bool had_view = floor_view_.has_value();
  const bool had_filter = !hidden_floors_.empty();
  floor_view_.reset();
  hidden_floors_.clear();
  apply_plan_view(false, /*restore_perspective=*/true, /*animate=*/false);
  frame_scene();
  if (had_filter) {
    emit visibility_changed();
  }
  if (had_view) {
    emit view_changed();
  }
}

// 某一层的视图：只留这一层 + 把它设为当前楼层 + 切到平面（2D）+ 相机框到这一层。
// 楼层管理页里双击某个楼层走这里。
void DocumentViewport::open_floor_view(std::size_t floor_index) {
  refresh_floors();
  if (floor_index >= floors_.size()) {
    return;
  }
  const std::optional<std::size_t> before = floor_view_;
  const bool had_filter = !hidden_floors_.empty();
  stop_view_animation();
  floor_view_ = floor_index;
  // 当前楼层跟着视图走；按几何临时分出来的层没有楼层记录，就保持当前楼层不动。
  const std::uint64_t storey_id = floors_[floor_index].storey_id;
  const bool storey_changed =
      storey_id != 0 && document_->bim().active_storey_id() != storey_id;
  if (storey_changed) {
    document_->set_active_storey(storey_id);
  }
  // 只留这一层：跨层构件碰到任意一个可见楼层就还看得见，和楼层面板一个口径。
  std::unordered_set<int> hidden;
  for (std::size_t i = 0; i < floors_.size(); ++i) {
    if (i != floor_index) {
      hidden.insert(static_cast<int>(i));
    }
  }
  const bool filter_changed = hidden != hidden_floors_;
  hidden_floors_ = std::move(hidden);
  apply_plan_view(true, /*restore_perspective=*/false, /*animate=*/false);
  const Aabb box = floor_view_box(floor_index);
  if (box.valid()) {
    camera_.frame_aabb(box);
  }
  request_redraw();
  if (storey_changed) {
    emit document_changed();
  }
  if (filter_changed || (had_filter && hidden_floors_.empty())) {
    emit visibility_changed();
  }
  if (floor_view_ != before) {
    emit view_changed();
  }
}

void DocumentViewport::hide_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  if (ids.empty()) {
    return;
  }
  for (const std::uint64_t id : ids) {
    hidden_ids_.insert(id);
    isolated_ids_.erase(id);
  }
  request_redraw();
  emit visibility_changed();
}

void DocumentViewport::isolate_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  if (ids.empty()) {
    return;
  }
  isolated_ids_.clear();
  for (const std::uint64_t id : ids) {
    isolated_ids_.insert(id);
    hidden_ids_.erase(id);
  }
  request_redraw();
  emit visibility_changed();
}

void DocumentViewport::show_all_visible() {
  const bool had_view = floor_view_.has_value();
  floor_view_.reset();
  hidden_ids_.clear();
  isolated_ids_.clear();
  hidden_kinds_.clear();
  hidden_floors_.clear();  // "全部显示"就是真的全部：楼层过滤也复位
  request_redraw();
  emit visibility_changed();
  if (had_view) {
    emit view_changed();
  }
}

void DocumentViewport::set_kind_hidden(EntityKind kind, bool hidden) {
  if (hidden) {
    hidden_kinds_.insert(kind);
  } else {
    hidden_kinds_.erase(kind);
    // 勾上 = 真的能看见：把这一类被单独隐藏的构件放回来。
    for (const auto& [unused, entity] : document_->entities()) {
      (void)unused;
      if (entity != nullptr && entity->kind() == kind) {
        hidden_ids_.erase(entity->id);
      }
    }
  }
  // 面板是唯一的显隐真相：碰过面板就撤掉逐件隔离，否则会出现"勾上却看不见"。
  isolated_ids_.clear();
  request_redraw();
  emit visibility_changed();
}

void DocumentViewport::set_kinds_hidden(const std::vector<EntityKind>& kinds, bool hidden) {
  bool changed = false;
  for (const EntityKind kind : kinds) {
    if (hidden) {
      changed = hidden_kinds_.insert(kind).second || changed;
    } else {
      changed = hidden_kinds_.erase(kind) != 0 || changed;
      for (const auto& [unused, entity] : document_->entities()) {
        (void)unused;
        if (entity != nullptr && entity->kind() == kind) {
          changed = hidden_ids_.erase(entity->id) != 0 || changed;
        }
      }
    }
  }
  changed = !isolated_ids_.empty() || changed;
  isolated_ids_.clear();
  if (!changed) {
    return;
  }
  request_redraw();
  emit visibility_changed();
}

void DocumentViewport::isolate_kind(EntityKind kind) {
  // "只显示这一类" = 其余类别整体隐藏 + 导入网格隐藏；单件隐藏/隔离先复位，
  // 否则会出现"勾上了却还是看不见"的困惑。
  hidden_ids_.clear();
  isolated_ids_.clear();
  hidden_kinds_.clear();
  for (const auto& [unused, entity] : document_->entities()) {
    (void)unused;
    if (entity != nullptr && entity->kind() != kind) {
      hidden_kinds_.insert(entity->kind());
    }
  }
  for (const std::uint64_t id : imported_node_ids()) {
    hidden_ids_.insert(id);
  }
  request_redraw();
  emit visibility_changed();
}

void DocumentViewport::frame_kind(EntityKind kind) {
  Aabb box{};
  bool any = false;
  for (const auto& [unused, entity] : document_->entities()) {
    (void)unused;
    if (entity == nullptr || entity->kind() != kind) {
      continue;
    }
    const SceneNode* node = document_->scene().find(entity->id);
    if (node == nullptr || !node->world_bounds.valid()) {
      continue;
    }
    if (!any) {
      box = node->world_bounds;
      any = true;
    } else {
      box.expand(node->world_bounds.min);
      box.expand(node->world_bounds.max);
    }
  }
  if (!any) {
    return;
  }
  stop_view_animation();
  camera_.frame_aabb(box);
  request_redraw();
}

VisibilityCounts DocumentViewport::visibility_counts() const {
  VisibilityCounts counts;
  for (const auto& [unused, entity] : document_->entities()) {
    (void)unused;
    if (entity != nullptr) {
      ++counts.kinds[entity->kind()];
    }
  }
  counts.imported = imported_node_ids().size();
  return counts;
}

bool DocumentViewport::imported_hidden() const {
  for (const std::uint64_t id : imported_node_ids()) {
    if (hidden_ids_.count(id) != 0) {
      return true;
    }
  }
  return false;
}

void DocumentViewport::set_imported_hidden(bool hidden) {
  bool changed = false;
  for (const std::uint64_t id : imported_node_ids()) {
    if (hidden) {
      changed = hidden_ids_.insert(id).second || changed;
    } else {
      changed = hidden_ids_.erase(id) != 0 || changed;
    }
  }
  if (!changed) {
    if (isolated_ids_.empty()) {
      return;
    }
  }
  isolated_ids_.clear();
  request_redraw();
  emit visibility_changed();
}

std::unordered_set<EntityKind> DocumentViewport::isolated_kinds() const {
  std::unordered_set<EntityKind> kinds;
  for (const std::uint64_t id : isolated_ids_) {
    const Entity* entity = document_->entity(id);
    if (entity != nullptr) {
      kinds.insert(entity->kind());
    }
  }
  return kinds;
}

bool DocumentViewport::has_active_filter() const {
  return !hidden_ids_.empty() || !isolated_ids_.empty() || !hidden_kinds_.empty() ||
         !hidden_floors_.empty();
}

void DocumentViewport::toggle_visibility_panel() {
  if (tool_panel_ != nullptr) {
    tool_panel_->toggle_visibility_page();
  }
}

void DocumentViewport::toggle_floor_panel() {
  if (tool_panel_ != nullptr) {
    tool_panel_->toggle_floor_page();
  }
}

void DocumentViewport::toggle_floor_manager_panel() {
  if (tool_panel_ != nullptr) {
    tool_panel_->toggle_floor_manager_page();
  }
}

void DocumentViewport::toggle_drawing_panel() {
  if (tool_panel_ != nullptr) {
    tool_panel_->toggle_drawing_page();
  }
}

void DocumentViewport::set_drawing_panel_open(bool open) {
  if (tool_panel_ != nullptr) {
    tool_panel_->set_drawing_page_open(open);
  }
}

// 图纸清单是文档的一部分（随 .tdoc 存），但增删都由视口落笔：标脏 + 通知面板刷新。
void DocumentViewport::add_document_drawings(const std::vector<std::string>& paths) {
  bool changed = false;
  for (const std::string& path : paths) {
    DrawingRef ref;
    ref.path = path;
    // 新挂上的图纸按图纸自带的单位（没写单元就按模型范围适配）摆一次，
    // 挂上就能看见；之后由「图纸设置」调准。
    ref.placement = default_drawing_placement_for(path);
    changed = document_->add_drawing(std::move(ref)) || changed;
  }
  if (!changed) {
    return;
  }
  document_->mark_dirty();
  request_redraw();
  emit document_changed();
  emit drawings_changed();
}

void DocumentViewport::remove_document_drawing(const std::string& path) {
  if (!document_->remove_drawing(path)) {
    return;
  }
  document_->mark_dirty();
  request_redraw();
  emit document_changed();
  emit drawings_changed();
}

// 摆放参考范围：有模型就用模型的俯视范围，模型还空着就用轴网（翻模最常用的
// 定位基准），都没有就返回空盒（默认摆放退回"图纸中心压世界原点"）。
Aabb2 DocumentViewport::drawing_footprint() const {
  Aabb2 box{};
  const Aabb bounds = document_->bounds();
  if (bounds.valid()) {
    box.expand(bounds.min.x, bounds.min.z);
    box.expand(bounds.max.x, bounds.max.z);
  }
  const Aabb grid = document_->bim().grid().bounds();
  if (grid.valid()) {
    box.expand(grid.min.x, grid.min.z);
    box.expand(grid.max.x, grid.max.z);
  }
  return box;
}

DrawingPlacement DocumentViewport::default_drawing_placement_for(const std::string& path) {
  DrawingPlacement placement;
  placement.elevation = static_cast<double>(grid_plane_y());
  QString error;
  std::unique_ptr<DrawingDocument> document =
      DrawingDocument::open(QString::fromStdString(path), error);
  if (!document) {
    placement.scale = 1.0;
    return placement;
  }
  const Aabb2 page = page_bounds_of(*document, 0);
  if (!page.valid()) {
    return placement;
  }
  return default_drawing_placement(page, drawing_footprint(), placement.elevation,
                                   document->declared_unit_scale());
}

void DocumentViewport::set_drawings_visible(bool visible) {
  if (drawings_visible_ == visible) {
    return;
  }
  drawings_visible_ = visible;
  request_redraw();
  emit drawings_changed();
}

void DocumentViewport::set_drawing_visible(const std::string& path, bool visible) {
  DrawingRef* ref = document_->drawing(path);
  if (ref == nullptr || ref->visible == visible) {
    return;
  }
  ref->visible = visible;
  document_->mark_dirty();
  request_redraw();
  emit document_changed();
  emit drawings_changed();
}

void DocumentViewport::set_drawing_placement(const std::string& path,
                                             const DrawingPlacement& placement, int page) {
  DrawingRef* ref = document_->drawing(path);
  if (ref == nullptr) {
    return;
  }
  ref->placement = placement;
  ref->page = std::max(0, page);
  document_->mark_dirty();
  request_redraw();
  emit document_changed();
  emit drawings_changed();
}

void DocumentViewport::fit_drawing_to_model(const std::string& path) {
  DrawingRef* ref = document_->drawing(path);
  if (ref == nullptr) {
    return;
  }
  const auto it = drawing_underlays_.find(path);
  if (it == drawing_underlays_.end() || !it->second.document) {
    return;
  }
  const Aabb2 page = page_bounds_of(*it->second.document, ref->page);
  if (!page.valid()) {
    return;
  }
  ref->placement = default_drawing_placement(page, drawing_footprint(), grid_plane_y(),
                                             it->second.document->declared_unit_scale());
  document_->mark_dirty();
  request_redraw();
  emit document_changed();
  emit drawings_changed();
}

void DocumentViewport::frame_drawing(const std::string& path) {
  const DrawingRef* ref = document_->drawing(path);
  if (ref == nullptr) {
    return;
  }
  const auto it = drawing_underlays_.find(path);
  if (it == drawing_underlays_.end() || !it->second.document) {
    return;
  }
  const Aabb2 page = page_bounds_of(*it->second.document, ref->page);
  if (!page.valid()) {
    return;
  }
  const Aabb2 footprint = drawing_plane_footprint(page, ref->placement);
  Aabb box{};
  const float elevation = static_cast<float>(ref->placement.elevation);
  box.expand({footprint.min_x, elevation, footprint.min_y});
  box.expand({footprint.max_x, elevation, footprint.max_y});
  stop_view_animation();
  camera_.frame_aabb(box);
  request_redraw();
}

QString DocumentViewport::drawing_status(const std::string& path) const {
  const auto it = drawing_underlays_.find(path);
  return it == drawing_underlays_.end() ? QString() : it->second.error;
}

std::optional<DocumentViewport::DrawingInfo> DocumentViewport::drawing_info(
    const std::string& path) {
  sync_drawing_underlays();  // 还没加载过的图纸先读进来，否则页数/单位是空的
  const auto it = drawing_underlays_.find(path);
  if (it == drawing_underlays_.end()) {
    return std::nullopt;
  }
  DrawingInfo info;
  info.error = it->second.error;
  if (!it->second.document) {
    return info;
  }
  info.page_count = std::max(1, it->second.document->page_count());
  info.declared_unit_scale = it->second.document->declared_unit_scale();
  return info;
}

std::optional<DrawingPlacement> DocumentViewport::suggested_drawing_placement(
    const std::string& path, int page) {
  sync_drawing_underlays();
  const auto it = drawing_underlays_.find(path);
  if (it == drawing_underlays_.end() || !it->second.document) {
    return std::nullopt;
  }
  const Aabb2 bounds = page_bounds_of(*it->second.document, page);
  if (!bounds.valid()) {
    return std::nullopt;
  }
  return default_drawing_placement(bounds, drawing_footprint(), grid_plane_y(),
                                   it->second.document->declared_unit_scale());
}

// 加载图纸 → 光栅化成透明底线稿 → 上传成贴图。加载失败（文件被挪走 / 格式读不了）
// 只记错误，不影响别的图纸，也不影响模型。
void DocumentViewport::sync_drawing_underlays() {
  if (render_thread_ == nullptr) {
    return;
  }
  const std::vector<DrawingRef>& refs = document_->drawings();
  // 1) 从清单上去掉的图纸，运行时状态一并丢掉。
  for (auto it = drawing_underlays_.begin(); it != drawing_underlays_.end();) {
    if (document_->drawing(it->first) == nullptr) {
      it = drawing_underlays_.erase(it);
    } else {
      ++it;
    }
  }
  // 2) 新挂上的图纸现读（图纸内容不并进文档）。
  for (const DrawingRef& ref : refs) {
    DrawingUnderlay& underlay = drawing_underlays_[ref.path];
    if (underlay.texture_asset_id == 0) {
      underlay.texture_asset_id = next_drawing_texture_asset_id_++;
    }
    if (underlay.document || !underlay.error.isEmpty()) {
      continue;
    }
    QString error;
    underlay.document = DrawingDocument::open(QString::fromStdString(ref.path), error);
    if (!underlay.document) {
      underlay.error = error.isEmpty() ? tr("Cannot read this drawing.") : error;
      continue;
    }
    underlay.needs_upload = true;
  }
  // 3) 页号变了要重新光栅化（多页 DWFx / PDF 换页）。
  for (const DrawingRef& ref : refs) {
    DrawingUnderlay& underlay = drawing_underlays_[ref.path];
    if (underlay.document && underlay.raster_page != ref.page) {
      underlay.needs_upload = true;
    }
  }
  // 4) 光栅化 + 上传（只在换页 / 首次 / 贴图被逐出时做）。
  for (const DrawingRef& ref : refs) {
    DrawingUnderlay& underlay = drawing_underlays_[ref.path];
    // 关掉的图纸先不光栅化：勾上时（raster_page 还是 -1）自然会在下一帧补上，
    // 免得挂一堆看不见的底图白占显存。
    if (!ref.visible || !underlay.document || !underlay.needs_upload) {
      continue;
    }
    const QImage image =
        underlay.document->render_page_rgba(ref.page, kDrawingRasterEdge, true);
    underlay.needs_upload = false;
    underlay.texture_id = 0;
    underlay.raster_page = ref.page;
    if (image.isNull()) {
      continue;
    }
    TextureAsset asset;
    asset.id = underlay.texture_asset_id;
    asset.name = QFileInfo(QString::fromStdString(ref.path)).fileName().toStdString();
    asset.source_path = ref.path;
    asset.width = static_cast<std::uint32_t>(image.width());
    asset.height = static_cast<std::uint32_t>(image.height());
    const auto* bits = image.constBits();
    asset.rgba.assign(bits, bits + static_cast<std::size_t>(image.sizeInBytes()));
    asset.srgb = true;
    asset.generation = underlay.texture_generation + 1;
    if (auto gpu = render_thread_->upload_texture(underlay.texture_asset_id, std::move(asset));
        !gpu) {
      log_error(gpu.error());
      underlay.needs_upload = true;
    } else {
      underlay.texture_id = *gpu;
      ++underlay.texture_generation;
    }
  }
}

// 把要画的图纸塞进这一帧：挡在它前面的构件遮住它，正好当"底图"用。
void DocumentViewport::submit_drawing_overlays(FrameSubmission& frame) {
  if (!drawings_visible_) {
    return;
  }
  for (const DrawingRef& ref : document_->drawings()) {
    if (!ref.visible) {
      continue;
    }
    const auto it = drawing_underlays_.find(ref.path);
    if (it == drawing_underlays_.end() || !it->second.document || it->second.texture_id == 0) {
      continue;
    }
    const Aabb2 page = page_bounds_of(*it->second.document, ref.page);
    if (!page.valid()) {
      continue;
    }
    DrawingOverlay overlay;
    overlay.model = drawing_plane_transform(page, ref.placement);
    overlay.texture_id = it->second.texture_id;
    overlay.opacity = kDrawingOverlayOpacity;
    frame.drawing_overlays.push_back(overlay);
  }
}

// 第一次要画标注时才找字体：主字体（拉丁）+ 中文回落字体。
// 一个都找不到就关掉整条通路（日志说一句），不影响别的功能。
void DocumentViewport::ensure_text_font() {
  if (text_font_loaded_) {
    return;
  }
  text_font_loaded_ = true;
  const std::vector<std::filesystem::path> dirs = app_font_dirs();
  const auto load = [](const std::optional<FontCandidate>& candidate) {
    if (!candidate.has_value()) {
      return std::shared_ptr<StbFont>{};
    }
    auto font = StbFont::load_file(candidate->path, candidate->face_index);
    if (!font) {
      log_error(font.error());
      return std::shared_ptr<StbFont>{};
    }
    return std::shared_ptr<StbFont>(std::move(*font));
  };

  if (auto primary = load(pick_default_font(dirs)); primary != nullptr) {
    label_fonts_.push_back(std::move(primary));
    const std::string& family = label_fonts_.front()->family();
    log_info("text: 标注主字体 " + (family.empty() ? std::string("(未命名)") : family));
  }
  // 中文回落：楼层叫「一层」时拉丁字体没这几个字，得换一套。
  if (auto cjk = load(pick_cjk_font(dirs)); cjk != nullptr) {
    if (label_fonts_.empty() || cjk->id() != label_fonts_.front()->id()) {
      label_fonts_.push_back(std::move(cjk));
    }
  }
  if (label_fonts_.empty()) {
    log_warn("text: 找不到可用字体，文字标注不显示（放一份 TTF 到 assets/fonts 即可）");
    return;
  }
  // 512² 起步、上限 2048²：标注用的字集很小，一张就够，装满自动扩容。
  glyph_atlas_ = std::make_unique<GlyphAtlas>(512, 2048, 1);
}

void DocumentViewport::set_label_kind_visible(TextKind kind, bool visible) {
  if (label_kinds_.visible(kind) == visible) {
    return;
  }
  label_kinds_.set(kind, visible);
  request_redraw();
}

void DocumentViewport::begin_text_placement() {
  ensure_text_font();  // 先把字体备好：点完才发现画不出来最扫兴
  text_placement_ = true;
  emit status_message(tr("Click in the view to place the text (Esc to cancel)"));
  request_redraw();
}

void DocumentViewport::cancel_text_placement() {
  if (!text_placement_) {
    return;
  }
  text_placement_ = false;
  request_redraw();
}

void DocumentViewport::select_text(std::uint64_t text_id) {
  if (selected_text_id_ == text_id) {
    return;
  }
  if (TextAnnotation* previous = document_->text_annotation(selected_text_id_)) {
    previous->selected = false;
  }
  selected_text_id_ = text_id;
  if (TextAnnotation* current = document_->text_annotation(text_id)) {
    current->selected = true;
  }
  request_redraw();
  emit selection_changed();
}

void DocumentViewport::commit_text_placement(const QPoint& pos) {
  text_placement_ = false;
  const Vec3 anchor = cursor_world_position(pos);
  bool accepted = false;
  const QString text = QInputDialog::getText(this, tr("Text"), tr("Text:"), QLineEdit::Normal,
                                             QString(), &accepted);
  if (!accepted || text.trimmed().isEmpty()) {
    request_redraw();
    return;
  }
  run_command("create_text", {{"text", text.toStdString()},
                              {"position", anchor},
                              {"size_px", 14.0}});
}

bool DocumentViewport::text_annotation_rect(const TextAnnotation& annotation, QRectF& out) const {
  if (annotation.text.empty() || label_fonts_.empty()) {
    return false;
  }
  const QSize area = scene_area_size();
  if (area.width() < 2 || area.height() < 2) {
    return false;
  }
  const float dpr = static_cast<float>(devicePixelRatioF());
  const float width = static_cast<float>(area.width()) * dpr;
  const float height = static_cast<float>(area.height()) * dpr;
  // 和提交帧用同一套投影（不含裁剪修正）；见 submit_current_frame()。
  const Mat4 view_proj =
      camera_.proj_matrix(width / height) * camera_.view_matrix();
  float sx = 0.f;
  float sy = 0.f;
  if (!project_world_to_screen(view_proj, annotation.anchor, width, height, sx, sy)) {
    return false;
  }
  const StbFont* font = pick_font_for_text(annotation.text, label_fonts_);
  if (font == nullptr) {
    font = label_fonts_.front().get();
  }
  TextStyle style{};
  style.size = annotation.size_px * dpr;
  style.opacity = annotation.opacity;
  const TextLayout layout = layout_text(annotation.text, style, annotation.align, 0.f, *font);
  if (layout.empty()) {
    return false;
  }
  float x = sx;
  if (annotation.align == TextAlign::Center) {
    x -= layout.width * 0.5f;
  } else if (annotation.align == TextAlign::Right) {
    x -= layout.width;
  }
  // 锚点是**首行基线左端**（像文字插入点），所以矩形要往上抬一个基线高度。
  const float y = sy - layout.first_baseline;
  // 设备像素 → Qt 逻辑像素（鼠标事件坐标是逻辑像素）。
  out = QRectF(x / dpr, y / dpr, layout.width / dpr, layout.height / dpr);
  return true;
}

std::uint64_t DocumentViewport::pick_text_annotation_at(const QPoint& pos) const {
  const std::vector<TextAnnotation>& annotations = document_->text_annotations();
  // 后放的压在上面：从后往前找第一个命中的。
  for (auto it = annotations.rbegin(); it != annotations.rend(); ++it) {
    QRectF rect;
    if (!text_annotation_rect(*it, rect)) {
      continue;
    }
    if (rect.adjusted(-2.0, -2.0, 2.0, 2.0).contains(QPointF(pos))) {
      return it->id;
    }
  }
  return 0;
}

void DocumentViewport::edit_text_annotation(std::uint64_t text_id) {
  const TextAnnotation* annotation = document_->text_annotation(text_id);
  if (annotation == nullptr) {
    return;
  }
  bool accepted = false;
  const QString text = QInputDialog::getText(this, tr("Edit Text"), tr("Text:"),
                                             QLineEdit::Normal,
                                             QString::fromStdString(annotation->text), &accepted);
  if (!accepted || text.trimmed().isEmpty()) {
    return;
  }
  run_command("update_text",
              {{"text_id", static_cast<std::int64_t>(text_id)}, {"text", text.toStdString()}});
}

// 一帧里的标注总入口：先清占用表，再按「谁更重要」的顺序摆，最后把图集推给渲染线程。
// 派生标注不落盘、不进语义树（见 docs/TEXT.md §5）。
void DocumentViewport::append_text_annotations(FrameSubmission& frame) {
  ensure_text_font();
  if (label_fonts_.empty() || glyph_atlas_ == nullptr || frame.width < 2 || frame.height < 2) {
    return;
  }
  const Mat4 view_proj = frame.proj * frame.view;
  label_occluder_.reset();
  ++text_tick_;
  // 顺序 = 优先级：轴号最要紧，标高次之，尺寸链最后（撞上就让位）。
  if (label_kinds_.visible(TextKind::AxisLabel)) {
    append_grid_axis_labels(frame, view_proj);
  }
  if (label_kinds_.visible(TextKind::StoreyLabel)) {
    append_storey_labels(frame, view_proj);
  }
  // 尺寸链只在平面图里画：三维视角下满屏尺寸没有意义（见 docs/TEXT.md §4.4）。
  if (label_kinds_.visible(TextKind::Dimension) && plan_view_) {
    append_grid_dimensions(frame, view_proj);
  }
  // 用户放的注记最后画：它们是内容（不是派生标注），不进占用表，永远画出来。
  append_user_text_annotations(frame, view_proj);

  // 图集变了才重传（白 + 预乘 alpha 的 RGBA8）。
  if (text_atlas_generation_ != glyph_atlas_->generation()) {
    TextureAsset atlas_texture{};
    atlas_texture.name = "text-atlas";
    atlas_texture.srgb = false;
    atlas_texture.width = static_cast<std::uint32_t>(glyph_atlas_->size());
    atlas_texture.height = atlas_texture.width;
    atlas_texture.rgba = glyph_atlas_->rgba();
    atlas_texture.generation = glyph_atlas_->generation();
    if (auto gpu = render_thread_->upload_texture(kTextAtlasTextureAssetId, std::move(atlas_texture));
        !gpu) {
      log_error(gpu.error());
    } else {
      text_atlas_texture_id_ = *gpu;
      text_atlas_generation_ = glyph_atlas_->generation();
    }
  }
  frame.text_atlas_texture_id = text_atlas_texture_id_;
}

// 一条标注的完整流程：投影锚点 → 挑字体 → 排版 → 去重叠 → 出四边形。
// offset 是相对锚点的屏幕像素偏移；align 决定文字块相对锚点怎么摆。
bool DocumentViewport::append_label(FrameSubmission& frame, const Mat4& view_proj,
                                    Vec3 anchor_world, const std::string& text,
                                    const TextStyle& style, TextAlign align, float offset_x,
                                    float offset_y) {
  if (text.empty()) {
    return false;
  }
  const float width = static_cast<float>(frame.width);
  const float height = static_cast<float>(frame.height);
  float sx = 0.f;
  float sy = 0.f;
  if (!project_world_to_screen(view_proj, anchor_world, width, height, sx, sy)) {
    return false;  // 锚点在相机后面
  }
  if (sx < -128.f || sy < -128.f || sx > width + 128.f || sy > height + 128.f) {
    return false;  // 视口外，连排版都省了
  }

  // 挑一套认全这段文字的字体；都认不全就用主字体画豆腐块——不静默丢标注。
  const StbFont* font = pick_font_for_text(text, label_fonts_);
  if (font == nullptr) {
    font = label_fonts_.front().get();
  }
  const TextLayout layout = layout_text(text, style, align, 0.f, *font);
  if (layout.empty()) {
    return false;
  }

  float origin_x = sx + offset_x;
  if (align == TextAlign::Center) {
    origin_x -= layout.width * 0.5f;
  } else if (align == TextAlign::Right) {
    origin_x -= layout.width;
  }
  const float origin_y = sy + offset_y;
  // 先到的占住位置；撞上更重要的标注就不画这一条。
  if (!label_occluder_.try_reserve(origin_x, origin_y, layout.width, layout.height)) {
    return false;
  }
  append_text_quads(layout, font->id(), origin_x, origin_y, style.size, style.color, style.opacity,
                    *font, *glyph_atlas_, text_tick_, frame.text_quads);
  return true;
}

// 轴网编号：入口 1（数据派生）的第一口。轴号本来就在 GridAxis::name 里。
void DocumentViewport::append_grid_axis_labels(FrameSubmission& frame, const Mat4& view_proj) {
  TextStyle style{};
  style.size = 12.f * static_cast<float>(devicePixelRatioF());
  style.color = Vec3{0.86f, 0.90f, 0.96f};
  const float grid_y = grid_plane_y();
  for (const GridAxis& axis : document_->bim().grid().axes()) {
    if (axis.name.empty() || axis.length() <= 0.0) {
      continue;
    }
    Vec3 anchor = axis.start_point();
    anchor.y = grid_y;
    // 往上挪约一个 ascent，别压住轴线端头（ascent ≈ 0.8 em）。
    append_label(frame, view_proj, anchor, axis.name, style, TextAlign::Center, 0.f,
                 -style.size * 1.15f);
  }
}

// 标高 / 楼层名：摆在模型平面范围左下角的外侧，每条楼层一行。
void DocumentViewport::append_storey_labels(FrameSubmission& frame, const Mat4& view_proj) {
  const std::vector<Storey>& storeys = document_->bim().storeys();
  if (storeys.empty()) {
    return;
  }
  const Aabb bounds = document_->bounds();
  if (!bounds.valid()) {
    return;  // 还没有几何：没有「模型边上」这个参照，不画
  }
  const float dpr = static_cast<float>(devicePixelRatioF());
  TextStyle style{};
  style.size = 12.f * dpr;
  style.color = Vec3{0.78f, 0.88f, 1.00f};
  for (const Storey& storey : storeys) {
    const std::string elevation = format_elevation(storey.elevation);
    const std::string text = storey.name.empty() ? elevation : storey.name + "  " + elevation;
    const Vec3 anchor{bounds.min.x, static_cast<float>(storey.elevation), bounds.min.z};
    // 右对齐：文字整个落在锚点左侧，不压住模型。
    append_label(frame, view_proj, anchor, text, style, TextAlign::Right, -8.f,
                 -style.size * 0.4f);
  }
}

// 尺寸链：相邻轴线间距，摆在轴网外缘再往外一格。
void DocumentViewport::append_grid_dimensions(FrameSubmission& frame, const Mat4& view_proj) {
  const std::vector<GridAxis>& axes = document_->bim().grid().axes();
  if (axes.size() < 2) {
    return;
  }
  constexpr float kChainOffset = 1.5f;  // 米：往外挪出轴线端头
  const float grid_y = grid_plane_y();
  const float dpr = static_cast<float>(devicePixelRatioF());
  TextStyle style{};
  style.size = 11.f * dpr;
  style.color = Vec3{0.74f, 0.82f, 0.92f};
  for (GridAxisDirection direction : {GridAxisDirection::AlongZ, GridAxisDirection::AlongX}) {
    for (const GridDimension& dimension : grid_dimension_chain(axes, direction)) {
      Vec3 anchor = dimension.anchor;
      // 沿哪一列轴量，就往那个方向再挪出去（链在建筑外侧）。
      if (direction == GridAxisDirection::AlongZ) {
        anchor.z += kChainOffset;
      } else {
        anchor.x += kChainOffset;
      }
      anchor.y = grid_y;
      append_label(frame, view_proj, anchor, format_distance(dimension.distance), style,
                   TextAlign::Center, 0.f, -style.size * 0.5f);
    }
  }
}

// 用户放的注记：世界锚点 + 屏幕朝向，大小是逻辑像素乘 DPR（不随缩放变）。
// 它们**不进占用表**：用户自己放的东西必须画出来，不能被派生标注挤掉。
void DocumentViewport::append_user_text_annotations(FrameSubmission& frame,
                                                    const Mat4& view_proj) {
  const float dpr = static_cast<float>(devicePixelRatioF());
  const float width = static_cast<float>(frame.width);
  const float height = static_cast<float>(frame.height);
  for (const TextAnnotation& annotation : document_->text_annotations()) {
    if (annotation.text.empty() || !label_kinds_.visible(annotation.kind)) {
      continue;
    }
    float sx = 0.f;
    float sy = 0.f;
    if (!project_world_to_screen(view_proj, annotation.anchor, width, height, sx, sy)) {
      continue;
    }
    if (sx < -256.f || sy < -256.f || sx > width + 256.f || sy > height + 256.f) {
      continue;
    }
    const StbFont* font = pick_font_for_text(annotation.text, label_fonts_);
    if (font == nullptr) {
      font = label_fonts_.front().get();
    }
    TextStyle style{};
    style.size = annotation.size_px * dpr;
    style.opacity = annotation.opacity;
    // 选中就换成高亮色——比画选框便宜，也一眼看得出来选的是哪一条。
    style.color = annotation.selected ? Vec3{1.f, 0.78f, 0.25f} : annotation.color;
    const TextLayout layout = layout_text(annotation.text, style, annotation.align, 0.f, *font);
    if (layout.empty()) {
      continue;
    }
    float origin_x = sx;
    if (annotation.align == TextAlign::Center) {
      origin_x -= layout.width * 0.5f;
    } else if (annotation.align == TextAlign::Right) {
      origin_x -= layout.width;
    }
    append_text_quads(layout, font->id(), origin_x, sy - layout.first_baseline, style.size,
                      style.color, style.opacity, *font, *glyph_atlas_, text_tick_,
                      frame.text_quads);
  }
}

std::vector<std::uint64_t> DocumentViewport::imported_node_ids() const {
  std::vector<std::uint64_t> ids;
  for (const SceneNode& node : document_->scene().nodes()) {
    if (node.mesh_asset_id != 0 && document_->entity(node.id) == nullptr) {
      ids.push_back(node.id);
    }
  }
  return ids;
}

void DocumentViewport::refresh_floors() {
  floors_ = viewport_floors(*document_);
  // 楼层表变了（增 / 删 / 重排）以后，按下标记的隐藏集合会错位：丢掉够不到的项。
  for (auto it = hidden_floors_.begin(); it != hidden_floors_.end();) {
    if (*it < 0 || *it >= static_cast<int>(floors_.size())) {
      it = hidden_floors_.erase(it);
    } else {
      ++it;
    }
  }
  // 楼层表改了以后，之前打开的楼层视图可能已经指不到任何一层。
  if (floor_view_.has_value() && *floor_view_ >= floors_.size()) {
    floor_view_.reset();
  }
}

std::vector<ViewportFloor> DocumentViewport::floors() {
  refresh_floors();
  return floors_;
}

bool DocumentViewport::floor_hidden(std::size_t index) const {
  return hidden_floors_.count(static_cast<int>(index)) != 0;
}

void DocumentViewport::set_floor_hidden(std::size_t index, bool hidden) {
  refresh_floors();
  if (index >= floors_.size()) {
    return;
  }
  const bool changed = hidden ? hidden_floors_.insert(static_cast<int>(index)).second
                              : hidden_floors_.erase(static_cast<int>(index)) != 0;
  if (!changed) {
    return;
  }
  // 手动调楼层显隐 = 离开"某一层的视图"（那张视图只留一层，动了就不再是它）。
  const bool had_view = floor_view_.has_value();
  floor_view_.reset();
  request_redraw();
  emit visibility_changed();
  if (had_view) {
    emit view_changed();
  }
}

void DocumentViewport::clear_floor_filter() {
  const bool had_view = floor_view_.has_value();
  floor_view_.reset();
  if (hidden_floors_.empty()) {
    if (had_view) {
      emit view_changed();
    }
    return;
  }
  hidden_floors_.clear();
  request_redraw();
  emit visibility_changed();
  if (had_view) {
    emit view_changed();
  }
}

bool DocumentViewport::node_visible_in_view(std::uint64_t id) const {
  if (hidden_ids_.count(id) != 0) {
    return false;
  }
  if (!isolated_ids_.empty() && isolated_ids_.count(id) == 0) {
    return false;
  }
  const Entity* entity = document_->entity(id);
  if (entity != nullptr && hidden_kinds_.count(entity->kind()) != 0) {
    return false;
  }
  if (!hidden_floors_.empty()) {
    const SceneNode* node = document_->scene().find(id);
    if (node != nullptr) {
      // 归属优先看 Location 的楼层（在 1 楼画的顶板就是 1 楼的），没有归属的
      // （导入网格、未归属构件）才按几何楼层带兜底。
      const Entity* entity = document_->entity(id);
      // 族实体自带楼层（entity_storey_id：族实体优先，基础体退回 Location）；
      // 连 Location 都没有的（导入网格）才是 0 → 按几何楼层带兜底。
      const std::uint64_t storey_id = entity != nullptr ? entity_storey_id(*entity) : 0;
      if (!viewport_floor_allows(floors_, storey_id, node->world_bounds, hidden_floors_)) {
        return false;
      }
    }
  }
  return true;
}

void DocumentViewport::apply_storey_settings(std::vector<Storey> storeys,
                                             std::uint64_t active_storey_id) {
  auto cmd = std::make_unique<UpdateStoreysCommand>(*document_, std::move(storeys),
                                                   active_storey_id);
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  command_system_.push_executed(std::move(cmd));
  refresh_floors();
  request_redraw();
  emit document_changed();
  emit visibility_changed();
}

void DocumentViewport::apply_grid_settings(std::vector<GridAxis> axes) {
  auto cmd = std::make_unique<UpdateGridCommand>(*document_, std::move(axes));
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  command_system_.push_executed(std::move(cmd));
  request_redraw();
  emit document_changed();
}

void DocumentViewport::begin_grid_placement(std::vector<GridAxis> axes, Vec2 anchor) {
  // 表是空的就没得放（清空轴网），直接落位，别让用户白点一下。
  if (axes.empty()) {
    clear_grid_placement();
    apply_grid_settings(std::move(axes));
    return;
  }
  cancel_plugin_point_input();
  command_system_.cancel();
  if (session_->tool_mode() != ToolMode::None) {
    set_tool(ToolMode::None);
  }
  pending_grid_ = GridPlacement{std::move(axes), anchor};  // 已有放置会话则换成新的
  setCursor(Qt::CrossCursor);
  setFocus();
  request_redraw();
  emit status_message(
      tr("Click in the viewport to place the grid (Esc or right-click cancels)"));
}

void DocumentViewport::cancel_grid_placement() {
  if (!pending_grid_) {
    return;
  }
  clear_grid_placement();
  request_redraw();
  emit status_message(tr("Grid placement cancelled — nothing changed"));
}

void DocumentViewport::clear_grid_placement() {
  if (!pending_grid_) {
    return;
  }
  pending_grid_.reset();
  unsetCursor();
}

Vec3 DocumentViewport::plan_position_at_storey(const QPoint& pos) const {
  const Ray ray = ray_at(pos);
  Vec3 hit = ray.origin + ray.direction * camera_.distance();
  const float plane_y = grid_plane_y();
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      hit = ray.origin + ray.direction * t;
    }
  }
  return hit;
}

std::vector<GridAxis> DocumentViewport::ghost_grid_axes(const QPoint& pos) const {
  if (!pending_grid_) {
    return {};
  }
  const Vec3 drop = plan_position_at_storey(pos);
  std::vector<GridAxis> ghost = pending_grid_->axes;
  translate_grid(ghost, static_cast<double>(drop.x - pending_grid_->anchor.x),
                 static_cast<double>(drop.z - pending_grid_->anchor.y));
  return ghost;
}

void DocumentViewport::commit_grid_placement(const QPoint& pos) {
  if (!pending_grid_) {
    return;
  }
  std::vector<GridAxis> placed = ghost_grid_axes(pos);
  const std::size_t count = placed.size();
  clear_grid_placement();
  apply_grid_settings(std::move(placed));
  emit status_message(tr("Grid placed: %1 axes").arg(static_cast<int>(count)));
}

void DocumentViewport::set_grid_visible(bool visible) {
  if (grid_visible_ == visible) {
    return;
  }
  grid_visible_ = visible;
  request_redraw();
}

void DocumentViewport::apply_drawing_import(DrawingImportPlan plan) {
  const QString summary = tr("Tracing done: %1 walls, %2 columns, %3 doors/windows")
                              .arg(plan.walls.size())
                              .arg(plan.columns.size())
                              .arg(plan.openings.size());
  auto cmd = std::make_unique<ImportDrawingCommand>(*document_, std::move(plan));
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    emit status_message(QString::fromStdString(r.error()));
    return;
  }
  command_system_.push_executed(std::move(cmd));
  refresh_after_edit();
  request_redraw();
  emit document_changed();
  emit status_message(summary);
}

bool DocumentViewport::pick_grip_at(const QPoint& pos, EntityGrip& out) const {
  if (session_->tool_mode() != ToolMode::None || document_ == nullptr) {
    return false;
  }
  const Mat4 vp = view_proj();
  const QSize area = scene_area_size();
  const float w = static_cast<float>(area.width());
  const float h = static_cast<float>(area.height());
  float best = 10.f;
  bool hit = false;
  for (const std::uint64_t id : document_->selected_ids()) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    for (const EntityGrip& grip : collect_entity_grips(*entity)) {
      float sx = 0.f;
      float sy = 0.f;
      if (!project_world_to_screen(vp, grip.world, w, h, sx, sy)) {
        continue;
      }
      const float dx = sx - static_cast<float>(pos.x());
      const float dy = sy - static_cast<float>(pos.y());
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d < best) {
        best = d;
        out = grip;
        hit = true;
      }
    }
  }
  return hit;
}

void DocumentViewport::clear_grip_preview() {
  grip_preview_polyline_.clear();
  grip_preview_points_.clear();
  grip_to_model_ = {};
  grip_to_transform_ = Mat4::identity();
  grip_preview_valid_ = false;
}

void DocumentViewport::apply_grip_at(const QPoint& pos) {
  Entity* entity = document_->entity(active_grip_.entity_id);
  if (entity == nullptr) {
    return;
  }
  Vec3 p = snapped_ground_position(pos);
  p.y = grip_from_world_.y;
  // Preview on a clone. Live document stays at the press-time shape until drop;
  // createGeom / EditEntityGripCommand run once in commit_grip_drag.
  // Always reshape from the press-time snapshot: canonical corner indices remap
  // after a rebuild, and applying incrementally would pin the wrong opposite
  // once the dragged corner crosses through it.
  std::unique_ptr<Entity> preview = entity->clone();
  preview->model = grip_from_model_;
  preview->local_transform = grip_from_transform_;
  if (!apply_entity_grip(*preview, active_grip_.index, p)) {
    return;
  }
  grip_to_model_ = preview->model;
  grip_to_transform_ = preview->local_transform;
  grip_preview_polyline_ = tamias::grip_preview_polyline(*preview);
  grip_preview_points_.clear();
  for (const EntityGrip& g : collect_entity_grips(*preview)) {
    grip_preview_points_.push_back(g.world);
  }
  grip_preview_valid_ = true;
  request_redraw();
}

void DocumentViewport::commit_grip_drag() {
  Entity* entity = document_->entity(active_grip_.entity_id);
  if (entity == nullptr || !grip_preview_valid_) {
    clear_grip_preview();
    return;
  }
  Vec3 to_world = grip_from_world_;
  if (active_grip_.index >= 0 &&
      active_grip_.index < static_cast<int>(grip_preview_points_.size())) {
    to_world = grip_preview_points_[static_cast<std::size_t>(active_grip_.index)];
  }
  if (length(to_world - grip_from_world_) < 1e-4f) {
    clear_grip_preview();
    return;
  }
  auto command = std::make_unique<EditEntityGripCommand>(
      *document_, entity->id, grip_from_model_, grip_from_transform_, grip_to_model_,
      grip_to_transform_);
  {
    TimingScope scope("edit_entity_grip", TimingCategory::Command);
    if (auto r = command->execute(); !r) {
      log_error(r.error());
      clear_grip_preview();
      return;
    }
  }
  command_system_.push_executed(std::move(command));
  resync_all_meshes();
  rebuild_bvh();
  clear_grip_preview();
  emit document_changed();
  request_redraw();
}

void DocumentViewport::fill_grip_overlay(FrameSubmission& frame) const {
  if (session_->tool_mode() != ToolMode::None || document_ == nullptr) {
    return;
  }
  if (gripping_ && grip_preview_polyline_.size() >= 2) {
    frame.preview_polyline = grip_preview_polyline_;
  }
  for (const std::uint64_t id : document_->selected_ids()) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    std::vector<Vec3> worlds;
    if (gripping_ && id == active_grip_.entity_id && !grip_preview_points_.empty()) {
      worlds = grip_preview_points_;
    } else {
      for (const EntityGrip& g : collect_entity_grips(*entity)) {
        worlds.push_back(g.world);
      }
    }
    if (worlds.empty()) {
      continue;
    }
    if ((entity->kind() == EntityKind::Bezier || entity->kind() == EntityKind::BSpline ||
         entity->kind() == EntityKind::Nurbs) &&
        frame.preview_control_polyline.empty()) {
      frame.preview_control_polyline = worlds;
    }
    for (const Vec3& p : worlds) {
      frame.grip_points.push_back(p);
    }
  }
}

void DocumentViewport::set_debug_overlay(std::optional<Aabb> aabb,
                                         std::optional<std::uint64_t> isolate_node) {
  debug_aabb_ = std::move(aabb);
  debug_isolate_node_ = isolate_node;
  request_redraw();
}

void DocumentViewport::set_debug_vertex(std::optional<DebugVertexOverlay> vertex) {
  debug_vertex_ = std::move(vertex);
  request_redraw();
}

void DocumentViewport::fill_debug_overlay(FrameSubmission& frame) const {
  if (debug_aabb_ && debug_aabb_->valid()) {
    const Aabb& box = *debug_aabb_;
    const Vec3 c[8] = {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
    };
    const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                              {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    frame.debug_line_segments.reserve(24);
    for (const auto& e : edges) {
      frame.debug_line_segments.push_back(c[e[0]]);
      frame.debug_line_segments.push_back(c[e[1]]);
    }
  }
  frame.debug_vertex = debug_vertex_;
}

}  // namespace tamias
