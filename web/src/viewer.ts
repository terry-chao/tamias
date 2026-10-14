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
  // 文件字节**必须**传 Uint8Array：embind 里 std::string 是按 UTF-8 注册的，
  // 传 JS 字符串会把每个 ≥ 0x80 的字节重新编成 2 字节，chunk 头跟着错位——
  // 现象就是 .tdoc / .trscn 打开时报 "chunk size exceeds remaining data"。
  loadFile(name: string, bytes: Uint8Array): boolean;
  // 新建文档：空文档 + 内置示例场景（几何由 wasm 里的建模内核求值）。
  newDocument(): boolean;
  // 关闭文档：丢掉几何与 GPU 资产，回到「没有打开的文档」。web 端没有保存，关掉即丢弃。
  closeDocument(): boolean;
  // 有没有打开的文档。页面据此决定显示视口还是欢迎卡片，并把依赖文档的工具灰掉。
  hasDocument(): boolean;
  resizeViewer(width: number, height: number): void;
  pointerDown(x: number, y: number, button: number): void;
  pointerMove(x: number, y: number): void;
  pointerUp(x: number, y: number, button: number): void;
  wheel(deltaY: number): void;
  frameAll(): void;
  renderFrame(): void;
  status(): string;
  documentName(): string;
  // 实际编进来的浏览器后端："WebGL2"（默认）或 "WebGPU"（实验预设 wasm-webgpu）。
  backendName(): string;
  // 该后端是不是实验特性（今天只有 WebGPU 是）。页面据此打「（实验）」角标。
  backendExperimental(): boolean;
  // **实际生效**的造型内核："occt"（桌面/服务端的默认）、"truck"（可选回退）或 "none"。
  // 和 backendName() 并列：想设的和真的用上的可能不一样（回退时引擎日志里有 warn）。
  kernelName(): string;
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
    // MODULARIZE 出来的工厂。可以传入 `wasmBinary` 把预取好的字节直接交给它——
    // 胶水里的 getWasmBinary 见到它就**不再 fetch**（见下面 preloadViewer 的说明）。
    createTamiasViewer?: (options?: { wasmBinary?: ArrayBuffer }) => Promise<TamiasViewerModule>;
  }
}

let modulePromise: Promise<TamiasViewerModule> | null = null;

function loadScript(): Promise<string> {
  return new Promise((resolve, reject) => {
    if (typeof window.createTamiasViewer === "function") {
      resolve(new URL("tamias_viewer.js", document.baseURI).href);
      return;
    }
    const script = document.createElement("script");
    // 与 index.html 同目录（dev 时在 public/，构建后被 CMake 拷到 wasm 输出目录）。
    script.src = "tamias_viewer.js";
    script.async = true;
    script.onload = () => resolve(script.src);
    script.onerror = () =>
      reject(new Error("加载 tamias_viewer.js 失败（请确认 WASM 产物与页面同目录）"));
    document.head.appendChild(script);
  });
}

/**
 * 引擎预加载的阶段。**显式阶段，不靠字节数推**——踩过的坑：预览服务原先用 `pipe` 流式发
 * `.wasm`、**不发 Content-Length**，于是 `total` 恒为 0；而界面把"下完了"判定成
 * `total > 0 && loaded >= total`，那个条件永远不成立，登录页就一直挂着"正在预热引擎…"，
 * 哪怕引擎早就可用了。更根本的是：**字节下完 ≠ 引擎可用**（26 MB 还要实例化几秒）。
 */
export type EnginePhase = "download" | "instantiate" | "ready";

/** 预加载进度：已下字节 / 总字节（**拿不到 Content-Length 时 total 为 0**）。 */
export interface PreloadProgress {
  /** `download` = 还在下；`instantiate` = 字节下完了，正在实例化/编译 wasm。 */
  phase: "download" | "instantiate";
  loaded: number;
  total: number;
}

/** 预加载进度 + 失败原因。App 持有它，登录页和加载画面都读它。 */
export interface EngineProgress {
  phase: EnginePhase;
  loaded: number;
  total: number;
  error: string | null;
}

export function formatMb(bytes: number): string {
  return (bytes / 1024 / 1024).toFixed(1);
}

/**
 * 自己把 .wasm 拉下来，并按块报进度。
 *
 * 为什么要自己 fetch：26 MB 的产物在冷启动时要下几秒，而胶水内部的 fetch 不给进度。
 * 把拿到的字节通过 `wasmBinary` 交给工厂即可——**不会因此多下一遍**：胶水的
 * `getWasmBinary` 第一句就是 `if (!wasmBinary)` 才去 fetch。这点很重要，因为预览服务
 * 强制 `Cache-Control: no-store`，让它自己再取一次就是实打实的两遍 26 MB。
 */
async function fetchWasmWithProgress(
  url: string,
  onProgress: (progress: PreloadProgress) => void,
): Promise<ArrayBuffer> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`加载 ${url} 失败：HTTP ${response.status}`);
  }
  const contentLength = Number(response.headers.get("content-length") ?? "0");
  if (response.body === null) {
    // 极端情况下没有流（老浏览器）：退化成一次性读取，进度只有最后一下。
    const buffer = await response.arrayBuffer();
    onProgress({ phase: "download", loaded: buffer.byteLength, total: buffer.byteLength });
    return buffer;
  }
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) {
      break;
    }
    if (value !== undefined) {
      chunks.push(value);
      loaded += value.byteLength;
      onProgress({ phase: "download", loaded, total: contentLength });
    }
  }
  const merged = new Uint8Array(loaded);
  let offset = 0;
  for (const chunk of chunks) {
    merged.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return merged.buffer;
}

/**
 * 加载（或复用）查看器模块。**在登录页就该调它**——26 MB 的产物别等用户点完登录才开始下，
 * 登录那几秒正好用来拉。重复调用共用同一个 promise；失败时会复位以便重试。
 *
 * 传 `onProgress` 才会走"自己带进度地下"那条路；不传就交给胶水自己取（没有进度可言）。
 */
export function preloadViewer(
  onProgress?: (progress: PreloadProgress) => void,
): Promise<TamiasViewerModule> {
  if (modulePromise !== null) {
    return modulePromise;
  }
  modulePromise = (async () => {
    const scriptUrl = await loadScript();
    if (typeof window.createTamiasViewer !== "function") {
      throw new Error("tamias_viewer.js 未导出 createTamiasViewer");
    }
    if (onProgress === undefined) {
      return window.createTamiasViewer();
    }
    const wasmUrl = new URL("tamias_viewer.wasm", scriptUrl).href;
    const wasmBinary = await fetchWasmWithProgress(wasmUrl, onProgress);
    // 字节下完 ≠ 引擎可用：26 MB 的 wasm 还要实例化/编译几秒，这一段**没有字节数可报**。
    // 所以显式报一个阶段，别让界面靠"loaded === total"去猜（服务端不发 Content-Length
    // 时那个条件永远不成立）。
    onProgress({ phase: "instantiate", loaded: wasmBinary.byteLength, total: wasmBinary.byteLength });
    // 让这一帧先画出来：下面就是重活，不给浏览器一次机会的话用户只看到"还没变过"。
    await new Promise((resolve) => {
      requestAnimationFrame(() => resolve(null));
    });
    return window.createTamiasViewer({ wasmBinary });
  })().catch((err: unknown) => {
    modulePromise = null; // 允许重试
    throw err;
  });
  return modulePromise;
}

export function loadViewer(): Promise<TamiasViewerModule> {
  return preloadViewer();
}

// **别把二进制文件拼成 JS 字符串再交给 embind。** `loadFile` 的第二个参数是
// `std::string`，而 Emscripten 给这个名字注册的类型是"UTF-8 字符串"
// （产物里 `stdStringIsUTF8=true`）：JS 字符串会被 `stringToUTF8` 重新编码，
// 每个 ≥ 0x80 的字节变成 2 字节，字节流整体错位——`.tdoc` / `.trscn` 的
// chunk 头随之读错，报 "chunk size exceeds remaining data"（纯 ASCII 的 `.obj`
// 碰巧看不出来）。直接传 `Uint8Array` 即可：embind 对它走 `HEAPU8.set()` 原样拷贝。
// 同理，将来若有"从 wasm 取回二进制"的导出口，`fromWireType` 也是 `UTF8ToString`，
// 同样不能当字符串取。

// 这个 wasm 用 Asyncify 编译（WebGPU 的适配器/设备请求要走异步）。Asyncify 下
// embind 导出**可能返回 Promise 而不是值**：调用一次挂起后，紧接着的调用也会返回
// Promise。JS 侧若当同步值用，就会把 Promise 塞进 React 子节点而崩掉整页。
// 统一用 settle 取值：普通值原样返回，Promise 等它 resolve。
export function settle<T>(value: T | Promise<T>): Promise<T> {
  return Promise.resolve(value);
}

// 打开失败的提示文案：引擎给的是英文诊断（`document_io: chunk size exceeds remaining
// data`），直接摆在横幅里中文用户读不懂。**第一句固定是"打开文档失败"**，后面跟中文
// 结论；认不出来的原因也照样带上"打开文档失败"这个前缀，原始诊断放在括号里，
// 查日志、报 bug 都要它。整句单行：横幅 `<div>` 不换行。
export function describeOpenFailure(status: string): string {
  const raw = status.trim();
  if (/chunk size exceeds remaining data|unexpected end of data/i.test(raw)) {
    return `打开文档失败：文件不完整或已损坏（${raw}）`;
  }
  if (/bad magic|Not a Tamias/i.test(raw)) {
    return `打开文档失败：这不是 Tamias 文件（${raw}）`;
  }
  if (/Unsupported (Tamias document|render scene) version/i.test(raw)) {
    return `打开文档失败：这个文件是其它版本的 Tamias 写的，当前版本读不了（${raw}）`;
  }
  return raw === "" ? "打开文档失败：原因未知" : `打开文档失败：${raw}`;
}
