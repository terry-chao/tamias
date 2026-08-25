// embind 暴露出来的 WASM 模块的类型声明 + 加载器。
// viewer_main.cpp 中 EMSCRIPTEN_BINDINGS 导出的函数一一对应。

export interface TamiasViewerModule {
  startViewer(canvasSelector: string): boolean;
  loadFile(name: string, bytes: string): boolean;
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
