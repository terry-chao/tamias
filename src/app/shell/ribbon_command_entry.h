#pragma once

#include <QString>

class QAction;

namespace tamias {

// 工具带搜索里的一条命令。
//
// 名字直接用 action 的 text（画出来时去掉助记符 &），这里只补两样：
// 它在界面上的**出处**（「分区 · 分组」，用来认人）与**关键字**——除了名字之外还想
// 让人搜到的词（悬浮提示等）。快捷键由搜索框自己补进关键字，不用每个宿主手写一遍。
struct RibbonCommandEntry {
  QAction* action = nullptr;
  QString location;
  QString keywords;
};

}  // namespace tamias
