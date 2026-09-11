#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/render/texture_asset.h"

#include <cstdint>

namespace tamias {

// 导入贴图（按内容指纹去重）。像素走构造函数，不进 CommandArg。
class ImportTextureCommand final : public Command {
 public:
  ImportTextureCommand(Document& document, TextureAsset asset);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;
  [[nodiscard]] std::uint64_t texture_id() const { return id_; }

 private:
  Document* document_ = nullptr;
  TextureAsset incoming_;
  TextureAsset stored_;
  std::uint64_t id_ = 0;
  bool added_ = false;
};

}  // namespace tamias
