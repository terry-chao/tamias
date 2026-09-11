#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/render/texture_asset.h"

#include <cstdint>

namespace tamias {

// 原地替换贴图像素（保留 id，generation++）。undo 写回完整旧资产。
class ReplaceTextureCommand final : public Command {
 public:
  ReplaceTextureCommand(Document& document, std::uint64_t id, TextureAsset incoming);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t id_ = 0;
  TextureAsset incoming_;
  TextureAsset old_;
  TextureAsset new_;
  bool executed_ = false;
};

}  // namespace tamias
