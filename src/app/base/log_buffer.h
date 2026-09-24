#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tamias {

// 最近日志行的环形缓冲。诊断面板靠它做到：客户不用去翻日志文件，点「复制报告」
// 就把最后几百行一起带走（支持工单最需要的那一段）。
//
// 它挂在 logging 的 sink 上，**不替换** stderr 输出（sink 是额外的回调）。
void install_log_buffer(std::size_t capacity = 400);

// 按时间顺序返回缓冲里的行（不取出，可重复读）。
[[nodiscard]] std::vector<std::string> recent_log_lines();

}  // namespace tamias
