#pragma once

namespace tamias {

enum class SceneDebugSkipReason {
  Drawn,
  Hidden,
  Isolated,
  Stepped,
  Culled,
};

}  // namespace tamias
