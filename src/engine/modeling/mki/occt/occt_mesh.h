#pragma once

#include "engine/base/result.h"
#include "engine/graphics/mesh.h"

#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <functional>

namespace tamias {

// BRep → MeshCpu 的唯一实现。求值路径（occt_kernel.cpp）与导入路径
// （occt_shape_ops.cpp）都走这里：BRepMesh 离散、按面抽三角、绕序/法向修正、
// Y-up 转换、包围盒重算只有一份，修 bug 不用改两个地方。
//
// face_color 逐面给顶点色：求值路径给白色让材质 base_color 透出，导入路径查 XCAF。
// copy_uv 只在导入路径打开（纹理要用 UV）。
[[nodiscard]] Result<MeshCpu> tessellate_brep(
    const TopoDS_Shape& shape, double linear_deflection,
    const std::function<Vec3(const TopoDS_Face&)>& face_color, bool copy_uv);

}  // namespace tamias
