#pragma once

#include "engine/render/text/font_candidate.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace tamias {

// 平台自带的字体目录（不存在的会被过滤掉）。engine 不认识 "assets/fonts"——
// 那是应用的事，壳应该把它**插在系统目录前面**：
//   dirs = { app_assets_fonts } ∪ default_font_dirs();
[[nodiscard]] std::vector<std::filesystem::path> default_font_dirs();

// 这些目录下的 .ttf / .otf / .ttc（按路径排序，结果稳定）。
[[nodiscard]] std::vector<std::filesystem::path> list_font_files(
    const std::vector<std::filesystem::path>& dirs);

// 挑一个「像正文」的默认字体：按常见家族名偏好匹配，都没有就退回第一个文件。
// 目录里一个字体都没有时返回 nullopt（调用方据此关掉文字通路，而不是崩）。
[[nodiscard]] std::optional<FontCandidate> pick_default_font(
    const std::vector<std::filesystem::path>& dirs);

// 挑一套**有中文字形**的字体（微软雅黑 / 等线 / 黑体 / 宋体 / Noto Sans CJK / 思源黑体…）。
// 界面上「一层」「一层 ±0.000」这类标签要靠它回落；找不到返回 nullopt。
[[nodiscard]] std::optional<FontCandidate> pick_cjk_font(
    const std::vector<std::filesystem::path>& dirs);

}  // namespace tamias
