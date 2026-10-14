// Web 查看器本体（引擎 + 视口 + 顶栏）。
//
// 顶栏是**照桌面端做的**：一行菜单 + 两段分区工具带 + 底部状态栏，
// 骨架与配色对着 src/app/shell/ribbon/ 与 src/app/base/theme.cpp 抄，见 web/src/ribbon/。
// 功能面仍和桌面端有距离（这条产品线还在盖，现状/缺口见 docs/WEB.md），
// 所以菜单里只放**真的能用**的命令，没有的就不放——不留点不动的死项。
//
// 这个组件是**登录之后**才挂载的（门禁在 App.tsx）：它一挂载就会加载 WASM 并起
// 渲染循环，所以门禁必须挡在它外面，不能靠一层 CSS 盖住。理由见 App.tsx 顶部。
import { useCallback, useEffect, useRef, useState } from "react";

import ParamPrompt from "./ribbon/ParamPrompt";
import Icon from "./ribbon/icons";
import RubberBand, { type ScreenPoint } from "./ribbon/RubberBand";
import FileManager, { type FileEntry } from "./files/FileManager";
import { bundled_samples, type SampleDoc } from "./files/samples";
import MenuBar from "./ribbon/MenuBar";
import RibbonBar from "./ribbon/RibbonBar";
import type { RibbonDisplayMode } from "./ribbon/types";
import { buildArgs, firstStepHint, type ToolSpec, type Vec3 } from "./modeling/catalog";
import EngineLoading from "./engine/EngineLoading";
import { loadViewer, settle, toBinaryString, type EngineProgress, type ViewerStats } from "./viewer";
import ViewCube, { type ViewAngles } from "./ViewCube";
import AboutBox from "./viewer/AboutBox";
import ViewportHud from "./viewer/ViewportHud";
import { buildToolbar } from "./viewer/commands";
import { COLLAPSED_KEY, loadCollapsed, loadDisplayMode, remember, STYLE_KEY } from "./viewer/prefs";
import type { PlacingState } from "./viewer/types";

import "./ribbon/ribbon.css";

// 顶栏形态的本地记忆挪到了 viewer/prefs.ts（键名与桌面端一致，理由写在那边）。

interface ViewerProps {
  /** 当前登录名，显示在顶栏。 */
  user: string;
  /** 退出登录：由 App 清掉会话，本组件随之卸载。 */
  onLogout: () => void;
  /** 引擎预加载进度（App 在登录页就开始拉了），加载画面用它显示进度。 */
  engine: EngineProgress;
  /** 重新预加载引擎（加载画面上的「重试」）。 */
  onRetryEngine: () => void;
}

// PlacingState 的字段说明见 viewer/types.ts（放那边是为了让 commands.ts 也能用，
// 又不和本文件形成 import 环）。

export default function Viewer({ user, onLogout, engine, onRetryEngine }: ViewerProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const stageRef = useRef<HTMLDivElement>(null);
  const moduleRef = useRef<Awaited<ReturnType<typeof loadViewer>> | null>(null);
  // 左键按下位置：用来区分「点击落点」和「拖拽」。
  const pressRef = useRef<{ x: number; y: number; button: number } | null>(null);

  const [ready, setReady] = useState(false);
  const [status, setStatus] = useState("正在启动引擎…");
  const [docName, setDocName] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [dragging, setDragging] = useState(false);
  // 视图模式：0 线框 / 1 着色 / 2 真实感 —— 与桌面端同一套 RenderMode。
  const [renderMode, setRenderMode] = useState(1);
  const [selectedCount, setSelectedCount] = useState(0);
  const [stats, setStats] = useState<ViewerStats>({
    draws: 0,
    triangles: 0,
    gpuMeshMb: 0,
    pendingTessellate: 0,
  });
  // 引擎的 warn / error：桌面端在状态栏/对话框里报，web 端显示成页面上的面板。
  const [logText, setLogText] = useState("");
  // 实际编进来的浏览器后端（"WebGL2" / "WebGPU"）+ 它是不是实验特性。
  // 空串 = 还没 startViewer，顶栏先不显示。
  const [backend, setBackend] = useState<{ name: string; experimental: boolean }>({
    name: "",
    experimental: false,
  });
  // **实际生效**的造型内核（"occt" / "truck" / "none"）。和 backend 并列：
  // 设的和真的用上的可能不一样（wasm 上没编 OCCT 就会回退到 Truck，日志里有 warn）。
  const [kernel, setKernel] = useState("");
  // 相机朝向：右上角立方体用它画姿态，也用它做点面转场。
  const [viewAngles, setViewAngles] = useState<ViewAngles>({ yaw: 0.785398163, pitch: 0.35 });
  const viewAnglesRef = useRef(viewAngles);
  const applyViewAngles = (angles: ViewAngles) => {
    viewAnglesRef.current = angles;
    setViewAngles(angles);
  };

  // 顶栏形态：从 localStorage 恢复（键名与桌面端一致）。
  const [displayMode, setDisplayMode] = useState<RibbonDisplayMode>(() => loadDisplayMode());
  const [collapsed, setCollapsed] = useState<boolean>(() => loadCollapsed());
  // 「视图 → 面板 → 引擎日志」：默认开，所以默认行为和以前完全一样
  // （面板有内容才画出来，见下面 log-panel 的渲染条件）。
  const [showLog, setShowLog] = useState(true);
  const [aboutOpen, setAboutOpen] = useState(false);
  // 「打开的文档」状态。false = 欢迎卡片（视口盖住），依赖文档的工具一起灰掉。
  const [hasDoc, setHasDoc] = useState(false);
  // 最近打开：存 **File 句柄本身**——它只是指向磁盘上那个文件，不把内容读进内存，
  // 点它时再 arrayBuffer() 读一次。所以这条是真能用的"最近打开"，不是只记个名字。
  // 代价：只在**本次会话**内有效（File 不可序列化）；要跨刷新保留得用
  // File System Access API 的句柄 + IndexedDB，见 docs/WEB.md。
  const [recent, setRecent] = useState<{ name: string; file: File; at: number }[]>([]);
  // 文件管理页：正在打开哪一份、打开失败的提示、以及"打开文件夹"列出来的本地文档。
  const [busy, setBusy] = useState<string | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [folderDocs, setFolderDocs] = useState<FileEntry[]>([]);
  // 仓库自带的示例文档清单：只算一次（glob 是构建期就定下来的）。
  const [samples] = useState<SampleDoc[]>(() => bundled_samples());
  // 初始化重试计数：变了就让下面那个 init effect 整个重跑（加载画面上的「重试」）。
  const [initAttempt, setInitAttempt] = useState(0);

  // 挑一个本地文件夹列出文档：File System Access API，只有 Chromium 系有。
  // 没有这个 API 就整个不出现（菜单项也不画）——不给一个点了必然报错的动作。
  // **必须放在命令表之前**：menus 在构造时就要读 canPickFolder，声明在后面会踩 TDZ。
  const directoryPicker = (
    window as unknown as {
      showDirectoryPicker?: () => Promise<FileSystemDirectoryHandle>;
    }
  ).showDirectoryPicker;
  const canPickFolder = typeof directoryPicker === "function";
  // 键位处理依赖为空，文档状态同样要一个 ref 镜像（和 placingRef 一个道理）。
  const hasDocRef = useRef(false);
  hasDocRef.current = hasDoc;
  const fileInputRef = useRef<HTMLInputElement>(null);

  // 只驱动引擎、不取返回值的调用：Asyncify 下可能返回 Promise，忽略它但要吃掉异常。
  const fire = (action: unknown) => {
    void Promise.resolve(action).catch(() => {});
  };

  const changeDisplayMode = (mode: RibbonDisplayMode) => {
    setDisplayMode(mode);
    remember(STYLE_KEY, mode);
  };

  const changeCollapsed = (next: boolean) => {
    setCollapsed(next);
    remember(COLLAPSED_KEY, next ? "1" : "0");
  };

  /** 加载画面上的「重试」：让 App 重新预加载，并让本组件的初始化整个重跑。 */
  const retryEngine = () => {
    onRetryEngine();
    setInitAttempt((n) => n + 1);
  };

  // ── 建模工具：目录 → 命令 ────────────────────────────────────────────────
  // 工具清单在 web/src/modeling/catalog.ts（分组照桌面端 ribbon 抄的）。这些命令
  // **都支持"直给点"**，所以 web 自己采点、把 args 拼全再 dispatch 就够——引擎侧
  // 不需要 feed_point。代价见下面的 RubberBand：拿不到 DragContext 那种实时实体预览。
  const notReady = !ready;

  // 正在放置的工具。world 用来拼 args，screen 用来画屏幕空间橡皮筋。
  const [placing, setPlacing] = useState<PlacingState | null>(null);
  // 键位处理注册在 window 上、依赖为空，所以放置状态另用一个 ref 镜像，
  // 免得闭包拿到过期的 placing（和 viewAnglesRef 同一个套路）。
  const placingRef = useRef<PlacingState | null>(null);
  placingRef.current = placing;
  // 需要参数的命令：先弹提示框，确定后再决定「立即执行」还是「进入放置」。
  const [prompt, setPrompt] = useState<{ spec: ToolSpec } | null>(null);

  const specId = (spec: ToolSpec) => spec.id ?? spec.command;

  const readSelection = async (): Promise<number[]> => {
    const module = moduleRef.current;
    if (module === null) {
      return [];
    }
    const ids: number[] = [];
    const count = await settle(module.selectionCount());
    for (let i = 0; i < count; i += 1) {
      ids.push(await settle(module.selectionIdAt(i)));
    }
    return ids;
  };

  /** 选择集够不够。不够就把话说明白，不要静默什么都不发生。 */
  const checkSelection = (spec: ToolSpec, ids: number[]): boolean => {
    const how = spec.selectionAs ?? "ids";
    const need = how === "pair" ? 2 : 1;
    if (ids.length < need) {
      const message =
        how === "pair"
          ? `${spec.label}需要先选中 2 个构件（现在 ${ids.length} 个）`
          : `${spec.label}需要先选中一个构件`;
      setStatus(message);
      setError(message);
      return false;
    }
    return true;
  };

  const reportResult = async (label: string, ok: boolean) => {
    const module = moduleRef.current;
    const message = module === null ? "" : await settle(module.status());
    setStatus(ok ? `${label} 完成` : message);
    setError(ok ? null : message);
    if (module !== null) {
      setSelectedCount(await settle(module.selectionCount()));
    }
  };

  /**
   * 把一条工具规格发出去。所有建模命令都是**把点/选择拼进 args 再一次 dispatch**，
   * 所以不需要引擎侧的 feed_point（见 modeling/catalog.ts 顶部）。
   */
  const dispatchTool = async (
    spec: ToolSpec,
    params: Record<string, string>,
    points: Vec3[],
    ids: number[],
  ): Promise<boolean> => {
    const module = moduleRef.current;
    if (module === null) {
      return false;
    }
    if (spec.perSelection === true) {
      // 每个选中实体各发一次（删除选中）。逐条发、逐条可撤销。
      let ok = true;
      for (const id of ids) {
        ok = (await settle(module.dispatch(spec.command, buildArgs(spec, params, points, [id])))) && ok;
      }
      return ok;
    }
    return await settle(module.dispatch(spec.command, buildArgs(spec, params, points, ids)));
  };

  const runImmediate = async (spec: ToolSpec, params: Record<string, string>, ids: number[]) => {
    setPlacing(null);
    const ok = await dispatchTool(spec, params, [], ids);
    await reportResult(spec.label, ok);
  };

  const finishPlacement = async (
    spec: ToolSpec,
    params: Record<string, string>,
    world: Vec3[],
    ids: number[],
  ) => {
    setPlacing(null);
    setStatus(`${spec.label}：正在执行…`);
    const ok = await dispatchTool(spec, params, world, ids);
    await reportResult(spec.label, ok);
  };

  const cancelPlacement = () => {
    if (placingRef.current === null) {
      return;
    }
    setPlacing(null);
    setStatus("已取消");
  };

  /** 开放式工具（折线 / 墙 / 板…）按 Enter 或双击结束。 */
  const confirmPlacement = () => {
    const current = placingRef.current;
    if (current === null) {
      return;
    }
    const min = current.spec.points?.min ?? 0;
    if (current.world.length < min) {
      setStatus(`${current.spec.label}：至少要 ${min} 个点（Esc 取消）`);
      return;
    }
    void finishPlacement(current.spec, current.params, current.world, current.ids);
  };

  // 没有对应引擎命令的纯 UI 动作：新建 / 打开 / 撤销 / 显示模式 / 面板 / 关于…
  const uiActions: Record<string, () => void> = {
    new: () => void newDocument(),
    open: () => fileInputRef.current?.click(),
    undo: () => fire(moduleRef.current?.undo()),
    redo: () => fire(moduleRef.current?.redo()),
    frame_all: () => fire(moduleRef.current?.frameAll()),
    wireframe: () => changeRenderMode(0),
    shaded: () => changeRenderMode(1),
    realistic: () => changeRenderMode(2),
    "log-panel": () => setShowLog((on) => !on),
    about: () => setAboutOpen(true),
    logout: onLogout,
  };

  /** 点工具：已经选中的再点一次 = 取消（桌面端同款手感）。 */
  const armTool = async (spec: ToolSpec) => {
    if (notReady) {
      return;
    }
    if (placing !== null && specId(placing.spec) === specId(spec)) {
      setPlacing(null);
      setStatus("已取消");
      return;
    }
    setError(null);

    // 无需点、无需参数：纯 UI 动作或直接可执行的命令。
    if (spec.immediate === true && (spec.fields ?? []).length === 0) {
      const action = uiActions[spec.command];
      if (action !== undefined) {
        action();
        return;
      }
      const ids = spec.needsSelection === true ? await readSelection() : [];
      if (spec.needsSelection === true && !checkSelection(spec, ids)) {
        return;
      }
      await runImmediate(spec, {}, ids);
      return;
    }

    // 有参数：先问，确定后再继续（下面 submitPrompt）。
    if ((spec.fields ?? []).length > 0) {
      setPrompt({ spec });
      return;
    }

    const ids = spec.needsSelection === true ? await readSelection() : [];
    if (spec.needsSelection === true && !checkSelection(spec, ids)) {
      return;
    }
    setPlacing({ spec, params: {}, world: [], screen: [], ids });
    setStatus(`${spec.label}：${firstStepHint(spec)}`);
  };

  const submitPrompt = async (values: Record<string, string>) => {
    const spec = prompt?.spec;
    setPrompt(null);
    if (spec === undefined) {
      return;
    }
    const ids = spec.needsSelection === true ? await readSelection() : [];
    if (spec.needsSelection === true && !checkSelection(spec, ids)) {
      return;
    }
    if (spec.points !== undefined) {
      setPlacing({ spec, params: values, world: [], screen: [], ids });
      setStatus(`${spec.label}：${firstStepHint(spec)}`);
      return;
    }
    await runImmediate(spec, values, ids);
  };

  // ── 工具带 / 菜单 ────────────────────────────────────────────────────────
  // 命令表在 viewer/commands.ts（工具规格 → 菜单项 + 工具带按钮）。它是个纯函数，
  // 每次渲染重建、不缓存——闭包必须看到当前的 ready / renderMode / selectedCount /
  // placing。**调用点在下面**（必须在 closeDocument / openFolder / reopenRecent 这些
  // handler 定义之后：它们以**值**的形式传进 ctx，提前引用会踩 TDZ）。

  // 初始化：加载 WASM 模块、绑定 canvas、跑渲染循环、监听尺寸变化。
  useEffect(() => {
    let cancelled = false;
    let rafId = 0;
    let observer: ResizeObserver | null = null;

    void (async () => {
      try {
        const module = await loadViewer();
        if (cancelled) return;
        // Asyncify 编译下这些导出返回 Promise，必须 await：否则 Promise 恒为真值，
        // 失败检测失效，而且 Promise 进入 React 子节点会直接崩掉整页（白/黑屏）。
        if (!(await settle(module.startViewer("#viewport")))) {
          throw new Error((await settle(module.status())) || "startViewer 失败");
        }
        moduleRef.current = module;
        setReady(true);
        setError(null);
        setStatus(await settle(module.status()));
        setDocName(await settle(module.documentName()));
        // **启动时没有默认文档**：ViewerHost::start() 只起渲染通道，所以这里通常是
        // false——页面落在文件管理页，由用户双击 / 新建决定开哪一份（见 docs/WEB.md）。
        setHasDoc(await settle(module.hasDocument()));
        // 跑在哪个后端上：顶栏读数 + 实验角标。别让用户靠猜（WebGL / WebGPU 的
        // 画质与限制并不一样，见 docs/WEB.md）。
        setBackend({
          name: await settle(module.backendName()),
          experimental: await settle(module.backendExperimental()),
        });
        // 造型内核：读一次就够（内核实例建好之后不会变，换内核要重启）。
        setKernel(await settle(module.kernelName()));

        const stage = stageRef.current;
        if (!stage) return;

        const syncSize = () => {
          const rect = stage.getBoundingClientRect();
          const dpr = window.devicePixelRatio || 1;
          fire(
            module.resizeViewer(
              Math.max(2, Math.floor(rect.width * dpr)),
              Math.max(2, Math.floor(rect.height * dpr)),
            ),
          );
        };
        syncSize();
        observer = new ResizeObserver(syncSize);
        observer.observe(stage);

        // 渲染循环把异常挡在这里：否则 wasm 中止 / WebGPU 报错会直接让画面变黑，
        // 而界面上什么都不说。停在第几帧也一起报出来，方便定位。
        let frameCount = 0;
        const loop = async () => {
          try {
            await settle(module.renderFrame());
          } catch (err) {
            const message = err instanceof Error ? err.message : String(err);
            console.error("[tamias] renderFrame 抛出异常，渲染循环已停止", err);
            setError(message || "renderFrame 失败");
            setStatus(`渲染循环在第 ${frameCount} 帧停止`);
            return;
          }
          frameCount += 1;
          rafId = requestAnimationFrame(loop);
        };
        rafId = requestAnimationFrame(loop);
      } catch (err) {
        if (!cancelled) {
          setError(err instanceof Error ? err.message : String(err));
          setStatus("引擎启动失败");
        }
      }
    })();

    return () => {
      cancelled = true;
      if (rafId !== 0) cancelAnimationFrame(rafId);
      observer?.disconnect();
      moduleRef.current = null;
      setReady(false);
    };
    // initAttempt 变了就整个重来（加载画面上的「重试」）——上面那套清理会让它干净地重启。
  }, [initAttempt]);

  // 桌面视口左下角的读数（draw / tri / gpu / tess）在 web 上同样常显。
  // 每 400ms 取一次即可，不必每帧穿过 embind。
  useEffect(() => {
    if (!ready) return;
    const tick = async () => {
      const module = moduleRef.current;
      if (!module) return;
      try {
        setStats(await settle(module.stats()));
        setSelectedCount(await settle(module.selectionCount()));
        setLogText(await settle(module.logText()));
        applyViewAngles({
          yaw: await settle(module.viewYaw()),
          pitch: await settle(module.viewPitch()),
        });
      } catch {
        // 忽略瞬时读取失败（例如正在重建文档）
      }
    };
    void tick();
    const timer = window.setInterval(() => void tick(), 400);
    return () => window.clearInterval(timer);
  }, [ready]);

  // 键盘快捷键：F = 框选全部。
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        // 放置中 = 取消这次放置（桌面端同款）。参数提示框自己也会处理 Esc。
        cancelPlacement();
        return;
      }
      if (event.key === "Enter" && placingRef.current !== null) {
        const tag = (event.target as HTMLElement | null)?.tagName;
        if (tag === "INPUT" || tag === "TEXTAREA") return;
        // 开放式工具（折线 / 墙 / 板）用 Enter 收尾。
        event.preventDefault();
        confirmPlacement();
        return;
      }
      const module = moduleRef.current;
      if (!module) return;
      if (event.ctrlKey || event.metaKey) {
        const tag = (event.target as HTMLElement | null)?.tagName;
        if (tag === "INPUT" || tag === "TEXTAREA") return;
        if (event.key === "w" || event.key === "W") {
          // Ctrl+W = 关闭文档（桌面端是关闭标签页）。浏览器自己的 Ctrl+W 也是关标签页，
          // 所以只有确实有一份文档时才拦下来，其余情况让浏览器照常处理。
          if (hasDocRef.current) {
            event.preventDefault();
            void closeDocument();
          }
          return;
        }
        if (event.key === "z" || event.key === "Z") {
          event.preventDefault();
          if (event.shiftKey) {
            module.redo();
          } else {
            module.undo();
          }
          return;
        }
        if (event.key === "y" || event.key === "Y") {
          event.preventDefault();
          fire(module.redo());
          return;
        }
      }
      if (event.key !== "f" && event.key !== "F") return;
      const tag = (event.target as HTMLElement | null)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;
      event.preventDefault();
      fire(module.frameAll());
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  const openFile = useCallback(async (file: File) => {
    const module = moduleRef.current;
    if (!module) return;
    setBusy(file.name);
    setFileError(null);
    try {
      setError(null);
      setStatus(`正在加载 ${file.name}…`);
      const bytes = new Uint8Array(await file.arrayBuffer());
      if (!(await settle(module.loadFile(file.name, toBinaryString(bytes))))) {
        throw new Error((await settle(module.status())) || "文件加载失败");
      }
      setDocName(file.name);
      setStatus(await settle(module.status()));
      // .trscn 自带视图模式，加载后把按钮状态同步过来。
      setRenderMode(await settle(module.renderMode()));
      setHasDoc(await settle(module.hasDocument()));
      // 换文档了，正在放置的工具要取消（点对应不上新文档了）。
      setPlacing(null);
      // 最近打开：同名去重后放最前，最多 5 条。存的是 File 句柄，几乎不占内存。
      setRecent((prev) =>
        [
          { name: file.name, file, at: Date.now() },
          ...prev.filter((entry) => entry.name !== file.name),
        ].slice(0, 5),
      );
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setStatus("加载失败");
      setFileError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(null);
    }
  }, []);

  // 新建：空文档 + 内置示例场景（4 柱 + 4 顶梁，几何由 wasm 里的 OCCT/Truck 内核求值）。
  const newDocument = async () => {
    const module = moduleRef.current;
    if (!module) return;
    const ok = await settle(module.newDocument());
    const message = await settle(module.status());
    setStatus(message);
    setError(ok ? null : message);
    setDocName(await settle(module.documentName()));
    setRenderMode(await settle(module.renderMode()));
    setHasDoc(await settle(module.hasDocument()));
    // 换文档了，正在放置的工具要取消。
    setPlacing(null);
  };

  /** 关闭文档：回到欢迎卡片。web 端**没有保存**，所以关掉即丢弃——先跟用户确认。 */
  const closeDocument = async () => {
    const module = moduleRef.current;
    if (!module || !hasDocRef.current) {
      return;
    }
    const dirty = await settle(module.canUndo());
    const message = dirty
      ? `关闭「${(await settle(module.documentName())) || "未命名"}」？\n\n` +
        "这个文档里有未保存的改动，而 **web 端目前还不能保存**（没有导出/另存为），" +
        "关掉之后改动就没了。要留住它，请先在桌面端打开。"
      : `关闭「${(await settle(module.documentName())) || "未命名"}」？`;
    if (!window.confirm(message)) {
      return;
    }
    await settle(module.closeDocument());
    setPlacing(null);
    setHasDoc(await settle(module.hasDocument()));
    setDocName(await settle(module.documentName()));
    setSelectedCount(await settle(module.selectionCount()));
    setStatus(await settle(module.status()));
    setError(null);
  };

  /** 最近打开里的一条：用当初拿到的 File 句柄再读一次磁盘。 */
  const reopenRecent = (file: File) => {
    void openFile(file);
  };

  /** 从「最近打开」里移除一条（文件管理页的右键菜单）。key 形如 recent:名字:时间。 */
  const removeRecent = (key: string) => {
    setRecent((prev) => prev.filter((entry) => `recent:${entry.name}:${entry.at}` !== key));
  };

  /** 打开仓库自带的示例。**双击时才拉字节**——不然一进文件管理页就会把那 3 个
   *  2 MB 的 .trscn 全下下来。 */
  const openSample = async (sample: SampleDoc) => {
    const module = moduleRef.current;
    if (module === null) {
      return;
    }
    setBusy(sample.name);
    setFileError(null);
    try {
      const response = await fetch(sample.url);
      if (!response.ok) {
        throw new Error(`拉取 ${sample.name} 失败：HTTP ${response.status}`);
      }
      const bytes = new Uint8Array(await response.arrayBuffer());
      if (!(await settle(module.loadFile(sample.name, toBinaryString(bytes))))) {
        throw new Error((await settle(module.status())) || "文件加载失败");
      }
      setDocName(sample.name);
      setRenderMode(await settle(module.renderMode()));
      setHasDoc(await settle(module.hasDocument()));
      setPlacing(null);
      setStatus(await settle(module.status()));
      setError(null);
    } catch (err) {
      setFileError(err instanceof Error ? err.message : String(err));
    } finally {
      setBusy(null);
    }
  };

  const openFolder = async () => {
    if (directoryPicker === undefined) {
      return;
    }
    try {
      const dir = await directoryPicker.call(window);
      const found: FileEntry[] = [];
      // TS 5.6 的 lib.dom 没声明 FileSystemDirectoryHandle 的 entries()/values()，但规范里
      // 它本身就是异步入迭代（等价于 entries()，产出 [name, handle]）。按规范的类型断言。
      const entries = dir as unknown as AsyncIterable<[string, FileSystemHandle]>;
      for await (const [entryName, handle] of entries) {
        if (handle.kind !== "file") {
          continue;
        }
        const name = entryName;
        const ext = (name.split(".").pop() ?? "").toLowerCase();
        if (ext !== "tdoc" && ext !== "trscn" && ext !== "obj") {
          continue;
        }
        const fileHandle = handle as FileSystemFileHandle;
        found.push({
          key: `folder:${dir.name}/${name}`,
          name,
          kind: ext,
          where: dir.name,
          source: "本地文件夹",
          open: () => {
            void fileHandle
              .getFile()
              .then((file) => openFile(file))
              .catch((err: unknown) =>
                setFileError(err instanceof Error ? err.message : String(err)),
              );
          },
        });
      }
      found.sort((a, b) => a.name.localeCompare(b.name));
      setFolderDocs(found);
      setFileError(found.length === 0 ? `「${dir.name}」里没有 .tdoc / .trscn / .obj` : null);
    } catch {
      // 用户取消了选择——不是错误，什么都不做。
    }
  };

  // 文件管理页的列表：最近打开 → 本地文件夹 → 示例。
  const fileEntries: FileEntry[] = [
    ...recent.map((entry) => ({
      key: `recent:${entry.name}:${entry.at}`,
      name: entry.name,
      // 能进"最近打开"的都是 loadFile 认过的扩展名，这里的窄化是安全的。
      kind: (entry.name.split(".").pop() ?? "").toLowerCase() as FileEntry["kind"],
      where: "—",
      size: entry.file.size,
      at: entry.at,
      source: "最近打开" as const,
      open: () => reopenRecent(entry.file),
    })),
    ...folderDocs,
    ...samples.map((sample) => ({
      key: `sample:${sample.key}`,
      name: sample.name,
      kind: sample.kind,
      where: sample.where,
      source: "示例" as const,
      open: () => void openSample(sample),
    })),
  ];

  // 视图模式：线框 / 着色 / 真实感（桌面端是同一套 RenderMode）。
  const changeRenderMode = (mode: number) => {
    setRenderMode(mode);
    fire(moduleRef.current?.setRenderMode(mode));
  };

  const clampPitch = (pitch: number) =>
    Math.max(-Math.PI / 2 + 1e-3, Math.min(Math.PI / 2 - 1e-3, pitch));

  // 点立方体的面：按桌面端的 280ms ease-out cubic 转到标准视图（yaw 走最短路径）。
  const animateView = (target: ViewAngles) => {
    const module = moduleRef.current;
    if (!module) return;
    const from = viewAnglesRef.current;
    let dy = target.yaw - from.yaw;
    while (dy > Math.PI) dy -= Math.PI * 2;
    while (dy < -Math.PI) dy += Math.PI * 2;
    const toPitch = clampPitch(target.pitch);
    const start = performance.now();
    const duration = 280;
    const step = () => {
      const t = Math.min(1, (performance.now() - start) / duration);
      const e = 1 - Math.pow(1 - t, 3);
      const angles = {
        yaw: from.yaw + dy * e,
        pitch: from.pitch + (toPitch - from.pitch) * e,
      };
      fire(module.setViewAngles(angles.yaw, angles.pitch));
      applyViewAngles(angles);
      if (t < 1) {
        requestAnimationFrame(step);
      } else {
        fire(module.setViewAngles(target.yaw, toPitch));
        applyViewAngles({ yaw: target.yaw, pitch: toPitch });
      }
    };
    requestAnimationFrame(step);
  };

  // 拖立方体：直接跟手，不做缓动。
  const orbitView = (angles: ViewAngles) => {
    const next = { yaw: angles.yaw, pitch: clampPitch(angles.pitch) };
    fire(moduleRef.current?.setViewAngles(next.yaw, next.pitch));
    applyViewAngles(next);
  };

  // 点击视口：把归一化坐标交给引擎反投影到 y = 0 工作面，取到一个世界点。
  // 这是整个放置流程的**唯一取点入口**——桌面端是 DragContext::cursor_world 干的活。
  const collectPoint = async (canvas: HTMLCanvasElement, clientX: number, clientY: number) => {
    const module = moduleRef.current;
    const current = placingRef.current;
    if (module === null || current === null) {
      return;
    }
    const rect = canvas.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) {
      return;
    }
    const nx = (clientX - rect.left) / rect.width;
    const ny = (clientY - rect.top) / rect.height;
    const point = await settle(module.pickWorkPlane(nx, ny, 0));
    if (!point) {
      setStatus("工作平面上没有交点（把相机转平一点再试）");
      return;
    }
    const coords = point.split(",").map(Number);
    if (coords.length < 3 || coords.some((v) => !Number.isFinite(v))) {
      setStatus(`工作平面交点解析失败：${point}`);
      return;
    }
    const world: Vec3 = [coords[0], coords[1], coords[2]];
    const screen: ScreenPoint = { x: clientX - rect.left, y: clientY - rect.top };
    const nextWorld = [...current.world, world];
    const nextScreen = [...current.screen, screen];
    const max = current.spec.points?.max ?? null;

    if (max !== null && nextWorld.length >= max) {
      await finishPlacement(current.spec, current.params, nextWorld, current.ids);
      return;
    }
    setPlacing({ ...current, world: nextWorld, screen: nextScreen });
    setStatus(
      max === null
        ? `${current.spec.label}：已取 ${nextWorld.length} 点，Enter / 双击结束（Esc 取消）`
        : `${current.spec.label}：已取 ${nextWorld.length} / ${max} 点，还差 ${max - nextWorld.length} 点`,
    );
  };

  // 左键点击点选：与桌面视口同一套物体级 BVH 拾取；点空白处清空选择。
  const pickAt = async (canvas: HTMLCanvasElement, clientX: number, clientY: number) => {
    const module = moduleRef.current;
    if (!module) return;
    const rect = canvas.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return;
    const id = await settle(
      module.pickEntity((clientX - rect.left) / rect.width, (clientY - rect.top) / rect.height),
    );
    setSelectedCount(await settle(module.selectionCount()));
    setStatus(id ? `选中 #${id}` : "点空了，已清空选择");
  };

  const onPointerDown = (event: React.PointerEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    event.currentTarget.setPointerCapture(event.pointerId);
    pressRef.current = { x: event.clientX, y: event.clientY, button: event.button };
    fire(moduleRef.current?.pointerDown(event.clientX, event.clientY, event.button));
  };

  const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>) => {
    fire(moduleRef.current?.pointerMove(event.clientX, event.clientY));
  };

  const onPointerUp = (event: React.PointerEvent<HTMLCanvasElement>) => {
    fire(moduleRef.current?.pointerUp(event.clientX, event.clientY, event.button));
    const press = pressRef.current;
    pressRef.current = null;
    // 左键没有拖拽手势（旋转是中键、平移是右键），所以按下+松开就算点击，
    // 不再卡「移动不超过 4px」——那个阈值会让手稍微一抖的点击被静默丢掉。
    if (!press || press.button !== 0 || event.button !== 0) return;
    if (placingRef.current !== null) {
      // 放置中：左键 = 取一个点（不再做点选）。
      void collectPoint(event.currentTarget, event.clientX, event.clientY);
      return;
    }
    // 不在放置流程里时，左键点击＝点选（与桌面视口一致）。
    void pickAt(event.currentTarget, event.clientX, event.clientY);
  };

  // 双击＝结束开放式工具（折线 / 墙 / 板），和桌面端一样。
  const onDoubleClick = () => {
    confirmPlacement();
  };

  const onWheel = (event: React.WheelEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    fire(moduleRef.current?.wheel(event.deltaY));
  };

  const onDragOver = (event: React.DragEvent) => {
    event.preventDefault();
    setDragging(true);
  };

  const onDragLeave = (event: React.DragEvent) => {
    event.preventDefault();
    setDragging(false);
  };

  const onDrop = (event: React.DragEvent) => {
    event.preventDefault();
    setDragging(false);
    const file = event.dataTransfer.files?.[0];
    if (file) void openFile(file);
  };

  // 工具带 / 菜单：命令表是纯函数，每次渲染重建（实现见 viewer/commands.ts）。
  // 调用点放这么后，是因为 ctx 里以**值**传了 closeDocument / openFolder / recent——
  // 它们定义在上面，提前引用会在求值对象字面量时踩 TDZ。
  const { sections, topCommands, menus } = buildToolbar({
    notReady,
    hasDoc,
    renderMode,
    selectedCount,
    showLog,
    placing,
    user,
    canPickFolder,
    recent,
    uiActions,
    armTool,
    closeDocument,
    openFolder,
    reopenRecent,
    onLogout,
  });

  const statusClass = error ? "error" : ready ? "ready" : "";

  return (
    <div className="app">
      {/* 顶栏：一行菜单 + 两段分区工具带。骨架与配色照桌面端，见 web/src/ribbon/。 */}
      <div
        className={`tb-root${displayMode === "icons" ? " icons" : ""}${
          collapsed ? " collapsed" : ""
        }`}
      >
        <MenuBar
          menus={menus}
          commands={topCommands}
          displayMode={displayMode}
          onDisplayMode={changeDisplayMode}
          collapsed={collapsed}
          onCollapsed={changeCollapsed}
          corner={
            <>
              {/* 当前文档：名字 + 关闭。文档名放顶栏（桌面端在标签上），底部状态栏只留消息。 */}
              <span
                className={`tb-chip tb-doc${hasDoc ? "" : " empty"}`}
                title={hasDoc ? `当前文档：${docName || "未命名"}` : "没有打开的文档"}
              >
                <span className="tb-doc-name">
                  {hasDoc ? docName || "未命名" : "没有打开的文档"}
                </span>
                {hasDoc && (
                  <button
                    type="button"
                    className="tb-doc-close"
                    title="关闭文档（Ctrl+W）"
                    aria-label="关闭文档"
                    onClick={() => void closeDocument()}
                  >
                    <Icon name="close" size={11} />
                  </button>
                )}
              </span>
              {/* 后端角标：默认 WebGL2；WebGPU 只在 wasm-webgpu 预设里出现。 */}
              {backend.name !== "" && (
                <span
                  className={`tb-chip${backend.experimental ? " exp" : ""}`}
                  title={
                    backend.experimental
                      ? `${backend.name} 后端为实验特性，功能与稳定性尚未与 WebGL 对齐`
                      : `图形后端：${backend.name}`
                  }
                >
                  {backend.name}
                  {backend.experimental ? " · 实验" : ""}
                </span>
              )}
              <span className="tb-chip" title={`当前登录：${user}（文件 → 退出登录）`}>
                {user}
              </span>
            </>
          }
        />
        <RibbonBar sections={sections} />
      </div>
      {/* 「打开」命令触发的隐藏 input（桌面端是 QFileDialog）。 */}
      <input
        ref={fileInputRef}
        type="file"
        accept=".tdoc,.trscn,.obj"
        hidden
        onChange={(event) => {
          const file = event.target.files?.[0];
          if (file) void openFile(file);
          event.target.value = "";
        }}
      />
      <div
        ref={stageRef}
        className="stage"
        onDragOver={onDragOver}
        onDragLeave={onDragLeave}
        onDrop={onDrop}
      >
        <canvas
          id="viewport"
          ref={canvasRef}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onPointerCancel={onPointerUp}
          onAuxClick={(event) => event.preventDefault()}
          onWheel={onWheel}
          onContextMenu={(event) => event.preventDefault()}
          onDoubleClick={onDoubleClick}
        />
        {dragging && <div className="drop-overlay">松开以打开文件</div>}
        {/* 引擎还没起来：给一块**有进度**的加载画面。以前这里是空白——登录之后 26 MB
            的 wasm 才开始下，用户对着空视口干等几秒。 */}
        {!ready && (
          <EngineLoading progress={engine} message={error ?? status} onRetry={retryEngine} />
        )}
        {/* 没有打开的文档 = 文件管理页（登录后的落地页）。引擎此刻已经在跑，
            render() 提交空帧；工具带里依赖文档的工具全灰着。 */}
        {ready && !hasDoc && (
          <FileManager
            entries={fileEntries}
            busy={busy}
            error={fileError}
            onRemoveRecent={removeRecent}
          />
        )}
        {/* 放置中：屏幕空间的橡皮筋。桌面端靠 DragContext 画实时实体预览，web 没有
            那条链路（详见 ribbon/RubberBand.tsx 顶部），所以只画点与连线。 */}
        {placing !== null && (
          <RubberBand
            screens={placing.screen}
            world={placing.world}
            need={
              placing.spec.points?.max === null
                ? null
                : Math.max(0, (placing.spec.points?.max ?? 0) - placing.world.length)
            }
          />
        )}
        <ViewCube
          yaw={viewAngles.yaw}
          pitch={viewAngles.pitch}
          onPick={animateView}
          onOrbit={orbitView}
        />
        <div className="hint">
          {placing === null ? (
            <>
              左键点选 · 中键旋转 · 右键平移 · 滚轮缩放 · F 框选全部 · Ctrl+Z 撤销 ·
              工具带里选一个工具开始建模
            </>
          ) : (
            <>
              <strong>{placing.spec.label}</strong>
              {placing.spec.points?.max === null
                ? ` · 已取 ${placing.world.length} 点 · Enter / 双击结束 · Esc 取消`
                : ` · 已取 ${placing.world.length} / ${placing.spec.points?.max ?? 0} 点 · Esc 取消`}
            </>
          )}
        </div>
        <ViewportHud
          stats={stats}
          backendName={backend.name}
          kernel={kernel}
          selectedCount={selectedCount}
          logText={logText}
          showLog={showLog}
        />
      </div>
      {/* 底部状态栏：桌面端的状态消息走 QStatusBar（main_window 里满地的
          statusBar()->showMessage），文档名在右端。 */}
      <div className="status-bar">
        <span className={`sb-message ${statusClass}`} title={error ?? status}>
          {error ?? status}
        </span>
      </div>
      {/* 关于（桌面端有关于框，这里是最小版）。 */}
      {aboutOpen && (
        <AboutBox
          backend={backend}
          kernel={kernel}
          stats={stats}
          user={user}
          onClose={() => setAboutOpen(false)}
        />
      )}
      {/* 需要参数的命令：桌面端各有专门对话框/面板，web 用一个通用小表单收值。 */}
      {prompt !== null && (
        <ParamPrompt
          title={prompt.spec.label}
          icon={prompt.spec.icon}
          fields={prompt.spec.fields ?? []}
          onSubmit={(values) => void submitPrompt(values)}
          onCancel={() => setPrompt(null)}
        />
      )}
    </div>
  );
}
