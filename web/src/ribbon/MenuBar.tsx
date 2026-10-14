// 菜单行：桌面端顶上那唯一的一行。
//
// 对着 src/app/shell/main_menu.cpp 的排法：文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助；
// 右端是命令搜索框 + 功能区样式按钮 + 卷起按钮（桌面端挂在 QMenuBar 的角部 widget 上，
// 不另占高度，见 docs/APP.md「顶栏」一节）。品牌 logo 不摆在这里——桌面端也只在欢迎页、
// 关于框和窗口图标上用。
//
// 菜单项与工具带按钮指向同一批 RibbonCommand（桌面端是同一批 QAction）。
import { useEffect, useMemo, useRef, useState } from "react";

import Icon from "./icons";
import type { MenuEntry, MenuSpec, RibbonCommand, RibbonDisplayMode } from "./types";

interface MenuBarProps {
  menus: MenuSpec[];
  /** 扁平命令表，命令搜索在它上面过滤（含只挂菜单、不在带上的命令）。 */
  commands: RibbonCommand[];
  displayMode: RibbonDisplayMode;
  onDisplayMode: (mode: RibbonDisplayMode) => void;
  collapsed: boolean;
  onCollapsed: (collapsed: boolean) => void;
  /** 右端角落：后端角标 / 当前登录（web 自己的东西）。 */
  corner?: React.ReactNode;
}

/** 命令搜索结果上限。桌面端那张表也是取最相关的若干条。 */
const kMaxHits = 12;

export default function MenuBar({
  menus,
  commands,
  displayMode,
  onDisplayMode,
  collapsed,
  onCollapsed,
  corner,
}: MenuBarProps) {
  const rootRef = useRef<HTMLDivElement>(null);
  const searchInputRef = useRef<HTMLInputElement>(null);

  const [openMenu, setOpenMenu] = useState<string | null>(null);
  const [styleOpen, setStyleOpen] = useState(false);
  const [query, setQuery] = useState("");
  const [searchOpen, setSearchOpen] = useState(false);

  const hits = useMemo(() => {
    const needle = query.trim().toLowerCase();
    if (needle === "") {
      return [];
    }
    // 名字、出处、悬浮提示、快捷键都算命中——「视图 网格」「Ctrl+S」也找得到。
    return commands
      .filter((command) => {
        const haystack = [command.label, command.location ?? "", command.tip ?? "", command.shortcut ?? ""]
          .join(" ")
          .toLowerCase();
        return haystack.includes(needle);
      })
      .slice(0, kMaxHits);
  }, [commands, query]);

  // 点别处 / Esc 收起来。菜单和搜索框共用一个「外面」。
  useEffect(() => {
    const onPointerDown = (event: PointerEvent) => {
      if (rootRef.current !== null && !rootRef.current.contains(event.target as Node)) {
        setOpenMenu(null);
        setStyleOpen(false);
        setSearchOpen(false);
      }
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setOpenMenu(null);
        setStyleOpen(false);
        setSearchOpen(false);
      }
    };
    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, []);

  // Ctrl+F 直接跳进搜索框（桌面端同款）。已有内容全选，接着打字就是新搜索。
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && (event.key === "f" || event.key === "F")) {
        event.preventDefault();
        const input = searchInputRef.current;
        if (input !== null) {
          input.focus();
          input.select();
          setSearchOpen(true);
        }
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  const invoke = (command: RibbonCommand) => {
    if (command.disabled === true) {
      return;
    }
    setOpenMenu(null);
    setStyleOpen(false);
    setSearchOpen(false);
    // 命令可能返回 Promise（embind / Asyncify 下很多导出都是）；这里吞掉异常，
    // 真正失败会走 ViewerApp 里的 error 状态与底部状态栏。
    void Promise.resolve(command.run()).catch(() => {});
  };

  const renderEntries = (entries: MenuEntry[]) =>
    entries.map((entry, index) => {
      if (entry.kind === "separator") {
        // 分隔线不需要 key 的语义，用下标即可（表是静态的）。
        return <div className="tb-menu-sep" key={`sep-${index}`} />;
      }
      if (entry.kind === "submenu") {
        return (
          <div className="tb-menu-sub" key={entry.title}>
            <button type="button" className="tb-menu-item">
              <span className="tb-menu-check" />
              <span className="tb-menu-label">{entry.title}</span>
              <span className="tb-menu-arrow">▸</span>
            </button>
            <div className="tb-menu-popup">{renderEntries(entry.entries)}</div>
          </div>
        );
      }
      const { command } = entry;
      return (
        <button
          type="button"
          key={command.id}
          className="tb-menu-item"
          disabled={command.disabled === true}
          title={command.tip ?? command.label}
          onClick={() => invoke(command)}
        >
          <span className="tb-menu-check">
            {command.checked === true && <Icon name="check" size={14} />}
          </span>
          <span className="tb-menu-label">{command.label}</span>
          {command.shortcut !== undefined && (
            <span className="tb-menu-shortcut">{command.shortcut}</span>
          )}
        </button>
      );
    });

  return (
    <div className="tb-menu-row" ref={rootRef}>
      <div className="tb-menu-titles">
          {menus.map((menu) => (
            <div className="tb-menu" key={menu.id}>
              <button
                type="button"
                className={`tb-menu-title${openMenu === menu.id ? " open" : ""}`}
                aria-haspopup="menu"
                aria-expanded={openMenu === menu.id}
                onClick={() => setOpenMenu((current) => (current === menu.id ? null : menu.id))}
                // 已经开着一个菜单时，滑过另一个就跟着切——桌面端菜单栏就是这个手感。
                onPointerEnter={() => setOpenMenu((current) => (current === null ? null : menu.id))}
              >
                {menu.title}
              </button>
              {openMenu === menu.id && (
                <div className="tb-menu-popup">{renderEntries(menu.entries)}</div>
              )}
            </div>
          ))}
        </div>

        <div className="tb-corner">
          {corner}

          <div className="tb-search">
            <div className="tb-search-box">
              <Icon name="search" size={13} />
              <input
                ref={searchInputRef}
                type="text"
                value={query}
                placeholder="搜索命令"
                title="搜索命令（Ctrl+F）"
                spellCheck={false}
                onChange={(event) => {
                  setQuery(event.target.value);
                  setSearchOpen(true);
                }}
                onFocus={() => setSearchOpen(true)}
                onKeyDown={(event) => {
                  if (event.key !== "Enter") {
                    return;
                  }
                  // 回车跑第一条能用的命中，和桌面端那张结果表一致。
                  const first = hits.find((hit) => hit.disabled !== true);
                  if (first !== undefined) {
                    invoke(first);
                  }
                }}
              />
            </div>
            {searchOpen && query.trim() !== "" && (
              <div className="tb-search-popup">
                {hits.length === 0 && <div className="tb-search-empty">没有匹配的命令</div>}
                {hits.map((hit) => (
                  <button
                    type="button"
                    key={hit.id}
                    className="tb-search-hit"
                    disabled={hit.disabled === true}
                    title={hit.tip ?? hit.label}
                    onClick={() => invoke(hit)}
                  >
                    <Icon name={hit.icon} size={16} />
                    <span>{hit.label}</span>
                    <span className="where">
                      {hit.location ?? ""}
                      {hit.shortcut !== undefined ? `  ${hit.shortcut}` : ""}
                    </span>
                  </button>
                ))}
              </div>
            )}
          </div>

          {/* 样式按钮：图标 + 文字 / 仅图标（桌面端 ui/ribbon_style）。 */}
          <div className="tb-menu">
            <button
              type="button"
              className="tb-corner-btn"
              title="功能区样式"
              aria-haspopup="menu"
              aria-expanded={styleOpen}
              onClick={() => setStyleOpen((open) => !open)}
            >
              <Icon name="ribbon_style" size={16} />
            </button>
            {styleOpen && (
              <div className="tb-menu-popup" style={{ left: "auto", right: 0 }}>
                <button
                  type="button"
                  className="tb-menu-item"
                  onClick={() => {
                    onDisplayMode("text");
                    setStyleOpen(false);
                  }}
                >
                  <span className="tb-menu-check">
                    {displayMode === "text" && <Icon name="check" size={14} />}
                  </span>
                  <span className="tb-menu-label">图标 + 文字</span>
                </button>
                <button
                  type="button"
                  className="tb-menu-item"
                  onClick={() => {
                    onDisplayMode("icons");
                    setStyleOpen(false);
                  }}
                >
                  <span className="tb-menu-check">
                    {displayMode === "icons" && <Icon name="check" size={14} />}
                  </span>
                  <span className="tb-menu-label">仅图标</span>
                </button>
              </div>
            )}
          </div>

          {/* 卷起 / 展开：只留菜单那一行。 */}
          <button
            type="button"
            className="tb-corner-btn"
            title={collapsed ? "展开功能区" : "卷起功能区（双击菜单行空白处也行）"}
            onClick={() => onCollapsed(!collapsed)}
          >
            <Icon name={collapsed ? "expand" : "collapse"} size={16} />
          </button>
      </div>
    </div>
  );
}
