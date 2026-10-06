#pragma once

namespace tac {

// 登记自研后端 "tac"，让 设置 → Interface → Toolkit → UI backend 能列出并选中它。
//
// 自研控件 / 绘制实现还没落地（docs/TAC.md 阶段 5），所以这里先给一个骨架后端：
// 平台服务是"明确不支持"的空实现，create_window / create_surface 返回 nullptr。
// 壳（src/app）完全迁到 tac 契约之前，选中它只是把选择落定并记日志，界面仍是 Qt
// （见 docs/TAC.md §6）。阶段 5 把自研实现长进 src/ui/tac 后，替换这里的工厂即可。
void register_native_backend();

}  // namespace tac
