// 顶栏形态的本地记忆。
//
// 键名**故意用桌面端的那两个**（`ui/ribbon_style`、`ui/ribbon_collapsed`，见
// src/app/shell/main_window.cpp 里的 settings 读写），这样两边对照时名字是一致的。
// 存 localStorage 而不是 sessionStorage：这是界面偏好，不该关掉标签页就忘。
import type { RibbonDisplayMode } from "../ribbon/types";

export const STYLE_KEY = "ui/ribbon_style";
export const COLLAPSED_KEY = "ui/ribbon_collapsed";

export function loadDisplayMode(): RibbonDisplayMode {
  try {
    return window.localStorage.getItem(STYLE_KEY) === "icons" ? "icons" : "text";
  } catch {
    return "text";
  }
}

export function loadCollapsed(): boolean {
  try {
    return window.localStorage.getItem(COLLAPSED_KEY) === "1";
  } catch {
    return false;
  }
}

export function remember(key: string, value: string): void {
  try {
    window.localStorage.setItem(key, value);
  } catch {
    // 隐私模式存不下：不影响本次会话，只是下次开不记得。
  }
}
