// 引擎加载中的画面。
//
// 以前这里是一块**空白**：登录之后 tamias_viewer.wasm（装了 OCCT 是 26 MB）才开始下载，
// 再加上实例化，几秒钟什么都没有。现在给确定的进度和动画，并且下载**提前到登录页**
// （见 App.tsx 的预加载），所以多数情况下这里一闪而过。
import { formatMb, type EngineProgress } from "../viewer";

interface EngineLoadingProps {
  /** 预加载进度（拿不到 Content-Length 时 total 为 0 → 退化成不确定进度）。 */
  progress: EngineProgress;
  /** 引擎自己的状态/错误文案（startViewer 失败时会带上原因）。 */
  message: string;
  onRetry: () => void;
}

export default function EngineLoading({ progress, message, onRetry }: EngineLoadingProps) {
  const determinate = progress.total > 0;
  const percent = determinate
    ? Math.min(100, Math.round((progress.loaded / progress.total) * 100))
    : 0;
  const failed = progress.error !== null;

  return (
    <div className="loading">
      <div className="loading-card">
        {failed ? (
          <div className="loading-title">引擎启动失败</div>
        ) : (
          <>
            <div className="spinner" aria-hidden="true" />
            <div className="loading-title">正在启动引擎</div>
          </>
        )}

        {/* 确定进度就画实际宽度；拿不到总长就画一条来回跑的，别假装知道百分比。 */}
        <div
          className={`loading-bar${determinate ? "" : " indeterminate"}`}
          role="progressbar"
          aria-valuemin={0}
          aria-valuemax={determinate ? 100 : undefined}
          aria-valuenow={determinate ? percent : undefined}
        >
          <div className="loading-bar-fill" style={determinate ? { width: `${percent}%` } : undefined} />
        </div>

        <div className="loading-meta">
          {failed
            ? progress.error
            : determinate
              ? `${formatMb(progress.loaded)} / ${formatMb(progress.total)} MB · ${percent}%`
              : "正在下载引擎…"}
        </div>

        {/* 引擎那句状态（"正在启动引擎…" / 渲染循环停在 N 帧之类）也带上，省得只看到进度条。 */}
        {!failed && message !== "" && <div className="loading-note">{message}</div>}

        {failed && (
          <button type="button" className="btn" onClick={onRetry}>
            重试
          </button>
        )}
      </div>
    </div>
  );
}
