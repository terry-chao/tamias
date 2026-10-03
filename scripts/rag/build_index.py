#!/usr/bin/env python3
"""构建 RAG 检索索引：docs/**/*.md -> resources/rag/{chunks.jsonl,manifest.json}

见 docs/PLAN-RAG.md §3 M0。两条硬约束：

1. **anchor 与官网一字不差**。所以不自己写 slugify，而是把整页喂给与 mkdocs
   同一套 markdown 实例，从 toc_tokens 里取 id——连重复标题的 `_1` 后缀都一致。
   注意中文标题在官网上本来就会被剥成 ASCII 残余（`## 1. 结论` -> `#1`），
   这里是照抄，不是 bug，别"顺手修好"，否则回链就点不开了。
2. **产物跨提交可 diff**。chunks.jsonl 完全由 docs 决定，不含任何 git 历史信息，
   所以它在任何提交里都稳定 —— CI 新鲜度检查只比它。
   manifest 里的 git_rev / built_at 是构建记录，**天然不可能与自己所在的那次提交
   自洽**（提交它会改掉 HEAD，重跑必 diff），所以那两个字段不参与那个检查。
   built_at 仍取 HEAD 的提交时间而非当前时间，是为了让同一 HEAD 下重跑幂等。

用法:
    python scripts/rag/build_index.py --docs docs --out resources/rag
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import markdown
    import yaml
except ImportError:  # pragma: no cover - 环境问题，给一句能照做的话
    sys.exit(
        "需要 mkdocs 的 Python 环境：pip install -r requirements.txt\n"
        "（脚本复用 markdown 与 PyYAML 来保证 anchor 与官网一致）"
    )

from markdown.extensions.toc import TocExtension

# 切块目标：正文 300–600 "词单位"（汉字按字、ASCII 按词）。
TARGET_UNITS = 600
MIN_UNITS = 120
# 只在 ## / ### 切；#### 及更深留在父块里。H1 是页标题，不是切点。
BOUNDARY_LEVELS = (2, 3)
# 参与面包屑的标题层级。
CRUMB_LEVELS = (2, 3)

CORPUS = "docs"
FIELDS = ("id", "corpus", "path", "anchor", "title", "breadcrumb", "lang", "url", "text")

# mkdocs 给每个站点强加的扩展，见 mkdocs/config/defaults.py 的 builtins。
MKDOCS_BUILTIN_EXTENSIONS = ("toc", "tables", "fenced_code")

_FENCE_RE = re.compile(r"^\s{0,3}(`{3,}|~{3,})")
_ATX_RE = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
_CJK_RE = re.compile(r"[㐀-䶿一-鿿豈-﫿]")
_WORD_RE = re.compile(r"[A-Za-z0-9_]+")


# —— 文本度量 ——


def units(text: str) -> int:
    """汉字按字、ASCII 按词，得到一个跨中英可比的长度。"""
    return len(_CJK_RE.findall(text)) + len(_WORD_RE.findall(text))


def detect_lang(text: str) -> str:
    return "zh" if len(_CJK_RE.findall(text)) >= len(_WORD_RE.findall(text)) else "en"


# —— mkdocs 配置 ——


def load_config(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return yaml.safe_load(handle) or {}


def build_markdown(config: dict) -> markdown.Markdown:
    """按 mkdocs 的实际扩展集装 Markdown —— anchor 由 toc 决定，必须同源。

    内建三个是 mkdocs 强加给每个站点的（`mkdocs/config/defaults.py` 里
    `builtins=['toc', 'tables', 'fenced_code']`），用户配置只能叠加。
    `fenced_code` 尤其不能漏：少了它，代码块里的 `# 注释` 会被当成标题。
    """
    entries = config.get("markdown_extensions") or []
    toc_options = {}
    for entry in entries:
        if isinstance(entry, dict) and entry.get("toc"):
            toc_options = entry["toc"]

    extensions: list = [
        TocExtension(**toc_options) if name == "toc" else name
        for name in MKDOCS_BUILTIN_EXTENSIONS
    ]
    seen = set(MKDOCS_BUILTIN_EXTENSIONS)
    for entry in entries:
        if isinstance(entry, str):
            if entry not in seen:
                seen.add(entry)
                extensions.append(entry)
        elif isinstance(entry, dict):
            for name, options in entry.items():
                if name in seen:
                    continue
                seen.add(name)
                extensions.append(TocExtension(**(options or {})) if name == "toc" else name)
    return markdown.Markdown(extensions=extensions)


def nav_breadcrumbs(config: dict) -> dict[str, list[str]]:
    """nav 树 -> {相对 docs 的路径: [分组名, ...]}。

    分组名末尾的「（英文）」剥掉：PLAN-RAG §4 的示例面包屑是「插件 > API 参考」，
    而 nav 里写的是「API 参考（API Reference）」。
    """
    crumbs: dict[str, list[str]] = {}

    def walk(node, trail: list[str]) -> None:
        if isinstance(node, str):
            crumbs[_norm_path(node)] = trail
        elif isinstance(node, list):
            for item in node:
                walk(item, trail)
        elif isinstance(node, dict):
            for label, child in node.items():
                clean = re.sub(r"（[^）]*）\s*$", "", str(label)).strip()
                walk(child, trail + ([clean] if clean else []))

    walk(config.get("nav") or [], [])
    return crumbs


def _norm_path(value: str) -> str:
    return value.replace("\\", "/").lstrip("./")


# —— 页面 -> 标题、祖先链、anchor ——


def extract_headings(text: str) -> list[dict]:
    """逐行扫出 ATX 标题，跳过围栏代码块。返回 [{line, level, title}]。"""
    headings: list[dict] = []
    fence: str | None = None
    for number, line in enumerate(text.splitlines()):
        match = _FENCE_RE.match(line)
        if match:
            marker = match.group(1)[0]
            if fence is None:
                fence = marker
            elif fence == marker:
                fence = None
            continue
        if fence is not None:
            continue
        atx = _ATX_RE.match(line)
        if atx:
            headings.append(
                {"line": number, "level": len(atx.group(1)), "title": atx.group(2).strip()}
            )
    return headings


def heading_table(text: str, md: markdown.Markdown) -> list[tuple[int, str]]:
    """整页走一遍 markdown，按文档顺序取 (层级, id)。

    走 markdown 而不是自己 slugify，是为了连重复标题的 `_1` 后缀都与官网一致。
    """
    md.reset()
    md.convert(text)
    table: list[tuple[int, str]] = []

    def walk(tokens) -> None:
        for token in tokens:
            table.append((token["level"], token["id"]))
            walk(token["children"])

    walk(md.toc_tokens)
    return table


def scan_page(page: str, text: str, md: markdown.Markdown) -> list[dict]:
    """标题 + 祖先链 + anchor。三者对不齐就报错——宁可炸也不能产出点不开的链接。"""
    headings = extract_headings(text)
    table = heading_table(text, md)
    if len(table) != len(headings) or any(
        level != heading["level"] for (level, _), heading in zip(table, headings)
    ):
        raise ValueError(
            f"逐行扫描到 {len(headings)} 个标题，markdown 解析出 {len(table)} 个；"
            "多半是 setext 标题或缩进代码块。补规则，不要绕过。"
        )
    for heading, (_, anchor) in zip(headings, table):
        heading["anchor"] = anchor

    # 祖先链：进入某个标题时，比它深或同级的记录作废。
    seen: dict[int, dict] = {}
    for heading in headings:
        if heading["level"] <= max(CRUMB_LEVELS):
            for level in [lvl for lvl in seen if lvl >= heading["level"]]:
                del seen[level]
            heading["trail"] = [seen[lvl]["title"] for lvl in sorted(seen) if lvl >= 2]
            seen[heading["level"]] = heading
        else:
            heading["trail"] = [seen[lvl]["title"] for lvl in sorted(seen) if lvl >= 2]
    return headings


# —— 切块 ——


def blocks_between(lines: list[str]) -> list[tuple[int, int]]:
    """把行区间切成块（行号半开区间）。空行分隔，围栏代码块整体成块。"""
    blocks: list[tuple[int, int]] = []
    start: int | None = None
    fence: str | None = None
    for index, line in enumerate(lines):
        match = _FENCE_RE.match(line)
        if fence is None:
            if match:
                if start is None:
                    start = index
                fence = match.group(1)[0]
            elif line.strip():
                if start is None:
                    start = index
            elif start is not None:
                blocks.append((start, index))
                start = None
        elif match and match.group(1)[0] == fence:
            fence = None
    if start is not None:
        blocks.append((start, len(lines)))
    return blocks


def pack(lines: list[str], start: int, stop: int) -> list[str]:
    """把一个 section 的行按块打包，超长就在块边界断开——代码块与表格不会被切开。"""
    body = lines[start:stop]
    if units("\n".join(body)) <= TARGET_UNITS:
        text = "\n".join(body).strip("\n")
        # 只有分隔线之类的 section 检不出任何东西，留着纯属噪声。
        return [text] if units(text) else []

    pieces: list[str] = []
    current: list[str] = []
    current_units = 0
    for first, last in blocks_between(body):
        block = "\n".join(body[first:last]).strip("\n")
        size = units(block)
        if current and current_units + size > TARGET_UNITS and current_units >= MIN_UNITS:
            pieces.append("\n\n".join(current).strip("\n"))
            current, current_units = [], 0
        current.append(block)
        current_units += size
    if current:
        pieces.append("\n\n".join(current).strip("\n"))
    return [piece for piece in pieces if units(piece)]


def page_url(rel: str, site_url: str) -> str:
    """docs/plugin/api/host.md -> <site>/plugin/api/host/（mkdocs 的目录式 URL）。"""
    path = rel[:-3] if rel.endswith(".md") else rel
    if path == "index":
        path = ""
    elif path.endswith("/index"):
        path = path[: -len("/index")]
    return f"{site_url}{path}/"


# 首页由 overrides/home.html 渲染，模板里没有 {{ page.content }}，
# 所以 docs/index.md 的标题在官网 HTML 里没有 id —— 给它的块带 fragment 就是死链。
PAGES_WITHOUT_RENDERED_CONTENT = {"index.md"}


def chunk_page(rel: str, text: str, crumbs: list[str], site_url: str, md) -> list[dict]:
    headings = scan_page(rel, text, md)
    lines = text.splitlines()

    h1 = next((h for h in headings if h["level"] == 1), None)
    nav_leaf = crumbs[-1] if crumbs else (h1["title"] if h1 else Path(rel).stem)
    boundaries = [h for h in headings if h["level"] in BOUNDARY_LEVELS]
    first_boundary = boundaries[0]["line"] if boundaries else len(lines)

    # (正文起始行, 结束行, 本节标题, anchor, 面包屑尾段)
    sections: list[tuple[int, int, str, str, list[str]]] = [
        (
            (h1["line"] + 1) if h1 is not None and h1["line"] < first_boundary else 0,
            first_boundary,
            nav_leaf,
            h1["anchor"] if h1 is not None else "",
            [],
        )
    ]
    for index, heading in enumerate(boundaries):
        stop = boundaries[index + 1]["line"] if index + 1 < len(boundaries) else len(lines)
        trail = list(heading["trail"]) + [heading["title"]]
        sections.append((heading["line"] + 1, stop, heading["title"], heading["anchor"], trail))

    chunks: list[dict] = []
    for start, stop, title, anchor, trail in sections:
        # nav 分组 + 标题栈；nav 叶子已经是本页时不再重复一遍
        parts = list(crumbs)
        if not (trail and crumbs and trail[-1] == crumbs[-1]):
            parts += trail if trail else [title]
        breadcrumb = " > ".join(dict.fromkeys(parts)) if parts else title
        has_anchor = bool(anchor) and rel not in PAGES_WITHOUT_RENDERED_CONTENT
        url = page_url(rel, site_url) + (f"#{anchor}" if has_anchor else "")
        for piece in pack(lines, start, stop):
            chunks.append(
                {
                    "corpus": CORPUS,
                    "path": rel,
                    "anchor": anchor if has_anchor else "",
                    "title": title,
                    "breadcrumb": breadcrumb,
                    "lang": detect_lang(piece),
                    "url": url,
                    "text": piece,
                }
            )

    for number, chunk in enumerate(chunks, start=1):
        chunk["id"] = f"{rel}#{number}"
    return [{key: chunk[key] for key in FIELDS} for chunk in chunks]


# —— git ——


def git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], capture_output=True, text=True, check=True
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ""


def build_stamp() -> tuple[str, str]:
    """(短 rev, ISO 时间)。时间取 HEAD 提交时间 —— 重跑必须产出同一份 manifest。"""
    rev = git("rev-parse", "--short", "HEAD") or "unknown"
    stamp = git("log", "-1", "--format=%cI")
    if stamp:
        return rev, stamp
    return rev, datetime.now(timezone.utc).replace(microsecond=0).isoformat()


# —— 主流程 ——


def main() -> int:
    parser = argparse.ArgumentParser(description="构建 RAG 索引（docs -> chunks.jsonl）")
    parser.add_argument("--docs", default="docs", type=Path)
    parser.add_argument("--mkdocs", default="mkdocs.yml", type=Path)
    parser.add_argument("--out", default="resources/rag", type=Path)
    args = parser.parse_args()

    config = load_config(args.mkdocs)
    site_url = config.get("site_url", "")
    if not site_url.endswith("/"):
        site_url += "/"
    crumbs_by_path = nav_breadcrumbs(config)
    md = build_markdown(config)

    # 按归一化后的相对路径排序，**不要**直接 sorted(Path)：Windows 上 Path 的比较
    # 大小写不敏感、Linux 上敏感，`APP.md` 与 `app/` 这类会排出不同顺序，
    # 于是 M4 的「重跑后 git diff --exit-code」在别的平台上就炸了。
    pages = sorted(
        (p for p in args.docs.rglob("*.md") if p.is_file()),
        key=lambda p: _norm_path(str(p.relative_to(args.docs))),
    )
    if not pages:
        raise SystemExit(f"{args.docs} 下没有 .md 文件")

    chunks: list[dict] = []
    missing_nav: list[str] = []
    broken: list[str] = []
    for page in pages:
        rel = _norm_path(str(page.relative_to(args.docs)))
        text = page.read_text(encoding="utf-8")
        crumbs = crumbs_by_path.get(rel)
        if crumbs is None:
            missing_nav.append(rel)
            crumbs = []
        try:
            chunks.extend(chunk_page(rel, text, crumbs, site_url, md))
        except ValueError as error:
            broken.append(f"{rel}: {error}")

    if broken:
        print("以下页面标题解析对不齐，未产出索引：", file=sys.stderr)
        for line in broken:
            print(f"  - {line}", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    body = "".join(json.dumps(c, ensure_ascii=False) + "\n" for c in chunks)
    (args.out / "chunks.jsonl").write_text(body, encoding="utf-8", newline="\n")

    rev, stamp = build_stamp()
    manifest = {
        "schema": 1,
        "git_rev": rev,
        "built_at": stamp,
        "chunks": len(chunks),
        "corpora": [{"name": CORPUS, "files": len(pages), "chunks": len(chunks)}],
        "embedding": None,
    }
    (args.out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n"
    )

    sizes = sorted(units(c["text"]) for c in chunks)
    print(f"{len(pages)} 篇 -> {len(chunks)} 块，写入 {args.out}")
    print(
        f"块长单位：min {sizes[0]} / p50 {sizes[len(sizes) // 2]} / "
        f"p90 {sizes[int(len(sizes) * 0.9)]} / max {sizes[-1]}"
    )
    print(f"内容 sha256 {hashlib.sha256(body.encode()).hexdigest()[:16]}（重跑应不变）")
    if missing_nav:
        print(f"警告：{len(missing_nav)} 篇不在 mkdocs.yml nav 里，面包屑缺分组：")
        for rel in missing_nav[:10]:
            print(f"  - {rel}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
