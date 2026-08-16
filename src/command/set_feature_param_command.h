#pragma once

#include "command/command.h"
#include "engine/document/document.h"

#include <cstdint>
#include <string>

namespace tamias {

// 修改实体某个特征参数，并重算其网格（可撤销）。
class SetFeatureParamCommand final : public Command {
 public:
  SetFeatureParamCommand(Document& document, std::uint64_t entity_id,
                         std::uint64_t feature_id, std::string param_name, double new_value);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  // 受影响的 mesh 资产 id（供视口上传 GPU）。
  [[nodiscard]] std::uint64_t mesh_asset_id() const { return mesh_asset_id_; }

 private:
  Result<void> apply(double value);

  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  std::uint64_t feature_id_ = 0;
  std::string param_name_;
  double old_value_ = 0.0;
  double new_value_ = 0.0;
  std::uint64_t mesh_asset_id_ = 0;
};

}  // namespace tamias
