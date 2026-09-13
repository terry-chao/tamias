// embind 暴露出来的 WASM 模块的类型声明 + 加载器。
// viewer_main.cpp 中 EMSCRIPTEN_BINDINGS 导出的函数一一对应。

// 桌面视口左下角读数的同款字段。
export interface ViewerStats {
  draws: number;
  triangles: number;
  gpuMeshMb: number;
  pendingTessellate: number;
}

export interface TamiasViewerModule {
  startViewer(canvasSelector: string): boolean;
  loadFile(name: string, bytes: string): boolean;
  // 新建文档：空文档 + 内置示例场景（几何由 wasm 里的建模内核求值）。
  newDocument(): boolean;
  resizeViewer(width: number, height: number): void;
  pointerDown(x: number, y: number, button: number): void;
  pointerMove(x: number, y: number): void;
  pointerUp(x: number, y: number, button: number): void;
  wheel(deltaY: number): void;
  frameAll(): void;
  renderFrame(): void;
  status(): string;
  documentName(): string;
  // 会话层能力（Session 的 embind 导出）
  dispatch(command: string, argsText?: string): boolean;
  undo(): void;
  redo(): void;
  canUndo(): boolean;
  canRedo(): boolean;
  selectionCount(): number;
  selectionIdAt(index: number): number;
  clearSelection(): void;
  // 视口归一化坐标 → 工作平面 (y = planeY) 上的世界点，返回 "x,y,z"；无交点返回空串。
  pickWorkPlane(nx: number, ny: number, planeY: number): string;
  // 视图模式：0 线框 / 1 着色 / 2 真实感（与桌面端同一套 RenderMode）。
  setRenderMode(mode: number): void;
  renderMode(): number;
  // 相机朝向，与桌面 ViewCube 同一套约定（Front=+Z, Right=+X, Top=+Y）。
  setViewAngles(yaw: number, pitch: number): void;
  viewYaw(): number;
  viewPitch(): number;
  // 点选：命中就选中并返回节点 id，未命中清空选择返回 0。
  pickEntity(nx: number, ny: number): number;
  stats(): ViewerStats;
  // 引擎最近的问题（warn / error），每行一条；空串表示没有。
  logText(): string;
}

declare global {
  interface Window {
    createTamiasViewer?: () => Promise<TamiasViewerModule>;
  }
}

let modulePromise: Promise<TamiasViewerModule> | null = null;

function loadScript(): Promise<void> {
  return new Promise((resolve, reject) => {
    if (typeof window.createTamiasViewer === "function") {
      resolve();
      return;
    }
    const script = document.createElement("script");
    // 与 index.html 同目录（dev 时在 public/，构建后被 CMake 拷到 wasm 输出目录）。
    script.src = "tamias_viewer.js";
    script.async = true;
    script.onload = () => resolve();
    script.onerror = () =>
      reject(new Error("加载 tamias_viewer.js 失败（请确认 WASM 产物与页面同目录）"));
    document.head.appendChild(script);
  });
}

export function loadViewer(): Promise<TamiasViewerModule> {
  if (modulePromise === null) {
    modulePromise = loadScript()
      .then(() => {
        if (typeof window.createTamiasViewer !== "function") {
          throw new Error("tamias_viewer.js 未导出 createTamiasViewer");
        }
        return window.createTamiasViewer();
      })
      .catch((err: unknown) => {
        modulePromise = null; // 允许重试
        throw err;
      });
  }
  return modulePromise;
}

// embind 的 loadFile 参数是 std::string，JS 侧用 latin1 二进制字符串传递。
export function toBinaryString(bytes: Uint8Array): string {
  const CHUNK = 0x8000;
  let out = "";
  for (let i = 0; i < bytes.length; i += CHUNK) {
    out += String.fromCharCode(...bytes.subarray(i, i + CHUNK));
  }
  return out;
}

// 这个 wasm 用 Asyncify 编译（WebGPU 的适配器/设备请求要走异步）。Asyncify 下
// embind 导出**可能返回 Promise 而不是值**：调用一次挂起后，紧接着的调用也会返回
// Promise。JS 侧若当同步值用，就会把 Promise 塞进 React 子节点而崩掉整页。
// 统一用 settle 取值：普通值原样返回，Promise 等它 resolve。
export function settle<T>(value: T | Promise<T>): Promise<T> {
  return Promise.resolve(value);
}
