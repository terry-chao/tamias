// 工具带：桌面端那两段分区（docs/APP.md「一条工具带，两段分区」）。
//
// 桌面端的骨架是 RibbonBar → RibbonPage（段）→ RibbonGroup（组）→ 按钮。要点：
//   - 段左侧一条 3px 主色竖线，**段名不画在带上**（看菜单也知道），颜色按段序取
//     蓝 / 青绿 / 橙 / 紫（ribbon_bar.cpp 的 section_accent 暗色档）；
//   - 段与段之间一条横线；
//   - 每个组下面画组名；按钮是「图标在上、文字在下」；
//   - 「仅图标」形态下按钮不写字、组名也不画（ribbon_group.cpp 的
//     title_->setVisible(!floating_ && !icon_only)），名字改走 tooltip。
import { Fragment } from "react";

import Icon from "./icons";
import type { RibbonSectionSpec } from "./types";

/** 段主色：对应 ribbon_bar.cpp section_accent 的暗色档。 */
const ACCENT: Record<RibbonSectionSpec["accent"], string> = {
  blue: "#6cb6ff",
  teal: "#3dc9b6",
  orange: "#e09a3d",
  purple: "#a98bea",
};

interface RibbonBarProps {
  sections: RibbonSectionSpec[];
}

export default function RibbonBar({ sections }: RibbonBarProps) {
  return (
    <div className="tb-pages">
      {sections.map((section, sectionIndex) => (
        <Fragment key={section.id}>
          {sectionIndex > 0 && <div className="tb-section-sep" />}
          <div className="tb-section">
            <span
              className="tb-section-bar"
              style={{ background: ACCENT[section.accent] }}
              title={`${section.title}（双击卷起）`}
            />
            <div className="tb-section-groups">
              {section.groups.map((group, groupIndex) => (
                <Fragment key={group.id}>
                  {groupIndex > 0 && <span className="tb-group-sep" />}
                  <div className="tb-group">
                    <div className="tb-group-btns">
                      {group.commands.map((command) => (
                        <button
                          type="button"
                          key={command.id}
                          className={`tb-btn${command.checked === true ? " checked" : ""}`}
                          disabled={command.disabled === true}
                          title={
                            command.tip === undefined
                              ? command.label
                              : `${command.label} — ${command.tip}`
                          }
                          onClick={() => {
                            // 命令可能返回 Promise（Asyncify 下的 embind 导出）；
                            // 异常吞在这里，真正失败走 ViewerApp 的 error / 状态栏。
                            void Promise.resolve(command.run()).catch(() => {});
                          }}
                        >
                          <Icon name={command.icon} size={24} />
                          <span className="tb-btn-label">{command.label}</span>
                        </button>
                      ))}
                    </div>
                    <div className="tb-group-title">{group.title}</div>
                  </div>
                </Fragment>
              ))}
            </div>
          </div>
        </Fragment>
      ))}
    </div>
  );
}
