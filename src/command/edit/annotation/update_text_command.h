#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/document/text_annotation.h"

#include <cstdint>

namespace tamias {

// 改一段已有注记（文本 / 锚点 / 字高 / 颜色 / 不透明度 / 对齐）。
// 第一次 execute 时记下原值，undo 整条回退——和 UpdateGridCommand 一个路子，
// 但粒度是单条注记，不整表替换。
class UpdateTextCommand final : public Command {
 public:
  UpdateTextCommand(Document& document, std::uint64_t text_id, TextAnnotation updated);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  void apply(const TextAnnotation& value);

  Document* document_ = nullptr;
  std::uint64_t text_id_ = 0;
  TextAnnotation after_;
  TextAnnotation before_;
  bool captured_ = false;
};

}  // namespace tamias
