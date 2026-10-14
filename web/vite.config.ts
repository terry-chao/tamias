import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

export default defineConfig({
  plugins: [react()],
  // 相对路径，保证 index.html 与 tamias_viewer.js/.wasm 同目录时可任意部署。
  base: "./",
  server: {
    port: 5173,
    fs: {
      // 工具带图标直接用桌面端那一套（../../../assets/icons/*.svg，在 web/ 之外），
      // dev server 默认不允许读 root 之外的文件，得显式放行。
      allow: [".."],
    },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
