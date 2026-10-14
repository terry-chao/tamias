// 登录：**纯前端 UI 门禁，不是安全边界**。
//
// 凭据是写死在这个模块里的明文，判定也整个跑在浏览器里——任何人都能从
// tamias_viewer.js / 这几个 chunk 里把它读出来，或者在 DevTools 里写一句
// localStorage.setItem("tamias.session", ...) 直接绕过。
//
// 所以它只解决三件事：界面上有个登录、能退出、知道当前是谁。
// 它**不解决认证**。
//
// 真正的认证必须在服务端。现状见 docs/DECISION-SERVER-LANGUAGE.md §8.2：
// 服务端目前**无认证**（谁都能建会话、跑任意命令）。在服务端加上鉴权、
// 把凭据搬过去之前，不要把这个页面当成访问控制。

export interface Session {
  /** 登录名。 */
  user: string;
  /** 登录时间（epoch ms）。 */
  since: number;
  /** 过期时间（epoch ms）。到点自动作废，见 SESSION_TTL_MS。 */
  expires: number;
}

// 演示凭据。改这里就是改账号密码；接服务端鉴权时整个模块替换掉。
// **导出**是为了让登录页把它们默认填好（进来直接回车即可），别在两处各写一份。
export const DEMO_USER = "admin";
export const DEMO_PASSWORD = "admin";

// 用 **localStorage**：它是同源**所有标签页共享**的，所以登录之后开新标签页不用再登
// 一次。踩过的坑：原来用 sessionStorage，而它是**按标签页隔离**的，新开一个标签页必然
// 要求重新登录（每个标签页各自一份，互不可见）。
// 代价是它跨浏览器重启也留着——所以配一个有效期（见下）。
const STORAGE_KEY = "tamias.session";

// 登录有效期。**每次打开页面顺延一次**（滑动过期），所以常用的人不会掉线；放着不管
// 一周才会失效。这里没有服务端参与，纯粹是"别让它永远有效"。
//
// 为什么是一周：它是本地查看器工具、不是对外服务，登录态本来就在同源标签页之间共享
// （见上面 STORAGE_KEY 的说明），动不动就要求重登只会让人烦。代价说清楚——**共用电脑
// 上"退出登录"必须真的点**，否则一周内谁打开都能进。
const SESSION_TTL_MS = 7 * 24 * 60 * 60 * 1000; // 一周

/** 校验凭据。注意它是同步的——真正的鉴权会是一次网络往返。 */
export function verify(user: string, password: string): boolean {
  return user.trim() === DEMO_USER && password === DEMO_PASSWORD;
}

function write(session: Session | null): void {
  try {
    if (session === null) {
      window.localStorage.removeItem(STORAGE_KEY);
    } else {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(session));
    }
  } catch {
    // 隐私模式禁用存储 / 配额满：不影响本次会话，React state 里已经有一份了。
  }
}

/** 读会话。过期 / 字段不全都当作未登录（并顺手把脏数据清掉）。 */
export function loadSession(): Session | null {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (raw === null) {
      return null;
    }
    const parsed: unknown = JSON.parse(raw);
    if (typeof parsed !== "object" || parsed === null) {
      return null;
    }
    const { user, since, expires } = parsed as Partial<Session>;
    if (typeof user !== "string" || typeof since !== "number" || typeof expires !== "number") {
      write(null);
      return null;
    }
    if (expires <= Date.now()) {
      write(null);
      return null;
    }
    // 滑动过期：这一页打开了就顺延，省得用着用着突然被踢出去。
    const renewed: Session = { user, since, expires: Date.now() + SESSION_TTL_MS };
    write(renewed);
    return renewed;
  } catch {
    // 存进去的不是 JSON：当作未登录，不要因此白屏。
    return null;
  }
}

/** 登录：建会话、落盘、返回它（调用方拿它去 setState）。 */
export function startSession(user: string): Session {
  const now = Date.now();
  const session: Session = { user, since: now, expires: now + SESSION_TTL_MS };
  write(session);
  return session;
}

/**
 * 存储到底能不能用。隐私模式 / 站点数据被禁时 localStorage 会抛或写不进去，那时候
 * **登录状态根本留不住**——不管代码怎么写，每个标签页都会要求重新登录。所以这个要
 * 能问出来，好让登录页把话说清楚，而不是让人以为是登录逻辑写错了。
 */
export function storageAvailable(): boolean {
  try {
    if (typeof window === "undefined" || window.localStorage === undefined) {
      return false;
    }
    const probe = "__tamias_probe__";
    window.localStorage.setItem(probe, "1");
    window.localStorage.removeItem(probe);
    return true;
  } catch {
    return false;
  }
}

export function clearSession(): void {
  write(null);
}

/**
 * 跨标签页同步登录状态。别的标签页登录/登出时回调会被叫到。
 *
 * 注意 `storage` 事件**只在"别的"标签页里触发**，发起变更的那个标签页不会收到——
 * 所以调用方自己 setState，别指望这里兜底。另外同一标签页内的变更也不会触发它。
 */
export function subscribeSession(onChange: (session: Session | null) => void): () => void {
  const handler = (event: StorageEvent) => {
    if (event.key !== null && event.key !== STORAGE_KEY) {
      return;
    }
    // event.key === null 表示整个存储被 clear() 了，也当作登出。
    onChange(loadSession());
  };
  window.addEventListener("storage", handler);
  return () => window.removeEventListener("storage", handler);
}
