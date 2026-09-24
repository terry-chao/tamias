#pragma once

#include <cstdint>

namespace tamias {

// 文字的锚定空间。两类文字的渲染通路不同，别混（见 docs/TEXT.md §4）。
//
//  Screen：anchor_world 是**世界点**，投到屏幕后按**像素**排布——大小不随缩放变。
//          轴网编号 / 标高 / 尺寸 / 构件标签走这条。
//  World：文字躺在世界里的一个平面上（right/up 给基向量），大小是**世界单位**——
//          平面图房间名 / 图框标题 / 刻进楼板的字走这条。
enum class TextSpace : std::uint8_t {
  Screen = 0,
  World = 1,
};

}  // namespace tamias
