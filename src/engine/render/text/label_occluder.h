#pragma once

#include <cstddef>
#include <vector>

namespace tamias {

// 屏幕矩形占用表：标注抢位置时，先到的占住，后来的只要和已占的矩形（含 padding）
// 相交就放弃。轴号 / 标高 / 尺寸共用同一张表，就不会互相压住。
//
// 贪心 + O(n²)：一张图上标注是几百个量级，够用；真要更快再换网格哈希。
class LabelOccluder {
 public:
  explicit LabelOccluder(float padding = 2.f) : padding_(padding) {}

  // 试占一个矩形：不撞就记下并返回 true；撞了就返回 false（调用方跳过这个标注）。
  [[nodiscard]] bool try_reserve(float x, float y, float width, float height);
  void reset() { rects_.clear(); }
  [[nodiscard]] std::size_t count() const { return rects_.size(); }

 private:
  struct Rect {
    float x0 = 0.f;
    float y0 = 0.f;
    float x1 = 0.f;
    float y1 = 0.f;
  };

  float padding_ = 2.f;
  std::vector<Rect> rects_;
};

}  // namespace tamias
