# 渲染

一帧怎么画、RHI 后端、屏外不发 draw。

- [管线与 RHI](../RENDERING.md) —— 从三角网到像素；RHI 抽象
- [视锥、NDC 与屏幕](../NDC.md) —— MVP 之后是立方体，不是一张纸
- [OpenGL 后端](../OPENGL.md) —— 窗口、VBO/IBO、draw、贴图绑定
- [浏览器 WebGPU](../WGPU.md) —— WASM 查看器的 RHI（桌面不做 wgpu-native）
- [Web 查看器](../WEB.md) —— 引擎 WASM + React 壳
- [视锥剔除](../FRUSTUM-CULLING.md) —— 屏外不发 draw（一期已落地）
- [渲染场景快照](../RENDER-SCENE.md) —— `.trscn`：烤好的 CPU 场景，给调试和测试
