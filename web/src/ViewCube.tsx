// 右上角的朝向立方体：与桌面 `view_cube_widget` 同一套数学
// （Front=+Z, Right=+X, Top=+Y；eye_dir = (cos p·sin y, sin p, cos p·cos y)）。
//
// 点一个面 → 相机转到该标准视图；点**角** → 转到那个方向的等轴测斜视角；在立方体上拖
// → 自由转相机。面与角的分工、几何、剔除阈值、命中半径、配色都对着桌面
// `view_cube_widget.cpp` 抄（桌面只有面和 8 个角，**没有边**）。
// 用 2D canvas 做正交投影，不依赖 WebGPU，所以没显卡时也能显示。
import { useCallback, useEffect, useRef } from "react";

// 顶点（8 个角）与几何常量在 viewer/viewCorners.ts——那张表是纯数据，单独放便于单测。
import {
  CORNERS,
  HALF,
  VERTEX_HIT_RADIUS,
  type Corner,
  type V3,
} from "./viewer/viewCorners";

// 投影的比例与中心。**画和命中测试必须用同一套**，所以只留一份，别再各写各的。
const VIEW_SCALE = 0.52;
const HALF_PI = Math.PI / 2;

export interface ViewAngles {
  yaw: number;
  pitch: number;
}

interface Face {
  id: string;
  label: string;
  color: string;
  corners: [V3, V3, V3, V3];
  /** 点这个面之后相机应该转到的角度（与桌面 on_view_cube_face 一致）。 */
  yaw: number;
  pitch: number;
}

const FACES: Face[] = [
  {
    id: "front",
    label: "前",
    color: "#50a06e",
    yaw: 0,
    pitch: 0,
    corners: [
      [-HALF, -HALF, HALF],
      [HALF, -HALF, HALF],
      [HALF, HALF, HALF],
      [-HALF, HALF, HALF],
    ],
  },
  {
    id: "back",
    label: "后",
    color: "#468c64",
    yaw: Math.PI,
    pitch: 0,
    corners: [
      [HALF, -HALF, -HALF],
      [-HALF, -HALF, -HALF],
      [-HALF, HALF, -HALF],
      [HALF, HALF, -HALF],
    ],
  },
  {
    id: "left",
    label: "左",
    color: "#b46455",
    yaw: -HALF_PI,
    pitch: 0,
    corners: [
      [-HALF, -HALF, -HALF],
      [-HALF, -HALF, HALF],
      [-HALF, HALF, HALF],
      [-HALF, HALF, -HALF],
    ],
  },
  {
    id: "right",
    label: "右",
    color: "#c86e5a",
    yaw: HALF_PI,
    pitch: 0,
    corners: [
      [HALF, -HALF, HALF],
      [HALF, -HALF, -HALF],
      [HALF, HALF, -HALF],
      [HALF, HALF, HALF],
    ],
  },
  {
    id: "top",
    label: "顶",
    color: "#4682c8",
    yaw: 0,
    pitch: HALF_PI,
    corners: [
      [-HALF, HALF, HALF],
      [HALF, HALF, HALF],
      [HALF, HALF, -HALF],
      [-HALF, HALF, -HALF],
    ],
  },
  {
    id: "bottom",
    label: "底",
    color: "#5a6478",
    yaw: 0,
    pitch: -HALF_PI,
    corners: [
      [-HALF, -HALF, -HALF],
      [HALF, -HALF, -HALF],
      [HALF, -HALF, HALF],
      [-HALF, -HALF, HALF],
    ],
  },
];

interface Basis {
  right: V3;
  up: V3;
  forward: V3;
}

function cameraBasis(yaw: number, pitch: number): Basis {
  const cp = Math.cos(pitch);
  const sp = Math.sin(pitch);
  const cy = Math.cos(yaw);
  const sy = Math.sin(yaw);
  const forward: V3 = [-cp * sy, -sp, -cp * cy]; // eye_dir 取反
  // right = normalize(cross(forward, world_up = (0,1,0))) = normalize((-fz, 0, fx))
  let right: V3 = [-forward[2], 0, forward[0]];
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right = [right[0] / rl, right[1] / rl, right[2] / rl];
  const up: V3 = [
    right[1] * forward[2] - right[2] * forward[1],
    right[2] * forward[0] - right[0] * forward[2],
    right[0] * forward[1] - right[1] * forward[0],
  ];
  return { right, up, forward };
}

const dot = (a: V3, b: V3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];

interface Projected {
  x: number;
  y: number;
  depth: number;
}

function projectPoint(p: V3, basis: Basis, cx: number, cy: number, scale: number): Projected {
  const view: V3 = [dot(p, basis.right), dot(p, basis.up), -dot(p, basis.forward)];
  // depth 用「沿视线方向的位移」：**越大越远**。draw 里按降序先画远的、
  // hit 里按升序先测近的，两边都依赖这个方向。
  return { x: cx + view[0] * scale, y: cy - view[1] * scale, depth: dot(p, basis.forward) };
}

/** 点在凸多边形内。面是四边形、角是三角形，同一套判据（叉积是否同号）。 */
function pointInPolygon(px: number, py: number, pts: Projected[]): boolean {
  let hasNeg = false;
  let hasPos = false;
  for (let i = 0; i < pts.length; i += 1) {
    const a = pts[i];
    const b = pts[(i + 1) % pts.length];
    const cross = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
    if (cross < 0) hasNeg = true;
    else if (cross > 0) hasPos = true;
    if (hasNeg && hasPos) return false;
  }
  return true;
}

interface FaceDraw {
  face: Face;
  pts: Projected[];
  depth: number;
}

/** 投影参数（基 + 中心 + 比例）。画和命中测试都从这里取，保证两边一致。 */
function projection(yaw: number, pitch: number, size: number) {
  return {
    basis: cameraBasis(yaw, pitch),
    cx: size * 0.5,
    cy: size * 0.5,
    scale: size * VIEW_SCALE,
  };
}

function layoutFaces(yaw: number, pitch: number, size: number): FaceDraw[] {
  const { basis, cx, cy, scale } = projection(yaw, pitch, size);
  const out: FaceDraw[] = [];
  for (const face of FACES) {
    const pts = face.corners.map((c) => projectPoint(c, basis, cx, cy, scale));
    const depth = pts.reduce((sum, p) => sum + p.depth, 0) / 4;
    out.push({ face, pts, depth });
  }
  return out;
}

interface CornerDraw {
  corner: Corner;
  pts: Projected[];
  depth: number;
}

/**
 * 朝向相机的角（背面剔除阈值与桌面同为 0.08）。它们画在面**之上**——
 * 桌面那句注释说得很直白：留在最上层才看得见、点得到。
 */
function layoutCorners(yaw: number, pitch: number, size: number): CornerDraw[] {
  const { basis, cx, cy, scale } = projection(yaw, pitch, size);
  const eye: V3 = [-basis.forward[0], -basis.forward[1], -basis.forward[2]];
  const out: CornerDraw[] = [];
  for (const corner of CORNERS) {
    if (dot(corner.normal, eye) < 0.08) {
      continue;
    }
    const pts = corner.pts.map((c) => projectPoint(c, basis, cx, cy, scale));
    out.push({ corner, pts, depth: pts.reduce((sum, p) => sum + p.depth, 0) / 3 });
  }
  return out;
}

function draw(canvas: HTMLCanvasElement, yaw: number, pitch: number, hoverId: string | null) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const dpr = window.devicePixelRatio || 1;
  const size = Math.max(48, Math.round(canvas.clientWidth || 96));
  const px = Math.round(size * dpr);
  if (canvas.width !== px || canvas.height !== px) {
    canvas.width = px;
    canvas.height = px;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, size, size);

  const faces = layoutFaces(yaw, pitch, size).sort((a, b) => b.depth - a.depth);
  ctx.font = `600 ${Math.round(size * 0.19)}px system-ui, sans-serif`;
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  for (const { face, pts } of faces) {
    ctx.beginPath();
    ctx.moveTo(pts[0].x, pts[0].y);
    for (let i = 1; i < 4; i += 1) ctx.lineTo(pts[i].x, pts[i].y);
    ctx.closePath();
    ctx.fillStyle = face.color;
    ctx.globalAlpha = hoverId === face.id ? 1 : 0.92;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.strokeStyle = "rgba(20, 22, 26, 0.85)";
    ctx.lineWidth = 1;
    ctx.stroke();

    const cxp = (pts[0].x + pts[1].x + pts[2].x + pts[3].x) / 4;
    const cyp = (pts[0].y + pts[1].y + pts[2].y + pts[3].y) / 4;
    ctx.fillStyle = "rgba(12, 14, 18, 0.92)";
    ctx.fillText(face.label, cxp, cyp);
  }

  // 角上的三角面画在面**之上**（顺序照桌面那句注释：留在最上层才看得见、点得到）。
  // 颜色也照抄：常态 rgba(200,200,210,0.82)、悬浮 #e6e6eb。
  const corners = layoutCorners(yaw, pitch, size).sort((a, b) => b.depth - a.depth);
  for (const { corner, pts } of corners) {
    const hovered = hoverId === corner.id;
    ctx.beginPath();
    ctx.moveTo(pts[0].x, pts[0].y);
    for (let i = 1; i < 3; i += 1) ctx.lineTo(pts[i].x, pts[i].y);
    ctx.closePath();
    ctx.fillStyle = hovered ? "#e6e6eb" : "rgba(200, 200, 210, 0.82)";
    ctx.fill();

    // **太扁的三角面不描边。** 贴着视线看的角（背面剔除阈值 0.08 附近那一段）投影出来是
    // 一条几像素宽的细条：扫过所有视角，被画出来的角三角里有 3.6% 面积不到 12px²，最扁的
    // 只有 8px²。这种形状上再描一圈 1px 深色边，**描边比填充本身还宽**，于是渲染成一条
    // 突兀的深色线——这就是"三角有时候渲染不正确"。
    //
    // 判据用**最小高**而不是面积：面积随画布尺寸变，高不随。最小高 = 2·面积 / 最长边；
    // 描边半宽 0.5px 两侧合计 1px，留点余量取 2px。
    const area = Math.abs(
      (pts[1].x - pts[0].x) * (pts[2].y - pts[0].y) -
        (pts[2].x - pts[0].x) * (pts[1].y - pts[0].y),
    ) / 2;
    const longest = Math.max(
      Math.hypot(pts[1].x - pts[0].x, pts[1].y - pts[0].y),
      Math.hypot(pts[2].x - pts[1].x, pts[2].y - pts[1].y),
      Math.hypot(pts[0].x - pts[2].x, pts[0].y - pts[2].y),
    );
    const minAltitude = longest > 0 ? (2 * area) / longest : 0;
    if (minAltitude < 2) {
      continue;
    }
    ctx.strokeStyle = hovered ? "rgba(40, 42, 48, 0.86)" : "rgba(40, 42, 48, 0.63)";
    ctx.lineWidth = hovered ? 1.4 : 1;
    ctx.stroke();
  }
}

export default function ViewCube({
  yaw,
  pitch,
  onPick,
  onOrbit,
}: {
  yaw: number;
  pitch: number;
  onPick: (angles: ViewAngles) => void;
  onOrbit: (angles: ViewAngles) => void;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const hoverRef = useRef<string | null>(null);
  const dragRef = useRef<{ x: number; y: number; moved: boolean } | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (canvas) draw(canvas, yaw, pitch, hoverRef.current);
  }, [yaw, pitch]);

  /**
   * 命中测试：**先角、后面**——与桌面 mousePressEvent 同序（角画在面之上，也更容易
   * 被误判成面）。角命中要求「在三角面内」或「离顶点 VERTEX_HIT_RADIUS 像素内」，
   * 后者是为了让被压扁的角还能点到（桌面 hit_corner 里那个 10px 同理）。
   *
   * 画与命中用的是同一套坐标与投影参数（内容框 clientWidth、减边框、projection()），
   * 否则点中的位置会和看到的差一截。
   */
  const hit = useCallback(
    (clientX: number, clientY: number): { id: string; yaw: number; pitch: number } | null => {
      const canvas = canvasRef.current;
      if (canvas === null) {
        return null;
      }
      const rect = canvas.getBoundingClientRect();
      const size = canvas.clientWidth;
      const px = clientX - rect.left - canvas.clientLeft;
      const py = clientY - rect.top - canvas.clientTop;
      const { basis, cx, cy, scale } = projection(yaw, pitch, size);
      const eye: V3 = [-basis.forward[0], -basis.forward[1], -basis.forward[2]];

      let best: { id: string; yaw: number; pitch: number; depth: number } | null = null;
      for (const corner of CORNERS) {
        if (dot(corner.normal, eye) < 0.08) {
          continue;
        }
        const pts = corner.pts.map((p) => projectPoint(p, basis, cx, cy, scale));
        const vertex = projectPoint(corner.vertex, basis, cx, cy, scale);
        const nearVertex = Math.hypot(px - vertex.x, py - vertex.y) <= VERTEX_HIT_RADIUS;
        if (!pointInPolygon(px, py, pts) && !nearVertex) {
          continue;
        }
        const depth = (pts[0].depth + pts[1].depth + pts[2].depth) / 3;
        if (best === null || depth < best.depth) {
          best = { id: corner.id, yaw: corner.yaw, pitch: corner.pitch, depth };
        }
      }
      if (best !== null) {
        return best;
      }

      let bestFace: { id: string; yaw: number; pitch: number; depth: number } | null = null;
      for (const { face, pts, depth } of layoutFaces(yaw, pitch, size)) {
        if (pointInPolygon(px, py, pts) && (bestFace === null || depth < bestFace.depth)) {
          bestFace = { id: face.id, yaw: face.yaw, pitch: face.pitch, depth };
        }
      }
      return bestFace;
    },
    [yaw, pitch],
  );
  const onPointerDown = (event: React.PointerEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    event.currentTarget.setPointerCapture(event.pointerId);
    dragRef.current = { x: event.clientX, y: event.clientY, moved: false };
  };

  const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag) {
      // 悬停高亮（角优先——和点击同一个判定）
      const id = hit(event.clientX, event.clientY)?.id ?? null;
      if (id !== hoverRef.current) {
        hoverRef.current = id;
        const canvas = canvasRef.current;
        if (canvas) draw(canvas, yaw, pitch, id);
      }
      return;
    }
    const dx = event.clientX - drag.x;
    const dy = event.clientY - drag.y;
    if (Math.hypot(dx, dy) > 3) drag.moved = true;
    if (!drag.moved) return;
    drag.x = event.clientX;
    drag.y = event.clientY;
    // 与桌面 view_cube_widget 的拖拽灵敏度同量级（0.01 rad/px）。
    onOrbit({ yaw: yaw - dx * 0.01, pitch: pitch + dy * 0.01 });
  };

  const onPointerUp = (event: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    dragRef.current = null;
    if (!drag || drag.moved) return;
    // 没拖动 = 点击：面转到该标准视图，角转到等轴测那个斜视角。
    const target = hit(event.clientX, event.clientY);
    if (target !== null) {
      onPick({ yaw: target.yaw, pitch: target.pitch });
    }
  };

  return (
    <canvas
      ref={canvasRef}
      className="view-cube"
      title="点面或角转到标准视图，拖动自由转相机"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerUp}
      onPointerCancel={onPointerUp}
      onPointerLeave={() => {
        if (hoverRef.current !== null) {
          hoverRef.current = null;
          const canvas = canvasRef.current;
          if (canvas) draw(canvas, yaw, pitch, null);
        }
      }}
    />
  );
}
