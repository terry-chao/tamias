#include "pin_result_dialog.h"

#include "engine/document/document.h"
#include "engine/render/render_scene_golden.h"
#include "golden_test_case.h"
#include "qt_path.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>
#include <filesystem>
#include <utility>

namespace tamias {
namespace {

struct Tally {
  int passed = 0;
  int failed = 0;

  void add(GoldenTestCase::Status status) {
    if (status == GoldenTestCase::Status::Passed) {
      ++passed;
    } else if (status == GoldenTestCase::Status::Failed) {
      ++failed;
    }
  }

  [[nodiscard]] bool ok() const { return failed == 0 && passed > 0; }
};

QTableWidget* make_table(QWidget* parent, const QStringList& headers) {
  auto* table = new QTableWidget(parent);
  table->setColumnCount(headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->horizontalHeader()->setStretchLastSection(true);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setWordWrap(true);
  return table;
}

QColor status_color(GoldenTestCase::Status status) {
  switch (status) {
    case GoldenTestCase::Status::Passed:
      return QColor(QStringLiteral("#1b7a3a"));
    case GoldenTestCase::Status::Skipped:
      return QColor(QStringLiteral("#8a6d00"));
    case GoldenTestCase::Status::Failed:
      return QColor(QStringLiteral("#b42318"));
  }
  return QColor(QStringLiteral("#b42318"));
}

QTableWidgetItem* cell(const QString& text, GoldenTestCase::Status status, bool colorize) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  if (colorize) {
    item->setForeground(status_color(status));
  }
  return item;
}

void add_row(QTableWidget* table, Tally* tally, const QString& a, const QString& b,
             const QString& c, GoldenTestCase::Status status) {
  const int row = table->rowCount();
  table->insertRow(row);
  table->setItem(row, 0, cell(a, status, false));
  table->setItem(row, 1, cell(b, status, false));
  table->setItem(row, 2, cell(c, status, true));
  if (tally != nullptr) {
    tally->add(status);
  }
}

QString status_text(const PinResultDialog& dlg, GoldenTestCase::Status status) {
  switch (status) {
    case GoldenTestCase::Status::Passed:
      return dlg.tr("通过");
    case GoldenTestCase::Status::Skipped:
      return dlg.tr("跳过");
    case GoldenTestCase::Status::Failed:
      return dlg.tr("失败");
  }
  return dlg.tr("失败");
}

QString mode_text(const PinResultDialog& dlg, RenderMode mode) {
  switch (mode) {
    case RenderMode::Wireframe:
      return dlg.tr("线框");
    case RenderMode::Shaded:
      return dlg.tr("着色");
    case RenderMode::Realistic:
      return dlg.tr("真实感");
  }
  return QString::number(static_cast<int>(mode));
}

QString fmt_vec(const Vec3& v) {
  return QStringLiteral("(%1, %2, %3)")
      .arg(v.x, 0, 'f', 3)
      .arg(v.y, 0, 'f', 3)
      .arg(v.z, 0, 'f', 3);
}

QString fmt_aabb(const Aabb& box) {
  if (!box.valid()) {
    return QStringLiteral("—");
  }
  return QStringLiteral("%1 – %2").arg(fmt_vec(box.min), fmt_vec(box.max));
}

QString fmt_tex(const PinResultDialog& dlg, std::uint64_t id) {
  return id == 0 ? dlg.tr("无") : QString::number(id);
}

QString fmt_translation(const Mat4& m) {
  return fmt_vec({m(0, 3), m(1, 3), m(2, 3)});
}

bool near_eq(float a, float b) {
  return std::fabs(a - b) <= 1.0e-4f;
}

bool vec_eq(const Vec3& a, const Vec3& b) {
  return near_eq(a.x, b.x) && near_eq(a.y, b.y) && near_eq(a.z, b.z);
}

bool aabb_eq(const Aabb& a, const Aabb& b) {
  if (a.valid() != b.valid()) {
    return false;
  }
  if (!a.valid()) {
    return true;
  }
  return vec_eq(a.min, b.min) && vec_eq(a.max, b.max);
}

bool mat_eq(const Mat4& a, const Mat4& b) {
  for (int i = 0; i < 16; ++i) {
    if (!near_eq(a.m[i], b.m[i])) {
      return false;
    }
  }
  return true;
}

std::uint64_t item_tris(const RenderScene& scene, const SceneDrawItem& item) {
  const auto it = scene.meshes.find(item.mesh_asset_id);
  if (it == scene.meshes.end() || it->second.line_list) {
    return 0;
  }
  return static_cast<std::uint64_t>(it->second.indices.size() / 3);
}

std::uint64_t vertex_count(const RenderScene& scene) {
  std::uint64_t n = 0;
  for (const auto& [id, mesh] : scene.meshes) {
    (void)id;
    n += static_cast<std::uint64_t>(mesh.vertices.size());
  }
  return n;
}

struct CaseCopy {
  QString title;
  QString meaning;
};

CaseCopy describe_case(const PinResultDialog& dlg, const QString& id) {
  const QString name = id.section(QLatin1Char('.'), -1);
  if (name == QLatin1String("PinWritesSidecarAndRoundTrips")) {
    return {dlg.tr("写盘与回读"), dlg.tr("Pin 写入 sidecar 后 digest 能往返")};
  }
  if (name == QLatin1String("RefreshSidecarFromExistingTrscn")) {
    return {dlg.tr("补全 sidecar"), dlg.tr("缺 json 时能从 .trscn 重建摘要")};
  }
  if (name == QLatin1String("IncompleteRepoFixturesAreRejected")) {
    return {dlg.tr("半成品夹具"), dlg.tr("仓库里不能只有 trscn 没有 json")};
  }
  if (name == QLatin1String("ScansRepositoryFixtures")) {
    return {dlg.tr("扫描仓库金样"),
            dlg.tr("所有夹具的 digest、条数、Mock draw 次数；以后改代码会碰这把锁")};
  }
  if (name == QLatin1String("RejectsBadSlug")) {
    return {dlg.tr("夹具名规则"), dlg.tr("非法名字会被拒绝")};
  }
  return {name, id};
}

std::pair<QString, GoldenTestCase::Status> check_u64(const PinResultDialog& dlg, std::uint64_t live,
                                                     bool loaded_ok, std::uint64_t file) {
  if (!loaded_ok) {
    return {dlg.tr("无法回读文件"), GoldenTestCase::Status::Failed};
  }
  if (live == file) {
    return {dlg.tr("回读一致"), GoldenTestCase::Status::Passed};
  }
  return {dlg.tr("回读=%1，不一致").arg(file), GoldenTestCase::Status::Failed};
}

std::pair<QString, GoldenTestCase::Status> check_text(const PinResultDialog& dlg,
                                                      const QString& live, bool loaded_ok,
                                                      const QString& file) {
  if (!loaded_ok) {
    return {dlg.tr("无法回读文件"), GoldenTestCase::Status::Failed};
  }
  if (live == file) {
    return {dlg.tr("回读一致"), GoldenTestCase::Status::Passed};
  }
  return {dlg.tr("回读不一致"), GoldenTestCase::Status::Failed};
}

std::pair<QString, GoldenTestCase::Status> item_check(const PinResultDialog& dlg, bool file_ok,
                                                      const SceneDrawItem* fitem,
                                                      const QString& file_v, bool same) {
  if (!file_ok || fitem == nullptr) {
    return {dlg.tr("无法回读文件"), GoldenTestCase::Status::Failed};
  }
  if (same) {
    return {dlg.tr("回读一致"), GoldenTestCase::Status::Passed};
  }
  return {dlg.tr("回读=%1").arg(file_v), GoldenTestCase::Status::Failed};
}

Tally fill_scene_table(PinResultDialog& dlg, QTableWidget* table, const RenderScene& live,
                       const QString& golden_dir) {
  Tally tally;
  const auto dir = qstring_to_path(golden_dir);
  auto loaded = load_render_scene(dir / "scene.trscn");
  auto loaded_meta = load_render_scene_golden_meta(dir);
  const bool file_ok = static_cast<bool>(loaded);
  const RenderScene* file = file_ok ? &*loaded : nullptr;

  const QString trscn_path = path_to_qstring(dir / "scene.trscn");
  const QString json_path = path_to_qstring(dir / "scene.meta.json");
  const QString inspect_path = path_to_qstring(dir / "scene.inspect.txt");
  const bool has_trscn = QFileInfo::exists(trscn_path);
  const bool has_json = QFileInfo::exists(json_path);
  const bool has_inspect = QFileInfo::exists(inspect_path);
  const bool sidecar_ok = has_trscn && has_json && has_inspect;
  add_row(table, &tally, dlg.tr("sidecar 文件"),
          dlg.tr("trscn / json / inspect"),
          sidecar_ok ? dlg.tr("三份都在") : dlg.tr("缺文件"),
          sidecar_ok ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed);

  const auto add_count = [&](const QString& name, std::uint64_t live_n, std::uint64_t file_n) {
    const auto [note, st] = check_u64(dlg, live_n, file_ok, file_n);
    add_row(table, &tally, name, QString::number(live_n), note, st);
  };

  add_count(dlg.tr("模型 / 网格"), static_cast<std::uint64_t>(live.meshes.size()),
            file ? static_cast<std::uint64_t>(file->meshes.size()) : 0);
  add_count(dlg.tr("顶点"), vertex_count(live), file ? vertex_count(*file) : 0);
  add_count(dlg.tr("三角面"), render_scene_triangle_count(live),
            file ? render_scene_triangle_count(*file) : 0);
  add_count(dlg.tr("Draw call（= Mock 应提交次数）"),
            static_cast<std::uint64_t>(live.items.size()),
            file ? static_cast<std::uint64_t>(file->items.size()) : 0);
  add_count(dlg.tr("贴图"), static_cast<std::uint64_t>(live.textures.size()),
            file ? static_cast<std::uint64_t>(file->textures.size()) : 0);

  const QString live_mode = mode_text(dlg, live.view.mode);
  {
    const auto [note, st] =
        check_text(dlg, live_mode, file_ok, file ? mode_text(dlg, file->view.mode) : QString());
    add_row(table, &tally, dlg.tr("渲染模式"), live_mode, note, st);
  }

  const QString live_digest = QString::fromStdString(render_scene_digest(live));
  QString digest_note = dlg.tr("无法回读文件");
  auto digest_st = GoldenTestCase::Status::Failed;
  if (file_ok) {
    const QString file_digest = QString::fromStdString(render_scene_digest(*file));
    if (live_digest == file_digest) {
      digest_note = dlg.tr("文件 digest 一致（网格/贴图像素/draw 未变）");
      digest_st = GoldenTestCase::Status::Passed;
    } else {
      digest_note = dlg.tr("文件 digest 不同");
    }
  }
  if (loaded_meta) {
    const QString json_digest = QString::fromStdString(loaded_meta->digest);
    if (live_digest == json_digest && digest_st == GoldenTestCase::Status::Passed) {
      digest_note = dlg.tr("文件与 json 均一致（以后改代码对这把锁）");
    } else if (live_digest != json_digest) {
      digest_note = dlg.tr("与 json digest 不一致");
      digest_st = GoldenTestCase::Status::Failed;
    }
  } else {
    digest_note = dlg.tr("缺 scene.meta.json");
    digest_st = GoldenTestCase::Status::Failed;
  }
  add_row(table, &tally, dlg.tr("digest 锁定"), live_digest, digest_note, digest_st);

  if (loaded_meta) {
    const auto json_count = [&](const QString& name, std::uint64_t live_n, std::uint64_t json_n) {
      const bool same = live_n == json_n;
      add_row(table, &tally, name, QString::number(live_n),
              same ? dlg.tr("与 json 一致") : dlg.tr("json=%1").arg(json_n),
              same ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed);
    };
    json_count(dlg.tr("json 网格数"), static_cast<std::uint64_t>(live.meshes.size()),
               loaded_meta->meshes);
    json_count(dlg.tr("json Draw 数"), static_cast<std::uint64_t>(live.items.size()),
               loaded_meta->items);
    json_count(dlg.tr("json 贴图数"), static_cast<std::uint64_t>(live.textures.size()),
               loaded_meta->textures);
    const bool mode_ok = static_cast<int>(live.view.mode) == loaded_meta->mode;
    add_row(table, &tally, dlg.tr("json 渲染模式"), live_mode,
            mode_ok ? dlg.tr("与 json 一致") : dlg.tr("json 不一致"),
            mode_ok ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed);
  }

  if (file_ok) {
    Document doc = document_from_render_scene(*file);
    const auto hydrated = static_cast<std::uint64_t>(doc.render_items().size());
    const auto expected = static_cast<std::uint64_t>(file->items.size());
    const bool same = hydrated == expected;
    add_row(table, &tally, dlg.tr("水合 Draw 条数"), QString::number(hydrated),
            same ? dlg.tr("读回后仍能展平同样条数")
                 : dlg.tr("期望 %1").arg(expected),
            same ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed);
  } else {
    add_row(table, &tally, dlg.tr("水合 Draw 条数"), QStringLiteral("—"),
            dlg.tr("无法回读文件"), GoldenTestCase::Status::Failed);
  }

  const std::size_t n = live.items.size();
  for (std::size_t i = 0; i < n; ++i) {
    const SceneDrawItem& item = live.items[i];
    const SceneDrawItem* fitem =
        (file && i < file->items.size()) ? &file->items[i] : nullptr;
    const QString prefix = dlg.tr("Draw %1").arg(i + 1);

    {
      const QString live_v = QString::number(item.mesh_asset_id);
      const QString file_v = fitem ? QString::number(fitem->mesh_asset_id) : QString();
      const auto [note, st] =
          item_check(dlg, file_ok, fitem, file_v, fitem && fitem->mesh_asset_id == item.mesh_asset_id);
      add_row(table, &tally, prefix + dlg.tr(" 网格 id"), live_v, note, st);
    }
    {
      const auto live_t = item_tris(live, item);
      const auto file_t = fitem ? item_tris(*file, *fitem) : 0;
      const auto [note, st] =
          item_check(dlg, file_ok, fitem, QString::number(file_t), fitem && file_t == live_t);
      add_row(table, &tally, prefix + dlg.tr(" 三角面"), QString::number(live_t), note, st);
    }
    {
      const QString live_v = fmt_translation(item.transform);
      const QString file_v = fitem ? fmt_translation(fitem->transform) : QString();
      const auto [note, st] =
          item_check(dlg, file_ok, fitem, file_v, fitem && mat_eq(item.transform, fitem->transform));
      add_row(table, &tally, prefix + dlg.tr(" 变换平移"), live_v, note, st);
    }
    {
      const QString live_v = fmt_aabb(item.bounds);
      const QString file_v = fitem ? fmt_aabb(fitem->bounds) : QString();
      const auto [note, st] =
          item_check(dlg, file_ok, fitem, file_v, fitem && aabb_eq(item.bounds, fitem->bounds));
      add_row(table, &tally, prefix + dlg.tr(" 包围盒"), live_v, note, st);
    }
    {
      const QString live_v = fmt_vec(item.color);
      const QString file_v = fitem ? fmt_vec(fitem->color) : QString();
      const auto [note, st] =
          item_check(dlg, file_ok, fitem, file_v, fitem && vec_eq(item.color, fitem->color));
      add_row(table, &tally, prefix + dlg.tr(" 颜色"), live_v, note, st);
    }
    {
      const QString live_v = QStringLiteral("R=%1 M=%2 A=%3")
                                 .arg(item.roughness, 0, 'f', 2)
                                 .arg(item.metallic, 0, 'f', 2)
                                 .arg(item.opacity, 0, 'f', 2);
      const QString file_v =
          fitem ? QStringLiteral("R=%1 M=%2 A=%3")
                      .arg(fitem->roughness, 0, 'f', 2)
                      .arg(fitem->metallic, 0, 'f', 2)
                      .arg(fitem->opacity, 0, 'f', 2)
                : QString();
      const bool same = fitem && near_eq(item.roughness, fitem->roughness) &&
                        near_eq(item.metallic, fitem->metallic) &&
                        near_eq(item.opacity, fitem->opacity);
      const auto [note, st] = item_check(dlg, file_ok, fitem, file_v, same);
      add_row(table, &tally, prefix + dlg.tr(" 粗糙度/金属度/不透明度"), live_v, note, st);
    }
    {
      const QString live_v = fmt_tex(dlg, item.albedo_texture_id);
      const QString file_v = fitem ? fmt_tex(dlg, fitem->albedo_texture_id) : QString();
      const auto [note, st] = item_check(
          dlg, file_ok, fitem, file_v, fitem && fitem->albedo_texture_id == item.albedo_texture_id);
      add_row(table, &tally, prefix + dlg.tr(" albedo 贴图"), live_v, note, st);
    }
    {
      const QString live_v = fmt_tex(dlg, item.normal_texture_id);
      const QString file_v = fitem ? fmt_tex(dlg, fitem->normal_texture_id) : QString();
      const auto [note, st] = item_check(
          dlg, file_ok, fitem, file_v, fitem && fitem->normal_texture_id == item.normal_texture_id);
      add_row(table, &tally, prefix + dlg.tr(" normal 贴图"), live_v, note, st);
    }
  }
  table->resizeRowsToContents();
  return tally;
}

}  // namespace

PinResultDialog::PinResultDialog(const RenderScene& scene, const QString& golden_dir,
                                 const QString& inspect_text, const GoldenTestRun& tests,
                                 QWidget* parent)
    : QDialog(parent) {
  resize(1240, 700);

  auto* root = new QVBoxLayout(this);
  auto* splitter = new QSplitter(Qt::Horizontal, this);

  auto* left = new QWidget(this);
  auto* left_lay = new QVBoxLayout(left);
  left_lay->setContentsMargins(0, 0, 0, 0);

  auto* summary_label = new QLabel(tr("能测到的（总评）"), left);
  auto* summary = make_table(left, {tr("测项"), tr("结果"), tr("说明")});
  auto* scene_label = new QLabel(tr("这一帧货单（视口 vs 刚写出的 .trscn）"), left);
  auto* scene_table = make_table(left, {tr("项目"), tr("这一帧"), tr("核对")});
  const Tally scene_tally = fill_scene_table(*this, scene_table, scene, golden_dir);

  Tally suite_tally;
  auto* suite_label = new QLabel(tr("金样套件（gtest，锁以后会不会偷偷变）"), left);
  auto* suite_table = make_table(left, {tr("测试"), tr("结果"), tr("说明")});
  const auto& cases = tests.cases;
  if (cases.isEmpty()) {
    const auto status =
        tests.ok ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed;
    add_row(suite_table, &suite_tally, tr("运行金样测试"), status_text(*this, status),
            tests.ok ? tr("未解析到分项，见右侧日志") : tr("未能运行测试，见右侧日志"),
            status);
  } else {
    for (const GoldenTestCase& c : cases) {
      const CaseCopy copy = describe_case(*this, c.id);
      QString title = copy.title;
      if (c.duration_ms >= 0) {
        title += QStringLiteral("  (%1 ms)").arg(c.duration_ms);
      }
      add_row(suite_table, &suite_tally, title, status_text(*this, c.status), copy.meaning,
              c.status);
    }
  }
  suite_table->resizeRowsToContents();

  const auto scene_st =
      scene_tally.ok() ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed;
  const auto suite_st = tests.ok ? GoldenTestCase::Status::Passed : GoldenTestCase::Status::Failed;
  add_row(summary, nullptr, tr("货单回读"), status_text(*this, scene_st),
          scene_tally.ok()
              ? tr("网格、三角、Draw、贴图、digest、每条 Draw 字段与文件一致")
              : tr("%1 项失败，见下方明细").arg(scene_tally.failed),
          scene_st);
  add_row(summary, nullptr, tr("digest 金样锁"), status_text(*this, scene_st),
          tr("写入 json 的哈希；以后改烘焙/展平会对不上"), scene_st);
  add_row(summary, nullptr, tr("金样套件"), status_text(*this, suite_st),
          tests.ok ? tr("RenderSceneGolden* 全部通过")
                   : tr("gtest 失败，见下方与右侧日志"),
          suite_st);
  summary->resizeRowsToContents();

  auto* note = new QLabel(
      tr("测不到：几何该不该长这样、shader / 深度 / 光照 / 像素、特征树与布尔。"), left);
  note->setWordWrap(true);

  left_lay->addWidget(summary_label);
  left_lay->addWidget(summary, 0);
  left_lay->addWidget(scene_label);
  left_lay->addWidget(scene_table, 3);
  left_lay->addWidget(suite_label);
  left_lay->addWidget(suite_table, 2);
  left_lay->addWidget(note);

  auto* edit = new QPlainTextEdit(this);
  edit->setReadOnly(true);
  edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  edit->setPlainText(inspect_text);

  splitter->addWidget(left);
  splitter->addWidget(edit);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  splitter->setSizes({580, 640});

  root->addWidget(splitter);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  root->addWidget(buttons);

  const bool all_ok = scene_tally.ok() && tests.ok;
  setWindowTitle(all_ok ? tr("钉住报告：能测到的都通过") : tr("钉住报告：有失败项"));
}

}  // namespace tamias
