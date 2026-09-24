#pragma once

#include "engine/render/text/stb_font.h"

#include <memory>
#include <string_view>
#include <vector>

namespace tamias {

// 一段文字该用哪套字体：返回**第一套每个码位都有字形**的字体。
// 一套都放不下时返回 nullptr——调用方自己决定是回落到主字体画豆腐块，还是干脆不画。
//
// 场景很具体：中文界面里楼层叫「一层」，拉丁字体没这几个字，得回落到 CJK 字体。
// 把回落做成显式的、可测的一步，比在渲染里瞎猜强。
[[nodiscard]] const StbFont* pick_font_for_text(std::string_view utf8,
                                                const std::vector<std::shared_ptr<StbFont>>& fonts);

}  // namespace tamias
