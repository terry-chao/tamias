// 「关于」框（桌面端有完整关于框，这里是最小版：后端 / 内核 / 读数 / 当前登录）。
//
// 纯展示：打开状态与数据都由 ViewerApp 给，这里不发请求也不读全局。
import type { ViewerStats } from "../viewer";

interface AboutBoxProps {
  /** 实际编进来的浏览器后端（"WebGL2" / "WebGPU"）与它是不是实验特性。 */
  backend: { name: string; experimental: boolean };
  /** **实际生效**的造型内核（"occt" / "truck" / "none"）。 */
  kernel: string;
  stats: ViewerStats;
  /** 当前登录名。 */
  user: string;
  onClose: () => void;
}

export default function AboutBox({ backend, kernel, stats, user, onClose }: AboutBoxProps) {
  return (
    <div className="about-backdrop" onClick={onClose}>
      <div
        className="about-card"
        role="dialog"
        aria-label="关于 Tamias Viewer"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="about-title">关于 Tamias Viewer</div>
        <dl className="about-rows">
          <dt>图形后端</dt>
          <dd>
            {backend.name === "" ? "—" : backend.name}
            {backend.experimental ? "（实验）" : ""}
          </dd>
          <dt>造型内核</dt>
          <dd>
            {kernel === "" ? "—" : kernel}
            {kernel === "truck" ? "（回退：这次构建没编进 OCCT）" : ""}
          </dd>
          <dt>渲染读数</dt>
          <dd>
            draw {stats.draws} · tri {stats.triangles} · gpu {stats.gpuMeshMb.toFixed(1)}MB
          </dd>
          <dt>当前登录</dt>
          <dd>{user}</dd>
        </dl>
        <button type="button" className="btn" onClick={onClose}>
          关闭
        </button>
      </div>
    </div>
  );
}
