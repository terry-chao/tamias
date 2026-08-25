#pragma once

#include "command/command.h"
#include "engine/document/document.h"

namespace tamias {

class SetLocationCommand final : public Command {
 public:
  SetLocationCommand(Document& document, std::uint64_t entity_id, std::uint64_t storey_id,
                     double elevation_offset);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  [[nodiscard]] Result<void> apply(std::uint64_t storey_id, double elevation_offset);

  Document* document_ = nullptr;
  std::uint64_t entity_id_ = 0;
  std::uint64_t from_storey_id_ = 0;
  std::uint64_t to_storey_id_ = 0;
  double from_offset_ = 0.0;
  double to_offset_ = 0.0;
};

}  // namespace tamias
