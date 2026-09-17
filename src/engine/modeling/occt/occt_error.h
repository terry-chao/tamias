#pragma once

#include "engine/base/result.h"

#include <Standard_Failure.hxx>
#include <Standard_Type.hxx>

#include <exception>
#include <string>

namespace tamias {

// OCCT 的构造器、布尔、离散、积分都会抛 Standard_Failure。内核边界是 Result：
// 调用方（求值器、open_file、TessWorker）都不接异常，所以每个出口都要过这里。
template <typename Fn>
auto guard_occt(const char* what, Fn&& fn) -> decltype(fn()) {
  try {
    return fn();
  } catch (const Standard_Failure& e) {
    return Err(std::string(what) + " failed: " + e.DynamicType()->Name());
  } catch (const std::exception& e) {
    return Err(std::string(what) + " failed: " + e.what());
  }
}

}  // namespace tamias
