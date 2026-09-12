#pragma once

#include "command/command.h"
#include "engine/document/document.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 楼层设置：一次替换整张楼层表（增 / 删 / 改名 / 改标高 / 改层高 / 夹层），
// 整条命令一步撤销。表里 id == 0 的条目是新增，命令首次执行时分配 id 并写回，
// 这样 redo 用的是同一批句柄，撤销再重做不会换 id。
class UpdateStoreysCommand final : public Command {
 public:
  UpdateStoreysCommand(Document& document, std::vector<Storey> storeys,
                       std::uint64_t active_storey_id)
      : document_(&document),
        after_(std::move(storeys)),
        after_active_(active_storey_id) {}

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  void apply(std::vector<Storey>& storeys, std::uint64_t active_storey_id);

  Document* document_ = nullptr;
  std::vector<Storey> after_;
  std::vector<Storey> before_;
  std::uint64_t after_active_ = 0;
  std::uint64_t before_active_ = 0;
  bool captured_ = false;
};

}  // namespace tamias
