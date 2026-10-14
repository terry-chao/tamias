// 顶栏的命令表：工具规格（`modeling/catalog.ts`）→ 菜单项 + 工具带按钮。
//
// 菜单项与工具带按钮指向**同一批对象**（桌面端是同一批 QAction），所以名字 / 图标 /
// 勾选态只有一份。
//
// 这里是个**纯函数**：每次渲染都重新调用、不缓存——闭包必须看到当前的 ready /
// renderMode / selectedCount / placing，缓存起来只会拿到过期状态。
//
// 从 ViewerApp 里抽出来的原因：这一段是全文件里**锚点最不好写**的地方（二十多处
// `findCommand("…")`、大量结构几乎相同的菜单项），单独成文件后每处的上下文都短且唯一。
import {
  HOME_GROUP_TITLES,
  HOME_TOOLS,
  VIEW_GROUP_TITLES,
  VIEW_TOOLS,
  type ToolSpec,
} from "../modeling/catalog";
import type {
  MenuEntry,
  MenuSpec,
  RibbonCommand,
  RibbonSectionSpec,
} from "../ribbon/types";
import type { PlacingState } from "./types";

export interface CommandContext {
  /** 引擎还没起来（= `!ready`）。 */
  notReady: boolean;
  /** 当前有没有打开的文档。 */
  hasDoc: boolean;
  /** 视图模式：0 线框 / 1 着色 / 2 真实感。 */
  renderMode: number;
  selectedCount: number;
  /** 引擎日志面板是否展开。 */
  showLog: boolean;
  placing: PlacingState | null;
  user: string;
  /** File System Access API 可用与否（决定「打开文件夹…」出不出现）。 */
  canPickFolder: boolean;
  recent: { name: string; file: File; at: number }[];
  /** 没有对应引擎命令的纯 UI 动作，按 command 名索引。 */
  uiActions: Record<string, () => void>;
  armTool: (spec: ToolSpec) => void | Promise<void>;
  closeDocument: () => void | Promise<void>;
  openFolder: () => void | Promise<void>;
  reopenRecent: (file: File) => void;
  onLogout: () => void;
}

export interface Toolbar {
  /** 两段分区（桌面端 home / view 同时铺开，没有页签）。 */
  sections: RibbonSectionSpec[];
  /** 顶栏「一行菜单」要用的全部命令（含只挂菜单的关闭 / 退出 / 最近打开）。 */
  topCommands: RibbonCommand[];
  menus: MenuSpec[];
}

export function buildToolbar(ctx: CommandContext): Toolbar {
  const { notReady, hasDoc, renderMode, selectedCount, showLog, placing, user } = ctx;

  const specId = (spec: ToolSpec) => spec.id ?? spec.command;

  const isToolActive = (spec: ToolSpec): boolean => {
    const id = specId(spec);
    if (placing !== null && specId(placing.spec) === id) {
      return true;
    }
    switch (spec.command) {
      case "wireframe":
        return renderMode === 0;
      case "shaded":
        return renderMode === 1;
      case "realistic":
        return renderMode === 2;
      case "log-panel":
        return showLog;
      default:
        return false;
    }
  };

  const toCommand = (spec: ToolSpec): RibbonCommand => {
    const action = ctx.uiActions[spec.command];
    const shortcut =
      spec.command === "undo"
        ? "Ctrl+Z"
        : spec.command === "redo"
          ? "Ctrl+Y"
          : spec.command === "frame_all"
            ? "F"
            : undefined;
    // 不依赖引擎就绪的只有"关于 / 退出登录"；不依赖**文档**的是"新建 / 打开 / 关于 /
    // 退出登录"——没有打开的文档时，其余工具全灰掉，而不是点了报错。
    const needsEngine = spec.command !== "about" && spec.command !== "logout";
    const needsDocument =
      spec.command !== "new" &&
      spec.command !== "open" &&
      spec.command !== "about" &&
      spec.command !== "logout";
    return {
      id: specId(spec),
      label:
        spec.command === "delete_entity" && selectedCount > 0
          ? `${spec.label} (${selectedCount})`
          : spec.label,
      icon: spec.icon,
      tip: spec.tip,
      location: spec.location,
      shortcut,
      disabled: (needsEngine && notReady) || (needsDocument && !hasDoc),
      checked: isToolActive(spec),
      run: action ?? (() => void ctx.armTool(spec)),
    };
  };

  const buildSection = (
    id: string,
    title: string,
    accent: RibbonSectionSpec["accent"],
    titles: Record<string, string>,
    tools: Record<string, ToolSpec[]>,
    order: string[],
  ): RibbonSectionSpec => ({
    id,
    title,
    accent,
    groups: order.map((group) => ({
      id: group,
      title: titles[group],
      commands: (tools[group] ?? []).map(toCommand),
    })),
  });

  const sections: RibbonSectionSpec[] = [
    buildSection(
      "home",
      "开始",
      "blue",
      HOME_GROUP_TITLES,
      HOME_TOOLS,
      ["file", "edit", "draw", "architectural", "structural", "modify", "annotate", "navigation", "grid", "storey", "help"],
    ),
    buildSection("view", "视图", "teal", VIEW_GROUP_TITLES, VIEW_TOOLS, ["display", "panels"]),
  ];

  const allCommands: RibbonCommand[] = sections.flatMap((section) =>
    section.groups.flatMap((group) => group.commands),
  );

  // 只挂菜单、不在带上的命令（退出登录）。
  const cLogout: RibbonCommand = {
    id: "logout",
    label: "退出登录",
    icon: "logout",
    tip: `退出 ${user}`,
    location: "文件",
    run: ctx.onLogout,
  };

  // 关闭文档：纯 UI 动作（引擎侧有个 closeDocument 导出，但不算"建模命令"）。
  const cClose: RibbonCommand = {
    id: "close-document",
    label: "关闭文档",
    icon: "close",
    tip: "关掉当前文档回到文件管理页（web 端还不能保存，关掉即丢弃）",
    location: "文件",
    shortcut: "Ctrl+W",
    disabled: !hasDoc,
    run: () => void ctx.closeDocument(),
  };

  // 「打开文件夹…」：Chromium 系的 File System Access API 才有的能力，所以只在菜单里
  // 按需出现（没有这个 API 时整个菜单项都不画）。
  const cOpenFolder: RibbonCommand = {
    id: "open-folder",
    label: "打开文件夹…",
    icon: "open",
    tip: "挑一个本地文件夹，列出里面的 .tdoc / .trscn / .obj",
    location: "文件",
    run: () => void ctx.openFolder(),
  };

  // 最近打开：每条就是一个命令，run 里拿当初那个 File 句柄重读磁盘。
  const recentCommands: RibbonCommand[] = ctx.recent.map((entry) => ({
    id: `recent:${entry.name}`,
    label: entry.name,
    icon: "open",
    tip: `重新打开 ${entry.name}`,
    location: "文件 · 最近打开",
    run: () => ctx.reopenRecent(entry.file),
  }));
  const recentEntries: MenuEntry[] = recentCommands.map((command) => ({
    kind: "command",
    command,
  }));
  const topCommands: RibbonCommand[] = [...allCommands, cClose, cLogout, ...recentCommands];

  const findCommand = (id: string): RibbonCommand => {
    const hit = allCommands.find((command) => command.id === id);
    if (hit !== undefined) {
      return hit;
    }
    throw new Error(`顶栏命令表里没有 "${id}"`);
  };
  const home = (group: string, index = 0) => {
    const section = sections[0].groups.find((g) => g.id === group);
    const command = section?.commands[index];
    if (command === undefined) {
      throw new Error(`开始段的 ${group} 组第 ${index} 个命令不存在`);
    }
    return command;
  };

  // 菜单。桌面端是 文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助；这里**没有「窗口」**——
  // 那一项在桌面端是标签页清单（下一个 / 上一个 / 打开的文档），而 web 端一次只有
  // 一份文档，放一个只有一项的菜单不如不放。等 web 支持多文档时再补。
  const menus: MenuSpec[] = [
    {
      id: "file",
      title: "文件",
      entries: [
        { kind: "command", command: findCommand("new") },
        { kind: "command", command: findCommand("open") },
        // 「打开文件夹…」原来在文件管理页的工具栏上；那一页照桌面端改成纯卡片墙之后，
        // 这个动作挪到文件菜单里（桌面端的打开类动作也都在菜单 + ribbon 上）。
        ...(ctx.canPickFolder
          ? [{ kind: "command" as const, command: cOpenFolder }]
          : []),
        // 最近打开只在本次会话内有内容（File 句柄不可序列化），空的时候不画这一项。
        ...(recentEntries.length > 0
          ? [{ kind: "submenu" as const, title: "最近打开", entries: recentEntries }]
          : []),
        { kind: "separator" },
        { kind: "command", command: cClose },
        { kind: "separator" },
        { kind: "command", command: cLogout },
      ],
    },
    {
      id: "edit",
      title: "编辑",
      entries: [
        { kind: "command", command: findCommand("undo") },
        { kind: "command", command: findCommand("redo") },
        { kind: "separator" },
        { kind: "command", command: findCommand("move_entities") },
        { kind: "command", command: findCommand("copy_entities") },
        { kind: "command", command: findCommand("delete_entity") },
      ],
    },
    {
      id: "view",
      title: "视图",
      entries: [
        {
          kind: "submenu",
          title: "显示模式",
          entries: [
            { kind: "command", command: findCommand("wireframe") },
            { kind: "command", command: findCommand("shaded") },
            { kind: "command", command: findCommand("realistic") },
          ],
        },
        { kind: "separator" },
        { kind: "command", command: findCommand("frame_all") },
        { kind: "command", command: findCommand("log-panel") },
      ],
    },
    {
      id: "draw",
      title: "绘制",
      entries: [
        { kind: "command", command: findCommand("create_line") },
        { kind: "command", command: findCommand("create_polyline") },
        { kind: "command", command: findCommand("create_rectangle") },
        { kind: "command", command: findCommand("create_circle") },
        { kind: "command", command: findCommand("create_arc") },
        { kind: "separator" },
        { kind: "command", command: findCommand("create_wall") },
        { kind: "command", command: findCommand("create_door") },
        { kind: "command", command: findCommand("create_window") },
        { kind: "separator" },
        { kind: "command", command: findCommand("create_beam") },
        { kind: "command", command: findCommand("create_column") },
        { kind: "command", command: findCommand("create_column_circle") },
        { kind: "command", command: findCommand("create_slab") },
        { kind: "command", command: findCommand("create_foundation") },
      ],
    },
    {
      id: "tools",
      title: "工具",
      entries: [
        { kind: "command", command: findCommand("create_text") },
        { kind: "separator" },
        { kind: "command", command: home("grid", 0) },
        { kind: "command", command: home("grid", 1) },
        { kind: "command", command: home("grid", 2) },
        { kind: "separator" },
        { kind: "command", command: home("storey", 0) },
      ],
    },
    {
      id: "help",
      title: "帮助",
      entries: [{ kind: "command", command: findCommand("about") }],
    },
  ];

  return { sections, topCommands, menus };
}
