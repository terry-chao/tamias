#pragma once

#include <string>
#include <string_view>

namespace tamias {

// UTF-8 → 码点。非法序列各产出一个 U+FFFD：
//  - 结构完整但码位非法（过长编码 / 代理区 / 超范围）：整段吞掉，一个替换符；
//  - 结构不完整（截断序列 / 续字节非法）：只吞一个字节，后面的内容继续解——
//    图纸或插件塞进来的脏字节不该把整段文字吃掉。
[[nodiscard]] std::u32string decode_utf8(std::string_view text);

}  // namespace tamias
