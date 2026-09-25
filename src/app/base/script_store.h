#pragma once

#include <QString>
#include <QStringList>

namespace tamias {

// 控制台脚本住在 <AppData>/scripts（Windows 上大致是 %APPDATA%/Tamias/scripts）。
//
// **脚本不进 `.tdoc`**：工作文档是数据，脚本是行为。混进文档会把「工作格式 vs 交换格式」
// 那层分层毁掉——脚本应该是能用 git 管、能拷给同事的文件。
[[nodiscard]] QString scripts_directory();

// 目录里的 *.cs，按文件名排序。目录不存在返回空表（不创建）。
[[nodiscard]] QStringList list_scripts();

// 新建脚本用的候选路径：script1.cs / script2.cs … 只挑名字，不落盘。
[[nodiscard]] QString next_script_path();

// 读写脚本；失败时 error 里是给人看的原因。
[[nodiscard]] bool read_script(const QString& path, QString& text, QString& error);
[[nodiscard]] bool write_script(const QString& path, const QString& text, QString& error);

}  // namespace tamias
