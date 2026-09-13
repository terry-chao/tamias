// WASM 预览用的极简静态服务器。不依赖 `npx serve`（离线可用、启动快），
// 并且强制 no-store —— 预览场景最怕浏览器缓存住旧的 tamias_viewer.wasm。
//
// 用法：node scripts/serve-wasm.mjs [目录] [端口]
import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import { extname, isAbsolute, join, relative, resolve } from "node:path";

const root = resolve(process.argv[2] ?? "build/wasm/bin");
const port = Number(process.argv[3] ?? 3000);

const types = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".wasm": "application/wasm",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".ico": "image/x-icon",
  ".ttf": "font/ttf",
  ".woff2": "font/woff2",
};

const server = createServer(async (req, res) => {
  let urlPath = decodeURIComponent((req.url ?? "/").split("?")[0]);
  if (urlPath.endsWith("/")) {
    urlPath += "index.html";
  }
  const filePath = resolve(join(root, urlPath));
  const rel = relative(root, filePath);
  if (rel.startsWith("..") || isAbsolute(rel)) {
    res.writeHead(403).end("forbidden");
    return;
  }
  try {
    const info = await stat(filePath);
    if (!info.isFile()) {
      res.writeHead(404).end("not found");
      return;
    }
    const body = await readFile(filePath);
    res.writeHead(200, {
      "Content-Type": types[extname(filePath).toLowerCase()] ?? "application/octet-stream",
      "Cache-Control": "no-store",
    });
    res.end(body);
  } catch {
    res.writeHead(404).end("not found");
  }
});

server.listen(port, () => {
  console.log(`[tamias] preview ready on http://localhost:${port}`);
  console.log(`[tamias] serving ${root}`);
  console.log("[tamias] 关掉这个窗口即停止预览");
});
