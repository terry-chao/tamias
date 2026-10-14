// 应用外壳：**只做登录门禁**，查看器本体在 ViewerApp.tsx。
//
// 为什么门禁是一个外层组件，而不是在 ViewerApp 里写 `if (!session) return <LoginPage/>`：
// ViewerApp 有 8 个 hook，登录前后 hook 数量不同会直接踩 React 的
// 「Rendered more hooks than during the previous render」并白屏。而且 ViewerApp 一挂载
// 就会加载 WASM、起渲染循环——门禁必须在它**外面**，不能靠一层 CSS 盖住。
//
// （文件名是 ViewerApp.tsx 而不是 Viewer.tsx：后者和同目录的 viewer.ts 只差大小写，
// 在 Windows / macOS 上会被 TS 判成同一个文件，tsc 直接报 TS1261。）
//
// 「这不是安全边界」，理由见 auth.ts 顶部。
import { useCallback, useEffect, useState } from "react";

import { clearSession, loadSession, startSession, subscribeSession, type Session } from "./auth";
import LoginPage from "./LoginPage";
import ViewerApp from "./ViewerApp";
import { preloadViewer, type EngineProgress } from "./viewer";

export default function App() {
  // 惰性初始化：刷新页面（以及**新开标签页**）时从 localStorage 恢复——会话是同源所有
  // 标签页共享的，所以登录过之后开新标签页不会再被要求登录一次。
  const [session, setSession] = useState<Session | null>(() => loadSession());

  const onAuthenticated = useCallback((user: string) => {
    setSession(startSession(user));
  }, []);

  const onLogout = useCallback(() => {
    clearSession();
    setSession(null);
  }, []);

  // 跨标签页同步：在别的标签页登出，这一页也跟着登出；在别的标签页登录，这一页立刻
  // 拿到会话。`storage` 事件只在**别的**标签页里触发，本页的变更靠上面的 setSession。
  useEffect(() => subscribeSession(setSession), []);

  // **在登录页就开始拉引擎。** 产物 26 MB（含 OCCT），别等用户点完登录才开始下——
  // 登录那几秒正好拿来下。ViewerApp 之后调 loadViewer() 会复用同一个 promise。
  const [engine, setEngine] = useState<EngineProgress>({
    phase: "download",
    loaded: 0,
    total: 0,
    error: null,
  });
  const [attempt, setAttempt] = useState(0);

  useEffect(() => {
    let cancelled = false;
    // 重试时先清掉错误，好让进度条重新出现。
    setEngine((prev) => ({ ...prev, phase: "download", error: null }));
    preloadViewer((progress) => {
      if (!cancelled) {
        setEngine({
          phase: progress.phase,
          loaded: progress.loaded,
          total: progress.total,
          error: null,
        });
      }
    })
      // **「就绪」必须由 promise 决定，不能靠字节数推。** 三条理由，以前每一条都会让
      // 登录页永远停在「正在预热引擎…」：
      //   1. 字节下完 ≠ 引擎可用（26 MB 还要实例化几秒）；
      //   2. 总长是**部署相关**的：预览服务原先流式发 .wasm 不发 Content-Length，
      //      total 恒为 0，「loaded >= total 且 total > 0」永远不成立；
      //   3. 重复调用（比如 StrictMode 下 effect 跑两次）会命中缓存的 promise，
      //      `onProgress` 一次都不回调。
      .then(() => {
        if (!cancelled) {
          setEngine((prev) => ({ ...prev, phase: "ready" }));
        }
      })
      .catch((err: unknown) => {
        if (!cancelled) {
          setEngine((prev) => ({
            ...prev,
            error: err instanceof Error ? err.message : String(err),
          }));
        }
      });
    return () => {
      cancelled = true;
    };
  }, [attempt]);

  const retryPreload = useCallback(() => setAttempt((n) => n + 1), []);

  if (session === null) {
    return <LoginPage onAuthenticated={onAuthenticated} engine={engine} />;
  }
  // 退出时会先切回 LoginPage，ViewerApp 随之卸载，它持有的 wasm 状态一并释放，
  // 所以这里不需要额外用 key 强制重建。
  return (
    <ViewerApp
      user={session.user}
      onLogout={onLogout}
      engine={engine}
      onRetryEngine={retryPreload}
    />
  );
}
