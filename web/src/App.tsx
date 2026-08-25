import { useCallback, useEffect, useRef, useState } from "react";

import { loadViewer, toBinaryString } from "./viewer";

export default function App() {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const stageRef = useRef<HTMLDivElement>(null);
  const moduleRef = useRef<Awaited<ReturnType<typeof loadViewer>> | null>(null);

  const [ready, setReady] = useState(false);
  const [status, setStatus] = useState("正在启动引擎…");
  const [docName, setDocName] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [dragging, setDragging] = useState(false);

  // 初始化：加载 WASM 模块、绑定 canvas、跑渲染循环、监听尺寸变化。
  useEffect(() => {
    let cancelled = false;
    let rafId = 0;
    let observer: ResizeObserver | null = null;

    void (async () => {
      try {
        const module = await loadViewer();
        if (cancelled) return;
        if (!module.startViewer("#viewport")) {
          throw new Error(module.status() || "startViewer 失败");
        }
        moduleRef.current = module;
        setReady(true);
        setError(null);
        setStatus(module.status());
        setDocName(module.documentName());

        const stage = stageRef.current;
        if (!stage) return;

        const syncSize = () => {
          const rect = stage.getBoundingClientRect();
          const dpr = window.devicePixelRatio || 1;
          module.resizeViewer(
            Math.max(2, Math.floor(rect.width * dpr)),
            Math.max(2, Math.floor(rect.height * dpr)),
          );
        };
        syncSize();
        observer = new ResizeObserver(syncSize);
        observer.observe(stage);

        const loop = () => {
          module.renderFrame();
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
          module.redo();
          return;
        }
      }
      if (event.key !== "f" && event.key !== "F") return;
      const tag = (event.target as HTMLElement | null)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA") return;
      event.preventDefault();
      module.frameAll();
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
      if (!module.loadFile(file.name, toBinaryString(bytes))) {
        throw new Error(module.status() || "文件加载失败");
      }
      setDocName(file.name);
      setStatus(module.status());
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setStatus("加载失败");
    }
  }, []);

  const onPointerDown = (event: React.PointerEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    event.currentTarget.setPointerCapture(event.pointerId);
    moduleRef.current?.pointerDown(event.clientX, event.clientY, event.button);
  };

  const onPointerMove = (event: React.PointerEvent<HTMLCanvasElement>) => {
    moduleRef.current?.pointerMove(event.clientX, event.clientY);
  };

  const onPointerUp = (event: React.PointerEvent<HTMLCanvasElement>) => {
    moduleRef.current?.pointerUp(event.clientX, event.clientY, event.button);
  };

  const onWheel = (event: React.WheelEvent<HTMLCanvasElement>) => {
    event.preventDefault();
    moduleRef.current?.wheel(event.deltaY);
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
          <label className="btn">
            打开 .tdoc / .obj
            <input
              type="file"
              accept=".tdoc,.obj"
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
            onClick={() => moduleRef.current?.undo()}
          >
            撤销
          </button>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => moduleRef.current?.redo()}
          >
            重做
          </button>
          <button
            type="button"
            className="btn"
            disabled={!ready}
            onClick={() => moduleRef.current?.frameAll()}
          >
            框选全部 <kbd>F</kbd>
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
        <div className="hint">中键旋转 · 右键平移 · 滚轮缩放 · F 框选全部 · Ctrl+Z 撤销</div>
      </div>
    </div>
  );
}
