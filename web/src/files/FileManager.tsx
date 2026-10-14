// 文件管理页：登录之后先落在这里，**双击**一份文档才进查看器。
//
// 单文档模型下（一个网页标签页 = 一份文档）这一页就是"首页 / 欢迎页"的位置——桌面端
// 也是没有标签页时回欢迎页、由用户决定开哪份（见 docs/APP.md）。
//
// 三个来源，合成一张表：
//   - 示例：仓库自带的 assets/samples（.trscn / .obj；**仓库里没有 .tdoc 示例**，
//     所以 .tdoc 要从"打开文件/打开文件夹"进来，之后会出现在"最近打开"里）；
//   - 最近打开：本次会话里打开过的（存的是 File 句柄，见 ViewerApp）；
//   - 本地文件夹：Chromium 系可用 File System Access API 挑一个目录，列出里面的文档。
//
// 交互照桌面端：**单击选中、双击打开**，选中后回车也能开。
import { useEffect, useState } from "react";

import Icon from "../ribbon/icons";

export interface FileEntry {
  key: string;
  name: string;
  kind: "tdoc" | "trscn" | "obj";
  /** 表格里的"位置"列：示例是仓库相对路径，本地文件夹是目录名。 */
  where: string;
  /** 字节数（最近打开 / 本地文件夹有；示例没有）。 */
  size?: number;
  /** 打开时间（只有"最近打开"有）。 */
  at?: number;
  source: "示例" | "最近打开" | "本地文件夹";
  open: () => void;
}

interface FileManagerProps {
  entries: FileEntry[];
  /** 正在打开哪一份（null = 空闲）。打开大文件要等一下，得让用户看见。 */
  busy: string | null;
  /** 错误/提示（打开失败时显示）。 */
  error: string | null;
  onOpenLocal: () => void;
  /** 没传 = 这个浏览器不支持 File System Access API，不显示按钮。 */
  onOpenFolder?: () => void;
  onNew: () => void;
}

function format_size(bytes: number | undefined): string {
  if (bytes === undefined) {
    return "—";
  }
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function format_time(at: number | undefined): string {
  if (at === undefined) {
    return "—";
  }
  const d = new Date(at);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getMonth() + 1}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

export default function FileManager({
  entries,
  busy,
  error,
  onOpenLocal,
  onOpenFolder,
  onNew,
}: FileManagerProps) {
  // 选中项。entries 会随"最近打开"变化，所以选中用 key 记，不用下标。
  const [selected, setSelected] = useState<string | null>(null);

  // 选中的那份被移走了（比如列表刷新）就把选中清掉，免得回车开到别的东西上。
  useEffect(() => {
    if (selected !== null && !entries.some((entry) => entry.key === selected)) {
      setSelected(null);
    }
  }, [entries, selected]);

  const openSelected = () => {
    const hit = entries.find((entry) => entry.key === selected);
    if (hit !== undefined && busy === null) {
      hit.open();
    }
  };

  return (
    <div className="fm">
      <div className="fm-bar">
        <div className="fm-title">文件管理</div>
        <div className="fm-actions">
          <button type="button" className="btn" onClick={onNew} disabled={busy !== null}>
            新建文档
          </button>
          <button type="button" className="btn" onClick={onOpenLocal} disabled={busy !== null}>
            打开文件…
          </button>
          {onOpenFolder !== undefined && (
            <button
              type="button"
              className="btn"
              onClick={onOpenFolder}
              disabled={busy !== null}
              title="挑一个本地文件夹，列出里面的 .tdoc / .trscn / .obj（Chromium 系可用）"
            >
              打开文件夹…
            </button>
          )}
        </div>
      </div>

      {busy !== null && (
        <div className="fm-busy">
          <Icon name="open" size={14} />
          正在打开 {busy}…
        </div>
      )}
      {error !== null && <div className="fm-error">{error}</div>}

      {entries.length === 0 ? (
        <div className="fm-empty">
          <div className="fm-empty-title">这里还没有文档</div>
          <p className="fm-empty-note">
            用「打开文件…」挑一份 <code>.tdoc</code> / <code>.trscn</code> / <code>.obj</code>
            ，或者「新建文档」开一份空文档。打开过的会留在列表里，下次双击即可。
          </p>
        </div>
      ) : (
        <div className="fm-table" role="listbox" aria-label="文档列表">
          <div className="fm-head">
            <span className="fm-col-name">名称</span>
            <span className="fm-col-kind">类型</span>
            <span className="fm-col-where">位置</span>
            <span className="fm-col-size">大小</span>
            <span className="fm-col-time">打开时间</span>
            <span className="fm-col-source">来源</span>
          </div>
          {entries.map((entry) => (
            <div
              key={entry.key}
              className={`fm-row${selected === entry.key ? " selected" : ""}`}
              role="option"
              aria-selected={selected === entry.key}
              tabIndex={0}
              title={`双击打开 ${entry.name}`}
              onClick={() => setSelected(entry.key)}
              onDoubleClick={() => {
                if (busy === null) {
                  entry.open();
                }
              }}
              onKeyDown={(event) => {
                if (event.key === "Enter") {
                  event.preventDefault();
                  openSelected();
                }
              }}
            >
              <span className="fm-col-name">
                <Icon name={entry.kind === "obj" ? "box" : "open"} size={16} />
                <span className="fm-name-text">{entry.name}</span>
              </span>
              <span className="fm-col-kind">
                <span className={`fm-kind k-${entry.kind}`}>{entry.kind}</span>
              </span>
              <span className="fm-col-where" title={entry.where}>
                {entry.where}
              </span>
              <span className="fm-col-size">{format_size(entry.size)}</span>
              <span className="fm-col-time">{format_time(entry.at)}</span>
              <span className="fm-col-source">{entry.source}</span>
            </div>
          ))}
        </div>
      )}

      <div className="fm-foot">
        双击一份文档打开（选中后按 Enter 也行）。一个网页标签页就是一份文档；
        浏览器不允许网页自己写磁盘，所以 web 端<strong>还不能保存</strong>——
        要留住改动请在桌面端打开同一份 .tdoc。
      </div>
    </div>
  );
}
