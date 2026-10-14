// 工具带 / 菜单图标。
//
// **直接用桌面端那一套 SVG**：`assets/icons/*.svg`，资源清单在 `src/app/resources.qrc`。
// 以前 web 这边是照语义自己重画了一套，两边会慢慢长歪——现在只有一份来源。
//
// 为什么用 <img> 而不是把 SVG 内联进 DOM：
//   1. 这 63 个图标是**全彩插画**（渐变 + 硬编码颜色，见 wall.svg / undo.svg），不是
//      单色字形，本来就不吃 currentColor，内联换不到任何好处；
//   2. 好几个 SVG 里都有 `id="top"` / `id="left"` / `id="right"` 这类通用 id，
//      内联到同一个文档里 `url(#top)` 会互相串色。<img> 各自是独立文档，天然隔离。
//
// 文件很小（63 个共约 53 KB，最大的 1.6 KB），Vite 会把它们内联成 data URL，
// 所以浏览器不会为了图标多发请求。
//
// ⚠️ 路径是**三层**：本文件在 web/src/ribbon/，上去三层才到仓库根的 assets/。
// 写成两层会静默匹配到 0 个文件（图标全变占位符），而 build 依然通过——别凭感觉改。
// import.meta.glob 的参数必须是字符串字面量（Vite 要静态分析），所以下面的 ICON_DIR
// 只用于运行时拼 key，改路径时两个地方要一起改。
const ICON_DIR = "../../../assets/icons/";
const DESKTOP_ICONS = import.meta.glob("../../../assets/icons/*.svg", {
  eager: true,
  query: "?url",
  import: "default",
}) as Record<string, string>;

function desktop_icon_url(name: string): string | null {
  return DESKTOP_ICONS[`${ICON_DIR}${name}.svg`] ?? null;
}

/** 冒烟用：glob 到底匹配到几个图标（0 就是路径写错了）。 */
export function desktop_icon_count(): number {
  return Object.keys(DESKTOP_ICONS).length;
}

/**
 * 图标名 = `assets/icons` 里的 basename（`wall` / `undo` / `frame_all` …），
 * 或者下面 CHROME 里这几个只在 web 用的装饰性字形。
 * 名字写错不会静默：会画一个可见的占位并 warn 一次。
 */
export type IconName = string;

/** 桌面端没有对应资源的装饰性字形（搜索框 / 卷起箭头 / 退出 / 删除）。 */
const CHROME: Record<string, React.ReactNode> = {
  // 桌面端的「删除」没有独立图标资源（走菜单/快捷键），web 这个按钮得自己画一个。
  delete: (
    <>
      <path d="M5 7h14" />
      <path d="M9 7V4h6v3" />
      <path d="M7 7l1 13h8l1-13" />
      <path d="M10 11v6M14 11v6" />
    </>
  ),
  search: (
    <>
      <circle cx="11" cy="11" r="6" />
      <path d="M15.5 15.5L21 21" />
    </>
  ),
  collapse: <path d="M6 14.5l6-6 6 6" />,
  expand: <path d="M6 9.5l6 6 6-6" />,
  // 关闭（文档 chip 上的 ✕）。桌面端关标签页用的是 QTabBar 自带的叉，没有图标资源。
  close: <path d="M6 6l12 12M18 6L6 18" />,
  logout: (
    <>
      <path d="M15 4h3a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-3" />
      <path d="M10 12H3" />
      <path d="M7 8l-4 4 4 4" />
    </>
  ),
};

interface IconProps {
  name: IconName;
  /** 边长（px）。工具带按钮用 24，菜单行角落用 16。 */
  size?: number;
}

// 名字写错只报一次，免得每帧刷屏。
const warned = new Set<string>();

export default function Icon({ name, size = 24 }: IconProps) {
  const url = desktop_icon_url(name);
  if (url !== null) {
    return (
      <img
        className="ico"
        src={url}
        width={size}
        height={size}
        alt=""
        aria-hidden="true"
        draggable={false}
      />
    );
  }

  const chrome = CHROME[name];
  if (chrome !== undefined) {
    return (
      <svg
        className="ico"
        width={size}
        height={size}
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        strokeWidth={1.6}
        strokeLinecap="round"
        strokeLinejoin="round"
        aria-hidden="true"
        focusable="false"
      >
        {chrome}
      </svg>
    );
  }

  if (!warned.has(name)) {
    warned.add(name);
    console.warn(
      `[tamias] 未知图标 "${name}"：assets/icons 里没有，也不在装饰性字形里。`,
    );
  }
  return (
    <span
      className="ico ico-missing"
      style={{ width: size, height: size }}
      title={`缺少图标：${name}`}
    />
  );
}
