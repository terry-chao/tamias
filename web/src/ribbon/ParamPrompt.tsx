// 需要参数的命令的提示框。
//
// 桌面端这些参数各有专门的对话框/面板（轴网设置、阵列对话框、楼层设置…）。web 端
// 没有那些面板，就用一个通用的小表单：按命令声明的字段收值，回车即执行。
//
// 字段类型与 args 文本的类型前缀一一对应（见 modeling/catalog.ts）：
//   d 小数 / i 整数 / s 文本 / v 向量 x,y,z / a 数组（用 | 分隔）
import { useState } from "react";

import Icon from "./icons";
import type { PromptField } from "../modeling/catalog";

interface ParamPromptProps {
  title: string;
  icon: string;
  fields: PromptField[];
  onSubmit: (values: Record<string, string>) => void;
  onCancel: () => void;
}

export default function ParamPrompt({
  title,
  icon,
  fields,
  onSubmit,
  onCancel,
}: ParamPromptProps) {
  const [values, setValues] = useState<Record<string, string>>(() =>
    Object.fromEntries(fields.map((field) => [field.key, field.value])),
  );

  return (
    <div className="prompt-backdrop" onClick={onCancel}>
      <form
        className="prompt-card"
        onClick={(event) => event.stopPropagation()}
        onSubmit={(event) => {
          event.preventDefault();
          onSubmit(values);
        }}
      >
        <div className="prompt-title">
          <Icon name={icon} size={20} />
          <span>{title}</span>
        </div>

        {fields.map((field) => (
          <label className="prompt-field" key={field.key}>
            <span>{field.label}</span>
            <input
              type="text"
              value={values[field.key] ?? ""}
              spellCheck={false}
              // 大部分字段是数字/向量，打开就全选，直接打字即替换。
              autoFocus={field === fields[0]}
              onChange={(event) =>
                setValues((prev) => ({ ...prev, [field.key]: event.target.value }))
              }
            />
          </label>
        ))}

        <div className="prompt-actions">
          <button type="button" className="btn" onClick={onCancel}>
            取消
          </button>
          <button type="submit" className="btn prompt-ok">
            确定
          </button>
        </div>
      </form>
    </div>
  );
}
