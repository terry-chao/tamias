#pragma once

#include "command/command.h"
#include "engine/document/document.h"

#include <string>

namespace tamias {

class CreateStoreyCommand final : public Command {
 public:
  CreateStoreyCommand(Document& document, std::string name, double elevation)
      : document_(&document), name_(std::move(name)), elevation_(elevation) {}

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::string name_;
  double elevation_ = 0.0;
  Storey storey_{};
  bool created_ = false;
};

}  // namespace tamias
