// 仓库自带的示例文档（assets/samples 下的 .tdoc / .trscn / .obj）。
//
// 用 Vite 的 glob 拿 URL（和工具带图标同一套机制），**双击时才 fetch**——不然一进
// 文件管理页就把那 3 个 2 MB 的 .trscn 全拉下来。
//
// ⚠️ 路径是**三层**：本文件在 web/src/files/，上去三层才到仓库根的 assets/。
// 写成两层会静默匹配到 0 个文件，而 build 照样通过（图标那边踩过一次，见
// ribbon/icons.tsx 顶部的注释）。
const SAMPLE_URLS = import.meta.glob("../../../assets/samples/**/*.{tdoc,trscn,obj}", {
  eager: true,
  query: "?url",
  import: "default",
}) as Record<string, string>;

export interface SampleDoc {
  /** 唯一 key（用相对路径，因为 basename 会重名：三个 scene.trscn）。 */
  key: string;
  name: string;
  kind: "tdoc" | "trscn" | "obj";
  /** 在仓库里的相对位置，给用户一点上下文（如 render/box）。 */
  where: string;
  url: string;
}

export function bundled_samples(): SampleDoc[] {
  const out: SampleDoc[] = [];
  for (const [path, url] of Object.entries(SAMPLE_URLS)) {
    // render/**/debug 里的 obj 是测试夹具，不当"示例文档"列出来。
    if (path.includes("/debug/")) {
      continue;
    }
    const rel = path.replace(/^.*\/assets\/samples\//, "");
    const name = rel.split("/").pop() ?? rel;
    const ext = (name.split(".").pop() ?? "").toLowerCase();
    if (ext !== "tdoc" && ext !== "trscn" && ext !== "obj") {
      continue;
    }
    out.push({
      key: rel,
      name,
      kind: ext,
      where: rel.includes("/") ? rel.slice(0, rel.lastIndexOf("/")) : "(根)",
      url,
    });
  }
  // 名字 → 位置，稳定排序，免得每次刷新列表都在跳。
  return out.sort((a, b) => a.name.localeCompare(b.name) || a.where.localeCompare(b.where));
}
