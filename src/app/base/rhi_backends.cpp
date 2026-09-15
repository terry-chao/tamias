#if defined(TAMIAS_HAS_RHI_VULKAN)
#include "engine/render/rhi/vulkan/vulkan_backend.h"
#endif
#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

namespace tamias {

void register_linked_rhi_backends() {
#if defined(TAMIAS_HAS_RHI_VULKAN)
  register_vulkan_backend();
#endif
#if defined(TAMIAS_HAS_RHI_OPENGL)
  register_opengl_backend();
#endif
}

}  // namespace tamias
