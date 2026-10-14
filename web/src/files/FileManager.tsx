// 文件管理页（登录后的落地页 / 当前没有打开的文档）。
//
// **排版照桌面端的欢迎页**（src/app/shell/home_page.cpp）：不是表格，是**缩略图卡片墙**，
// 加一行分区标题（左边粗体标签、右边灰色提示）。
//
// 从桌面端逐条抄来的东西（改之前先看一眼那个文件）：
//   - 页面底色 #1e1f22（比工具带的 #2b2d30 深一档）
//   - 卡片：#2b2d30 底 + #3c3f41 边 + 2px 圆角；悬浮边 #3d8bd4、底 #323437；
//     选中边 #3d8bd4、底 #2a3848、字 #ffffff
//   - 缩略图 200×112 比例，没有真缩略图时用 #2d2f32 底 + 居中 48×48 的 logo
//     （和桌面端 make_thumb_icon() 的兜底一模一样，用同一份 assets/branding/logo.png）
//   - 分区标题 12px 粗体 #cfcfcf；右侧提示 11px #7a7a7a
//   - 时间写法：今天, HH:mm / 昨天, HH:mm / 短日期
//   - **没有工具栏**：新建/打开在顶栏的「文件」组和文件菜单里，这一页只管列文档
//
// web 自己的部分：三个来源（最近打开 / 本地文件夹 / 示例），双击打开、右键「从最近打开移除」。
import { useEffect, useState } from "react";

import Icon from "../ribbon/icons";
import logo from "../../../assets/branding/logo.png";

export interface FileEntry {
  key: string;
  name: string;
  kind: "tdoc" | "trscn" | "obj";
  /** 副标题里跟在类型后面的那半句：示例是仓库位置，本地文件夹是目录名。 */
  where: string;
  /** 字节数（只有最近打开 / 本地文件夹有）。只进 tooltip，不排进卡片。 */
  size?: number;
  /** 打开时间（只有"最近打开"有）。 */
  at?: number;
  source: "示例" | "最近打开" | "本地文件夹";
  /**
   * 能写进 URL 的地址（只有仓库自带的示例有：它们在 assets/ 下有真 URL）。
   * 有它就说明这份文档**可以在新标签页里打开**——浏览器不给本地文件路径，所以
   * 最近打开 / 本地文件夹那两类没有 href，也就没法在新标签页里直接开。
   */
  href?: string;
  open: () => void;
}

interface FileManagerProps {
  entries: FileEntry[];
  /** 正在打开哪一份（null = 空闲）。打开大文件要等一下，得让用户看见。 */
  busy: string | null;
  /** 错误/提示（打开失败时显示）。 */
  error: string | null;
  /** 从「最近打开」里移除一条（右键菜单）。只有最近打开的那些 key 会传进来。 */
  onRemoveRecent: (key: string) => void;
}

/** 桌面端 format_opened_time() 的同款写法。 */
function format_opened_time(at: number | undefined): string {
  if (at === undefined) {
    return "";
  }
  const d = new Date(at);
  const pad = (n: number) => String(n).padStart(2, "0");
  const hhmm = `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  const today = new Date();
  const same_day = (a: Date, b: Date) =>
    a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
  if (same_day(d, today)) {
    return `今天, ${hhmm}`;
  }
  const yesterday = new Date(today);
  yesterday.setDate(today.getDate() - 1);
  if (same_day(d, yesterday)) {
    return `昨天, ${hhmm}`;
  }
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
}

function format_size(bytes: number | undefined): string {
  if (bytes === undefined) {
    return "";
  }
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function subtitle(entry: FileEntry): string {
  const kind = entry.kind.toUpperCase();
  if (entry.source === "最近打开") {
    return `${kind} · ${format_opened_time(entry.at)}`;
  }
  return `${kind} · ${entry.where}`;
}

function tooltip(entry: FileEntry): string {
  const size = format_size(entry.size);
  return size === "" ? entry.name : `${entry.name}（${size}）`;
}

export default function FileManager({
  entries,
  busy,
  error,
  onRemoveRecent,
}: FileManagerProps) {
  // 选中项。entries 会随"最近打开"变化，所以选中用 key 记，不用下标。
  const [selected, setSelected] = useState<string | null>(null);
  // 右键菜单：{ key, x, y }，null = 没开。
  const [menu, setMenu] = useState<{ key: string; x: number; y: number } | null>(null);

  // 选中的那份被移走了（比如从最近打开里删掉）就把选中清掉，免得回车开到别的东西上。
  useEffect(() => {
    if (selected !== null && !entries.some((entry) => entry.key === selected)) {
      setSelected(null);
    }
  }, [entries, selected]);

  // 点别处 / Esc 收起右键菜单。
  useEffect(() => {
    if (menu === null) {
      return;
    }
    const close = () => setMenu(null);
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        close();
      }
    };
    document.addEventListener("pointerdown", close);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("pointerdown", close);
      document.removeEventListener("keydown", onKey);
    };
  }, [menu]);

  const openSelected = () => {
    const hit = entries.find((entry) => entry.key === selected);
    if (hit !== undefined && busy === null) {
      hit.open();
    }
  };

  // 分区。空的来源不画那一节（桌面端也是：没有最近文件就直接给空面板）。
  const groups: { id: string; title: string; hint: string; items: FileEntry[] }[] = [
    {
      id: "recent",
      title: "最近打开",
      hint: "双击打开 · 右键可移除",
      items: entries.filter((entry) => entry.source === "最近打开"),
    },
    {
      id: "folder",
      title: "本地文件夹",
      hint: "双击打开",
      items: entries.filter((entry) => entry.source === "本地文件夹"),
    },
    {
      id: "sample",
      title: "示例文档",
      hint: "仓库自带 · 双击打开",
      items: entries.filter((entry) => entry.source === "示例"),
    },
  ].filter((group) => group.items.length > 0);

  const menuEntry = menu === null ? undefined : entries.find((entry) => entry.key === menu.key);

  return (
    <div className="hm">
      {busy !== null && (
        <div className="hm-status hm-busy">
          <Icon name="open" size={14} />
          正在打开 {busy}…
        </div>
      )}
      {error !== null && <div className="hm-status hm-error">{error}</div>}

      {groups.length === 0 ? (
        // 空状态照桌面端：标题 + 一句"用上面的打开…"（这一页没有工具栏，动作在顶上）。
        <div className="hm-empty">
          <div className="hm-empty-title">还没有文档</div>
          <div className="hm-empty-note">
            用上面「文件」组里的<strong>打开</strong>挑一份 <code>.tdoc</code> /{" "}
            <code>.trscn</code> / <code>.obj</code>，<strong>新建</strong>开一份空文档；
            也可以在「文件」菜单里<strong>打开文件夹</strong>，把某个目录下的文档列到这里。
          </div>
        </div>
      ) : (
        groups.map((group) => (
          <section className="hm-section" key={group.id}>
            <div className="hm-section-row">
              <span className="hm-section-title">{group.title}</span>
              <span className="hm-section-hint">{group.hint}</span>
            </div>
            <div className="hm-gallery" role="listbox" aria-label={group.title}>
              {group.items.map((entry) => {
                const cls = `hm-card${selected === entry.key ? " selected" : ""}`;
                const sel = selected === entry.key;
                const body = (
                  <>
                    {/* 缩略图位置：web 没有真缩略图，用桌面端同款兜底——#2d2f32 底 + 居中 logo。 */}
                    <span className="hm-thumb">
                      <img src={logo} alt="" draggable={false} />
                    </span>
                    <span className="hm-name">{entry.name}</span>
                    <span className="hm-sub">{subtitle(entry)}</span>
                  </>
                );
                const handlers = {
                  onDoubleClick: () => {
                    if (busy === null) {
                      entry.open();
                    }
                  },
                  onKeyDown: (event: React.KeyboardEvent<HTMLElement>) => {
                    if (event.key === "Enter") {
                      event.preventDefault();
                      openSelected();
                    }
                  },
                  onContextMenu: (event: React.MouseEvent<HTMLElement>) => {
                    // 示例有「在新标签页打开」，最近打开有「移除」。两样都没有就交回浏览器，
                    // 别弹一个空菜单。
                    if (entry.source !== "最近打开" && entry.href === undefined) {
                      return;
                    }
                    event.preventDefault();
                    setSelected(entry.key);
                    setMenu({ key: entry.key, x: event.clientX, y: event.clientY });
                  },
                };
                // 有 URL 的（示例）渲染成**真链接**：这样 Ctrl/⌘/Shift+点击、中键、
                // 以及浏览器自带的右键「在新标签页打开」全都天然可用，不用我教。
                // 本地文件没有 URL（浏览器不给路径），只能是 div。
                return entry.href !== undefined ? (
                  <a
                    key={entry.key}
                    className={cls}
                    role="option"
                    aria-selected={sel}
                    tabIndex={0}
                    href={entry.href}
                    title={tooltip(entry)}
                    onClick={(event) => {
                      // 带修饰键 = 用户要开新标签页，别拦，让浏览器按 href 去开。
                      if (!event.ctrlKey && !event.metaKey && !event.shiftKey) {
                        event.preventDefault();
                      }
                      setSelected(entry.key);
                    }}
                    {...handlers}
                  >
                    {body}
                  </a>
                ) : (
                  <div
                    key={entry.key}
                    className={cls}
                    role="option"
                    aria-selected={sel}
                    tabIndex={0}
                    title={tooltip(entry)}
                    onClick={() => setSelected(entry.key)}
                    {...handlers}
                  >
                    {body}
                  </div>
                );
              })}
            </div>
          </section>
        ))
      )}

      {menu !== null && menuEntry !== undefined && (
        <div
          className="hm-menu"
          style={{ left: menu.x, top: menu.y }}
          onPointerDown={(event) => event.stopPropagation()}
        >
          <button
            type="button"
            className="hm-menu-item"
            onClick={() => {
              setMenu(null);
              menuEntry.open();
            }}
          >
            打开
          </button>
          {/* 只有有 URL 的那类（示例）能这么开。本地文件浏览器不给路径，没有地址可开。 */}
          {menuEntry.href !== undefined && (
            <button
              type="button"
              className="hm-menu-item"
              onClick={() => {
                setMenu(null);
                window.open(menuEntry.href, "_blank", "noopener");
              }}
            >
              在新标签页打开
            </button>
          )}
          <button
            type="button"
            className="hm-menu-item"
            onClick={() => {
              setMenu(null);
              onRemoveRecent(menu.key);
            }}
          >
            从最近打开移除
          </button>
        </div>
      )}
    </div>
  );
}
