#include "engine/interaction/drag_manager.h"
#include "engine/interaction/event_source.h"
#include "command/create/beam_drag.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {
namespace {

// —— 测试替身 -------------------------------------------------------------
//
// 关键约束：**drag 的生命周期在 on_end 之后就结束了**（管理器随即销毁它）。
// 所以观察结果必须记在一块活得比 drag 长的 Recorder 上，不能回头去读 drag
// 自己的成员——那会读到已释放内存。

struct Recorder {
  int starts = 0;
  int threshold_crossings = 0;
  int ends = 0;
  std::optional<DragEnd> last_end;
  std::vector<DragEnd> end_history;
  std::vector<PointerPhase> phases;
  std::vector<ButtonId> buttons;
  std::vector<KeyCode> keys;
  // 命名钩子的调用计数（见 Drag::on_pointer 的默认分发）。
  int left_down = 0;
  int middle_down = 0;
  int right_down = 0;
  int left_up = 0;
  int mouse_moves = 0;
  int pointer_cancels = 0;
  int raw_pointer = 0;  // 重写了 on_pointer 时的计数

  [[nodiscard]] bool saw(PointerPhase phase) const {
    for (const PointerPhase p : phases) {
      if (p == phase) {
        return true;
      }
    }
    return false;
  }
};

struct FakeContext final : DragContext {
  DragOwnerId id = 1;
  bool is_alive = true;
  int redraws = 0;
  // 预览推送的观察点（drag 主动推给宿主的那一路）。
  int preview_clears = 0;
  std::vector<Vec3> polyline;
  std::vector<Vec3> points;
  std::optional<ScreenRect> rect;

  [[nodiscard]] DragOwnerId owner() const override { return id; }
  [[nodiscard]] Vec2 viewport_size() const override { return {800.f, 600.f}; }
  [[nodiscard]] float device_pixel_ratio() const override { return 1.f; }
  [[nodiscard]] bool alive() const override { return is_alive; }
  void request_redraw() override { ++redraws; }
  // 测试里不做投影：屏幕 x/y 直接当世界 x/z，y 用 drag 指定的工作面标高。
  [[nodiscard]] Vec3 cursor_world(Vec2 screen_pos, float plane_y) const override {
    return {screen_pos.x, plane_y, screen_pos.y};
  }

  void set_drag_polyline(std::span<const Vec3> line) override {
    polyline.assign(line.begin(), line.end());
  }
  void set_drag_points(std::span<const Vec3> pts) override {
    points.assign(pts.begin(), pts.end());
  }
  void set_drag_screen_rect(std::optional<ScreenRect> box) override { rect = box; }
  void clear_drag_preview() override {
    ++preview_clears;
    polyline.clear();
    points.clear();
    rect.reset();
  }
};

struct FakeDrag final : Drag {
  Recorder* rec = nullptr;
  std::string_view drag_name = "fake";
  EventStatus pointer_result = EventStatus::Consumed;
  EventStatus key_result = EventStatus::Consumed;
  bool cancel_on_escape = true;

  [[nodiscard]] std::string_view name() const override { return drag_name; }

  void on_start(DragContext&) override {
    if (rec) {
      ++rec->starts;
    }
  }

  InteractionResult on_pointer(const PointerEvent& event) override {
    if (rec) {
      rec->phases.push_back(event.phase);
      rec->buttons.push_back(event.button);
    }
    return {pointer_result};
  }

  InteractionResult on_key(const KeyEvent& event) override {
    if (rec) {
      rec->keys.push_back(event.code);
    }
    if (event.down && event.code == KeyCode::Escape && cancel_on_escape) {
      return InteractionResult::cancelled();
    }
    return {key_result};
  }

  void on_threshold_crossed(const PointerEvent&) override {
    if (rec) {
      ++rec->threshold_crossings;
    }
  }

  void on_end(DragEnd reason) override {
    if (rec) {
      ++rec->ends;
      rec->last_end = reason;
      rec->end_history.push_back(reason);
    }
  }
};

// 接管所有按下的 handler，用来测 press 起手那条路。
struct AlwaysClaimHandler final : DragHandler {
  Recorder* rec = nullptr;
  std::string_view drag_name = "fake";

  [[nodiscard]] std::string_view name() const override { return "claim"; }
  [[nodiscard]] std::unique_ptr<Drag> on_press(const PointerEvent&, DragContext&) override {
    auto drag = std::make_unique<FakeDrag>();
    drag->rec = rec;
    drag->drag_name = drag_name;
    return drag;
  }
};

// 只接管指定按钮的 handler，用来验证仲裁顺序。
struct ButtonHandler final : DragHandler {
  Recorder* rec = nullptr;
  ButtonId want = ButtonId::Primary;
  std::string_view drag_name = "button";
  int claims = 0;

  [[nodiscard]] std::string_view name() const override { return drag_name; }
  [[nodiscard]] std::unique_ptr<Drag> on_press(const PointerEvent& event,
                                              DragContext&) override {
    if (event.button != want) {
      return nullptr;
    }
    ++claims;
    auto drag = std::make_unique<FakeDrag>();
    drag->rec = rec;
    drag->drag_name = drag_name;
    return drag;
  }
};

// 只重写命名钩子的 drag —— 这就是"继承 + 重写鼠标方法"的日常写法。
struct NamedHooksDrag final : Drag {
  Recorder* rec = nullptr;
  std::string_view drag_name = "named";

  [[nodiscard]] std::string_view name() const override { return drag_name; }

  InteractionResult on_left_button_down(const PointerEvent&) override {
    if (rec) {
      ++rec->left_down;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_middle_button_down(const PointerEvent&) override {
    if (rec) {
      ++rec->middle_down;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_right_button_down(const PointerEvent&) override {
    if (rec) {
      ++rec->right_down;
    }
    return InteractionResult::cancelled();
  }
  InteractionResult on_left_button_up(const PointerEvent&) override {
    if (rec) {
      ++rec->left_up;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_mouse_move(const PointerEvent&) override {
    if (rec) {
      ++rec->mouse_moves;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_pointer_cancel(const PointerEvent&) override {
    if (rec) {
      ++rec->pointer_cancels;
    }
    return InteractionResult::consumed();
  }
};

// 同时重写 on_pointer 和命名钩子：前者应当完全接管，后者不该被调用。
struct OverridingBothDrag final : Drag {
  Recorder* rec = nullptr;

  [[nodiscard]] std::string_view name() const override { return "both"; }

  InteractionResult on_pointer(const PointerEvent&) override {
    if (rec) {
      ++rec->raw_pointer;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_left_button_down(const PointerEvent&) override {
    if (rec) {
      ++rec->left_down;
    }
    return InteractionResult::consumed();
  }
};

// 给阻塞式 doIt / run_until_finished 用的脚本化 drag。
struct ScriptedDrag final : Drag {
  Recorder* rec = nullptr;
  bool finish_on_up = true;

  [[nodiscard]] std::string_view name() const override { return "scripted"; }

  InteractionResult on_left_button_down(const PointerEvent&) override {
    if (rec) {
      ++rec->left_down;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_mouse_move(const PointerEvent&) override {
    if (rec) {
      ++rec->mouse_moves;
    }
    return InteractionResult::consumed();
  }
  InteractionResult on_left_button_up(const PointerEvent&) override {
    if (rec) {
      ++rec->left_up;
    }
    return finish_on_up ? InteractionResult::finished() : InteractionResult::consumed();
  }
  InteractionResult on_key(const KeyEvent& event) override {
    if (rec) {
      rec->keys.push_back(event.code);
    }
    if (event.down && event.code == KeyCode::Escape) {
      return InteractionResult::cancelled();
    }
    return InteractionResult::consumed();
  }
};

// 拖拽期间主动把预览推给宿主的 drag —— 这是预览的唯一通路。
struct PushPreviewDrag final : Drag {
  Recorder* rec = nullptr;
  DragContext* ctx = nullptr;
  std::vector<Vec3> line{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
  std::vector<Vec3> pts{{2.f, 0.f, 0.f}};

  [[nodiscard]] std::string_view name() const override { return "push"; }

  void on_start(DragContext& context) override { ctx = &context; }

  InteractionResult on_mouse_move(const PointerEvent&) override {
    if (rec) {
      ++rec->mouse_moves;
    }
    if (ctx != nullptr) {
      ctx->set_drag_polyline(line);
      ctx->set_drag_points(pts);
    }
    return InteractionResult::consumed();
  }
};

// Drag::doIt 走的是管理器单例，用例之间要清干净。
struct SingletonScope {
  DragManager& manager = DragManager::instance();
  SingletonScope() {
    manager.reset();
    manager.set_event_source(nullptr);
    manager.set_watchdog_timeout(0.0);
  }
  ~SingletonScope() {
    manager.reset();
    manager.set_event_source(nullptr);
    manager.set_watchdog_timeout(0.0);
  }
};

struct NamedHooksHandler final : DragHandler {
  Recorder* rec = nullptr;

  [[nodiscard]] std::string_view name() const override { return "named-claim"; }
  [[nodiscard]] std::unique_ptr<Drag> on_press(const PointerEvent&, DragContext&) override {
    auto drag = std::make_unique<NamedHooksDrag>();
    drag->rec = rec;
    return drag;
  }
};

PointerEvent make_pointer(PointerPhase phase, ButtonId button, Vec2 pos, double t) {
  PointerEvent event{};
  event.phase = phase;
  event.button = button;
  event.pos = pos;
  event.time_seconds = t;
  if (phase == PointerPhase::Down) {
    event.buttons.add(button);
  }
  return event;
}

PointerEvent make_down(Vec2 pos, double t = 1.0) {
  return make_pointer(PointerPhase::Down, ButtonId::Primary, pos, t);
}
PointerEvent make_move(Vec2 pos, double t = 1.1) {
  return make_pointer(PointerPhase::Move, ButtonId::None, pos, t);
}
PointerEvent make_up(Vec2 pos, double t = 1.2) {
  return make_pointer(PointerPhase::Up, ButtonId::Primary, pos, t);
}

KeyEvent make_key(KeyCode code, double t = 1.3) {
  KeyEvent event{};
  event.code = code;
  event.time_seconds = t;
  return event;
}

// —— 激活式（命令 / 工具启动） --------------------------------------------

TEST(DragManager, ActivateStyleDispatchesAndCommits) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;

  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));
  EXPECT_TRUE(manager.active());
  EXPECT_EQ(manager.active_name(), std::string_view{"fake"});
  EXPECT_EQ(rec.starts, 1);
  // 激活式没有"点击 vs 拖拽"的犹豫期。
  EXPECT_TRUE(manager.past_threshold());

  EXPECT_TRUE(manager.handle_pointer(context, make_move({10.f, 10.f})));
  EXPECT_TRUE(manager.handle_pointer(context, make_up({10.f, 10.f})));
  EXPECT_TRUE(rec.saw(PointerPhase::Move));
  EXPECT_TRUE(rec.saw(PointerPhase::Up));
  EXPECT_EQ(rec.threshold_crossings, 0);
  manager.cancel_all();
  EXPECT_EQ(rec.last_end, DragEnd::Cancelled);

  // 第二轮：drag 报 Finished，管理器应转成 Committed 并卸下状态。
  Recorder finisher_rec;
  auto finisher = std::make_unique<FakeDrag>();
  finisher->rec = &finisher_rec;
  finisher->pointer_result = EventStatus::Finished;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(finisher)));

  EXPECT_TRUE(manager.handle_pointer(context, make_up({12.f, 10.f})));
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(finisher_rec.last_end, DragEnd::Committed);
  EXPECT_EQ(manager.last_end(), DragEnd::Committed);
}

TEST(DragManager, StartRejectsDeadOwnerAndNullDrag) {
  DragManager manager;
  FakeContext context;
  context.is_alive = false;
  EXPECT_FALSE(manager.start(context.owner(), context, std::make_unique<FakeDrag>()));
  context.is_alive = true;
  EXPECT_FALSE(manager.start(context.owner(), context, nullptr));
  EXPECT_FALSE(manager.active());
}

TEST(DragManager, StartWhileActivePreemptsPrevious) {
  DragManager manager;
  FakeContext context;
  Recorder first_rec;
  Recorder second_rec;

  auto first = std::make_unique<FakeDrag>();
  first->rec = &first_rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(first)));

  auto second = std::make_unique<FakeDrag>();
  second->rec = &second_rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(second)));

  EXPECT_EQ(first_rec.last_end, DragEnd::Cancelled);  // 被抢占 = 取消
  EXPECT_EQ(second_rec.ends, 0);
  EXPECT_EQ(manager.ended_count(), 1u);
  EXPECT_TRUE(manager.active());
}

// —— press 起手（阈值 / 点击语义） ----------------------------------------

TEST(DragManager, PressDrivenWaitsForThreshold) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto handler = std::make_unique<AlwaysClaimHandler>();
  handler->rec = &rec;
  manager.add_handler(std::move(handler));

  ASSERT_TRUE(manager.handle_pointer(context, make_down({100.f, 100.f})));
  ASSERT_TRUE(manager.active());
  EXPECT_EQ(rec.starts, 1);
  EXPECT_TRUE(rec.saw(PointerPhase::Down));  // 触发的那一下也交给 drag
  EXPECT_FALSE(manager.past_threshold());

  // 阈值内移动：吞掉，drag 一点都看不到。
  EXPECT_TRUE(manager.handle_pointer(context, make_move({102.f, 101.f})));
  EXPECT_FALSE(rec.saw(PointerPhase::Move));
  EXPECT_EQ(rec.threshold_crossings, 0);

  // 越过阈值：先通知 threshold_crossed，再派发 Move。
  EXPECT_TRUE(manager.handle_pointer(context, make_move({140.f, 100.f})));
  EXPECT_TRUE(manager.past_threshold());
  EXPECT_EQ(rec.threshold_crossings, 1);
  EXPECT_TRUE(rec.saw(PointerPhase::Move));

  // 之后的移动正常派发，且阈值只通知一次。
  EXPECT_TRUE(manager.handle_pointer(context, make_move({160.f, 100.f})));
  EXPECT_EQ(rec.threshold_crossings, 1);
}

TEST(DragManager, PressWithoutMovementEndsAsClick) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto handler = std::make_unique<AlwaysClaimHandler>();
  handler->rec = &rec;
  manager.add_handler(std::move(handler));

  ASSERT_TRUE(manager.handle_pointer(context, make_down({50.f, 50.f})));
  EXPECT_TRUE(manager.handle_pointer(context, make_up({51.f, 50.f})));

  EXPECT_FALSE(manager.active());
  EXPECT_EQ(rec.last_end, DragEnd::Clicked);
  EXPECT_FALSE(rec.saw(PointerPhase::Up));  // 点击不给 drag 看，走 on_end
  EXPECT_EQ(rec.ends, 1);
}

TEST(DragManager, HandlersAreArbitratedInRegistrationOrder) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto primary = std::make_unique<ButtonHandler>();
  primary->rec = &rec;
  primary->want = ButtonId::Primary;
  primary->drag_name = "primary";
  auto secondary = std::make_unique<ButtonHandler>();
  secondary->rec = &rec;
  secondary->want = ButtonId::Secondary;
  secondary->drag_name = "secondary";
  ButtonHandler* primary_watch = primary.get();
  ButtonHandler* secondary_watch = secondary.get();
  manager.add_handler(std::move(primary));
  manager.add_handler(std::move(secondary));

  // 右键按下：primary 不认，落给 secondary。
  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Down, ButtonId::Secondary, {0.f, 0.f}, 1.0)));
  EXPECT_EQ(primary_watch->claims, 0);
  EXPECT_EQ(secondary_watch->claims, 1);
  EXPECT_EQ(manager.active_name(), std::string_view{"secondary"});
  manager.cancel_all();

  // 没人接管的按下 → 返回 false，让壳走默认逻辑（选择 / 右键菜单）。
  EXPECT_FALSE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Down, ButtonId::Middle, {0.f, 0.f}, 2.0)));
  EXPECT_FALSE(manager.active());
}

// —— 取消 / 宿主销毁 / 看门狗 ---------------------------------------------

// —— 命名钩子层（Drag::on_pointer 的默认分发） -----------------------------

TEST(DragHooks, DefaultDispatchMapsPhaseAndButton) {
  // 回调是 protected 的，所以只能经管理器进；这也顺带覆盖了真实入口。
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<NamedHooksDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  // 激活式没有阈值犹豫期，所有事件都直达分发表。
  EXPECT_TRUE(manager.handle_pointer(context, make_down({0.f, 0.f})));
  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Down, ButtonId::Middle, {0.f, 0.f}, 1.0)));
  EXPECT_TRUE(manager.handle_pointer(context, make_move({1.f, 1.f})));
  EXPECT_TRUE(manager.handle_pointer(context, make_up({1.f, 1.f})));
  EXPECT_EQ(rec.left_down, 1);
  EXPECT_EQ(rec.middle_down, 1);
  EXPECT_EQ(rec.mouse_moves, 1);
  EXPECT_EQ(rec.left_up, 1);

  // 右键按下 → on_right_button_down 返回 cancelled → 管理器结束。
  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Down, ButtonId::Secondary, {1.f, 1.f}, 1.1)));
  EXPECT_EQ(rec.right_down, 1);
  EXPECT_FALSE(manager.active());
}

TEST(DragHooks, CancelPhaseRoutesToPointerCancel) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<NamedHooksDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Cancel, ButtonId::None, {1.f, 1.f}, 1.0)));
  EXPECT_EQ(rec.pointer_cancels, 1);
  manager.cancel_all();
}

TEST(DragHooks, LeftButtonUpOnlyFiresForLeftButton) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<NamedHooksDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Up, ButtonId::Secondary, {1.f, 1.f}, 1.0)));
  EXPECT_EQ(rec.left_up, 0);
  EXPECT_TRUE(manager.handle_pointer(context, make_up({1.f, 1.f})));
  EXPECT_EQ(rec.left_up, 1);
  manager.cancel_all();
}

TEST(DragHooks, OverridingOnPointerBypassesNamedHooks) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<OverridingBothDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  EXPECT_TRUE(manager.handle_pointer(context, make_down({5.f, 5.f})));
  EXPECT_EQ(rec.raw_pointer, 1);
  EXPECT_EQ(rec.left_down, 0);  // 被 on_pointer 接管，命名钩子不参与
  manager.cancel_all();
}

TEST(DragManager, NamedHooksDriveARealPressDrag) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto handler = std::make_unique<NamedHooksHandler>();
  handler->rec = &rec;
  manager.add_handler(std::move(handler));

  ASSERT_TRUE(manager.handle_pointer(context, make_down({100.f, 100.f})));
  EXPECT_EQ(rec.left_down, 1);

  // 阈值内：连 on_mouse_move 都不该触发。
  EXPECT_TRUE(manager.handle_pointer(context, make_move({102.f, 101.f})));
  EXPECT_EQ(rec.mouse_moves, 0);

  EXPECT_TRUE(manager.handle_pointer(context, make_move({140.f, 100.f})));
  EXPECT_EQ(rec.mouse_moves, 1);

  EXPECT_TRUE(manager.handle_pointer(context, make_up({142.f, 100.f})));
  EXPECT_EQ(rec.left_up, 1);
  EXPECT_TRUE(manager.active());  // 钩子返回 consumed，drag 继续

  // 设备丢失（Cancel 相位）走 on_pointer_cancel。
  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Cancel, ButtonId::None, {142.f, 100.f}, 1.3)));
  EXPECT_EQ(rec.pointer_cancels, 1);
  manager.cancel_all();
}

TEST(DragManager, RightButtonDownHookCanCancelTheDrag) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<NamedHooksDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  // 激活式：右键（"直到右键退出"那条路径）由 drag 自己决定结束。
  EXPECT_TRUE(manager.handle_pointer(
      context, make_pointer(PointerPhase::Down, ButtonId::Secondary, {0.f, 0.f}, 1.0)));
  EXPECT_EQ(rec.right_down, 1);
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(manager.last_end(), DragEnd::Cancelled);
}

TEST(DragManager, EscapeCancelsThroughKeyPath) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  EXPECT_TRUE(manager.handle_key(context, make_key(KeyCode::Escape)));
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(rec.last_end, DragEnd::Cancelled);
}

TEST(DragManager, IgnoredKeyFallsThroughToShell) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  drag->cancel_on_escape = false;
  drag->key_result = EventStatus::Ignored;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  EXPECT_FALSE(manager.handle_key(context, make_key(KeyCode::Delete)));
  EXPECT_TRUE(manager.active());  // drag 还在，只是这个键没消费
  manager.cancel_all();
  EXPECT_EQ(rec.keys.size(), 1u);
}

TEST(DragManager, OwnerDestroyedEndsDrag) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  manager.owner_destroyed(context.owner());
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(rec.last_end, DragEnd::OwnerGone);

  // 幂等：再调不会重复 on_end。
  manager.owner_destroyed(context.owner());
  EXPECT_EQ(rec.ends, 1);
  EXPECT_EQ(manager.ended_count(), 1u);
}

TEST(DragManager, WatchdogTimesOutIdleDrag) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  double fake_now = 100.0;
  manager.set_clock([&fake_now] { return fake_now; });
  manager.set_watchdog_timeout(2.0);

  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  fake_now = 101.0;
  manager.tick(fake_now);
  EXPECT_TRUE(manager.active());

  fake_now = 103.5;
  manager.tick(fake_now);
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(rec.last_end, DragEnd::Timeout);
}

// —— 所有权 / 观测 ---------------------------------------------------------

TEST(DragManager, EventsFromOtherOwnerAreIgnored) {
  DragManager manager;
  FakeContext mine;
  mine.id = 1;
  FakeContext theirs;
  theirs.id = 2;
  Recorder rec;

  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(mine.owner(), mine, std::move(drag)));

  EXPECT_FALSE(manager.handle_pointer(theirs, make_move({1.f, 1.f})));
  EXPECT_FALSE(manager.handle_key(theirs, make_key(KeyCode::Escape)));
  EXPECT_TRUE(manager.active());
  EXPECT_EQ(rec.ends, 0);
  EXPECT_EQ(rec.phases.size(), 0u);
  manager.cancel_all();
}

TEST(DragManager, IdleAccessorsAreSafe) {
  DragManager manager;
  FakeContext context;
  EXPECT_EQ(manager.active_name(), std::string_view{});
  EXPECT_EQ(manager.owner(), 0u);
  EXPECT_FALSE(manager.past_threshold());

  auto drag = std::make_unique<FakeDrag>();
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));
  EXPECT_EQ(manager.owner(), context.owner());
  manager.cancel_all();
}

// 预览是 drag → 宿主单向推的：基类上没有 preview() 可问。
TEST(DragManager, DragPushesPreviewAndManagerClearsItAroundTheDrag) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  const int clears_before = context.preview_clears;
  const int redraws_before = context.redraws;

  auto drag = std::make_unique<PushPreviewDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));
  // 开始就清一次：上一轮留下的橡皮筋不能带到这一轮。
  EXPECT_EQ(context.preview_clears, clears_before + 1);
  // 而且清完必须重绘，否则屏幕还停在旧帧上。
  EXPECT_EQ(context.redraws, redraws_before + 1);
  EXPECT_TRUE(context.polyline.empty());

  EXPECT_TRUE(manager.handle_pointer(context, make_move({10.f, 10.f})));
  EXPECT_EQ(context.polyline.size(), 2u);
  EXPECT_EQ(context.points.size(), 1u);

  manager.cancel_all();
  EXPECT_EQ(context.preview_clears, clears_before + 2);  // 结束再清一次
  EXPECT_EQ(context.redraws, redraws_before + 2);        // 结束也必须重绘
  EXPECT_TRUE(context.polyline.empty());
  EXPECT_TRUE(context.points.empty());
  EXPECT_FALSE(context.rect.has_value());
}

TEST(DragManager, StalePreviewIsClearedWhenANewDragPreempts) {
  DragManager manager;
  FakeContext context;
  Recorder rec;

  auto first = std::make_unique<PushPreviewDrag>();
  first->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(first)));
  manager.handle_pointer(context, make_move({5.f, 5.f}));
  ASSERT_FALSE(context.polyline.empty());

  // 新交互抢占：旧的取消 + 预览清空，新的从干净画布开始。
  auto second = std::make_unique<PushPreviewDrag>();
  second->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(second)));
  EXPECT_TRUE(context.polyline.empty());
  manager.cancel_all();
}

TEST(DragManager, CompletionCallbackRunsAfterOnEndAndCanStartAgain) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  Recorder second_rec;

  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;

  bool ended_before_callback = false;
  std::vector<std::string> observed;
  manager.set_end_observer([&](DragOwnerId, std::string_view name, DragEnd reason) {
    observed.push_back(std::string(name) + ":" + std::string(to_string(reason)));
  });

  ASSERT_TRUE(manager.start(
      context.owner(), context, std::move(drag), [&](DragEnd reason) {
        // 回调时 drag 自己的 on_end 已经跑过了，且 drag 仍然活着。
        ended_before_callback = rec.ends == 1;
        observed.push_back(std::string("cb:") + std::string(to_string(reason)));
        // 重入安全：回调里可以立刻起下一个 drag。
        auto next = std::make_unique<FakeDrag>();
        next->rec = &second_rec;
        (void)manager.start(context.owner(), context, std::move(next));
      }));

  manager.cancel_all(DragEnd::Committed);
  EXPECT_TRUE(ended_before_callback);
  ASSERT_EQ(observed.size(), 2u);
  EXPECT_EQ(observed[0], "cb:committed");
  EXPECT_EQ(observed[1], "fake:committed");
  EXPECT_TRUE(manager.active());  // 回调里新起的那个还在
  manager.cancel_all();
  EXPECT_EQ(second_rec.last_end, DragEnd::Cancelled);
}

TEST(DragManager, ResetEndsActiveDragAndClearsCounters) {
  DragManager manager;
  FakeContext context;
  Recorder rec;
  auto drag = std::make_unique<FakeDrag>();
  drag->rec = &rec;
  ASSERT_TRUE(manager.start(context.owner(), context, std::move(drag)));

  manager.reset();
  EXPECT_FALSE(manager.active());
  EXPECT_EQ(rec.last_end, DragEnd::Cancelled);
  EXPECT_EQ(manager.ended_count(), 0u);
  EXPECT_FALSE(manager.last_end().has_value());
}

// —— 阻塞式：Drag::doIt / run_until_finished（借用语义） -------------------

TEST(DragDoIt, RunsToCompletionWithQueuedSource) {
  SingletonScope scope;
  FakeContext context;
  Recorder rec;

  QueuedEventSource source;
  source.push(make_down({10.f, 10.f}));
  source.push(make_move({40.f, 10.f}));
  source.push(make_up({40.f, 10.f}));  // ScriptedDrag 抬起即 finished
  scope.manager.set_event_source(&source);

  ScriptedDrag drag;  // 栈对象：这就是 doIt 的借用语义要支持的写法
  drag.rec = &rec;

  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Committed);
  EXPECT_EQ(rec.left_down, 1);
  EXPECT_EQ(rec.mouse_moves, 1);
  EXPECT_EQ(rec.left_up, 1);
  EXPECT_FALSE(scope.manager.active());
}

TEST(DragDoIt, EscapeCancelsAndReturnsToCaller) {
  SingletonScope scope;
  FakeContext context;
  Recorder rec;

  QueuedEventSource source;
  source.push(make_down({10.f, 10.f}));
  source.push(make_key(KeyCode::Escape));
  scope.manager.set_event_source(&source);

  ScriptedDrag drag;
  drag.rec = &rec;

  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Cancelled);
  EXPECT_EQ(rec.ends, 0);  // ScriptedDrag 没记 on_end，这里只确认返回码
  EXPECT_FALSE(scope.manager.active());
}

TEST(DragDoIt, ReportsErrorWithoutEventSource) {
  SingletonScope scope;
  FakeContext context;
  ScriptedDrag drag;

  const auto end = drag.doIt(context);
  EXPECT_FALSE(end);
  EXPECT_FALSE(scope.manager.active());
}

TEST(DragDoIt, ClosedSourceEndsAsOwnerGone) {
  SingletonScope scope;
  FakeContext context;
  Recorder rec;

  QueuedEventSource source;
  source.push(make_down({10.f, 10.f}));
  source.close();  // 队列吐空后 => alive() == false
  scope.manager.set_event_source(&source);

  ScriptedDrag drag;
  drag.rec = &rec;

  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::OwnerGone);
  EXPECT_EQ(rec.left_down, 1);  // 关闭前排队的事件没有被丢掉
}

TEST(DragDoIt, BlockedRunHitsWatchdogWhenHostStarvesEvents) {
  SingletonScope scope;
  FakeContext context;
  Recorder rec;

  QueuedEventSource source;  // 永不产事件，也不 close
  scope.manager.set_event_source(&source);
  scope.manager.set_watchdog_timeout(0.001);  // 1ms；poll 默认 50ms

  ScriptedDrag drag;
  drag.rec = &rec;

  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Timeout);
  EXPECT_FALSE(scope.manager.active());
}

TEST(DragDoIt, BorrowedDragIsNotOwnedOrDestroyed) {
  SingletonScope scope;
  FakeContext context;
  Recorder rec;

  QueuedEventSource source;
  source.push(make_up({1.f, 1.f}));
  scope.manager.set_event_source(&source);

  // 堆上分配、由调用方持有：如果管理器误删，用例结束时会二次释放。
  auto drag = std::make_unique<ScriptedDrag>();
  drag->rec = &rec;

  const auto end = drag->doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Committed);
  EXPECT_EQ(drag->name(), std::string_view{"scripted"});  // 对象仍然完好
  EXPECT_EQ(rec.left_up, 1);
}

// —— 第一个真 drag：BeamDrag（命令里的两点采集） ---------------------------

TEST(BeamDragTest, CollectsTwoScreenPointsAndCommits) {
  SingletonScope scope;
  FakeContext context;
  QueuedEventSource source;
  // 测试宿主的 cursor_world 把屏幕 (x,y) 当世界 (x,0,y) 用。
  source.push(make_down({10.f, 20.f}));   // 第一点
  source.push(make_move({20.f, 30.f}));   // 橡皮筋跟着走
  source.push(make_down({30.f, 40.f}));   // 第二点 → 提交
  scope.manager.set_event_source(&source);

  // 工作面给个非零标高：验证"点落在 drag 指定的平面上"这条链路。
  BeamDrag drag{3.f};  // 栈对象：借用语义，doIt 之后仍然能读结果
  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Committed);
  EXPECT_TRUE(drag.has_start());
  EXPECT_FLOAT_EQ(drag.start().x, 10.f);
  EXPECT_FLOAT_EQ(drag.start().y, 3.f);   // ← 不是在 0（地面）上
  EXPECT_FLOAT_EQ(drag.start().z, 20.f);
  EXPECT_FLOAT_EQ(drag.end().x, 30.f);
  EXPECT_FLOAT_EQ(drag.end().y, 3.f);
  EXPECT_FLOAT_EQ(drag.end().z, 40.f);
}

TEST(BeamDragTest, PushesRubberBandPreviewWhileMoving) {
  SingletonScope scope;
  FakeContext context;
  QueuedEventSource source;
  source.push(make_down({0.f, 0.f}));
  source.push(make_move({5.f, 5.f}));
  source.push(make_pointer(PointerPhase::Cancel, ButtonId::None, {5.f, 5.f}, 2.0));
  scope.manager.set_event_source(&source);

  BeamDrag drag{0.f};
  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Cancelled);       // Cancel 相位交给 drag，drag 取消
  EXPECT_TRUE(drag.has_start());             // 一个点已经采到了
  // 预览在移动时推过；管理器在收尾时清掉。
  EXPECT_TRUE(context.polyline.empty());
}

TEST(BeamDragTest, RightButtonCancelsBeforeSecondPoint) {
  SingletonScope scope;
  FakeContext context;
  QueuedEventSource source;
  source.push(make_down({1.f, 1.f}));
  source.push(make_pointer(PointerPhase::Down, ButtonId::Secondary, {2.f, 2.f}, 2.0));
  scope.manager.set_event_source(&source);

  BeamDrag drag{0.f};
  const auto end = drag.doIt(context);
  ASSERT_TRUE(end);
  EXPECT_EQ(*end, DragEnd::Cancelled);
}

}  // namespace
}  // namespace tamias
