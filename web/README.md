# Tamias Web UI（Vite + React + TypeScript）

浏览器查看器的 UI 壳。引擎（C++ → WASM）在 `src/wasm/`，本项目只负责界面和交互，
通过 embind 暴露的 `createTamiasViewer()` 与引擎通信，不依赖 Qt。

## 开发

```sh
npm install        # 首次安装依赖（项目内 node_modules 不入库）
npm run dev        # 需要先构建 wasm 预设，脚本会把产物拷到 public/
```

## 构建

```sh
npm run build      # tsc 类型检查 + vite build，产物在 dist/
```

wasm 预设的 CMake 构建会自动执行上面的构建并把 `dist/` 拷进 `build/wasm/bin/`，
与 `tamias_viewer.js` / `.wasm` 放在同一目录。也可以手动拷：

```sh
npm run build
Copy-Item dist\* ..\build\wasm\bin\ -Recurse -Force
```

## 结构

- `src/viewer.ts` — WASM 模块的类型声明（与 `viewer_main.cpp` 的 embind 导出对应）和加载器
- `src/App.tsx` — 界面与交互（工具栏、文件打开、拖放、状态栏、键盘快捷键）
- `src/styles.css` — 深色主题样式
- `scripts/copy-wasm.mjs` — 把 `build/wasm/bin` 的 WASM 产物拷到 `public/` 供 dev server 使用
