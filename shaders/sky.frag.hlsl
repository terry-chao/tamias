#include "mesh.hlsli"

// 视口背景（backdrop）：一块全屏三角，一次画完「天空 + 地面 + 工作平面网格」。
// 它是视口最底下的一层，不测深度、不写深度——模型、轴网、预览线全部压在它上面
// （图层顺序见 docs/RENDERING.md §7）。
//
// 相机射线就在片元里重建。pc.color.xy 传的是 1/|proj(0,0)|、1/|proj(1,1)|：
//   透视（proj 是透视矩阵）：sx/sy = tan(半视角)，起点 = 眼睛，方向 = fwd + right·ndc.x·sx + up·ndc.y·sy
//   正交（proj 是 ortho 矩阵）：sx/sy = 半宽 / 半高，起点 = 眼睛 + right·ndc.x·sx + up·ndc.y·sy，方向 = fwd
// 同一份代码两种投影都对：正交顶视图里地面铺满整屏、网格照常出现，不会像只认透视
// 那样把平面视图糊掉。
//
// 配色是一套**深色 CAD 视口**。调色板按 sRGB 写（也就是「在屏幕上看到的那个颜色」，
// 和界面里的 QColor 一个读法），最后一步统一转成线性——渲染目标本身是 sRGB 编码的，
// 硬件写回时还会再编一次，两边抵消，屏幕上就正好是这里写的那组值。
// 换主题只改这一段；视口 widget 的底色和 clear_color 跟着它走（render_runtime.h）。

// —— 调色板（sRGB，十六进制见行尾）——
static const float3 kSkyZenith = float3(0.125, 0.188, 0.290);    // #20304A 天顶：深蓝
static const float3 kSkyHorizon = float3(0.431, 0.525, 0.639);   // #6E86A3 天际线：淡蓝灰
static const float3 kHorizonGlow = float3(0.663, 0.737, 0.824);  // #A9BCD2 地平线那一道柔亮
static const float3 kGroundNear = float3(0.118, 0.149, 0.188);   // #1E2630 近处地面：比天空暗，构件才压得住
static const float3 kGridMinor = float3(0.290, 0.345, 0.439);    // #4A5870 次网格线（1 m 级）
static const float3 kGridMajor = float3(0.420, 0.498, 0.639);    // #6B7FA3 主网格线（每 5 格一条）
static const float3 kAxisXColor = float3(0.690, 0.376, 0.376);   // #B06060 地面上的世界 X 轴（红）
static const float3 kAxisZColor = float3(0.376, 0.533, 0.784);   // #6088C8 地面上的世界 Z 轴（蓝）

// 地面 / 工作平面标高：和轴网同面（见 src/bim/grid.h 的 kGridPlaneY）。
static const float kGroundY = 0.0;
// 网格基准间距 1 m、主网格 5 m：和吸附用的 kGridMinorSpacing / kGridMajorSpacing
// 一致（src/engine/math/grid.h），所以画出来的格子和落位吸附的格子是同一张。
static const float kGridBaseSpacing = 1.0;
static const float kGridMajorEvery = 5.0;
// 格子密到每格不足这么多像素就升一级间距（1→10→100…米），免得远景糊成一片灰。
static const float kGridMinPixels = 8.0;
// log10(x) = log2(x) * kLog10：GLSL ES 没有 log10，三份 shader 统一用 log2。
static const float kLog10 = 0.30102999566;

// 到最近一条格线的强度：1 = 压在线上，0 = 格子正中。coord 是地面世界坐标，
// deriv 是 fwidth(coord)，就是「一个像素跨多少米」，两者相除得到线宽（像素）。
float grid_line(float2 coord, float spacing, float2 deriv) {
  float2 cell = abs(frac(coord / spacing + 0.5) - 0.5);
  float2 px = cell * spacing / max(deriv, 1e-5);
  return 1.0 - saturate(min(px.x, px.y));
}

// 一条穿过原点的世界轴线：到轴线的距离换算成像素，1 个像素宽。
float axis_line(float distance_world, float deriv) {
  return 1.0 - saturate(abs(distance_world) / max(deriv, 1e-5));
}

// sRGB → 线性。近似 pow(c, 2.2) 也能看，这条是标准曲线，和硬件那一段严格互逆。
float3 srgb_to_linear(float3 c) {
  return lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
}

struct SkyVsOutput {
  float4 position : SV_Position;
  [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 main(SkyVsOutput input) : SV_Target0 {
  // 1. 这条像素的相机射线 -------------------------------------------------------
  float2 ndc = input.uv * 2.0 - 1.0;
#if defined(TAMIAS_VULKAN)
  ndc.y = -ndc.y;  // Vulkan 的裁剪空间 Y 向下（见 clip_space_correction_matrix）
#endif
  const float3x3 cam = (float3x3)pc.model;  // 相机朝向；平移已被上层清零
  const float3 cam_right = mul(cam, float3(1.0, 0.0, 0.0));
  const float3 cam_up = mul(cam, float3(0.0, 1.0, 0.0));
  const float3 cam_fwd = mul(cam, float3(0.0, 0.0, -1.0));
  const float3 eye = pc.eye_pos_mode.xyz;
  const float2 ray_scale = pc.color.xy;  // tan(半视角) 或 半宽 / 半高

  float3 ray_dir;
  float3 ray_origin;
  if (pc.material.x > 0.5) {  // 正交：平面视图 / 立面视图
    ray_dir = cam_fwd;
    ray_origin = eye + cam_right * (ndc.x * ray_scale.x) + cam_up * (ndc.y * ray_scale.y);
  } else {  // 透视
    ray_dir = normalize(cam_fwd + cam_right * (ndc.x * ray_scale.x) +
                        cam_up * (ndc.y * ray_scale.y));
    ray_origin = eye;
  }

  // 2. 与地面求交。放在分支外算：导数在非一致控制流里没有定义，而下面要用 fwidth。
  const float denom = min(ray_dir.y, -1e-6);
  const float t_raw = (kGroundY - ray_origin.y) / denom;
  const float hit = (ray_dir.y < -1e-6 && t_raw > 0.0) ? 1.0 : 0.0;
  const float3 ground_point = ray_origin + ray_dir * max(t_raw, 0.0);
  const float2 coord = ground_point.xz;
  const float2 deriv = float2(fwidth(coord.x), fwidth(coord.y));

  // 3. 天空：天顶 → 地平线的竖向渐变 -------------------------------------------
  const float elevation = saturate(ray_dir.y);
  float3 sky = lerp(kSkyHorizon, kSkyZenith, pow(elevation, 0.55));

  // 4. 地面：近处一层填充，越远越融进天际线（空气透视），网格跟着淡出 -----------
  const float view_scale = max(pc.eye_pos_mode.w, 1.0);  // 视距：淡出半径按它缩放
  const float dist = length(ground_point - eye);
  const float grid_fade = 1.0 - smoothstep(5.0 * view_scale, 28.0 * view_scale, dist);
  float haze = smoothstep(2.0 * view_scale, 26.0 * view_scale, dist);
  // 没有交点 = 射线和地面平行（立面视图）：脚下那半边给一层没细节的地面填充。
  haze = haze * hit + (1.0 - hit) * 0.55;
  float3 ground = lerp(kGroundNear, kSkyHorizon, haze * 0.78);

  // 自适应网格：每格至少 kGridMinPixels 像素，不够就 1→10→100… 升一级。两级之间
  // 插值，缩小的时候网格不会突然跳一下；再近也不再比 1 m 更细（建筑尺度）。
  // 还要算上「坐标本身的精度」：|world| 到 1e6 米时 float32 只剩 ~0.06 m 的量化
  // 步长，再按 1 m 画线就碎成噪声（见 docs/FAQ.md）。把它并进每像素世界尺度，
  // 网格会自动升到画得出来的间距。
  const float precision_step = max(abs(coord.x), abs(coord.y)) * 1e-6;
  const float world_per_pixel = max(max(deriv.x, deriv.y), precision_step);
  const float lod =
      max(log2(max(world_per_pixel * kGridMinPixels / kGridBaseSpacing, 1.0)) * kLog10,
          0.0);
  const float lod_base = floor(lod);
  const float lod_frac = saturate(lod - lod_base);
  const float minor_lo = kGridBaseSpacing * pow(10.0, lod_base);
  const float minor_hi = minor_lo * 10.0;
  const float minor = lerp(grid_line(coord, minor_lo, deriv),
                           grid_line(coord, minor_hi, deriv), lod_frac);
  const float major = lerp(grid_line(coord, minor_lo * kGridMajorEvery, deriv),
                           grid_line(coord, minor_hi * kGridMajorEvery, deriv),
                           lod_frac);
  ground = lerp(ground, kGridMinor, saturate(minor * 0.55) * grid_fade * hit);
  ground = lerp(ground, kGridMajor, saturate(major * 0.80) * grid_fade * hit);

  // 地面上的世界轴：X 红、Z 蓝，和主网格一起淡出，用来看朝向。
  const float axis_x = axis_line(coord.y, deriv.y) * grid_fade * hit;
  const float axis_z = axis_line(coord.x, deriv.x) * grid_fade * hit;
  ground = lerp(ground, kAxisXColor, saturate(axis_x) * 0.70);
  ground = lerp(ground, kAxisZColor, saturate(axis_z) * 0.70);

  // 5. 合成 --------------------------------------------------------------------
  // 视线落在地面这一侧吗？透视俯视 / 正交顶视都有交点；立面视图（dir.y ≈ 0）
  // 射线和地面平行，改用「射线起点在地面以下」判断脚下那半边。
  float on_ground = 0.0;
  if (ray_dir.y < -1e-6) {
    on_ground = 1.0;
  } else if (ray_dir.y <= 1e-6 && ray_origin.y <= kGroundY) {
    on_ground = 1.0;
  }
  float3 color = lerp(sky, ground, on_ground);
  // 地平线压一道柔亮：天空和地面在这里接缝，不画线只有一条硬边。
  const float band = pow(1.0 - saturate(abs(ray_dir.y) / 0.05), 3.0);
  color = lerp(color, kHorizonGlow, band * 0.30);
  return float4(srgb_to_linear(color), 1.0);
}
