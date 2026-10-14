// 放置流程的屏幕空间橡皮筋。
//
// 桌面端靠 DragContext 让命令**实时画出预览几何**（drag.h 的
// set_drag_polyline / preview_points，由 DocumentViewport::DragHost 实现）。web 端
// 的 ViewerHost 没有装 DragContext，embind 也没导出 feed_point，所以拿不到那种
// "跟着鼠标长的实体预览"。
//
// 能做的是**屏幕空间**的等价反馈：用户点过的每个点当初就是从那个屏幕位置反投影到
// 工作平面上的，所以把这些屏幕点连起来，就是这条折线在屏幕上的投影——够用来确认
// "我点了哪几个点、还差几个"。它不画尺寸、不做吸附预览，那是引擎侧的事。
import type { Vec3 } from "../modeling/catalog";

export interface ScreenPoint {
  x: number;
  y: number;
}

interface RubberBandProps {
  /** 已经采到的点（屏幕坐标，相对 stage）。 */
  screens: ScreenPoint[];
  /** 采到的世界点，画在旁边当读数。 */
  world: Vec3[];
  /** 还要几个点（null = 开放式）。 */
  need: number | null;
}

export default function RubberBand({ screens, world, need }: RubberBandProps) {
  if (screens.length === 0) {
    return null;
  }
  const path = screens.map((p) => `${p.x},${p.y}`).join(" ");
  const last = screens[screens.length - 1];
  const lastWorld = world[world.length - 1];

  return (
    <svg className="rubber-band" aria-hidden="true">
      {screens.length > 1 && <polyline className="rb-line" points={path} />}
      {screens.map((p, index) => (
        <circle
          className={index === screens.length - 1 ? "rb-dot rb-dot-last" : "rb-dot"}
          key={`${p.x}:${p.y}:${index}`}
          cx={p.x}
          cy={p.y}
          r={index === screens.length - 1 ? 4 : 3}
        />
      ))}
      <text className="rb-readout" x={last.x + 10} y={last.y - 10}>
        {lastWorld === undefined
          ? ""
          : `${lastWorld.map((v) => v.toFixed(2)).join(", ")}${need === null ? "" : ` · 还差 ${need} 点`}`}
      </text>
    </svg>
  );
}
