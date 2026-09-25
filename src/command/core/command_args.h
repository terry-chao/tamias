#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tamias {

// 命令参数：异构值（数值 / 整数 id / 向量 / 字符串 / 数组）。
// 单独一个头：Command 基类要声明 echo_args()，command_system.h 又要 include
// command.h，两边都 include 本文件，避免循环。
using CommandArg =
    std::variant<double, std::int64_t, Vec3, std::string, std::vector<Vec3>, std::vector<double>>;
using CommandArgs = std::unordered_map<std::string, CommandArg>;

}  // namespace tamias
