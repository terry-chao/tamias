// 顶栏（菜单行 + 工具带）的规格类型。
//
// 形状照着桌面端来：RibbonBar 加「段」（RibbonPage），段里加「组」（RibbonGroup），
// 组里放命令；菜单和工具带**共用同一批命令对象**——桌面端就是这么做的
// （main_window 里建一次 QAction，菜单项和功能区都引它），改名 / 换图标 / 勾选态只有一份。
import type { IconName } from "./icons";

/** 工具带形态。对应桌面端 ui/ribbon_style 的两个取值。 */
export type RibbonDisplayMode = "text" | "icons";

/** 一条命令。菜单项和工具带按钮都指向它。 */
export interface RibbonCommand {
  id: string;
  label: string;
  icon: IconName;
  /** 悬浮提示。桌面端 tooltip 是「命令名 + 一句说明」。 */
  tip?: string;
  /** 只用于显示，如 "Ctrl+Z"。真正的按键监听仍在 ViewerApp 里。 */
  shortcut?: string;
  /** 出处，画在命令搜索结果右边，形如「开始 · 文件」。 */
  location?: string;
  disabled?: boolean;
  /** 给了就画勾选态（单选的视图模式 / 可勾的面板开关）。 */
  checked?: boolean;
  run: () => void;
}

export interface RibbonGroupSpec {
  id: string;
  title: string;
  commands: RibbonCommand[];
}

/** 段（桌面端的 RibbonPage）。段左侧一条 3px 主色竖线。 */
export interface RibbonSectionSpec {
  id: string;
  title: string;
  /** 取桌面端 section_accent 的暗色档：蓝 / 青绿 / 橙 / 紫，循环。 */
  accent: "blue" | "teal" | "orange" | "purple";
  groups: RibbonGroupSpec[];
}

export type MenuEntry =
  | { kind: "separator" }
  | { kind: "command"; command: RibbonCommand }
  | { kind: "submenu"; title: string; entries: MenuEntry[] };

export interface MenuSpec {
  id: string;
  title: string;
  entries: MenuEntry[];
}
