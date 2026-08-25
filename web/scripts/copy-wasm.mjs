// 把 build/wasm/bin 下的 WASM 产物拷到 public/，供 vite dev 使用。
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const wasmBin = join(root, "build", "wasm", "bin");
const publicDir = join(root, "web", "public");

mkdirSync(publicDir, { recursive: true });

for (const name of ["tamias_viewer.js", "tamias_viewer.wasm"]) {
  const src = join(wasmBin, name);
  if (!existsSync(src)) {
    console.warn(`[copy-wasm] 未找到 ${name}，请先构建 wasm 预设（CMake preset: wasm）`);
    continue;
  }
  copyFileSync(src, join(publicDir, name));
  console.log(`[copy-wasm] ${name} -> public/`);
}
