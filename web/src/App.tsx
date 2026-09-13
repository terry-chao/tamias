// ⚠️ 施工中（WIP）：Web 查看器前端
//
// 这条产品线和桌面端远未对齐：功能在陆续补，交互和接口随时可能变。顶栏那个
// 「施工中」角标就是给使用者看的提醒。现状/缺口见 docs/WEB.md。
import { useCallback, useEffect, useRef, useState } from "react";

import { loadViewer, settle, toBinaryString, type ViewerStats } from "./viewer";
import ViewCube, { type ViewAngles } from "./ViewCube";

export default function App() {
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
  // 建模放置模式：active 时左键点击视口就在水平面上落一个构件。
  const [placeMode, setPlaceMode] = useState<"none" | "rect" | "circle">("none");
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
  // 相机朝向：右上角立方体用它画姿态，也用它做点面转场。
  const [viewAngles, setViewAngles] = useState<ViewAngles>({ yaw: 0.785398163, pitch: 0.35 });
  const viewAnglesRef = useRef(viewAngles);
  const applyViewAngles = (angles: ViewAngles) => {
    viewAnglesRef.current = angles;
    setViewAngles(angles);
  };

  // 只驱动引擎、不取返回值的调用：Asyncify 下可能返回 Promise，忽略它但要吃掉异常。
  const fire = (action: unknown) => {
    void Promise.resolve(action).catch(() => {});
  };

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
  }, []);

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
        setPlaceMode("none");
        return;
      }
      const module = moduleRef.current;
      if (!module) return;
      if (event.ctrlKey || event.metaKey) {
        const tag = (event.target as HTMLElement | null)?.tagName;
        if (tag === "INPUT" || tag === "TEXTAREA") return;
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
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setStatus("加载失败");
    }
  }, []);

  // 新建：空文档 + 内置示例场景（4 柱 + 4 顶梁，几何由 wasm 里的 Truck 内核求值）。
  const newDocument = async () => {
    const module = moduleRef.current;
    if (!module) return;
    const ok = await settle(module.newDocument());
    const message = await settle(module.status());
    setStatus(message);
    setError(ok ? null : message);
    setDocName(await settle(module.documentName()));
    setRenderMode(await settle(module.renderMode()));
    setPlaceMode("none");
  };

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

  // 删除选中：先把 id 收集完再逐条 dispatch——边删边读选择会看到过期列表。
  const deleteSelection = async () => {
    const module = moduleRef.current;
    if (!module) return;
    const count = await settle(module.selectionCount());
    if (count === 0) {
      setStatus("没有选中的构件");
      return;
    }
    const ids: number[] = [];
    for (let i = 0; i < count; i += 1) {
      ids.push(await settle(module.selectionIdAt(i)));
    }
    let ok = true;
    for (const id of ids) {
      ok = (await settle(module.dispatch("delete_entity", `i:entity_id=${id}`))) && ok;
    }
    const message = await settle(module.status());
    setStatus(ok ? `已删除 ${ids.length} 个构件` : message);
    setError(ok ? null : message);
    setSelectedCount(await settle(module.selectionCount()));
  };

  // 建模：在指定世界点脚本式建一根柱子（带预设位置 → 命令非交互，立即建体）。
  // 几何由 WASM 里的 Truck 内核求值，见 docs/WEB.md。
  const placeColumn = async (subType: "rect" | "circle", point: string) => {
    const module = moduleRef.current;
    if (!module) return;
    const args =
      subType === "circle"
        ? `s:sub_type=circle;d:diameter=0.6;d:height=3;p:points=${point}`
        : `s:sub_type=rect;d:width=0.6;d:depth=0.6;d:height=3;p:points=${point}`;
    const ok = await settle(module.dispatch("create_column", args));
    if (ok) {
      // 明确回报落点，省得"点了没反应"和"放到了别处"分不清。
      setStatus(`已放置${subType === "circle" ? "圆柱" : "方柱"} @ ${point}`);
      setError(null);
    } else {
      const message = await settle(module.status());
      setStatus(message);
      setError(message);
    }
  };

  // 点击视口：把归一化坐标交给引擎反投影到 y = 0 工作面，命中就落构件。
  const placeAt = async (canvas: HTMLCanvasElement, clientX: number, clientY: number) => {
    const module = moduleRef.current;
    if (!module || placeMode === "none") return;
    const rect = canvas.getBoundingClientRect();
    if (rect.width < 1 || rect.height < 1) return;
    const nx = (clientX - rect.left) / rect.width;
    const ny = (clientY - rect.top) / rect.height;
    const point = await settle(module.pickWorkPlane(nx, ny, 0));
    if (!point) {
      setStatus("工作平面上没有交点（把相机转平一点再试）");
      return;
    }
    await placeColumn(placeMode, point);
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
    if (placeMode === "none") {
      // 不在放置模式时，左键点击＝点选（与桌面视口一致）。
      void pickAt(event.currentTarget, event.clientX, event.clientY);
      return;
    }
    void placeAt(event.currentTarget, event.clientX, event.clientY);
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

  const statusClass = error ? "error" : ready ? "ready" : "";

  return (
    <div className="app">
      <header>
        <div className="brand">
          <strong>Tamias Viewer</strong>
          <span>引擎 WASM · Web 查看器</span>
          <span className="wip" title="和桌面端尚未对齐，功能与接口都会变">
            施工中
          </span>
        </div>
        <div className="actions">
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => void newDocument()}
            title="新建一个文档，内含内置示例场景（几何由 wasm 里的 Truck 内核求值）"
          >
            新建
          </button>
          <label className="btn">
            打开 .tdoc / .trscn / .obj
            <input
              type="file"
              accept=".tdoc,.trscn,.obj"
              hidden
              onChange={(event) => {
                const file = event.target.files?.[0];
                if (file) void openFile(file);
                event.target.value = "";
              }}
            />
          </label>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => fire(moduleRef.current?.undo())}
          >
            撤销
          </button>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => fire(moduleRef.current?.redo())}
          >
            重做
          </button>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => fire(moduleRef.current?.frameAll())}
          >
            框选全部 <kbd>F</kbd>
          </button>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => void deleteSelection()}
            title="删除选中的构件（每条都是一步可撤销的命令）"
          >
            删除选中{selectedCount > 0 ? ` (${selectedCount})` : ""}
          </button>
          <span className="divider" />
          {/* 视图模式：与桌面端同一套 RenderMode，0 线框 / 1 着色 / 2 真实感 */}
          {["线框", "着色", "真实"].map((label, index) => (
            <button
              key={label}
              type="button"
              className={`btn${renderMode === index ? " active" : ""}`}
              disabled={!ready}
              onClick={() => changeRenderMode(index)}
              title={`${label}模式`}
            >
              {label}
            </button>
          ))}
          <span className="divider" />
          <button
            type="button"
            className={`btn${placeMode === "rect" ? " active" : ""}`}
            disabled={!ready}
            onClick={() => setPlaceMode((mode) => (mode === "rect" ? "none" : "rect"))}
            title="进入放置模式后，点视口在水平面上放一根方柱（Esc 退出）"
          >
            方柱
          </button>
          <button
            type="button"
            className={`btn${placeMode === "circle" ? " active" : ""}`}
            disabled={!ready}
            onClick={() => setPlaceMode((mode) => (mode === "circle" ? "none" : "circle"))}
            title="进入放置模式后，点视口在水平面上放一根圆柱（Esc 退出）"
          >
            圆柱
          </button>
        </div>
        {docName && (
          <span className="doc-name" title={docName}>
            {docName}
          </span>
        )}
        <span className={`status ${statusClass}`} title={error ?? status}>
          {error ?? status}
        </span>
      </header>
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
        />
        {dragging && <div className="drop-overlay">松开以打开文件</div>}
        <ViewCube
          yaw={viewAngles.yaw}
          pitch={viewAngles.pitch}
          onPick={animateView}
          onOrbit={orbitView}
        />
        <div className="hint">
          左键点选 · 中键旋转 · 右键平移 · 滚轮缩放 · F 框选全部 · Ctrl+Z 撤销 ·{" "}
          {placeMode === "none" ? "「方柱 / 圆柱」可点视口落点" : "左键点击放置，Esc 退出"}
        </div>
        {/* 与桌面视口左下角同一组读数 */}
        <div className="stats" title="与桌面视口左下角同一组读数">
          <span>draw {stats.draws}</span>
          <span>tri {stats.triangles}</span>
          <span>gpu {stats.gpuMeshMb.toFixed(1)}MB</span>
          <span>tess {stats.pendingTessellate}</span>
          <span>选中 {selectedCount}</span>
        </div>
        {/* 引擎报错：原来只能翻 DevTools，现在直接显示在页面上 */}
        {logText.trim() !== "" && (
          <div className="log-panel" title="引擎的 warn / error 日志">
            <div className="log-title">引擎日志</div>
            <pre>{logText.trim()}</pre>
          </div>
        )}
      </div>
    </div>
  );
}
