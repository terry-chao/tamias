// 登录页。校验逻辑与「这不是安全边界」的说明见 auth.ts 顶部。
import { useState } from "react";

import { DEMO_PASSWORD, DEMO_USER, verify } from "./auth";

interface LoginPageProps {
  /** 校验通过后回调，由 App 负责建立会话。 */
  onAuthenticated: (user: string) => void;
}

export default function LoginPage({ onAuthenticated }: LoginPageProps) {
  // 演示凭据**默认填好**，进来直接回车就进（这也是为什么下面还有一行明写着凭据——
  // 反正它在 bundle 里，藏起来只是自欺欺人）。
  const [user, setUser] = useState(DEMO_USER);
  const [password, setPassword] = useState(DEMO_PASSWORD);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = (event: React.FormEvent) => {
    event.preventDefault();
    if (busy) {
      return;
    }
    const name = user.trim();
    if (name === "" || password === "") {
      setError("请输入账号和密码");
      return;
    }
    setBusy(true);
    // 凭据在本地比对，没有网络往返；留一个 tick 只是为了让「登录中…」能被画出来。
    // 换成服务端鉴权时，这里就是那个 await fetch。
    window.setTimeout(() => {
      if (verify(name, password)) {
        setError(null);
        onAuthenticated(name);
        return;
      }
      setBusy(false);
      setPassword("");
      setError("账号或密码不正确");
    }, 0);
  };

  return (
    <div className="login">
      <form className="login-card" onSubmit={submit}>
        <div className="login-brand">
          <strong>Tamias</strong>
          <span>引擎 WASM · Web 查看器</span>
        </div>

        <label className="login-field">
          <span>账号</span>
          <input
            type="text"
            value={user}
            autoFocus
            spellCheck={false}
            autoComplete="username"
            onChange={(event) => {
              setUser(event.target.value);
              setError(null);
            }}
          />
        </label>

        <label className="login-field">
          <span>密码</span>
          <input
            type="password"
            value={password}
            autoComplete="current-password"
            onChange={(event) => {
              setPassword(event.target.value);
              setError(null);
            }}
          />
        </label>

        {error !== null && (
          <div className="login-error" role="alert">
            {error}
          </div>
        )}

        <button type="submit" className="btn login-submit" disabled={busy}>
          {busy ? "登录中…" : "登录"}
        </button>

        {/* 凭据就在 bundle 里，藏起来是自欺欺人。接入服务端鉴权后删掉这行。 */}
        <div className="login-note">
          演示凭据：<code>admin</code> / <code>admin</code>
        </div>
      </form>
    </div>
  );
}
