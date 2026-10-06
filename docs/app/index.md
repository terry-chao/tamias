# 客户端

壳：窗口、视口、属性面板。几何真相不在这一层。插件挂在壳上，编辑仍走命令。
界面库本身也不在这层——壳只依赖 `tac` 的契约，Qt 是两套实现之一。

- [Qt 壳](../APP.md)
- [界面层（tac / Qt）](../TAC.md) —— 自研界面契约 + Qt 后端：换界面库不动上层
- [参考图纸](../DRAWING.md) —— 把 PDF / DXF / SVG / 图片格式的已有图纸贴进视口当底图
- [什么时候要嵌浏览器](../DECISION-EMBEDDED-BROWSER.md) —— 加 QtWebEngine / CEF / WebView2 的门禁
- [插件系列](../plugin/index.md) —— 设计理念、使用、宿主 API、怎么写插件
