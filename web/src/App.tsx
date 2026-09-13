import { useCallback, useEffect, useRef, useState } from "react";

import { loadViewer, settle, toBinaryString } from "./viewer";

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
    setPlaceMode("none");
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
    const message = await settle(module.status());
    setStatus(message);
    setError(ok ? null : message);
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
    // 只认「左键按下后没怎么动」的点击；拖拽过就不落点。
    if (!press || press.button !== 0 || event.button !== 0) return;
    if (Math.hypot(event.clientX - press.x, event.clientY - press.y) > 4) return;
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
        <div className="hint">
          中键旋转 · 右键平移 · 滚轮缩放 · F 框选全部 · Ctrl+Z 撤销 ·{" "}
          {placeMode === "none"
            ? "选「方柱 / 圆柱」后点视口落点（Truck 建模）"
            : "左键点击放置，Esc 退出"}
        </div>
      </div>
    </div>
  );
}
