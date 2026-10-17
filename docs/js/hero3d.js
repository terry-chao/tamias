/* ==========================================================================
   Tamias 首页 Hero 三维演示（无依赖，WebGL2）
   --------------------------------------------------------------------------
   这不是查看器，只是一段"会动的封面"：一栋 12 层钢混塔楼 + 塔吊 + 场地网格，
   用来在首屏把「BIM 大模型」与「几何内核」讲清楚。设计约束：

   - **不引入任何第三方库**：官网是 MkDocs 静态站，首屏不该为一段演示多背 600 KB；
   - **只读、可降级**：拿不到 WebGL2 就显示静态海报，页面其余部分照常；
   - **不抢滚动**：只有指针在画布上时滚轮才缩放，页面滚动不受影响；
   - **不打扰**：默认极慢自转，用户一碰就停；prefers-reduced-motion 下完全静止。

   场景单位是米，Y 轴向上。相机绕 target(0, 27, 0) 转，滚轮改距离。
   ========================================================================== */
(function () {
  'use strict';

  var reduceMotion =
    window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ------------------------------------------------------------------ 数学 */

  function mat4Perspective(out, fovy, aspect, near, far) {
    var f = 1 / Math.tan(fovy / 2);
    var nf = 1 / (near - far);
    out[0] = f / aspect; out[1] = 0; out[2] = 0; out[3] = 0;
    out[4] = 0; out[5] = f; out[6] = 0; out[7] = 0;
    out[8] = 0; out[9] = 0; out[10] = (far + near) * nf; out[11] = -1;
    out[12] = 0; out[13] = 0; out[14] = 2 * far * near * nf; out[15] = 0;
    return out;
  }

  function mat4LookAt(out, eye, center, up) {
    var zx = eye[0] - center[0], zy = eye[1] - center[1], zz = eye[2] - center[2];
    var zl = Math.hypot(zx, zy, zz) || 1;
    zx /= zl; zy /= zl; zz /= zl;

    var xx = up[1] * zz - up[2] * zy;
    var xy = up[2] * zx - up[0] * zz;
    var xz = up[0] * zy - up[1] * zx;
    var xl = Math.hypot(xx, xy, xz) || 1;
    xx /= xl; xy /= xl; xz /= xl;

    var yx = zy * xz - zz * xy;
    var yy = zz * xx - zx * xz;
    var yz = zx * xy - zy * xx;

    out[0] = xx; out[1] = yx; out[2] = zx; out[3] = 0;
    out[4] = xy; out[5] = yy; out[6] = zy; out[7] = 0;
    out[8] = xz; out[9] = yz; out[10] = zz; out[11] = 0;
    out[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
    out[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
    out[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
    out[15] = 1;
    return out;
  }

  function mat4Multiply(out, a, b) {
    for (var c = 0; c < 4; c++) {
      for (var r = 0; r < 4; r++) {
        out[c * 4 + r] =
          a[r] * b[c * 4] +
          a[4 + r] * b[c * 4 + 1] +
          a[8 + r] * b[c * 4 + 2] +
          a[12 + r] * b[c * 4 + 3];
      }
    }
    return out;
  }

  /* -------------------------------------------------------------- 着色器源码 */

  var SOLID_VS = [
    '#version 300 es',
    'in vec3 aPos; in vec3 aNormal; in vec3 aColor;',
    'uniform mat4 uViewProj;',
    'out vec3 vWorld; out vec3 vNormal; out vec3 vColor;',
    'void main() {',
    '  vWorld = aPos; vNormal = aNormal; vColor = aColor;',
    '  gl_Position = uViewProj * vec4(aPos, 1.0);',
    '}'
  ].join('\n');

  var SOLID_FS = [
    '#version 300 es',
    'precision highp float;',
    'in vec3 vWorld; in vec3 vNormal; in vec3 vColor;',
    'uniform vec3 uEye; uniform float uCut; uniform int uMode; uniform float uOpacity;',
    'out vec4 outColor;',
    'void main() {',
    '  if (uMode == 2 && vWorld.y > uCut) discard;',
    '  vec3 N = normalize(vNormal);',
    '  vec3 V = normalize(uEye - vWorld);',
    '  vec3 L1 = normalize(vec3(-0.42, 0.86, 0.34));',
    '  vec3 L2 = normalize(vec3(0.66, 0.22, -0.48));',
    '  float d1 = max(dot(N, L1), 0.0);',
    '  float d2 = max(dot(N, L2), 0.0);',
    '  vec3 col = vColor * (0.30 + 0.78 * d1) + vColor * 0.20 * d2;',
    '  col += vec3(0.05, 0.10, 0.16) * max(-N.y, 0.0) * 0.25;',
    '  col += vec3(0.015, 0.065, 0.075) * exp(-max(vWorld.y, 0.0) / 26.0);',
    '  float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);',
    '  col += vec3(0.30, 0.56, 0.92) * fres * 0.26;',
    '  float dist = length(uEye - vWorld);',
    '  col = mix(col, vec3(0.018, 0.028, 0.050), smoothstep(70.0, 190.0, dist));',
    '  outColor = vec4(col, uOpacity);',
    '}'
  ].join('\n');

  var GLASS_VS = [
    '#version 300 es',
    'in vec3 aPos; in vec3 aNormal; in vec3 aColor;',
    'uniform mat4 uViewProj;',
    'out vec3 vWorld; out vec3 vNormal; out vec3 vColor;',
    'void main() {',
    '  vWorld = aPos; vNormal = aNormal; vColor = aColor;',
    '  gl_Position = uViewProj * vec4(aPos, 1.0);',
    '}'
  ].join('\n');

  var GLASS_FS = [
    '#version 300 es',
    'precision highp float;',
    'in vec3 vWorld; in vec3 vNormal; in vec3 vColor;',
    'uniform vec3 uEye; uniform float uCut; uniform int uMode; uniform float uAlpha;',
    'out vec4 outColor;',
    'void main() {',
    '  if (uMode == 2 && vWorld.y > uCut) discard;',
    '  vec3 N = normalize(vNormal);',
    '  vec3 V = normalize(uEye - vWorld);',
    '  float facing = max(dot(N, V), 0.0);',
    '  float fres = pow(1.0 - facing, 2.2);',
    '  vec3 L1 = normalize(vec3(-0.42, 0.86, 0.34));',
    '  float spec = pow(max(dot(reflect(-L1, N), V), 0.0), 42.0);',
    '  vec3 col = vColor * (0.42 + 0.40 * facing) + vec3(0.55, 0.86, 1.0) * fres * 0.55;',
    '  col += vec3(0.10, 0.22, 0.40) * smoothstep(8.0, 60.0, vWorld.y);',
    '  col += vec3(0.75, 0.92, 1.0) * spec * 0.55;',
    '  float a = uAlpha + fres * 0.30 + spec * 0.12;',
    '  float dist = length(uEye - vWorld);',
    '  a *= 1.0 - smoothstep(90.0, 190.0, dist) * 0.65;',
    '  outColor = vec4(col, a);',
    '}'
  ].join('\n');

  var LINE_VS = [
    '#version 300 es',
    'in vec3 aPos; in vec3 aColor;',
    'uniform mat4 uViewProj;',
    'out vec3 vWorld; out vec3 vColor;',
    'void main() {',
    '  vWorld = aPos; vColor = aColor;',
    '  gl_Position = uViewProj * vec4(aPos, 1.0);',
    '}'
  ].join('\n');

  var LINE_FS = [
    '#version 300 es',
    'precision highp float;',
    'in vec3 vWorld; in vec3 vColor;',
    'uniform vec3 uEye; uniform float uCut; uniform int uMode; uniform float uAlpha;',
    'out vec4 outColor;',
    'void main() {',
    '  if (uMode == 2 && vWorld.y > uCut) discard;',
    '  float dist = length(uEye - vWorld);',
    '  float fog = 1.0 - smoothstep(90.0, 200.0, dist);',
    '  outColor = vec4(vColor, uAlpha * fog);',
    '}'
  ].join('\n');

  var GRID_VS = [
    '#version 300 es',
    'in vec3 aPos;',
    'uniform mat4 uViewProj;',
    'uniform float uY;',
    'out vec3 vWorld;',
    'void main() {',
    '  vWorld = vec3(aPos.x, uY, aPos.z);',
    '  gl_Position = uViewProj * vec4(vWorld, 1.0);',
    '}'
  ].join('\n');

  var GRID_FS = [
    '#version 300 es',
    'precision highp float;',
    'in vec3 vWorld;',
    'uniform vec3 uColor;',
    'uniform float uAlpha;',
    'uniform float uTime;',
    'uniform float uSection;',
    'uniform vec2 uSize;',
    'out vec4 outColor;',
    'float lineMask(vec2 p, float step, float width) {',
    '  vec2 d = abs(fract(p / step - 0.5) - 0.5) * step;',
    '  return 1.0 - smoothstep(0.0, width, min(d.x, d.y));',
    '}',
    'void main() {',
    '  vec2 p = vWorld.xz;',
    '  float minor = lineMask(p, 2.0, 0.028) * 0.30;',
    '  float major = lineMask(p, 10.0, 0.040) * 0.85;',
    '  float grid = minor + major;',
    '  vec3 col = uColor;',
    '  float alpha = grid * uAlpha;',
    '  if (uSection > 0.5) {',
    '    vec2 b = abs(p) / uSize;',
    '    float edge = max(b.x, b.y);',
    '    if (edge > 1.008) discard;',
    '    float border = smoothstep(0.975, 0.997, edge) * (1.0 - smoothstep(1.0, 1.006, edge));',
    '    float fill = 0.075;',
    '    alpha = (fill + grid * 0.35 + border * 1.0) * uAlpha;',
    '    col = mix(col, vec3(1.0), border * 0.6);',
    '  } else {',
    '    float r = length(p);',
    '    float ring = mod(uTime * 5.0, 70.0);',
    '    float scan = 1.0 - smoothstep(0.0, 1.6, abs(r - ring));',
    '    scan *= 1.0 - smoothstep(30.0, 68.0, r);',
    '    alpha += scan * 0.10;',
    '    alpha *= 1.0 - smoothstep(18.0, 52.0, length(p));',
    '  }',
    '  outColor = vec4(col, alpha);',
    '}'
  ].join('\n');

  /* ------------------------------------------------------------------ 小工具 */

  function hexColor(hex) {
    return [
      parseInt(hex.slice(1, 3), 16) / 255,
      parseInt(hex.slice(3, 5), 16) / 255,
      parseInt(hex.slice(5, 7), 16) / 255
    ];
  }

  function clamp(v, lo, hi) {
    return v < lo ? lo : v > hi ? hi : v;
  }

  function easeOutCubic(t) {
    return 1 - Math.pow(1 - t, 3);
  }

  function compile(gl, type, source, label) {
    var sh = gl.createShader(type);
    gl.shaderSource(sh, source);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      // 不让一段演示把整页拖下去：编译失败就当作不支持，走静态海报。
      console.error('[tamias-hero] ' + label + ' shader:', gl.getShaderInfoLog(sh));
      return null;
    }
    return sh;
  }

  function program(gl, vsSource, fsSource, label) {
    var vs = compile(gl, gl.VERTEX_SHADER, vsSource, label + ' vs');
    var fs = compile(gl, gl.FRAGMENT_SHADER, fsSource, label + ' fs');
    if (!vs || !fs) return null;
    var p = gl.createProgram();
    gl.attachShader(p, vs);
    gl.attachShader(p, fs);
    gl.bindAttribLocation(p, 0, 'aPos');
    gl.bindAttribLocation(p, 1, 'aNormal');
    gl.bindAttribLocation(p, 2, 'aColor');
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
      console.error('[tamias-hero] link ' + label + ':', gl.getProgramInfoLog(p));
      return null;
    }
    return p;
  }

  function uniforms(gl, prog, names) {
    var out = {};
    for (var i = 0; i < names.length; i++) out[names[i]] = gl.getUniformLocation(prog, names[i]);
    return out;
  }

  /* ------------------------------------------------------------------ 场景 */

  function buildScene() {
    var solid = [];   // x,y,z, nx,ny,nz, r,g,b —— 每 3 个顶点一个三角面
    var glass = [];   // 同上，带 alpha 的盒子逐面排队
    var lines = [];   // x,y,z, r,g,b
    var glow = [];    // 自发光小面：x,y,z, r,g,b（窗户、灯带、信标）
    var section = []; // 剖切轮廓（只在剖切模式画）
    var glassRanges = [];

    var C = {
      slab: hexColor('#b8c4d6'),
      slabEdge: hexColor('#dce8fb'),
      column: hexColor('#6f7f96'),
      core: hexColor('#1b2536'),
      coreEdge: hexColor('#5d86c9'),
      podium: hexColor('#8b9ab0'),
      roof: hexColor('#4d5d75'),
      glass: hexColor('#3f6fd0'),
      mullion: hexColor('#6ea8ff'),
      site: hexColor('#3fd7c4'),
      crane: hexColor('#ffb469'),
      warm: hexColor('#ffd9a4'),
      cool: hexColor('#8ff4ff')
    };

    function tri(ax, ay, az, bx, by, bz, cx, cy, cz, nx, ny, nz, color, dst) {
      dst.push(ax, ay, az, nx, ny, nz, color[0], color[1], color[2]);
      dst.push(bx, by, bz, nx, ny, nz, color[0], color[1], color[2]);
      dst.push(cx, cy, cz, nx, ny, nz, color[0], color[1], color[2]);
    }

    function quadFace(p0, p1, p2, p3, n, color, dst) {
      tri(p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], n[0], n[1], n[2], color, dst);
      tri(p0[0], p0[1], p0[2], p2[0], p2[1], p2[2], p3[0], p3[1], p3[2], n[0], n[1], n[2], color, dst);
    }

    // 六个面的朝向都按「从外面看逆时针」排列，背面剔除才不会把该看见的面剔掉。
    function boxFaces(x0, y0, z0, x1, y1, z1, color, dst, sidesOnly) {
      if (!sidesOnly) {
        quadFace([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], [0, 1, 0], color, dst);
        quadFace([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], [0, -1, 0], color, dst);
      }
      quadFace([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [1, 0, 0], color, dst);
      quadFace([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], [-1, 0, 0], color, dst);
      quadFace([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], [0, 0, 1], color, dst);
      quadFace([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], [0, 0, -1], color, dst);
    }

    function solidBox(x0, y0, z0, x1, y1, z1, color) {
      boxFaces(x0, y0, z0, x1, y1, z1, color, solid, false);
    }

    function glassBox(x0, y0, z0, x1, y1, z1, color) {
      // 只画四个立面：上下两面永远被楼板盖住，少一批透明三角形。
      var start = glass.length / 9;
      boxFaces(x0, y0, z0, x1, y1, z1, color, glass, true);
      glassRanges.push({
        start: start,
        count: glass.length / 9 - start,
        center: [(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2]
      });
    }

    function line(x0, y0, z0, x1, y1, z1, color, alpha) {
      var c = color;
      if (alpha !== undefined) c = [color[0] * alpha, color[1] * alpha, color[2] * alpha];
      lines.push(x0, y0, z0, c[0], c[1], c[2]);
      lines.push(x1, y1, z1, c[0], c[1], c[2]);
    }

    function glowQuad(a, b, c, d, color) {
      // 两个三角面，顶点颜色直接进 glow 缓冲；画的时候关掉背面剔除。
      glow.push(a[0], a[1], a[2], color[0], color[1], color[2]);
      glow.push(b[0], b[1], b[2], color[0], color[1], color[2]);
      glow.push(c[0], c[1], c[2], color[0], color[1], color[2]);
      glow.push(a[0], a[1], a[2], color[0], color[1], color[2]);
      glow.push(c[0], c[1], c[2], color[0], color[1], color[2]);
      glow.push(d[0], d[1], d[2], color[0], color[1], color[2]);
    }

    function glowWindow(cx, cy, cz, halfW, halfH, face, color) {
      // face: 0=+z 1=-z 2=+x 3=-x。窗洞贴在幕墙外 0.035 m，避免 z-fighting。
      var o = 0.035;
      var a, b, c, d;
      if (face === 0) {
        a = [cx - halfW, cy - halfH, cz + o]; b = [cx + halfW, cy - halfH, cz + o];
        c = [cx + halfW, cy + halfH, cz + o]; d = [cx - halfW, cy + halfH, cz + o];
      } else if (face === 1) {
        a = [cx + halfW, cy - halfH, cz - o]; b = [cx - halfW, cy - halfH, cz - o];
        c = [cx - halfW, cy + halfH, cz - o]; d = [cx + halfW, cy + halfH, cz - o];
      } else if (face === 2) {
        a = [cx + o, cy - halfH, cz + halfW]; b = [cx + o, cy - halfH, cz - halfW];
        c = [cx + o, cy + halfH, cz - halfW]; d = [cx + o, cy + halfH, cz + halfW];
      } else {
        a = [cx - o, cy - halfH, cz - halfW]; b = [cx - o, cy - halfH, cz + halfW];
        c = [cx - o, cy + halfH, cz + halfW]; d = [cx - o, cy + halfH, cz - halfW];
      }
      glowQuad(a, b, c, d, color);
    }

    function boxEdges(x0, y0, z0, x1, y1, z1, color, alpha, skipBottom) {
      var yb = skipBottom ? y1 : y0;
      // 四条竖棱
      line(x0, y0, z0, x0, y1, z0, color, alpha);
      line(x1, y0, z0, x1, y1, z0, color, alpha);
      line(x1, y0, z1, x1, y1, z1, color, alpha);
      line(x0, y0, z1, x0, y1, z1, color, alpha);
      // 顶圈 + 底圈
      line(x0, y1, z0, x1, y1, z0, color, alpha);
      line(x1, y1, z0, x1, y1, z1, color, alpha);
      line(x1, y1, z1, x0, y1, z1, color, alpha);
      line(x0, y1, z1, x0, y1, z0, color, alpha);
      if (!skipBottom) {
        line(x0, yb, z0, x1, yb, z0, color, alpha);
        line(x1, yb, z0, x1, yb, z1, color, alpha);
        line(x1, yb, z1, x0, yb, z1, color, alpha);
        line(x0, yb, z1, x0, yb, z0, color, alpha);
      }
    }

    /* 场地：一块薄基座 + 四角定位桩 */
    solidBox(-30, -0.7, -25, 30, -0.06, 25, hexColor('#0b111b'));
    line(-30, -0.05, -25, 30, -0.05, -25, C.site, 0.22);
    line(30, -0.05, -25, 30, -0.05, 25, C.site, 0.22);
    line(30, -0.05, 25, -30, -0.05, 25, C.site, 0.22);
    line(-30, -0.05, 25, -30, -0.05, -25, C.site, 0.22);

    var site = [
      [-27, -21], [27, -21], [27, 21], [-27, 21]
    ];
    for (var s = 0; s < 4; s++) {
      var a = site[s];
      var b = site[(s + 1) % 4];
      line(a[0], 0.04, a[1], b[0], 0.04, b[1], C.site, 0.30);
    }
    for (var m = 0; m < 4; m++) {
      var px = site[m][0];
      var pz = site[m][1];
      line(px - 1.6, 0.05, pz, px + 1.6, 0.05, pz, C.site, 0.55);
      line(px, 0.05, pz - 1.6, px, 0.05, pz + 1.6, C.site, 0.55);
    }

    // 场地基准环：数字孪生常见的"扫描盘"，也是画面里唯一持续转动的元素。
    function ring(radius, y, color, alpha, segments) {
      for (var i = 0; i < segments; i++) {
        var a0 = (i / segments) * Math.PI * 2;
        var a1 = ((i + 1) / segments) * Math.PI * 2;
        line(
          Math.cos(a0) * radius, y, Math.sin(a0) * radius,
          Math.cos(a1) * radius, y, Math.sin(a1) * radius,
          color, alpha
        );
      }
    }
    ring(23.5, 0.035, C.site, 0.08, 96);
    ring(20.0, 0.035, C.site, 0.12, 96);
    ring(16.5, 0.035, C.site, 0.18, 96);
    for (var tick = 0; tick < 12; tick++) {
      var ta = (tick / 12) * Math.PI * 2;
      var r0 = tick % 3 === 0 ? 21.0 : 23.0;
      line(Math.cos(ta) * r0, 0.04, Math.sin(ta) * r0,
        Math.cos(ta) * 23.8, 0.04, Math.sin(ta) * 23.8, C.site, tick % 3 === 0 ? 0.45 : 0.22);
    }

    /* 裙房：6 m 高的玻璃盒 + 屋面 + 入口雨棚 */
    glassBox(-13, 0.5, -10, 13, 5.7, 10, C.glass);
    solidBox(-13.4, 5.7, -10.4, 13.4, 6.3, 10.4, C.podium);
    boxEdges(-13.4, 5.7, -10.4, 13.4, 6.3, 10.4, C.slabEdge, 0.42);
    solidBox(-6.5, 5.9, 10.4, 6.5, 6.15, 13.6, C.roof);
    line(-6.5, 6.1, 13.55, 6.5, 6.1, 13.55, C.warm, 0.5);

    // 大堂灯带：暖色竖条，给冷色幕墙一点人气
    for (var lx = -10; lx <= 10; lx += 5) {
      glowWindow(lx, 3.05, 10.0, 0.22, 2.25, 0, C.warm);
    }
    line(-13, 0.55, 10.02, 13, 0.55, 10.02, C.warm, 0.55);
    line(-13, 5.65, 10.02, 13, 5.65, 10.02, C.warm, 0.3);

    /* 塔楼：三段收分；每段一套楼板 / 幕墙 / 柱 */
    var segments = [
      { y0: 6, y1: 26, w: 18, d: 14, cx: 0 },
      { y0: 26, y1: 40, w: 15, d: 12, cx: -1.6 },
      { y0: 40, y1: 54, w: 12, d: 10, cx: -3.2 }
    ];

    function segmentAtY(y) {
      for (var i = segments.length - 1; i >= 0; i--) {
        if (y >= segments[i].y0) return segments[i];
      }
      return segments[0];
    }

    // 楼层：每 4 m 一道板，顶部加屋面
    var floors = [];
    for (var fy = 6; fy <= 54; fy += 4) floors.push(fy);

    // 每层四个立面各开几扇亮窗：[+z, -z, +x, -x]，值是窗位种子。
    // 固定表（不随机）保证每次刷新看到的是同一栋楼，截图和视觉回归也稳定。
    var floorLightPattern = [
      [[1], [0], [1], []],
      [[0], [1], [], [1]],
      [[1, 2], [], [1], []],
      [[], [1], [0], []],
      [[1], [], [], [2]],
      [[0, 1], [], [], []],
      [[2], [], [1], []],
      [[], [0], [1], []],
      [[1], [2], [], [1]],
      [[], [1], [], []],
      [[0], [], [1], []],
      [[1], [], [], [0]]
    ];

    for (var f = 0; f < floors.length; f++) {
      var y = floors[f];
      var seg = segmentAtY(Math.max(6, y - 0.01));
      var hw = seg.w / 2;
      var hd = seg.d / 2;

      // 楼板（比幕墙外挑 0.65 m，侧面看是一条亮线）
      var t = f === floors.length - 1 ? 0.52 : 0.32;
      solidBox(seg.cx - hw - 0.65, y - t, -hd - 0.65, seg.cx + hw + 0.65, y, hd + 0.65, C.slab);
      boxEdges(seg.cx - hw - 0.65, y - t, -hd - 0.65, seg.cx + hw, y, hd + 0.65, C.slabEdge, 0.34, true);

      // 幕墙：从楼板上表面到下一层板底
      if (f < floors.length - 1) {
        glassBox(seg.cx - hw, y + 0.02, -hd, seg.cx + hw, y + 3.6, hd, C.glass);
        boxEdges(seg.cx - hw, y + 0.02, -hd, seg.cx + hw, y + 3.6, hd, C.mullion, 0.20);

        // 每层幕墙顶部一条暖色灯带：远看是水平亮线，近看才分得出是灯。
        line(seg.cx - hw, y + 3.52, hd, seg.cx + hw, y + 3.52, hd, C.warm, 0.44);
        line(seg.cx - hw, y + 3.52, -hd, seg.cx + hw, y + 3.52, -hd, C.warm, 0.26);
        line(seg.cx + hw, y + 3.52, -hd, seg.cx + hw, y + 3.52, hd, C.warm, 0.32);
        line(seg.cx - hw, y + 3.52, -hd, seg.cx - hw, y + 3.52, hd, C.warm, 0.32);

        // 零星几扇亮着的窗：全部朝外贴在幕墙前，用确定性伪随机选位置。
        // 亮窗是整段演示里最"有人味"的细节——没有它，玻璃盒只是个模型。
        var lit = floorLightPattern[f % floorLightPattern.length];
        for (var lf = 0; lf < 4; lf++) {
          var slots = lit[lf];
          if (!slots) continue;
          for (var wi = 0; wi < slots.length; wi++) {
            var slot = slots[wi];
            var spanX = seg.w - 3;
            var spanZ = seg.d - 3;
            var px2 = seg.cx - spanX / 2 + spanX * ((slot * 37 + f * 13) % 100) / 100;
            var pz2 = -spanZ / 2 + spanZ * ((slot * 53 + f * 7) % 100) / 100;
            var wc = slot % 3 === 0 ? C.cool : C.warm;
            if (lf === 0) glowWindow(px2, y + 1.85, hd, 0.5, 0.62, 0, wc);
            else if (lf === 1) glowWindow(px2, y + 1.85, -hd, 0.5, 0.62, 1, wc);
            else if (lf === 2) glowWindow(seg.cx + hw, y + 1.85, pz2, 0.5, 0.62, 2, wc);
            else glowWindow(seg.cx - hw, y + 1.85, pz2, 0.5, 0.62, 3, wc);
          }
        }
      }
    }

    // 竖向幕墙分格：每 3 m 一根，只画看得见的外轮廓方向
    for (var fi = 0; fi < floors.length - 1; fi++) {
      var yy = floors[fi];
      var sg = segmentAtY(yy + 0.01);
      var w2 = sg.w / 2;
      var d2 = sg.d / 2;
      var top = yy + 3.6;
      for (var x = -w2 + 3; x < w2 - 0.5; x += 3) {
        line(sg.cx + x, yy + 0.05, -d2, sg.cx + x, top, -d2, C.mullion, 0.10);
        line(sg.cx + x, yy + 0.05, d2, sg.cx + x, top, d2, C.mullion, 0.10);
      }
      for (var z = -d2 + 3; z < d2 - 0.5; z += 3) {
        line(sg.cx - w2, yy + 0.05, z, sg.cx - w2, top, z, C.mullion, 0.10);
        line(sg.cx + w2, yy + 0.05, z, sg.cx + w2, top, z, C.mullion, 0.10);
      }
    }

    // 框架柱：每段 8 根
    for (var si = 0; si < segments.length; si++) {
      var seg2 = segments[si];
      var hw2 = seg2.w / 2;
      var hd2 = seg2.d / 2;
      var colY0 = seg2.y0;
      var colY1 = seg2.y1;
      var spots = [
        [-hw2, -hd2], [hw2, -hd2], [hw2, hd2], [-hw2, hd2],
        [0, -hd2], [0, hd2], [-hw2, 0], [hw2, 0]
      ];
      for (var ci = 0; ci < spots.length; ci++) {
        var sx = seg2.cx + spots[ci][0];
        var sz = spots[ci][1];
        solidBox(sx - 0.21, colY0, sz - 0.21, sx + 0.21, colY1, sz + 0.21, C.column);
      }
    }

    // 核心筒：深色实体，给透明幕墙一点"里面真有东西"的纵深
    for (var cs = 0; cs < segments.length; cs++) {
      var seg3 = segments[cs];
      var cw = seg3.w * 0.30;
      var cd = seg3.d * 0.34;
      solidBox(seg3.cx - cw, seg3.y0 + 0.4, -cd, seg3.cx + cw, seg3.y1, cd, C.core);
      boxEdges(seg3.cx - cw, seg3.y0 + 0.4, -cd, seg3.cx + cw, seg3.y1, cd, C.coreEdge, 0.26);
    }

    // 屋面：女儿墙 + 机房 + 天线 + 航空障碍灯
    var roofSeg = segments[2];
    solidBox(roofSeg.cx - roofSeg.w / 2 - 0.25, 54, -roofSeg.d / 2 - 0.25,
      roofSeg.cx - roofSeg.w / 2 + 0.35, 55.15, roofSeg.d / 2 + 0.25, C.roof);
    solidBox(roofSeg.cx + roofSeg.w / 2 - 0.35, 54, -roofSeg.d / 2 - 0.25,
      roofSeg.cx + roofSeg.w / 2 + 0.25, 55.15, roofSeg.d / 2 + 0.25, C.roof);
    solidBox(roofSeg.cx - roofSeg.w / 2, 54, -roofSeg.d / 2 - 0.25,
      roofSeg.cx + roofSeg.w / 2, 55.15, -roofSeg.d / 2 + 0.35, C.roof);
    solidBox(roofSeg.cx - roofSeg.w / 2, 54, roofSeg.d / 2 - 0.35,
      roofSeg.cx + roofSeg.w / 2, 55.15, roofSeg.d / 2 + 0.25, C.roof);
    line(roofSeg.cx - roofSeg.w / 2, 55.2, roofSeg.d / 2, roofSeg.cx + roofSeg.w / 2, 55.2, roofSeg.d / 2, C.warm, 0.4);

    solidBox(-3.2 - 3.1, 54, -2.6, -3.2 + 3.1, 56.4, 2.6, C.roof);
    boxEdges(-6.3, 54, -2.6, 0.1, 56.4, 2.6, C.slabEdge, 0.45);
    line(-3.2, 56.4, 0, -3.2, 62.2, 0, C.slabEdge, 0.55);
    line(-4.7, 59.4, 0, -1.7, 59.4, 0, C.slabEdge, 0.45);
    glowWindow(-3.2, 62.5, 0, 0.45, 0.45, 0, C.cool);
    glowWindow(-3.2, 62.5, 0, 0.45, 0.45, 2, C.cool);

    /* 塔吊：只走线，暖色，和冷色的楼形成一点对比。
       放在场地左前侧（不是楼后），不然从默认机位看会被塔楼完全挡住。 */
    var craneX = -23;
    var craneZ = 17;
    var mastTop = 35;
    var jibY = 35;
    var jibTip = craneX + 21;
    var jibBack = craneX - 7;
    var apexY = 44;

    for (var cy = 0; cy <= mastTop; cy += 2.6) {
      line(craneX - 0.7, cy, craneZ - 0.7, craneX + 0.7, cy, craneZ + 0.7, C.crane, 1.0);
      line(craneX - 0.7, cy, craneZ + 0.7, craneX + 0.7, cy, craneZ - 0.7, C.crane, 1.0);
    }
    line(craneX - 0.7, 0, craneZ - 0.7, craneX - 0.7, mastTop, craneZ - 0.7, C.crane, 0.85);
    line(craneX + 0.7, 0, craneZ - 0.7, craneX + 0.7, mastTop, craneZ - 0.7, C.crane, 0.85);
    line(craneX + 0.7, 0, craneZ + 0.7, craneX + 0.7, mastTop, craneZ + 0.7, C.crane, 0.85);
    line(craneX - 0.7, 0, craneZ + 0.7, craneX - 0.7, mastTop, craneZ + 0.7, C.crane, 0.85);

    line(craneX, mastTop, craneZ, craneX, apexY, craneZ, C.crane, 1.0);
    line(craneX, jibY, craneZ, jibTip, jibY, craneZ, C.crane, 0.95);
    line(craneX, jibY, craneZ, jibBack, jibY, craneZ, C.crane, 0.95);
    line(jibTip, jibY, craneZ, craneX, apexY, craneZ, C.crane, 0.72);
    line(jibBack, jibY, craneZ, craneX, apexY, craneZ, C.crane, 0.72);

    // 吊钩：小车在跨中偏外，钢索垂下来，下面吊一根钢梁
    var trolleyX = craneX + 13;
    line(craneX, jibY, craneZ, trolleyX, jibY, craneZ, C.crane, 0.2);
    line(trolleyX, jibY, craneZ, trolleyX, 24, craneZ, C.crane, 0.85);
    solidBox(trolleyX - 0.55, 22.7, craneZ - 0.55, trolleyX + 0.55, 24, craneZ + 0.55, C.crane);
    line(trolleyX, 23.4, craneZ, trolleyX - 2.7, 21.0, craneZ, C.crane, 0.62);
    line(trolleyX, 23.4, craneZ, trolleyX + 2.7, 21.0, craneZ, C.crane, 0.62);
    solidBox(trolleyX - 3.0, 20.3, craneZ - 0.42, trolleyX + 3.0, 21.0, craneZ + 0.42, C.column);
    glowWindow(trolleyX, 20.65, craneZ - 0.46, 2.3, 0.12, 1, C.warm);

    // 塔吊配重
    solidBox(jibBack - 1.4, jibY - 1.8, craneZ - 0.9, jibBack, jibY, craneZ + 0.9, C.crane);

    // 塔吊顶部的航空灯（两块互相垂直的自发光面，从各方向都能看到一点）
    glowWindow(craneX, apexY + 0.35, craneZ, 0.4, 0.4, 0, C.crane);
    glowWindow(craneX, apexY + 0.35, craneZ, 0.4, 0.4, 2, C.crane);

    /* 剖切轮廓：剖切模式固定切在 38.2 m（段 B 的顶部），
       画一圈发光的切口边，比半透明平面干净，也更像 BIM 里的"当前楼层"。 */
    var sy = 38.2;
    var sx0 = -9.3, sx1 = 6.1, sz0 = -6.2, sz1 = 6.2;
    function secLine(x0, y0, z0, x1, y1, z1) {
      section.push(x0, y0, z0, C.site[0], C.site[1], C.site[2]);
      section.push(x1, y1, z1, C.site[0], C.site[1], C.site[2]);
    }
    secLine(sx0, sy, sz0, sx1, sy, sz0);
    secLine(sx1, sy, sz0, sx1, sy, sz1);
    secLine(sx1, sy, sz1, sx0, sy, sz1);
    secLine(sx0, sy, sz1, sx0, sy, sz0);
    var corners = [[sx0, sz0], [sx1, sz0], [sx1, sz1], [sx0, sz1]];
    for (var cc = 0; cc < corners.length; cc++) {
      var cx0 = corners[cc][0];
      var cz0 = corners[cc][1];
      var dirX = cx0 < 0 ? -1 : 1;
      var dirZ = cz0 < 0 ? -1 : 1;
      secLine(cx0, sy, cz0, cx0 + dirX * 1.5, sy, cz0);
      secLine(cx0, sy, cz0, cx0, sy, cz0 + dirZ * 1.5);
    }

    return {
      solid: new Float32Array(solid),
      glass: new Float32Array(glass),
      lines: new Float32Array(lines),
      glow: new Float32Array(glow),
      section: new Float32Array(section),
      glassRanges: glassRanges
    };
  }

  /* ------------------------------------------------------------------ 初始化 */

  function init() {
    var stage = document.querySelector('[data-tm-scene]');
    if (!stage) return;

    var canvas = stage.querySelector('canvas');
    if (!canvas) return;

    // 海报按需加载：WebGL 可用时完全不下载这张图；降级时才把 data-src 换成 src。
    var fallBack = function () {
      stage.classList.remove('is-live');
      stage.classList.add('is-fallback');
      var poster = stage.querySelector('.tm-scene-poster[data-src]');
      if (poster && !poster.getAttribute('src')) {
        poster.setAttribute('src', poster.getAttribute('data-src'));
      }
    };

    var gl = canvas.getContext('webgl2', {
      alpha: true,
      antialias: true,
      depth: true,
      stencil: false,
      premultipliedAlpha: false,
      powerPreference: 'high-performance'
    });

    if (!gl) {
      fallBack();
      return;
    }

    var solidProg = program(gl, SOLID_VS, SOLID_FS, 'solid');
    var glassProg = program(gl, GLASS_VS, GLASS_FS, 'glass');
    var lineProg = program(gl, LINE_VS, LINE_FS, 'line');
    var gridProg = program(gl, GRID_VS, GRID_FS, 'grid');
    if (!solidProg || !glassProg || !lineProg || !gridProg) {
      fallBack();
      return;
    }

    stage.classList.add('is-live');

    // 驱动重置 / 上下文丢失时别留一块黑画布：退回海报。
    canvas.addEventListener('webglcontextlost', function (event) {
      event.preventDefault();
      fallBack();
    });

    gl.clearColor(0, 0, 0, 0);
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.CULL_FACE);
    gl.cullFace(gl.BACK);
    gl.enable(gl.BLEND);

    var scene = buildScene();

    function makeBuffer(data, stride, layout) {
      var vao = gl.createVertexArray();
      var buf = gl.createBuffer();
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
      for (var i = 0; i < layout.length; i++) {
        gl.enableVertexAttribArray(layout[i][0]);
        gl.vertexAttribPointer(layout[i][0], 3, gl.FLOAT, false, stride, layout[i][1]);
      }
      gl.bindVertexArray(null);
      return { vao: vao, count: data.length / (stride / 4) };
    }

    var solidBuf = makeBuffer(scene.solid, 36, [[0, 0], [1, 12], [2, 24]]);
    var glassBuf = makeBuffer(scene.glass, 36, [[0, 0], [1, 12], [2, 24]]);
    var lineBuf = makeBuffer(scene.lines, 24, [[0, 0], [2, 12]]);
    var glowBuf = makeBuffer(scene.glow, 24, [[0, 0], [2, 12]]);
    var sectionBuf = makeBuffer(scene.section, 24, [[0, 0], [2, 12]]);

    var gridVao = gl.createVertexArray();
    var gridBuf = gl.createBuffer();
    gl.bindVertexArray(gridVao);
    gl.bindBuffer(gl.ARRAY_BUFFER, gridBuf);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([
        -100, 0, -100,
        100, 0, -100,
        -100, 0, 100,
        -100, 0, 100,
        100, 0, -100,
        100, 0, 100
      ]),
      gl.STATIC_DRAW
    );
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 12, 0);
    gl.bindVertexArray(null);

    var solidU = uniforms(gl, solidProg, ['uViewProj', 'uEye', 'uCut', 'uMode', 'uOpacity']);
    var glassU = uniforms(gl, glassProg, ['uViewProj', 'uEye', 'uCut', 'uMode', 'uAlpha']);
    var lineU = uniforms(gl, lineProg, ['uViewProj', 'uEye', 'uCut', 'uMode', 'uAlpha']);
    var gridU = uniforms(gl, gridProg, ['uViewProj', 'uY', 'uColor', 'uAlpha', 'uTime', 'uSection', 'uSize']);

    /* 相机与交互状态 */
    var view = {
      az: 0.16,
      el: 0.22,
      dist: 126,
      target: [-1.8, 30.0, 0]
    };
    var introStart = performance.now();
    var introDone = reduceMotion;
    var mode = 0;
    var cut = 0;
    var cutTarget = 38.2;
    var autoSpin = !reduceMotion;
    var spinPausedUntil = 0;
    var dragging = false;
    var lastPointer = null;
    var velocity = { az: 0, el: 0 };
    var visible = true;
    var raf = 0;
    var lastFrame = performance.now();
    var cssW = 1;
    var cssH = 1;
    var dpr = 1;
    var cameraPos = [0, 0, 0];
    var viewMat = new Float32Array(16);
    var projMat = new Float32Array(16);
    var vpMat = new Float32Array(16);

    var tags = [];
    var tagNodes = stage.querySelectorAll('[data-scene-tag]');
    for (var ti = 0; ti < tagNodes.length; ti++) {
      var node = tagNodes[ti];
      tags.push({
        el: node,
        p: [
          parseFloat(node.getAttribute('data-x') || '0'),
          parseFloat(node.getAttribute('data-y') || '0'),
          parseFloat(node.getAttribute('data-z') || '0')
        ]
      });
      node.style.opacity = '0';
    }

    function resize() {
      var rect = canvas.getBoundingClientRect();
      cssW = Math.max(2, Math.round(rect.width));
      cssH = Math.max(2, Math.round(rect.height));
      dpr = Math.min(window.devicePixelRatio || 1, 2);
      var pw = Math.round(cssW * dpr);
      var ph = Math.round(cssH * dpr);
      if (canvas.width !== pw || canvas.height !== ph) {
        canvas.width = pw;
        canvas.height = ph;
      }
      gl.viewport(0, 0, pw, ph);
    }

    function currentEye() {
      var ce = Math.cos(view.el);
      return [
        view.target[0] + view.dist * ce * Math.sin(view.az),
        view.target[1] + view.dist * Math.sin(view.el),
        view.target[2] + view.dist * ce * Math.cos(view.az)
      ];
    }

    function project(p) {
      var x = p[0], y = p[1], z = p[2];
      var w = vpMat[3] * x + vpMat[7] * y + vpMat[11] * z + vpMat[15];
      if (w <= 0.0001) return null;
      var cx = (vpMat[0] * x + vpMat[4] * y + vpMat[8] * z + vpMat[12]) / w;
      var cy = (vpMat[1] * x + vpMat[5] * y + vpMat[9] * z + vpMat[13]) / w;
      return [(cx * 0.5 + 0.5) * cssW, (0.5 - cy * 0.5) * cssH];
    }

    function updateTags() {
      for (var i = 0; i < tags.length; i++) {
        if (mode === 2 && tags[i].p[1] > cut + 0.05) {
          tags[i].el.style.opacity = '0';
          continue;
        }
        var pt = project(tags[i].p);
        if (!pt) {
          tags[i].el.style.opacity = '0';
          continue;
        }
        var x = clamp(pt[0], -40, cssW + 40);
        var y = clamp(pt[1], -40, cssH + 40);
        var edgeFade = 1;
        if (pt[0] < 24 || pt[0] > cssW - 24) edgeFade = 0;
        if (pt[1] < 16 || pt[1] > cssH - 16) edgeFade = 0;
        tags[i].el.style.transform = 'translate3d(' + x.toFixed(1) + 'px,' + y.toFixed(1) + 'px,0)';
        tags[i].el.style.opacity = edgeFade ? '1' : '0';
      }
    }

    function draw() {
      var now = performance.now();
      var dt = Math.min(0.05, (now - lastFrame) / 1000);
      lastFrame = now;

      // 入场：从更远、更偏的机位轻轻推近；用户一动就收手。
      if (!introDone) {
        var t = clamp((now - introStart) / 2600, 0, 1);
        var e = easeOutCubic(t);
        view.az = 0.16 + (0.62 - 0.16) * e;
        view.dist = 126 - 24 * e;
        if (t >= 1) introDone = true;
      }

      if (!dragging) {
        if (Math.abs(velocity.az) > 0.0001 || Math.abs(velocity.el) > 0.0001) {
          view.az += velocity.az * dt;
          view.el = clamp(view.el + velocity.el * dt, 0.02, 1.18);
          var damp = Math.pow(0.0025, dt);
          velocity.az *= damp;
          velocity.el *= damp;
        } else if (autoSpin && now > spinPausedUntil && visible && !document.hidden) {
          view.az += 0.055 * dt;
        }
      }

      if (mode === 2) {
        if (cut < cutTarget) cut = Math.min(cutTarget, cut + (cutTarget - cut) * Math.min(1, dt * 2.4) + dt * 1.2);
      } else {
        cut += (0 - cut) * Math.min(1, dt * 6);
      }

      var eye = currentEye();
      cameraPos = eye;

      mat4Perspective(projMat, (38 * Math.PI) / 180, cssW / cssH, 0.5, 260);
      mat4LookAt(viewMat, eye, view.target, [0, 1, 0]);
      mat4Multiply(vpMat, projMat, viewMat);

      gl.viewport(0, 0, canvas.width, canvas.height);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

      gl.enable(gl.DEPTH_TEST);
      gl.depthMask(false);
      gl.disable(gl.CULL_FACE);

      // 地面网格（加色，先画；后面的实体写深度会把它挡住）
      gl.useProgram(gridProg);
      gl.uniformMatrix4fv(gridU.uViewProj, false, vpMat);
      gl.uniform3f(gridU.uEye, eye[0], eye[1], eye[2]);
      gl.uniform1f(gridU.uY, 0.02);
      gl.uniform3f(gridU.uColor, 0.34, 0.60, 0.72);
      gl.uniform1f(gridU.uAlpha, 1.0);
      gl.uniform1f(gridU.uTime, now / 1000);
      gl.uniform1f(gridU.uSection, 0);
      gl.uniform2f(gridU.uSize, 100, 100);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
      gl.bindVertexArray(gridVao);
      gl.drawArrays(gl.TRIANGLES, 0, 6);

      gl.depthMask(true);
      gl.enable(gl.CULL_FACE);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

      // 实体
      gl.useProgram(solidProg);
      gl.uniformMatrix4fv(solidU.uViewProj, false, vpMat);
      gl.uniform3f(solidU.uEye, eye[0], eye[1], eye[2]);
      gl.uniform1f(solidU.uCut, cut);
      gl.uniform1i(solidU.uMode, mode);
      if (mode === 1) {
        gl.depthMask(false);
        gl.uniform1f(solidU.uOpacity, 0.055);
      } else {
        gl.uniform1f(solidU.uOpacity, 1.0);
      }
      gl.bindVertexArray(solidBuf.vao);
      gl.drawArrays(gl.TRIANGLES, 0, solidBuf.count);

      // 幕墙：按离相机从远到近画，透明叠得才顺眼
      if (mode !== 1) {
        gl.depthMask(false);
        gl.useProgram(glassProg);
        gl.uniformMatrix4fv(glassU.uViewProj, false, vpMat);
        gl.uniform3f(glassU.uEye, eye[0], eye[1], eye[2]);
        gl.uniform1f(glassU.uCut, cut);
        gl.uniform1i(glassU.uMode, mode);
        gl.uniform1f(glassU.uAlpha, mode === 2 ? 0.09 : 0.16);
        gl.bindVertexArray(glassBuf.vao);

        var order = scene.glassRanges.slice().sort(function (a, b) {
          var da = (a.center[0] - eye[0]) * (a.center[0] - eye[0]) +
            (a.center[1] - eye[1]) * (a.center[1] - eye[1]) +
            (a.center[2] - eye[2]) * (a.center[2] - eye[2]);
          var db = (b.center[0] - eye[0]) * (b.center[0] - eye[0]) +
            (b.center[1] - eye[1]) * (b.center[1] - eye[1]) +
            (b.center[2] - eye[2]) * (b.center[2] - eye[2]);
          return db - da;
        });
        for (var gi = 0; gi < order.length; gi++) {
          gl.drawArrays(gl.TRIANGLES, order[gi].start, order[gi].count);
        }
      }

      // 线框 / 轮廓光
      gl.useProgram(lineProg);
      gl.uniformMatrix4fv(lineU.uViewProj, false, vpMat);
      gl.uniform3f(lineU.uEye, eye[0], eye[1], eye[2]);
      gl.uniform1f(lineU.uCut, cut);
      gl.uniform1i(lineU.uMode, mode);
      gl.uniform1f(lineU.uAlpha, mode === 1 ? 1.0 : 0.82);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
      gl.depthMask(false);
      if (mode === 1) {
        gl.disable(gl.DEPTH_TEST);
      }
      gl.bindVertexArray(lineBuf.vao);
      gl.drawArrays(gl.LINES, 0, lineBuf.count);

      // 自发光：亮窗、灯带、塔吊顶灯。关闭背面剔除，双面可见。
      gl.disable(gl.CULL_FACE);
      gl.useProgram(lineProg);
      gl.uniform1f(lineU.uAlpha, 0.9);
      gl.bindVertexArray(glowBuf.vao);
      gl.drawArrays(gl.TRIANGLES, 0, glowBuf.count);
      gl.enable(gl.CULL_FACE);
      gl.enable(gl.DEPTH_TEST);

      // 剖切轮廓：切口边缘的一圈发光线。cut 快到 38.2 时才淡入，
      // 这样剖切动画从下往上扫的时候不会先看到一圈悬空的框。
      if (mode === 2) {
        gl.useProgram(lineProg);
        gl.uniformMatrix4fv(lineU.uViewProj, false, vpMat);
        gl.uniform3f(lineU.uEye, eye[0], eye[1], eye[2]);
        gl.uniform1f(lineU.uCut, 1000.0);
        gl.uniform1i(lineU.uMode, 0);
        gl.uniform1f(lineU.uAlpha, clamp((cut - 37.0) / 1.3, 0.0, 1.0) * 0.95);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
        gl.bindVertexArray(sectionBuf.vao);
        gl.drawArrays(gl.LINES, 0, sectionBuf.count);
      }

      updateTags();
      gl.bindVertexArray(null);

      if (visible && !document.hidden) {
        raf = requestAnimationFrame(draw);
      } else {
        raf = 0;
      }
    }

    function start() {
      if (!raf) {
        lastFrame = performance.now();
        raf = requestAnimationFrame(draw);
      }
    }

    function stop() {
      if (raf) {
        cancelAnimationFrame(raf);
        raf = 0;
      }
    }

    function pauseSpin(ms) {
      spinPausedUntil = performance.now() + ms;
    }

    /* ---- 模式切换 ---- */
    var modeButtons = stage.querySelectorAll('[data-scene-mode]');
    function setMode(next) {
      mode = next;
      if (mode === 2) {
        cut = 0;
      }
      for (var i = 0; i < modeButtons.length; i++) {
        var active = Number(modeButtons[i].getAttribute('data-scene-mode')) === mode;
        modeButtons[i].classList.toggle('is-active', active);
        modeButtons[i].setAttribute('aria-pressed', active ? 'true' : 'false');
      }
      start();
    }
    for (var bi = 0; bi < modeButtons.length; bi++) {
      (function (btn) {
        btn.addEventListener('click', function () {
          setMode(Number(btn.getAttribute('data-scene-mode')));
        });
      })(modeButtons[bi]);
    }

    /* ---- 复位 / 自转 / 缩放 ---- */
    function resetView() {
      view.az = 0.62;
      view.el = 0.22;
      view.dist = 102;
      velocity.az = 0;
      velocity.el = 0;
      introDone = true;
      start();
    }

    var resetBtn = stage.querySelector('[data-scene-reset]');
    if (resetBtn) resetBtn.addEventListener('click', resetView);

    var spinBtn = stage.querySelector('[data-scene-spin]');
    function syncSpin() {
      if (!spinBtn) return;
      spinBtn.setAttribute('aria-pressed', autoSpin ? 'true' : 'false');
      spinBtn.classList.toggle('is-paused', !autoSpin);
      var icon = spinBtn.querySelector('i');
      if (icon) icon.className = autoSpin ? 'fas fa-pause' : 'fas fa-play';
      spinBtn.setAttribute('aria-label', autoSpin ? '暂停自动旋转' : '继续自动旋转');
      spinBtn.setAttribute('title', autoSpin ? '自动旋转' : '继续自动旋转');
    }
    if (spinBtn) {
      spinBtn.addEventListener('click', function () {
        autoSpin = !autoSpin;
        if (!autoSpin) velocity.az = 0;
        syncSpin();
        start();
      });
      syncSpin();
    }

    var zoomIn = stage.querySelector('[data-scene-zoom="in"]');
    var zoomOut = stage.querySelector('[data-scene-zoom="out"]');
    function zoom(factor) {
      view.dist = clamp(view.dist * factor, 55, 155);
      introDone = true;
      start();
    }
    if (zoomIn) zoomIn.addEventListener('click', function () { zoom(0.88); });
    if (zoomOut) zoomOut.addEventListener('click', function () { zoom(1.12); });

    // 允许页面上用 data-mode 指定初始模式（截图 / 视觉回归 / 深链调试用）。
    var initialMode = Number(stage.getAttribute('data-mode') || '0');
    if (initialMode === 1 || initialMode === 2) {
      setMode(initialMode);
      // 截图 / 视觉回归时直接给到展开状态，不用等动画跑完。
      if (initialMode === 2) cut = cutTarget;
    }

    /* ---- 指针 ---- */
    canvas.addEventListener('pointerdown', function (event) {
      if (event.button !== 0 && event.pointerType === 'mouse') return;
      dragging = true;
      introDone = true;
      velocity.az = 0;
      velocity.el = 0;
      lastPointer = { x: event.clientX, y: event.clientY };
      canvas.setPointerCapture(event.pointerId);
      canvas.classList.add('is-grabbing');
      start();
    });

    canvas.addEventListener('pointermove', function (event) {
      if (!dragging || !lastPointer) return;
      var dx = event.clientX - lastPointer.x;
      var dy = event.clientY - lastPointer.y;
      lastPointer = { x: event.clientX, y: event.clientY };
      var dAz = -dx * 0.0062;
      var dEl = dy * 0.0045;
      view.az += dAz;
      view.el = clamp(view.el + dEl, 0.02, 1.18);
      velocity.az = dAz * 6;
      velocity.el = dEl * 6;
    });

    function endDrag(event) {
      if (!dragging) return;
      dragging = false;
      lastPointer = null;
      canvas.classList.remove('is-grabbing');
      pauseSpin(2600);
      if (event && event.pointerId !== undefined && canvas.hasPointerCapture(event.pointerId)) {
        canvas.releasePointerCapture(event.pointerId);
      }
    }
    canvas.addEventListener('pointerup', endDrag);
    canvas.addEventListener('pointercancel', endDrag);
    canvas.addEventListener('pointerleave', function () {
      if (dragging) endDrag();
    });

    canvas.addEventListener(
      'wheel',
      function (event) {
        // 只在指针确实落在画布上时接管滚轮；页面滚动不受影响。
        event.preventDefault();
        introDone = true;
        var delta = event.deltaMode === 1 ? event.deltaY * 16 : event.deltaY;
        view.dist = clamp(view.dist * Math.exp(delta * 0.0011), 55, 155);
        pauseSpin(2200);
        start();
      },
      { passive: false }
    );

    canvas.addEventListener('dblclick', function (event) {
      event.preventDefault();
      resetView();
    });

    canvas.addEventListener('keydown', function (event) {
      var handled = true;
      switch (event.key) {
        case 'ArrowLeft': view.az -= 0.09; break;
        case 'ArrowRight': view.az += 0.09; break;
        case 'ArrowUp': view.el = clamp(view.el + 0.06, 0.02, 1.18); break;
        case 'ArrowDown': view.el = clamp(view.el - 0.06, 0.02, 1.18); break;
        case '+': case '=': zoom(0.9); break;
        case '-': case '_': zoom(1.1); break;
        case '1': setMode(0); break;
        case '2': setMode(1); break;
        case '3': setMode(2); break;
        case '0': resetView(); break;
        default: handled = false;
      }
      if (handled) {
        event.preventDefault();
        introDone = true;
        pauseSpin(2200);
        start();
      }
    });

    /* ---- 尺寸 / 可见性 ---- */
    if ('ResizeObserver' in window) {
      var ro = new ResizeObserver(function () {
        resize();
        start();
      });
      ro.observe(canvas);
    } else {
      window.addEventListener('resize', function () {
        resize();
        start();
      });
    }

    if ('IntersectionObserver' in window) {
      var io = new IntersectionObserver(
        function (entries) {
          visible = entries[0].isIntersecting;
          if (visible) start();
          else stop();
        },
        { rootMargin: '120px' }
      );
      io.observe(stage);
    }

    document.addEventListener('visibilitychange', function () {
      if (document.hidden) stop();
      else start();
    });

    resize();
    start();

    // 首帧之后再把标签露出来，避免 0,0 位置闪一下。
    setTimeout(function () {
      stage.classList.add('is-ready');
    }, 420);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();





