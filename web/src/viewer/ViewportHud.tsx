// 视口左下角的常显读数 + 引擎日志面板。
//
// 两者都是**盖在 stage 上的浮层**，所以这个组件返回 fragment，由 ViewerApp 直接放进
// `.stage` 里。纯展示：数据全由 props 给。
import type { ViewerStats } from "../viewer";

interface ViewportHudProps {
  stats: ViewerStats;
  /** 空串 = 还没 startViewer，读数显示成 "—"。 */
  backendName: string;
  /** 空串 = 还没读到内核。 */
  kernel: string;
  selectedCount: number;
  logText: string;
  /** 「视图 → 面板 → 引擎日志」的开关。 */
  showLog: boolean;
}

export default function ViewportHud({
  stats,
  backendName,
  kernel,
  selectedCount,
  logText,
  showLog,
}: ViewportHudProps) {
  return (
    <>
      {/* 与桌面视口左下角同一组读数 */}
      <div className="stats" title="与桌面视口左下角同一组读数">
        <span>draw {stats.draws}</span>
        <span>tri {stats.triangles}</span>
        <span>gpu {stats.gpuMeshMb.toFixed(1)}MB</span>
        <span>backend {backendName === "" ? "—" : backendName}</span>
        <span>kernel {kernel === "" ? "—" : kernel}</span>
        <span>tess {stats.pendingTessellate}</span>
        <span>选中 {selectedCount}</span>
      </div>
      {/* 引擎报错：原来只能翻 DevTools，现在直接显示在页面上。
          「视图 → 面板 → 引擎日志」可以关掉它（默认开）。 */}
      {showLog && logText.trim() !== "" && (
        <div className="log-panel" title="引擎的 warn / error 日志">
          <div className="log-title">引擎日志</div>
          <pre>{logText.trim()}</pre>
        </div>
      )}
    </>
  );
}
