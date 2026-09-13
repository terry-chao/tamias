// 右上角的朝向立方体：与桌面 `view_cube_widget` 同一套数学
// （Front=+Z, Right=+X, Top=+Y；eye_dir = (cos p·sin y, sin p, cos p·cos y)）。
//
// 点一个面 → 相机转到该标准视图；在立方体上拖 → 自由转相机。
// 用 2D canvas 做正交投影，不依赖 WebGPU，所以没显卡时也能显示。
import { useCallback, useEffect, useRef } from "react";

const HALF = 0.55;
const HALF_PI = Math.PI / 2;

type V3 = [number, number, number];

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
  // hitFace 里按升序先测近的，两边都依赖这个方向。
  return { x: cx + view[0] * scale, y: cy - view[1] * scale, depth: dot(p, basis.forward) };
}

function pointInQuad(px: number, py: number, q: Projected[]): boolean {
  let hasNeg = false;
  let hasPos = false;
  for (let i = 0; i < 4; i += 1) {
    const a = q[i];
    const b = q[(i + 1) % 4];
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

function layoutFaces(yaw: number, pitch: number, size: number): FaceDraw[] {
  const basis = cameraBasis(yaw, pitch);
  const scale = size * 0.52;
  const cx = size * 0.5;
  const cy = size * 0.5;
  const out: FaceDraw[] = [];
  for (const face of FACES) {
    const pts = face.corners.map((c) => projectPoint(c, basis, cx, cy, scale));
    const depth = pts.reduce((sum, p) => sum + p.depth, 0) / 4;
    out.push({ face, pts, depth });
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

  const hitFace = useCallback(
    (clientX: number, clientY: number): Face | null => {
      const canvas = canvasRef.current;
      if (!canvas) return null;
      const rect = canvas.getBoundingClientRect();
      // 与 draw() 用同一套坐标：内容框（clientWidth），并减掉边框宽度。
      const size = canvas.clientWidth;
      const px = clientX - rect.left - canvas.clientLeft;
      const py = clientY - rect.top - canvas.clientTop;
      const faces = layoutFaces(yaw, pitch, size).sort((a, b) => a.depth - b.depth);
      for (const { face, pts } of faces) {
        if (pointInQuad(px, py, pts)) return face;
      }
      return null;
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
      // 悬停高亮
      const face = hitFace(event.clientX, event.clientY);
      const id = face ? face.id : null;
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
    const face = hitFace(event.clientX, event.clientY);
    if (face) onPick({ yaw: face.yaw, pitch: face.pitch });
  };

  return (
    <canvas
      ref={canvasRef}
      className="view-cube"
      title="点面转到标准视图，拖动自由转相机"
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
