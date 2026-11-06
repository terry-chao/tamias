#pragma once

#include <QString>

#include <string>

namespace tamias {

// 节点编辑器里给用户看的文字。引擎 / 命令层的消息是英文稳定串（测试也断言它们），
// 所以翻译只做在 UI 边界这一层：认识的给中文，认不出的原样返回，
// 别把技术细节（内核报错原文）吞掉。
//
// 新增引擎消息时在这里补一条映射；找不到映射的会退回英文原文。
[[nodiscard]] QString node_state_message(const std::string& raw);
[[nodiscard]] QString node_command_error(const std::string& raw);

}  // namespace tamias
