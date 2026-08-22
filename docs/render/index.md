# 渲染

一帧怎么画、RHI 双后端、屏外不发 draw。

- [管线与 RHI](../RENDERING.md) —— 从三角网到像素；RHI 抽象
- [视锥、NDC 与屏幕](../NDC.md) —— MVP 之后是立方体，不是一张纸
- [OpenGL 后端](../OPENGL.md) —— 窗口、VBO/IBO、draw、贴图绑定
- [wgpu 接入](../WGPU.md) —— 第三 RHI 后端方案（代码未落地）
- [Web 查看器](../WEB.md) —— 引擎 WASM + WebGL2（阶段 0/1）
- [视锥剔除](../FRUSTUM-CULLING.md) —— 屏外不发 draw（一期已落地）
