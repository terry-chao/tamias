#pragma once

#include "engine/render/render_scene.h"

#include <QMainWindow>
#include <QPointer>
#include <filesystem>
#include <memory>
#include <optional>

class QAction;
class QLabel;
class QSlider;
class QTableWidget;
class QCloseEvent;

namespace tamias {

class DocumentViewport;
class ReplayViewport;
class RenderSceneInspector;
class RenderThread;

class SceneDebuggerWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit SceneDebuggerWindow(std::shared_ptr<RenderThread> render_thread,
                               QWidget* parent = nullptr);
  ~SceneDebuggerWindow() override;

  void open_scene(RenderScene scene, std::filesystem::path path = {},
                  QPointer<DocumentViewport> source = {});
  [[nodiscard]] bool is_for_path(const std::filesystem::path& path) const;

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void rebuild_commands();
  void sync_step_controls();
  void set_source_path(std::filesystem::path path);
  void open_file();
  void save_snapshot();
  void pin_golden();
  void write_debug_files();
  void write_selected_draw();
  void recapture();
  void compare_with_file();
  void set_mode(RenderMode mode);
  void on_step_changed(int value);

  ReplayViewport* replay_ = nullptr;
  RenderSceneInspector* inspector_ = nullptr;
  QTableWidget* commands_ = nullptr;
  QLabel* commands_summary_ = nullptr;
  QSlider* step_slider_ = nullptr;
  QLabel* step_label_ = nullptr;
  QAction* recapture_action_ = nullptr;
  QAction* wireframe_action_ = nullptr;
  QAction* shaded_action_ = nullptr;
  QAction* realistic_action_ = nullptr;
  QAction* axes_action_ = nullptr;
  QAction* apply_hidden_action_ = nullptr;
  std::filesystem::path path_;
  QPointer<DocumentViewport> source_;
};

}  // namespace tamias
