# 明确未完成

当前为可用的 mesh / CAD 查看器骨架（v0.1.0，M0–M5 已完成）。下列能力尚未实现：

## 保存 / 导出

只能打开文件，不能写回任何格式。

## 真正建模

无布尔、圆角、拉伸、草图等操作；`IShapeOps` 目前只有 `read_file` + `tessellate`。

## ASCII `.gltf`

打开对话框可选 `*.gltf`，但加载会失败（仅支持 `.glb` / `.obj`）。需补 ASCII glTF，或从过滤器中去掉该扩展名。

## 纹理 / 材质

两端 RHI 的 `create_texture` 仍为占位；无贴图采样、无 PBR 材质管线。

## 装配树

OCCT 导入后打成单一 mesh，无零件 / 场景节点层级。

## 平台

- 无 D3D12 / Metal / macOS
- Linux 仅 X11，无 Wayland

# 半成品 / 退化

## 显示模式

渲染管线仍有线框 / 着色 / 真实模式，但 UI / 快捷键已去掉，`set_render_mode` 基本无调用方。

## GLB

自研最小解析，不是完整 glTF。

## 文档模型

`document` / `scene` / `asset` 偏薄，无序列化、无 dirty / 撤销。

## Truck

`shape_ops.h` 里仍写着以后可插，尚未落地。

## MeshShape

仍作 mesh 占位；注释写 “until OCCT arrives” 已过时。

## RHI Texture API

接口在，两端实现仍返回占位错误。

## vcpkg 依赖

`fastgltf` / `spdlog` 已声明，Windows 路径未用（自研 GLB + 自写 log）。

## OCCT

可选依赖；无 `OCCT_ROOT` 时 CAD 格式不可用。

## i18n

`.ts` 与源码不同步（显示模式等字符串成死翻译）。

# 体验缺口

## UI 能力

无属性面板、大纲、测量、撤销 / 重做、新建空文档（仅打开 + Alvin demo）。

## 语言切换

需重启应用后生效。

## 测试

仅少量 smoke（无真实文件 IO、无 GPU / UI 回归）。

## 文档 / 路线图

README 停在 M5，无 M6+ / ROADMAP；无 `AGENTS.md` / `CONTRIBUTING.md` / `docs/`。
