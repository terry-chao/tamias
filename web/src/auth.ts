// 登录：**纯前端 UI 门禁，不是安全边界**。
//
// 凭据是写死在这个模块里的明文，判定也整个跑在浏览器里——任何人都能从
// tamias_viewer.js / 这几个 chunk 里把它读出来，或者在 DevTools 里写一句
// sessionStorage.setItem("tamias.session", ...) 直接绕过。
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
}

// 演示凭据。改这里就是改账号密码；接服务端鉴权时整个模块替换掉。
// **导出**是为了让登录页把它们默认填好（进来直接回车即可），别在两处各写一份。
export const DEMO_USER = "admin";
export const DEMO_PASSWORD = "admin";

// 用 sessionStorage 而不是 localStorage：关掉标签页就登出，且每个标签页各自一份。
// 查看器是本地工具，没有「记住我」的需求。
const STORAGE_KEY = "tamias.session";

/** 校验凭据。注意它是同步的——真正的鉴权会是一次网络往返。 */
export function verify(user: string, password: string): boolean {
  return user.trim() === DEMO_USER && password === DEMO_PASSWORD;
}

export function loadSession(): Session | null {
  try {
    const raw = window.sessionStorage.getItem(STORAGE_KEY);
    if (raw === null) {
      return null;
    }
    const parsed: unknown = JSON.parse(raw);
    if (typeof parsed !== "object" || parsed === null) {
      return null;
    }
    const { user, since } = parsed as Partial<Session>;
    if (typeof user !== "string" || typeof since !== "number") {
      return null;
    }
    return { user, since };
  } catch {
    // 隐私模式禁用存储 / 存进去的不是 JSON：当作未登录，不要因此白屏。
    return null;
  }
}

export function saveSession(session: Session): void {
  try {
    window.sessionStorage.setItem(STORAGE_KEY, JSON.stringify(session));
  } catch {
    // 存不下不影响本次会话：React state 里已经有一份了。
  }
}

export function clearSession(): void {
  try {
    window.sessionStorage.removeItem(STORAGE_KEY);
  } catch {
    // 同上。
  }
}
