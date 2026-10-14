// 朝向立方体的顶点（8 个角）与相关几何常量。
//
// **逐行对应桌面 `view_cube_widget.cpp` 的 rebuild_corners()**：顶点是三个符号的乘积，
// 三角面从顶点沿三条棱各缩进 `CORNER_INSET` 切出来，法向是指向角外的那条对角线
// （用来剔背面）。桌面只有「面 + 8 个角」，**没有边**可点（提示语也是
// "Click a face or corner to snap the view"）。
//
// 单独成模块有两个原因：一是这张表是纯数据、可单测（角度算错会让点角转到奇怪的方向，
// 值得断言）；二是 ViewCube.tsx 已经不短了。
//
// 坐标约定与 ViewCube 一致：Front=+Z、Right=+X、Top=+Y。

export type V3 = [number, number, number];

/** 立方体半边长。与桌面 `kHalf` 同值。 */
export const HALF = 0.55;
/** 三角面从顶点沿每条棱缩进多少（world units）。与桌面 `kCornerInset` 同值。 */
export const CORNER_INSET = 0.22;
/**
 * 「离顶点这么近也算命中」的像素半径。角被压扁时三角面很细，只靠点中三角会点不到；
 * 桌面 `hit_corner` 里也是这个 10px。
 */
export const VERTEX_HIT_RADIUS = 10;

export interface Corner {
  /** 形如 `corner:rtf`，与桌面 `ViewCubeCorner` 枚举名一一对应（RightTopFront）。 */
  id: string;
  /** 角点本身（±HALF, ±HALF, ±HALF）。 */
  vertex: V3;
  /** 切出来的三角面（三个顶点）。 */
  pts: [V3, V3, V3];
  /** 指向角外的单位对角线，用于背面剔除。 */
  normal: V3;
  /** 点它之后相机要转到的角度。 */
  yaw: number;
  pitch: number;
}

function buildCorners(): Corner[] {
  const out: Corner[] = [];
  const inv = 1 / Math.sqrt(3);
  for (const sx of [1, -1]) {
    for (const sy of [1, -1]) {
      for (const sz of [1, -1]) {
        // 与桌面 on_view_cube_corner 同式：眼沿着这个角的方向，pitch = asin(dy)、
        // yaw = atan2(dx, dz)。对 (1,1,1) 就是经典的等轴测 45° / 35.26°。
        const d: V3 = [sx * inv, sy * inv, sz * inv];
        out.push({
          id: `corner:${sx > 0 ? "r" : "l"}${sy > 0 ? "t" : "b"}${sz > 0 ? "f" : "b"}`,
          vertex: [sx * HALF, sy * HALF, sz * HALF],
          pts: [
            [sx * (HALF - CORNER_INSET), sy * HALF, sz * HALF],
            [sx * HALF, sy * (HALF - CORNER_INSET), sz * HALF],
            [sx * HALF, sy * HALF, sz * (HALF - CORNER_INSET)],
          ],
          normal: d,
          pitch: Math.asin(d[1]),
          yaw: Math.atan2(d[0], d[2]),
        });
      }
    }
  }
  return out;
}

/** 八个角。顺序无意义（绘制与命中都按深度自己排）。 */
export const CORNERS: Corner[] = buildCorners();
