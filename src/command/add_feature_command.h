#pragma once

#include "command/command.h"
#include "engine/document/document.h"
#include "engine/modeling/feature.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace tamias {

// 往实体追加一个特征（如 Fillet / Chamfer）并重算其网格（可撤销）。
// input 自动取实体当前的输出特征，追加的特征成为新的输出。
class AddFeatureCommand final : public Command {
 public:
  AddFeatureCommand(Document& document, std::uint64_t entity_id, FeatureKind kind,
                    std::unordered_map<std::string, double> params);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t feature_id() const { return feature_id_; }

 private:
  Result<void> apply(bool add);

  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  FeatureKind kind_ = FeatureKind::Fillet;
  std::unordered_map<std::string, double> params_;
  std::uint64_t feature_id_ = 0;
};

}  // namespace tamias
