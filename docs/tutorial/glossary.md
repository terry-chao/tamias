# 术语速查

> 教程里出现过的名词，一句话解释。按字母序排；括号里是首次出现的章节。

| 术语 | 一句话解释 |
|---|---|
| **AABB**（第 6 章） | 轴对齐包围盒，包住几何的「长方体」。比几何简单得多，用来做剔除和拾取判断 |
| **BIM**（第 1 章） | 建筑信息模型。带语义的建筑数据：墙、门、窗、楼层、属性 |
| **BRep**（第 5 章） | Boundary Representation。CAD 内核里的精确几何：参数化曲面 + 拓扑边/面 |
| **BVH**（第 8 章） | 包围盒层次结构，空间加速结构；Tamias 用它做 CPU 拾取 |
| **CMake Preset**（第 2 章） | 把 CMake 常用配置存成命名的预设，一行 `cmake --preset msvc` 即可配置 |
| **draw call**（第 8 章） | 一次「画这批三角」的 GPU 提交；数量多了是性能瓶颈（合批解决它） |
| **DrawItem / SceneDrawItem**（第 4 章） | 语义树展平后的一个绘制条目：网格 + 世界矩阵 + 材质 + 选中 |
| **Emscripten**（第 10 章） | 把 C++ 编译成 WebAssembly 的工具链 |
| **Extrude**（第 5 章） | 拉伸：把 2D 轮廓沿方向拉成 3D 实体 |
| **Feature（特征）**（第 5 章） | 配方里的一个步骤：类型 + 参数 + 依赖 |
| **FeatureModel（特征树）**（第 5 章） | 一串特征的集合，可求值、可序列化、是几何的「真相」 |
| **FrameSubmission**（第 8 章） | 视口交给渲染线程的「快递单」：窗口、相机、绘制清单 |
| **Frustum（视锥）**（第 8 章） | 相机能看到的四棱台（近/远/左/右/上/下六面） |
| **GpuMesh**（第 8 章） | 上传到显存后的网格：顶点缓冲 + 索引缓冲 |
| **IFC**（第 9 章） | 建筑的交换格式（类比 PDF），Tamias 用 IfcOpenShell 读 |
| **IGeometryBuilder**（第 5 章） | 配方 → 几何的接口（当前 OCCT 实现） |
| **IShapeOps**（第 5 章） | 文件 → 几何的接口：读 STEP/IGES/BREP 再离散 |
| **MCAD**（第 1 章） | 机械设计 CAD：零件、装配、布尔、倒角 |
| **MeshCpu**（第 8 章） | CPU 内存里的三角网：顶点 + 索引 |
| **NDC**（第 8 章） | 归一化设备坐标：透视除法后的立方体，xy 上屏、z 进深度缓冲 |
| **OCCT**（第 1 章） | Open CASCADE Technology，几何内核：BRep、布尔、离散化 |
| **push constants**（第 8 章） | 每画一次物体塞给 shader 的一小包数据（MVP、颜色等） |
| **RHI**（第 8 章） | Rendering Hardware Interface：把绘制代码和 Vulkan/OpenGL 隔离的抽象层 |
| **Scene（语义树）**（第 6 章） | 域无关的父子树：谁是谁的孩子、变换、包围盒 |
| **SceneNode**（第 6 章） | 语义树的一个节点：parent / local + world transform / mesh_asset_id |
| **tessellate**（第 5 章） | 离散化：把精确 BRep 变成 GPU 能画的三角网 |
| **TopoDS_Shape**（第 5 章） | OCCT 的形状类型；只在求值器内部出现，不外泄 |
| **triplanar（三平面贴图）**（第 8 章） | 用世界坐标往三个平面投影采样贴图，不需要 UV |
| **VBO / IBO**（第 8 章） | 顶点缓冲 / 索引缓冲：GPU 里的网格数据 |
| **WASM**（第 10 章） | WebAssembly：C++ 编译到浏览器运行的字节码 |
| **wgpu**（第 10 章） | WebGPU 的桌面实现，计划成为第三个 RHI 后端 |
| **Z-up / Y-up**（第 5 章） | 坐标约定：OCCT 是 Z 朝上，Tamias 视口是 Y 朝上，出网前要转换 |
| **拓扑命名**（第 5 章） | 让「给这条边倒角」在参数变化后仍然指向同一条边的难题（路线图 P3） |
| **插件宿主**（第 3、7 章） | 用 hostfxr 加载 C# DLL；插件只读文档快照，改模型必须 `Dispatch` 已有命令 |
| **HostApi**（插件） | C ABI 函数指针表（版本 1）：选择、实体、日志、登记命令、dispatch |
| **Dispatch**（插件） | 按命令名把参数交给 `CommandSystem`，和工具条走同一条撤销栈 |

回到[教程首页](index.md)。
