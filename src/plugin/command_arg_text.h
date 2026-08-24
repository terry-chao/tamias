#pragma once

#include "command/command_system.h"
#include "engine/core/result.h"

#include <string_view>

namespace tamias {

// Parse plugin dispatch args: `i:entity_id=3;d:radius=0.1;s:name=foo;v:origin=1,2,3`.
// Untyped `key=value` is inferred (int / double / vec3 / string).
[[nodiscard]] Result<CommandArgs> parse_command_arg_text(std::string_view text);

}  // namespace tamias
