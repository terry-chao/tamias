// 工具目录：桌面端 ribbon 的工具在 web 端的规格。
//
// 为什么可以整份照搬：这些命令**全都支持"直给点"**（`p:points=x,y,z|x,y,z` 或
// `v:origin` / `v:position`），给了点就不再走交互 pending（见
// src/command/core/command_system.cpp 的 interactive 分支）。所以 web 这边只要自己
// 采集点、把 args 拼全，就能用 `dispatch` 驱动它们——**引擎侧不用加 feed_point**。
//
// 拿不到的能力（都在文档里逐条写了原因，见 docs/WEB.md）：
//   - import_mesh：命令只实现了"从真实文件系统读"，web 没有 FS 挂载；
//   - set_param / set_material / set_location：要一整套属性/材质/楼层面板才有意义；
//   - create_columns_on_grid 的 a:axis_ids：web 拿不到轴网轴线 id（没有导出）；
//   - create_beam 的 tee / i 断面：注释明说"带点的 T/I 走 interactive"，web 喂不了。

export type Vec3 = [number, number, number];

/** 提示框里的一个字段。type 是 args 文本的类型前缀。 */
export interface PromptField {
  key: string;
  label: string;
  /** d=double / i=int / s=string / v=vec3("x,y,z") / a=double 数组("|" 分隔)。 */
  type: "d" | "i" | "s" | "v" | "a";
  value: string;
}

export interface ToolSpec {
  /**
   * 唯一 id（React key / 搜索去重）。默认等于 command——只有"同一个命令的不同预设"
   * 才需要单独给（圆柱走 create_column + sub_type=circle，三个布尔按钮走同一个
   * boolean 命令 + 不同的 operation）。
   */
  id?: string;
  /** 命令名（register_commands.cpp 里注册的那个）。'new'/'open'/'frame_all' 这类
   *  没有对应命令的纯 UI 动作由 ViewerApp 特判，不会进 dispatch。 */
  command: string;
  label: string;
  /** assets/icons 里的 basename（与桌面端 ribbon_icon 的名字一致）。 */
  icon: string;
  tip?: string;
  /** 出处，画在命令搜索结果右边，形如「绘制 · 直线」。 */
  location: string;
  /**
   * 要几个点。max 为 null = 开放式（按 Enter 或双击结束）。
   * 不填 = 这个工具不吃点。
   */
  points?: { min: number; max: number | null };
  /**
   * 点怎么进 args：默认 `p:points=`（多点）；"position" / "origin" 是单点用 vec3 传
   * （create_text 只认 v:position）。
   */
  pointArg?: "points" | "position" | "origin";
  /** 固定参数，原样进 args（含类型前缀），如 `s:sub_type=rect`。 */
  fixed?: string[];
  /** 需要问用户的参数。 */
  fields?: PromptField[];
  /** 选择集怎么进 args：ids=`a:ids=`（默认）/ pair=`i:a`+`i:b` / entity=`i:entity_id`。 */
  selectionAs?: "ids" | "pair" | "entity";
  /** true = 对每个选中的实体**各发一次**命令（「删除选中」就是这种）。 */
  perSelection?: boolean;
  /** true = 需要先选中实体（ids 从当前选择取）。 */
  needsSelection?: boolean;
  /** 不需要点、也不需要选择，字段填完直接 dispatch。 */
  immediate?: boolean;
}

const fmtPoint = (p: Vec3) => `${p[0]},${p[1]},${p[2]}`;

/**
 * 拼 args 文本。语法见 src/host/command_arg_text.cpp：
 * `;` 分隔，`[类型:]key=value`，点/数组用 `|` 分隔，vec3 是 `x,y,z`。
 */
export function buildArgs(
  spec: ToolSpec,
  params: Record<string, string>,
  points: Vec3[],
  ids: number[],
): string {
  const parts: string[] = [...(spec.fixed ?? [])];

  for (const field of spec.fields ?? []) {
    const raw = (params[field.key] ?? field.value).trim();
    if (raw !== "") {
      parts.push(`${field.type}:${field.key}=${raw}`);
    }
  }

  const how = spec.selectionAs ?? "ids";
  if (how === "pair") {
    // 布尔要两个操作数，且必须不同（boolean_command.cpp 会校验）。
    if (ids.length >= 2) {
      parts.push(`i:a=${ids[0]}`, `i:b=${ids[1]}`);
    }
  } else if (how === "entity") {
    if (ids.length >= 1) {
      parts.push(`i:entity_id=${ids[0]}`);
    }
  } else if (ids.length > 0) {
    parts.push(`a:ids=${ids.join("|")}`);
  }

  if (points.length > 0) {
    if (spec.pointArg === "position") {
      parts.push(`v:position=${fmtPoint(points[0])}`);
    } else if (spec.pointArg === "origin") {
      parts.push(`v:origin=${fmtPoint(points[0])}`);
    } else {
      parts.push(`p:points=${points.map(fmtPoint).join("|")}`);
    }
  }

  return parts.join(";");
}

// ── 工具清单 ─────────────────────────────────────────────────────────────────
// 分组名与顺序照桌面端 main_window.cpp 的 ribbon（home: File/Edit/Draw/
// Architectural/Structural/Modify/Annotate/Navigation/Help；view: Display/Panels）。

export const HOME_TOOLS: Record<string, ToolSpec[]> = {
  file: [
    { command: "new", label: "新建", icon: "new", location: "开始 · 文件", tip: "空文档 + 内置示例场景", immediate: true },
    { command: "open", label: "打开", icon: "open", location: "开始 · 文件", tip: "打开 .tdoc / .trscn / .obj", immediate: true },
  ],
  edit: [
    { command: "undo", label: "撤销", icon: "undo", location: "开始 · 编辑", tip: "撤销上一步编辑", immediate: true },
    { command: "redo", label: "重做", icon: "redo", location: "开始 · 编辑", tip: "重做被撤销的编辑", immediate: true },
  ],
  draw: [
    { command: "create_line", label: "直线", icon: "line", location: "开始 · 绘制", tip: "两点一条直线", points: { min: 2, max: 2 } },
    { command: "create_polyline", label: "折线", icon: "polyline", location: "开始 · 绘制", tip: "连续点，Enter / 双击结束", points: { min: 2, max: null } },
    { command: "create_rectangle", label: "矩形", icon: "rectangle", location: "开始 · 绘制", tip: "两个对角点", points: { min: 2, max: 2 } },
    { command: "create_circle", label: "圆", icon: "circle", location: "开始 · 绘制", tip: "圆心 + 半径上一点", points: { min: 2, max: 2 } },
    { command: "create_arc", label: "圆弧", icon: "arc", location: "开始 · 绘制", tip: "三点定弧", points: { min: 3, max: 3 } },
    { command: "create_bezier", label: "贝塞尔", icon: "bezier", location: "开始 · 绘制", tip: "控制点，Enter / 双击结束", points: { min: 2, max: null } },
    { command: "create_bspline", label: "B样条", icon: "bspline", location: "开始 · 绘制", tip: "控制点，Enter / 双击结束", points: { min: 2, max: null } },
  ],
  architectural: [
    { command: "create_wall", label: "墙", icon: "wall", location: "开始 · 建筑", tip: "两点一堵墙；多点连成墙run，Enter 结束", points: { min: 2, max: null }, fixed: ["d:thickness=0.2", "d:height=3"] },
    { command: "create_door", label: "门", icon: "door", location: "开始 · 建筑", tip: "在墙上点一下放置", points: { min: 1, max: 1 }, fixed: ["d:width=0.9", "d:height=2.1"] },
    { command: "create_window", label: "窗", icon: "window", location: "开始 · 建筑", tip: "在墙上点一下放置", points: { min: 1, max: 1 }, fixed: ["d:width=1.2", "d:height=1.5"] },
    { command: "create_curtain_wall", label: "幕墙", icon: "curtain_wall", location: "开始 · 建筑", tip: "两点一段幕墙", points: { min: 2, max: 2 }, fixed: ["d:thickness=0.15", "d:height=3"] },
  ],
  structural: [
    { command: "create_beam", label: "梁", icon: "beam", location: "开始 · 结构", tip: "两点一根梁（矩形断面；T/I 断面暂不支持）", points: { min: 2, max: 2 }, fixed: ["s:sub_type=rect", "d:width=0.3", "d:depth=0.4"] },
    { command: "create_column", label: "方柱", icon: "column", location: "开始 · 结构", tip: "在工作平面上点一下", points: { min: 1, max: 1 }, fixed: ["s:sub_type=rect", "d:width=0.4", "d:depth=0.4", "d:height=3"] },
    { id: "create_column_circle", command: "create_column", label: "圆柱", icon: "cylinder", location: "开始 · 结构", tip: "在工作平面上点一下", points: { min: 1, max: 1 }, fixed: ["s:sub_type=circle", "d:diameter=0.6", "d:height=3"] },
    { command: "create_slab", label: "板", icon: "slab", location: "开始 · 结构", tip: "多边形轮廓，Enter 结束", points: { min: 3, max: null }, fixed: ["d:thickness=0.2"] },
    { command: "create_structural_wall", label: "结构墙", icon: "structural_wall", location: "开始 · 结构", tip: "两点一段，Enter 结束", points: { min: 2, max: null }, fixed: ["d:thickness=0.25", "d:height=3"] },
    { command: "create_foundation", label: "基础", icon: "foundation", location: "开始 · 结构", tip: "点一下（独立基础）或框出轮廓，Enter 结束", points: { min: 1, max: null }, fixed: ["s:sub_type=isolated", "d:length=1.2", "d:width=1.2", "d:height=0.5"] },
  ],
  modify: [
    { command: "move_entities", label: "移动", icon: "move", location: "开始 · 修改", tip: "把选中的构件按增量移动", needsSelection: true, immediate: true, fields: [
      { key: "delta", label: "增量 dx,dy,dz", type: "v", value: "0,0,0" },
    ] },
    { command: "copy_entities", label: "复制", icon: "copy", location: "开始 · 修改", tip: "把选中的构件按增量复制一份", needsSelection: true, immediate: true, fields: [
      { key: "delta", label: "增量 dx,dy,dz", type: "v", value: "0,0,0" },
    ] },
    { command: "rotate_entities", label: "旋转", icon: "rotate", location: "开始 · 修改", tip: "绕竖直轴旋转选中的构件", needsSelection: true, immediate: true, fields: [
      { key: "angle", label: "角度（度）", type: "d", value: "90" },
    ] },
    { command: "mirror_entities", label: "镜像", icon: "mirror", location: "开始 · 修改", tip: "点两个点定镜像轴", needsSelection: true, points: { min: 2, max: 2 } },
    { command: "array_entities", label: "阵列", icon: "array", location: "开始 · 修改", tip: "线性阵列选中的构件", needsSelection: true, immediate: true, fixed: ["s:mode=linear"], fields: [
      { key: "count", label: "个数", type: "i", value: "3" },
      { key: "direction", label: "方向 dx,dy,dz", type: "v", value: "1,0,0" },
      { key: "spacing", label: "间距", type: "d", value: "1.0" },
    ] },
    { command: "fillet", label: "圆角", icon: "fillet", location: "开始 · 修改", tip: "给选中构件的第 N 条棱倒圆角", needsSelection: true, immediate: true, selectionAs: "entity", fields: [
      { key: "edge", label: "棱序号", type: "i", value: "1" },
      { key: "radius", label: "半径", type: "d", value: "0.05" },
    ] },
    { command: "chamfer", label: "倒角", icon: "chamfer", location: "开始 · 修改", tip: "给选中构件的第 N 条棱倒角", needsSelection: true, immediate: true, selectionAs: "entity", fields: [
      { key: "edge", label: "棱序号", type: "i", value: "1" },
      { key: "distance", label: "距离", type: "d", value: "0.05" },
    ] },
    { id: "boolean_fuse", command: "boolean", label: "并集", icon: "box", location: "开始 · 修改", tip: "选中两个构件求并集", needsSelection: true, immediate: true, selectionAs: "pair", fixed: ["i:operation=0"] },
    { id: "boolean_common", command: "boolean", label: "交集", icon: "box", location: "开始 · 修改", tip: "选中两个构件求交集", needsSelection: true, immediate: true, selectionAs: "pair", fixed: ["i:operation=1"] },
    { id: "boolean_cut", command: "boolean", label: "差集", icon: "box", location: "开始 · 修改", tip: "选中两个构件，第一个减第二个", needsSelection: true, immediate: true, selectionAs: "pair", fixed: ["i:operation=2"] },
    { command: "delete_entity", label: "删除选中", icon: "delete", location: "开始 · 修改", tip: "删除选中的构件（一步可撤销）", needsSelection: true, immediate: true, selectionAs: "entity", perSelection: true },
  ],
  annotate: [
    { command: "create_text", label: "文字", icon: "text_label", location: "开始 · 注释", tip: "在工作平面上放一段文字", points: { min: 1, max: 1 }, pointArg: "position", fixed: ["s:kind=world"], fields: [
      { key: "text", label: "内容", type: "s", value: "文字" },
      { key: "size_px", label: "字号(px)", type: "d", value: "16" },
    ] },
  ],
  navigation: [
    { command: "frame_all", label: "全部框显", icon: "frame_all", location: "开始 · 导航", tip: "把相机框到整个文档", immediate: true },
  ],
  grid: [
    { command: "auto_grid", label: "自动轴网", icon: "grid", location: "开始 · 轴网", tip: "按间距生成正交轴网", immediate: true, fields: [
      { key: "x_spacings", label: "纵轴间距(| 分隔)", type: "a", value: "6|6|6" },
      { key: "z_spacings", label: "横轴间距(| 分隔)", type: "a", value: "6|6" },
      { key: "margin", label: "外扩", type: "d", value: "1.0" },
    ] },
    { command: "create_grid_axis", label: "加轴线", icon: "grid", location: "开始 · 轴网", tip: "加一根轴线", immediate: true, fields: [
      { key: "name", label: "轴号", type: "s", value: "1" },
      { key: "direction", label: "方向 x/z", type: "s", value: "z" },
      { key: "position", label: "位置", type: "d", value: "0" },
      { key: "start", label: "起点", type: "d", value: "-6" },
      { key: "end", label: "终点", type: "d", value: "6" },
    ] },
    { command: "create_columns_on_grid", label: "柱网", icon: "column", location: "开始 · 轴网", tip: "在轴网交点批量布柱", immediate: true, fixed: ["s:sub_type=rect", "d:width=0.4", "d:depth=0.4", "d:height=3"] },
  ],
  storey: [
    { command: "create_storey", label: "新建楼层", icon: "storey", location: "开始 · 楼层", tip: "加一个新楼层", immediate: true, fields: [
      { key: "name", label: "层名", type: "s", value: "2F" },
      { key: "elevation", label: "标高", type: "d", value: "3.0" },
    ] },
  ],
  help: [
    { command: "about", label: "关于", icon: "about", location: "开始 · 帮助", tip: "版本、图形后端与造型内核", immediate: true },
  ],
};

export const VIEW_TOOLS: Record<string, ToolSpec[]> = {
  display: [
    { command: "wireframe", label: "线框", icon: "wireframe", location: "视图 · 显示", tip: "只画边，看结构", immediate: true },
    { command: "shaded", label: "着色", icon: "shaded", location: "视图 · 显示", tip: "默认显示模式", immediate: true },
    { command: "realistic", label: "真实", icon: "realistic", location: "视图 · 显示", tip: "带环境光的真实感显示", immediate: true },
  ],
  panels: [
    { command: "log-panel", label: "引擎日志", icon: "console", location: "视图 · 面板", tip: "视口右下角的引擎 warn / error 面板", immediate: true },
  ],
};

/** 组名（与桌面端一致：File/Edit/Draw/Architectural/Structural/Modify/Annotate/Navigation/Help）。 */
/** 第一个点的提示语（状态栏用）。点数和收尾方式都不一样，别写死一句"点击放置"。 */
export function firstStepHint(spec: ToolSpec): string {
  const points = spec.points;
  if (points === undefined) {
    return "开始";
  }
  if (spec.pointArg === "position" || spec.pointArg === "origin") {
    return "点一下确定位置（Esc 取消）";
  }
  if (points.max === 1) {
    return "点一下放置（Esc 取消）";
  }
  if (points.max === null) {
    return points.min >= 3
      ? `点出轮廓（至少 ${points.min} 点），Enter / 双击结束`
      : `点起点（至少 ${points.min} 点），Enter / 双击结束`;
  }
  return `点第 1 / ${points.max} 个点（Esc 取消）`;
}

export const HOME_GROUP_TITLES: Record<string, string> = {
  file: "文件",
  edit: "编辑",
  draw: "绘制",
  architectural: "建筑",
  structural: "结构",
  modify: "修改",
  annotate: "注释",
  navigation: "导航",
  grid: "轴网",
  storey: "楼层",
  help: "帮助",
};

export const VIEW_GROUP_TITLES: Record<string, string> = {
  display: "显示",
  panels: "面板",
};
