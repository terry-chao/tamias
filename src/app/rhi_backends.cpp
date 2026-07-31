#include "engine/rhi_vulkan/vulkan_backend.h"

#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/rhi_opengl/opengl_backend.h"
#endif

namespace tamias {

void register_linked_rhi_backends() {
  register_vulkan_backend();
#if defined(TAMIAS_HAS_RHI_OPENGL)
  register_opengl_backend();
#endif
}

}  // namespace tamias
