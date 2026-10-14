// 查看器里跨模块共用的类型。
//
// 放在独立模块是为了避免循环 import：`commands.ts` 需要 `PlacingState`，而
// `ViewerApp.tsx` 又要 import `commands.ts`——两边都从这个文件取类型就没有环。
import type { ToolSpec, Vec3 } from "../modeling/catalog";
import type { ScreenPoint } from "../ribbon/RubberBand";

/** 正在放置的工具：world 拼 args，screen 画橡皮筋。 */
export interface PlacingState {
  spec: ToolSpec;
  params: Record<string, string>;
  world: Vec3[];
  screen: ScreenPoint[];
  ids: number[];
}
