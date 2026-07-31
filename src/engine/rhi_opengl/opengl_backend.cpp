#include "opengl_backend.h"

#include "core/log.h"

namespace tamias {
namespace {

Result<std::unique_ptr<RHIDevice>> create_opengl_device(const DeviceCreateInfo&) {
  return Err("OpenGL backend is a stub (M4). Enable isolation path in a later milestone.");
}

}  // namespace

void register_opengl_backend() {
  log_info("Registering OpenGL RHI stub (not implemented)");
  register_backend(BackendModule{GraphicsBackend::OpenGL, create_opengl_device});
}

}  // namespace tamias
