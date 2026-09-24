#include "command/core/command_system.h"
#include "engine/document/document.h"
#include "engine/document/document_io.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace tamias {
namespace {

std::filesystem::path temp_dir() {
  const auto dir = std::filesystem::temp_directory_path() / "tamias_text_annotation_tests";
  std::filesystem::create_directories(dir);
  return dir;
}

struct Cmd {
  CommandRegistry registry;
  CommandSystem system;
  Document doc;

  explicit Cmd(const char* name) : system(registry), doc(name) { register_commands(registry); }
};

CommandArgs create_args(const std::string& text, Vec3 position, double size_px) {
  CommandArgs args;
  args["text"] = text;
  args["position"] = position;
  args["size_px"] = size_px;
  return args;
}

TEST(TextAnnotationCommands, CreateUndoRedoKeepsId) {
  Cmd cmd("anno");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("会议室", Vec3{1.f, 0.f, 2.f}, 18.0)));
  ASSERT_EQ(cmd.doc.text_annotations().size(), 1u);
  const TextAnnotation& added = cmd.doc.text_annotations().front();
  EXPECT_NE(added.id, 0u);
  EXPECT_EQ(added.text, "会议室");
  EXPECT_EQ(added.kind, TextKind::Annotation);
  EXPECT_FLOAT_EQ(added.size_px, 18.f);
  EXPECT_FLOAT_EQ(added.anchor.x, 1.f);
  EXPECT_FLOAT_EQ(added.anchor.z, 2.f);
  const std::uint64_t id = added.id;

  cmd.system.undo();
  EXPECT_TRUE(cmd.doc.text_annotations().empty());
  cmd.system.redo();
  ASSERT_EQ(cmd.doc.text_annotations().size(), 1u);
  EXPECT_EQ(cmd.doc.text_annotations().front().id, id);  // redo 保留 id
}

TEST(TextAnnotationCommands, UpdateOnlyTouchesGivenFields) {
  Cmd cmd("anno");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("A", Vec3{1.f, 0.f, 1.f}, 20.0)));
  const std::uint64_t id = cmd.doc.text_annotations().front().id;

  CommandArgs args;
  args["text_id"] = static_cast<std::int64_t>(id);
  args["text"] = std::string("B");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "update_text", args));
  ASSERT_EQ(cmd.doc.text_annotations().size(), 1u);
  EXPECT_EQ(cmd.doc.text_annotations().front().text, "B");
  EXPECT_FLOAT_EQ(cmd.doc.text_annotations().front().size_px, 20.f);  // 没给就保持
  EXPECT_FLOAT_EQ(cmd.doc.text_annotations().front().anchor.x, 1.f);

  cmd.system.undo();
  EXPECT_EQ(cmd.doc.text_annotations().front().text, "A");
  EXPECT_FLOAT_EQ(cmd.doc.text_annotations().front().size_px, 20.f);
  cmd.system.redo();
  EXPECT_EQ(cmd.doc.text_annotations().front().text, "B");
}

TEST(TextAnnotationCommands, UpdateMovesAndRestyles) {
  Cmd cmd("anno");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("tag", Vec3{0.f, 0.f, 0.f}, 14.0)));
  const std::uint64_t id = cmd.doc.text_annotations().front().id;

  CommandArgs args;
  args["text_id"] = static_cast<std::int64_t>(id);
  args["position"] = Vec3{5.f, 1.f, 6.f};
  args["size_px"] = 24.0;
  args["align"] = std::string("center");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "update_text", args));
  const TextAnnotation& moved = cmd.doc.text_annotations().front();
  EXPECT_FLOAT_EQ(moved.anchor.x, 5.f);
  EXPECT_FLOAT_EQ(moved.anchor.y, 1.f);
  EXPECT_FLOAT_EQ(moved.size_px, 24.f);
  EXPECT_EQ(moved.align, TextAlign::Center);

  cmd.system.undo();
  EXPECT_FLOAT_EQ(cmd.doc.text_annotations().front().anchor.x, 0.f);
  EXPECT_EQ(cmd.doc.text_annotations().front().align, TextAlign::Left);
}

TEST(TextAnnotationCommands, DeleteUndoRestoresAtSameIndex) {
  Cmd cmd("anno");
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("first", Vec3{0.f, 0.f, 0.f}, 14.0)));
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("second", Vec3{1.f, 0.f, 0.f}, 14.0)));
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "create_text",
                                  create_args("third", Vec3{2.f, 0.f, 0.f}, 14.0)));
  const std::uint64_t middle_id = cmd.doc.text_annotations()[1].id;

  CommandArgs args;
  args["text_id"] = static_cast<std::int64_t>(middle_id);
  ASSERT_TRUE(cmd.system.dispatch(cmd.doc, "delete_text", args));
  ASSERT_EQ(cmd.doc.text_annotations().size(), 2u);

  cmd.system.undo();
  ASSERT_EQ(cmd.doc.text_annotations().size(), 3u);
  EXPECT_EQ(cmd.doc.text_annotations()[1].id, middle_id);  // 回到原位置
  EXPECT_EQ(cmd.doc.text_annotations()[1].text, "second");
}

TEST(TextAnnotationCommands, RejectsUnknownId) {
  Cmd cmd("anno");
  CommandArgs args;
  args["text_id"] = static_cast<std::int64_t>(999);
  EXPECT_FALSE(cmd.system.dispatch(cmd.doc, "update_text", args).has_value());
  EXPECT_FALSE(cmd.system.dispatch(cmd.doc, "delete_text", args).has_value());
}

TEST(TextAnnotationPersistence, RoundTripsThroughTdoc) {
  Document doc("anno-io");
  TextAnnotation annotation;
  annotation.kind = TextKind::RoomName;
  annotation.text = "首层平面";
  annotation.anchor = Vec3{3.f, 0.f, -2.f};
  annotation.size_px = 20.f;
  annotation.color = Vec3{0.9f, 0.8f, 0.7f};
  annotation.opacity = 0.75f;
  annotation.align = TextAlign::Center;
  const std::uint64_t id = doc.add_text_annotation(annotation).id;

  const auto path = temp_dir() / "text_annotation_roundtrip.tdoc";
  ViewportState viewport{};
  ASSERT_TRUE(save_document(path, doc, viewport));
  auto loaded = load_document(path);
  ASSERT_TRUE(loaded) << loaded.error();

  const std::vector<TextAnnotation>& annotations = loaded->document.text_annotations();
  ASSERT_EQ(annotations.size(), 1u);
  EXPECT_EQ(annotations.front().id, id);
  EXPECT_EQ(annotations.front().kind, TextKind::RoomName);
  EXPECT_EQ(annotations.front().text, "首层平面");
  EXPECT_FLOAT_EQ(annotations.front().anchor.x, 3.f);
  EXPECT_FLOAT_EQ(annotations.front().anchor.z, -2.f);
  EXPECT_FLOAT_EQ(annotations.front().size_px, 20.f);
  EXPECT_FLOAT_EQ(annotations.front().color.y, 0.8f);
  EXPECT_FLOAT_EQ(annotations.front().opacity, 0.75f);
  EXPECT_EQ(annotations.front().align, TextAlign::Center);
  EXPECT_GE(loaded->document.next_text_annotation_id(), id + 1);

  // 旧文档没有 ANNO 块：读到空表，也不算错（向后兼容）。
  Document plain("plain");
  const auto plain_path = temp_dir() / "text_annotation_empty.tdoc";
  ASSERT_TRUE(save_document(plain_path, plain, viewport));
  auto plain_loaded = load_document(plain_path);
  ASSERT_TRUE(plain_loaded) << plain_loaded.error();
  EXPECT_TRUE(plain_loaded->document.text_annotations().empty());
}

}  // namespace
}  // namespace tamias
