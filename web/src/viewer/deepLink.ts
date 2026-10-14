// 文档的 URL 深链：`?doc=sample:<key>`。
//
// **只有仓库自带的示例能这样分享**——它们在 `assets/` 下有真 URL。本地文件浏览器不给
// 路径，写不进 URL；要在新标签页里继续用同一份本地文件，得把 `FileSystemFileHandle`
// 存进 IndexedDB（那是另一条路，见 docs/WEB.md 的文档模型一节）。
//
// 用 `history.replaceState` 而不是 pushState：切文档不该在浏览器历史里堆一步——按返回键
// 应该是"离开这个页面"，而不是"回到上一份文档"。
const PARAM = "doc";
const SAMPLE_PREFIX = "sample:";

/** 从当前地址读示例 key。没有 / 不是示例写法 / 在 SSR 里 → null。 */
export function readDeepLinkSampleKey(): string | null {
  if (typeof window === "undefined") {
    return null;
  }
  try {
    const raw = new URLSearchParams(window.location.search).get(PARAM);
    if (raw === null || !raw.startsWith(SAMPLE_PREFIX)) {
      return null;
    }
    const key = raw.slice(SAMPLE_PREFIX.length);
    return key === "" ? null : key;
  } catch {
    // 地址被搞坏时当作没有深链，不要让整个页面挂掉。
    return null;
  }
}

/** 卡片的 `href`（也是「在新标签页打开」用的地址）。 */
export function sampleHref(key: string): string {
  return `?${PARAM}=${SAMPLE_PREFIX}${encodeURIComponent(key)}`;
}

/**
 * 把地址栏同步成当前文档。`null` = 回到干净地址（关闭文档、或打开的是一份本地文件——
 * 后者没法用 URL 表示，地址栏不能撒谎）。
 */
export function syncDeepLink(key: string | null): void {
  if (typeof window === "undefined" || typeof window.history?.replaceState !== "function") {
    return;
  }
  try {
    window.history.replaceState(null, "", key === null ? window.location.pathname : sampleHref(key));
  } catch {
    // file:// 或沙箱里 replaceState 可能被拒：不影响功能，只是地址栏不同步。
  }
}
