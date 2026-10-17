/* ==========================================================================
   Tamias 首页 Hero 三维演示（无依赖，WebGL2）
   --------------------------------------------------------------------------
   这不是查看器，只是一段"会动的封面"：一个混合开发街区：商场裙房、三栋塔楼、广场与街道，
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
    '  col = mix(col, vec3(0.018, 0.028, 0.050), smoothstep(180.0, 460.0, dist));',
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
    '  a *= 1.0 - smoothstep(220.0, 520.0, dist) * 0.65;',
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
    '  float fog = 1.0 - smoothstep(200.0, 520.0, dist);',
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
    '  float minor = lineMask(p, 5.0, 0.055) * 0.26;',
    '  float major = lineMask(p, 25.0, 0.11) * 0.55;',
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
    '    float ring = mod(uTime * 4.0, 190.0);',
    '    float scan = 1.0 - smoothstep(0.0, 2.6, abs(r - ring));',
    '    scan *= 1.0 - smoothstep(50.0, 190.0, r);',
    '    alpha += scan * 0.10;',
    '    alpha *= 1.0 - smoothstep(90.0, 280.0, length(p));',
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
      slab: hexColor('#93a1b6'),
      slabEdge: hexColor('#b6c6db'),
      column: hexColor('#6f7f96'),
      core: hexColor('#1b2536'),
      coreEdge: hexColor('#5d86c9'),
      podium: hexColor('#8b9ab0'),
      roof: hexColor('#4d5d75'),
      glass: hexColor('#3566c4'),
      mullion: hexColor('#5f86d6'),
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

    var P = {
      mall: hexColor('#77828f'),
      mallDark: hexColor('#5f6d83'),
      interior: hexColor('#ffd9a4'),
      leaf: hexColor('#3f7a63'),
      leaf2: hexColor('#539a7b'),
      trunk: hexColor('#5b4a3a'),
      context: hexColor('#192332'),
      context2: hexColor('#202d40'),
      road: hexColor('#0a0f16'),
      plaza: hexColor('#151d29'),
      plazaLine: hexColor('#2b3a50'),
      water: hexColor('#1b4b5b'),
      carWhite: hexColor('#d6deea'),
      carBlue: hexColor('#3d6fd0'),
      carDark: hexColor('#1b2636'),
      red: hexColor('#ff6b5c'),
      screen: hexColor('#8ff4ff')
    };

    function sphere(cx, cy, cz, rx, ry, rz, color, latSeg, lonSeg) {
      latSeg = latSeg || 3;
      lonSeg = lonSeg || 6;
      for (var i = 0; i < latSeg; i++) {
        var v0 = -Math.PI / 2 + Math.PI * i / latSeg;
        var v1 = -Math.PI / 2 + Math.PI * (i + 1) / latSeg;
        for (var j = 0; j < lonSeg; j++) {
          var u0 = Math.PI * 2 * j / lonSeg;
          var u1 = Math.PI * 2 * (j + 1) / lonSeg;
          var p0 = [cx + rx * Math.cos(v0) * Math.cos(u0), cy + ry * Math.sin(v0), cz + rz * Math.cos(v0) * Math.sin(u0)];
          var p1 = [cx + rx * Math.cos(v0) * Math.cos(u1), cy + ry * Math.sin(v0), cz + rz * Math.cos(v0) * Math.sin(u1)];
          var p2 = [cx + rx * Math.cos(v1) * Math.cos(u1), cy + ry * Math.sin(v1), cz + rz * Math.cos(v1) * Math.sin(u1)];
          var p3 = [cx + rx * Math.cos(v1) * Math.cos(u0), cy + ry * Math.sin(v1), cz + rz * Math.cos(v1) * Math.sin(u0)];
          var ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
          var bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
          var nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
          var nl = Math.hypot(nx, ny, nz);
          if (nl < 0.0001) continue;
          nx /= nl; ny /= nl; nz /= nl;
          tri(p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], p2[0], p2[1], p2[2], nx, ny, nz, color, solid);
          tri(p0[0], p0[1], p0[2], p2[0], p2[1], p2[2], p3[0], p3[1], p3[2], nx, ny, nz, color, solid);
        }
      }
    }

    /* ---------------------------------------------------------- 场地 */
    solidBox(-88, -0.8, -72, 88, -0.05, 82, hexColor('#0b111b'));

    var siteCorners = [[-84, -78], [84, -78], [84, 78], [-84, 78]];
    for (var si = 0; si < 4; si++) {
      var sa = siteCorners[si];
      var sb = siteCorners[(si + 1) % 4];
      line(sa[0], 0.12, sa[1], sb[0], 0.12, sb[1], C.site, 0.26);
    }
    for (var si2 = 0; si2 < 4; si2++) {
      var sp = siteCorners[si2];
      line(sp[0] - 2.2, 0.13, sp[1], sp[0] + 2.2, 0.13, sp[1], C.site, 0.55);
      line(sp[0], 0.13, sp[1] - 2.2, sp[0], 0.13, sp[1] + 2.2, C.site, 0.55);
    }

    function siteRing(radius, y, alpha, seg) {
      for (var i = 0; i < seg; i++) {
        var a0 = (i / seg) * Math.PI * 2;
        var a1 = ((i + 1) / seg) * Math.PI * 2;
        line(Math.cos(a0) * radius, y, Math.sin(a0) * radius,
          Math.cos(a1) * radius, y, Math.sin(a1) * radius, C.site, alpha);
      }
    }
    siteRing(82, 0.13, 0.07, 128);
    siteRing(68, 0.13, 0.10, 128);
    siteRing(50, 0.13, 0.15, 96);
    for (var tick = 0; tick < 16; tick++) {
      var ta = (tick / 16) * Math.PI * 2;
      var r0 = tick % 4 === 0 ? 62 : 80;
      line(Math.cos(ta) * r0, 0.14, Math.sin(ta) * r0,
        Math.cos(ta) * 82, 0.14, Math.sin(ta) * 82, C.site, tick % 4 === 0 ? 0.4 : 0.2);
    }

    /* 前场铺装 / 道路 */
    solidBox(-70, 0, 40, 70, 0.07, 64, P.plaza);
    for (var px = -66; px <= 66; px += 6) {
      line(px, 0.1, 40, px, 0.1, 64, P.plazaLine, 0.3);
    }
    for (var pz = 42; pz <= 62; pz += 5) {
      line(-70, 0.1, pz, 70, 0.1, pz, P.plazaLine, 0.25);
    }
    solidBox(-84, 0, 64, 84, 0.05, 78, P.road);
    line(-84, 0.1, 64, 84, 0.1, 64, C.site, 0.22);
    line(-84, 0.1, 78, 84, 0.1, 78, C.site, 0.18);
    for (var lx = -80; lx < 80; lx += 9) {
      line(lx, 0.09, 71, lx + 4, 0.09, 71, C.slabEdge, 0.22);
    }
    for (var cz = 65.5; cz < 78; cz += 2) {
      line(-9, 0.09, cz, 9, 0.09, cz, C.slabEdge, 0.18);
    }

    /* ---------------------------------------------------------- 商场裙房 */
    function mallBlock(x0, x1, z0, z1, levels, fh, front) {
      var top = levels * fh;
      for (var f = 0; f < levels; f++) {
        var y = f * fh;
        solidBox(x0 - 0.55, y, z0 - 0.55, x1 + 0.55, y + 0.4, z1 + 0.55, P.mall);
        boxEdges(x0 - 0.55, y, z0 - 0.55, x1 + 0.55, y + 0.4, z1 + 0.55, C.slabEdge, 0.32, true);
        glassBox(x0, y + 0.4, z0, x1, y + fh - 0.05, z1, C.glass);
        boxEdges(x0, y + 0.4, z0, x1, y + fh - 0.05, z1, C.mullion, 0.18);

        line(x0, y + 0.65, z1, x1, y + 0.65, z1, P.interior, 0.55);
        line(x0, y + fh - 0.3, z1, x1, y + fh - 0.3, z1, P.interior, 0.4);
        line(x1, y + 0.65, z0, x1, y + 0.65, z1, P.interior, 0.22);
        line(x0, y + 0.65, z0, x0, y + 0.65, z1, P.interior, 0.22);

        if (front) {
          for (var wx = x0 + 8; wx < x1 - 6; wx += 14) {
            glowWindow(wx, y + fh * 0.5, z1, 2.1, fh * 0.24, 0, P.interior);
          }
        }
      }

      for (var mx = x0 + 3; mx < x1 - 1; mx += 6) {
        line(mx, 0.2, z1 + 0.02, mx, top, z1 + 0.02, C.mullion, 0.12);
      }
      for (var mz = z0 + 3; mz < z1 - 1; mz += 6) {
        line(x1 + 0.02, 0.2, mz, x1 + 0.02, top, mz, C.mullion, 0.1);
      }

      solidBox(x0 - 0.8, top, z0 - 0.8, x1 + 0.8, top + 0.55, z1 + 0.8, C.roof);
      boxEdges(x0 - 0.8, top, z0 - 0.8, x1 + 0.8, top + 0.55, z1 + 0.8, C.slabEdge, 0.4);
      for (var cx2 = x0 + 2; cx2 <= x1 - 2; cx2 += 8) {
        solidBox(cx2 - 0.3, top, z1 - 0.6, cx2 + 0.3, top + 2.4, z1 + 0.3, C.column);
      }
      return top;
    }

    var mallTop = mallBlock(-60, 60, 0, 40, 5, 4.4, true);
    var backTop = mallBlock(-60, 60, -26, 0, 6, 4.33, false);

    /* 中庭天窗 */
    glassBox(-28, mallTop + 0.55, 6, 28, mallTop + 2.8, 30, C.glass);
    boxEdges(-28, mallTop + 0.55, 6, 28, mallTop + 2.8, 30, C.slabEdge, 0.32);
    for (var sx = -26; sx <= 26; sx += 8) {
      line(sx, mallTop + 2.85, 6, sx, mallTop + 2.85, 30, C.slabEdge, 0.2);
    }
    line(-28, mallTop + 1.7, 18, 28, mallTop + 1.7, 18, P.interior, 0.35);

    /* 商场入口、雨棚、大屏、台阶 */
    solidBox(-15, 0, 38.8, -12, 26, 40.8, P.mallDark);
    solidBox(12, 0, 38.8, 15, 26, 40.8, P.mallDark);
    solidBox(-15, 24, 38.8, 15, 26, 40.8, P.mallDark);
    solidBox(-12, 0, 39.6, 12, 23.5, 40.4, C.glass);
    line(-15, 12, 40.85, -15, 26, 40.85, C.site, 0.3);
    line(15, 12, 40.85, 15, 26, 40.85, C.site, 0.3);
    line(-15, 21, 40.85, 15, 21, 40.85, P.interior, 0.4);
    solidBox(-16, 5.4, 40, 16, 6.1, 49, C.roof);
    line(-16, 5.5, 48.9, 16, 5.5, 48.9, P.interior, 0.65);
    for (var lz = 41; lz <= 48; lz += 1.6) {
      glowWindow(0, 5.1, lz, 14, 0.08, 0, P.interior);
    }
    solidBox(-14, 1.2, 40.4, 14, 2.0, 40.9, P.mallDark);
    solidBox(-9.2, 12.3, 40.5, 9.2, 19.3, 40.72, P.mallDark);
    glowQuad(
      [-8.2, 13.2, 40.78], [8.2, 13.2, 40.78],
      [8.2, 18.4, 40.78], [-8.2, 18.4, 40.78], hexColor('#1f7f92')
    );
    for (var sx3 = -4; sx3 <= 4; sx3 += 4) {
      line(sx3, 13.2, 40.8, sx3, 18.4, 40.8, C.site, 0.22);
    }
    line(-8.4, 12.9, 40.8, 8.4, 12.9, 40.8, C.site, 0.45);
    line(-8.4, 18.8, 40.8, 8.4, 18.8, 40.8, C.site, 0.3);

    for (var st = 0; st < 5; st++) {
      solidBox(-11 + st * 0.5, 0, 41 + st * 0.7, 11 - st * 0.5, 0.18 + st * 0.34, 42.3 + st * 0.7, P.mall);
      line(-11 + st * 0.5, 0.2 + st * 0.34, 42.3 + st * 0.7,
        11 - st * 0.5, 0.2 + st * 0.34, 42.3 + st * 0.7, C.site, 0.3);
    }

    for (var ax = -52; ax <= 52; ax += 13) {
      if (Math.abs(ax) < 20) continue;
      solidBox(ax - 4, 4.6, 40, ax + 4, 5.0, 42, P.mallDark);
      line(ax - 4, 4.65, 41.9, ax + 4, 4.65, 41.9, P.interior, 0.55);
      solidBox(ax - 4, 9.0, 40, ax + 4, 9.4, 42, P.mallDark);
      line(ax - 4, 9.05, 41.9, ax + 4, 9.05, 41.9, P.interior, 0.45);
      glowWindow(ax, 5.65, 40.1, 2.6, 0.22, 0, (ax % 2 === 0) ? C.cool : P.interior);
    }

    /* 屋顶花园 + 天窗 + 露台 */
    for (var rp = 0; rp < 7; rp++) {
      var rxp = -54 + rp * 16;
      if (rxp > -16 && rxp < 16) continue;
      solidBox(rxp - 2, mallTop + 0.55, 30, rxp + 2, mallTop + 1.4, 34, P.leaf);
      solidBox(rxp - 2.2, mallTop + 0.55, 29.8, rxp + 2.2, mallTop + 0.85, 34.2, P.plazaLine);
    }
    solidBox(30, mallTop + 0.55, 8, 46, mallTop + 3.6, 22, C.roof);
    boxEdges(30, mallTop + 0.55, 8, 46, mallTop + 3.6, 22, C.slabEdge, 0.3);
    line(31, mallTop + 1.2, 21.9, 45, mallTop + 1.2, 21.9, P.interior, 0.45);
    for (var pc = 0; pc < 4; pc++) {
      var pcx = -50 + pc * 33;
      solidBox(pcx - 0.4, mallTop + 0.55, 2, pcx + 0.4, mallTop + 3.4, 3, C.column);
    }

    /* ---------------------------------------------------------- 塔楼 */
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

    function tower(opts) {
      var cx = opts.cx, cz = opts.cz, w = opts.w, d = opts.d;
      var base = opts.base, floors = opts.floors, fh = opts.fh || 4;
      var setback = opts.setback || floors;
      var top = base + floors * fh;
      var style = opts.style || 'office';

      for (var f = 0; f < floors; f++) {
        var narrow = f >= setback;
        var ww = narrow ? w * 0.82 : w;
        var dd = narrow ? d * 0.82 : d;
        var y = base + f * fh;

        solidBox(cx - ww / 2 - 0.5, y, cz - dd / 2 - 0.5, cx + ww / 2 + 0.5, y + 0.3, cz + dd / 2 + 0.5, C.slab);
        boxEdges(cx - ww / 2 - 0.5, y, cz - dd / 2 - 0.5, cx + ww / 2 + 0.5, y + 0.3, cz + dd / 2 + 0.5, C.slabEdge, 0.34, true);

        if (f < floors - 1) {
          glassBox(cx - ww / 2, y + 0.3, cz - dd / 2, cx + ww / 2, y + fh - 0.05, cz + dd / 2, C.glass);
          boxEdges(cx - ww / 2, y + 0.3, cz - dd / 2, cx + ww / 2, y + fh - 0.05, cz + dd / 2, C.mullion, 0.18);
          line(cx - ww / 2, y + fh - 0.22, cz + dd / 2, cx + ww / 2, y + fh - 0.22, cz + dd / 2, C.warm, 0.32);
          line(cx - ww / 2, y + fh - 0.22, cz - dd / 2, cx + ww / 2, y + fh - 0.22, cz - dd / 2, C.warm, 0.18);

          var lit = floorLightPattern[f % floorLightPattern.length];
          for (var lf = 0; lf < 4; lf++) {
            var slots = lit[lf];
            if (!slots) continue;
            for (var wi = 0; wi < slots.length; wi++) {
              var slot = slots[wi];
              var spanX = ww - 3;
              var spanZ = dd - 3;
              var px2 = cx - spanX / 2 + spanX * ((slot * 37 + f * 13) % 100) / 100;
              var pz2 = cz - spanZ / 2 + spanZ * ((slot * 53 + f * 7) % 100) / 100;
              var wc = slot % 3 === 0 ? C.cool : C.warm;
              if (lf === 0) glowWindow(px2, y + fh * 0.5, cz + dd / 2, 0.55, 0.68, 0, wc);
              else if (lf === 1) glowWindow(px2, y + fh * 0.5, cz - dd / 2, 0.55, 0.68, 1, wc);
              else if (lf === 2) glowWindow(cx + ww / 2, y + fh * 0.5, pz2, 0.55, 0.68, 2, wc);
              else glowWindow(cx - ww / 2, y + fh * 0.5, pz2, 0.55, 0.68, 3, wc);
            }
          }

          if (style === 'res') {
            solidBox(cx - ww / 2, y + 0.3, cz + dd / 2, cx + ww / 2, y + 0.8, cz + dd / 2 + 1.5, C.slab);
            line(cx - ww / 2, y + 0.9, cz + dd / 2 + 1.5, cx + ww / 2, y + 0.9, cz + dd / 2 + 1.5, C.warm, 0.28);
          }
        }
      }

      var mainHalf = w / 2;
      var mainDepth = d / 2;
      for (var cxi = -1; cxi <= 1; cxi += 2) {
        for (var czi = -1; czi <= 1; czi += 2) {
          solidBox(cx + cxi * mainHalf - 0.22, base, cz + czi * mainDepth - 0.22,
            cx + cxi * mainHalf + 0.22, base + setback * fh, cz + czi * mainDepth + 0.22, C.column);
          solidBox(cx + cxi * mainHalf * 0.82 - 0.22, base + setback * fh, cz + czi * mainDepth * 0.82 - 0.22,
            cx + cxi * mainHalf * 0.82 + 0.22, top, cz + czi * mainDepth * 0.82 + 0.22, C.column);
        }
      }

      solidBox(cx - w * 0.16, base, cz - d * 0.16, cx + w * 0.16, top, cz + d * 0.16, C.core);
      boxEdges(cx - w * 0.16, base, cz - d * 0.16, cx + w * 0.16, top, cz + d * 0.16, C.coreEdge, 0.2);

      var roofW = w * 0.82;
      var roofD = d * 0.82;
      solidBox(cx - roofW / 2 - 0.4, top, cz - roofD / 2 - 0.4, cx + roofW / 2 + 0.4, top + 1.4, cz + roofD / 2 + 0.4, C.roof);
      boxEdges(cx - roofW / 2 - 0.4, top, cz - roofD / 2 - 0.4, cx + roofW / 2 + 0.4, top + 1.4, cz + roofD / 2 + 0.4, C.slabEdge, 0.4);
      line(cx - roofW / 2, top + 1.45, cz + roofD / 2, cx + roofW / 2, top + 1.45, cz + roofD / 2, C.warm, 0.4);

      if (opts.spire) {
        line(cx, top + 1.4, cz, cx, top + 7.5, cz, C.slabEdge, 0.65);
        line(cx - 1.6, top + 5.2, cz, cx + 1.6, top + 5.2, cz, C.slabEdge, 0.4);
        glowWindow(cx, top + 7.9, cz, 0.42, 0.42, 0, C.cool);
        glowWindow(cx, top + 7.9, cz, 0.42, 0.42, 2, C.cool);
      }
      return top;
    }

    var towerATop = tower({ cx: -36, cz: -13, w: 26, d: 26, base: backTop, floors: 24, fh: 4, setback: 18, style: 'office', spire: true });
    var towerBTop = tower({ cx: 36, cz: -13, w: 22, d: 24, base: backTop, floors: 18, fh: 4, setback: 14, style: 'res' });

    /* 空中连廊 */
    solidBox(-23, 58, -17, 25, 58.5, -7, C.slab);
    glassBox(-23, 58.5, -16.8, 25, 61.2, -7.2, C.glass);
    boxEdges(-23, 58, -17, 25, 61.2, -7, C.slabEdge, 0.4);
    line(-23, 61.25, -7, 25, 61.25, -7, C.warm, 0.35);
    for (var bx = -20; bx <= 20; bx += 8) {
      glowWindow(bx, 59.8, -7.18, 2.4, 0.7, 0, P.interior);
    }

    /* 酒店 / 公寓 */
    var hotelTop = tower({ cx: 0, cz: -44, w: 36, d: 24, base: 0, floors: 11, fh: 4, setback: 5, style: 'res' });
    solidBox(-24, 0, -57, 24, 12, -31, P.mall);
    boxEdges(-24, 0, -57, 24, 12, -31, C.slabEdge, 0.25);
    for (var hf = 0; hf < 3; hf++) {
      line(-24, hf * 4 + 3.8, -30.9, 24, hf * 4 + 3.8, -30.9, P.interior, 0.35);
    }

    /* ---------------------------------------------------------- 前场景观 */
    function tree(x, z, s) {
      solidBox(x - 1.9 * s, 0, z - 1.9 * s, x + 1.9 * s, 0.45, z + 1.9 * s, P.plazaLine);
      solidBox(x - 0.24 * s, 0.45, z - 0.24 * s, x + 0.24 * s, 2.4 * s, z + 0.24 * s, P.trunk);
      sphere(x, 3.5 * s, z, 1.9 * s, 2.1 * s, 1.9 * s, P.leaf, 3, 6);
      sphere(x + 0.9 * s, 3.1 * s, z - 0.4 * s, 1.3 * s, 1.5 * s, 1.3 * s, P.leaf2, 3, 5);
      sphere(x - 0.9 * s, 3.0 * s, z + 0.5 * s, 1.2 * s, 1.4 * s, 1.2 * s, P.leaf2, 3, 5);
    }

    var treeSpots = [
      [-56, 47, 1.1], [-44, 56, 1.0], [-32, 47, 1.15], [-20, 58, 0.9],
      [20, 58, 0.9], [32, 47, 1.1], [44, 56, 1.0], [56, 47, 1.05],
      [-58, 60, 0.85], [58, 60, 0.85]
    ];
    for (var ti2 = 0; ti2 < treeSpots.length; ti2++) {
      tree(treeSpots[ti2][0], treeSpots[ti2][1], treeSpots[ti2][2]);
    }

    /* 水景 */
    solidBox(-9, 0.15, 48, 9, 0.8, 58, P.water);
    boxEdges(-9, 0.15, 48, 9, 0.8, 58, C.site, 0.3);
    line(-9, 0.85, 48, 9, 0.85, 48, C.site, 0.55);
    line(-9, 0.85, 58, 9, 0.85, 58, C.site, 0.4);
    for (var wj = -6; wj <= 6; wj += 4) {
      line(wj, 0.85, 53, wj, 3.0, 53, C.cool, 0.28);
    }

    /* 路灯 */
    function lamp(x, z) {
      solidBox(x - 0.12, 0, z - 0.12, x + 0.12, 5.4, z + 0.12, C.column);
      glowWindow(x, 5.55, z, 0.38, 0.32, 0, C.warm);
      glowWindow(x, 5.55, z, 0.38, 0.32, 2, C.warm);
    }
    var lampSpots = [[-62, 62], [-40, 62], [-18, 62], [18, 62], [40, 62], [62, 62], [-48, 44], [48, 44]];
    for (var li2 = 0; li2 < lampSpots.length; li2++) lamp(lampSpots[li2][0], lampSpots[li2][1]);

    /* 车 */
    function car(x, z, color, flip) {
      solidBox(x - 2.1, 0.2, z - 0.9, x + 2.1, 1.2, z + 0.9, color);
      solidBox(x - 1.15, 1.2, z - 0.78, x + 1.15, 1.85, z + 0.78, P.carDark);
      var wheelY = 0.0;
      for (var wx2 = -1.5; wx2 <= 1.5; wx2 += 3) {
        solidBox(x + wx2 - 0.35, wheelY, z - 1.0, x + wx2 + 0.35, 0.55, z - 0.72, P.carDark);
        solidBox(x + wx2 - 0.35, wheelY, z + 0.72, x + wx2 + 0.35, 0.55, z + 1.0, P.carDark);
      }
      if (flip) {
        glowWindow(x - 2.12, 0.85, z - 0.5, 0.2, 0.16, 3, C.warm);
        glowWindow(x - 2.12, 0.85, z + 0.5, 0.2, 0.16, 3, C.warm);
        glowWindow(x + 2.12, 0.8, z - 0.55, 0.18, 0.13, 2, P.red);
      } else {
        glowWindow(x + 2.12, 0.85, z - 0.5, 0.2, 0.16, 2, C.cool);
        glowWindow(x + 2.12, 0.85, z + 0.5, 0.2, 0.16, 2, C.cool);
        glowWindow(x - 2.12, 0.8, z - 0.55, 0.18, 0.13, 3, P.red);
      }
    }
    car(-62, 69, P.carWhite, false);
    car(-48, 72, P.carBlue, true);
    car(-30, 69, P.carDark, false);
    car(-12, 73, P.carWhite, false);
    car(10, 69, P.carBlue, false);
    car(30, 72, P.carDark, true);
    car(50, 69, P.carWhite, false);
    car(66, 73, P.carDark, false);
    solidBox(-40, 0.2, 75, -30, 3.0, 77, P.carWhite);
    solidBox(-38, 3.0, 75.2, -32, 3.4, 76.8, P.carDark);
    for (var busX = -39; busX <= -31; busX += 1.6) {
      glowWindow(busX, 2.45, 77.02, 0.5, 0.28, 0, C.warm);
    }

    /* ---------------------------------------------------------- 周边城市 */
    function contextBlock(x, z, w, d, h, color, seed) {
      solidBox(x - w / 2, 0, z - d / 2, x + w / 2, h, z + d / 2, color);
      boxEdges(x - w / 2, 0, z - d / 2, x + w / 2, h, z + d / 2, C.coreEdge, 0.12);
      for (var f = 2; f < h - 2; f += 4) {
        for (var wi = 0; wi < 4; wi++) {
          if ((f * 7 + wi * 11 + seed) % 5 !== 0) continue;
          var wx3 = x - w / 2 + 3 + wi * ((w - 6) / 4);
          glowWindow(wx3, f, z + d / 2, 0.5, 0.45, 0, (seed + wi) % 4 === 0 ? C.cool : C.warm);
        }
      }
    }
    contextBlock(-102, -40, 28, 34, 34, P.context, 1);
    contextBlock(-104, 6, 24, 30, 22, P.context2, 2);
    contextBlock(-100, 44, 30, 28, 16, P.context, 3);
    contextBlock(104, -42, 26, 32, 28, P.context2, 4);
    contextBlock(106, 2, 28, 34, 38, P.context, 5);
    contextBlock(102, 44, 24, 28, 18, P.context2, 6);
    contextBlock(-56, -88, 34, 28, 44, P.context2, 7);
    contextBlock(-16, -92, 28, 26, 30, P.context, 8);
    contextBlock(22, -90, 32, 30, 48, P.context2, 9);
    contextBlock(58, -86, 26, 26, 26, P.context, 10);

    /* ---------------------------------------------------------- 剖切轮廓 */
    var sy = 40.2;
    function secRect(x0, x1, z0, z1) {
      section.push(x0, sy, z0, C.site[0], C.site[1], C.site[2]);
      section.push(x1, sy, z0, C.site[0], C.site[1], C.site[2]);
      section.push(x1, sy, z0, C.site[0], C.site[1], C.site[2]);
      section.push(x1, sy, z1, C.site[0], C.site[1], C.site[2]);
      section.push(x1, sy, z1, C.site[0], C.site[1], C.site[2]);
      section.push(x0, sy, z1, C.site[0], C.site[1], C.site[2]);
      section.push(x0, sy, z1, C.site[0], C.site[1], C.site[2]);
      section.push(x0, sy, z0, C.site[0], C.site[1], C.site[2]);
      var tickLen = 2.4;
      var cs = [[x0, z0, -1, -1], [x1, z0, 1, -1], [x1, z1, 1, 1], [x0, z1, -1, 1]];
      for (var ci = 0; ci < 4; ci++) {
        var ccx = cs[ci][0], ccz = cs[ci][1];
        section.push(ccx, sy, ccz, C.site[0], C.site[1], C.site[2]);
        section.push(ccx + cs[ci][2] * tickLen, sy, ccz, C.site[0], C.site[1], C.site[2]);
        section.push(ccx, sy, ccz, C.site[0], C.site[1], C.site[2]);
        section.push(ccx, sy, ccz + cs[ci][3] * tickLen, C.site[0], C.site[1], C.site[2]);
      }
    }
    secRect(-49, -23, -26, 0);
    secRect(25, 47, -25, -1);
    secRect(-18, 18, -56, -32);


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
        -300, 0, -300,
        300, 0, -300,
        -300, 0, 300,
        -300, 0, 300,
        300, 0, -300,
        300, 0, 300
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
      el: 0.16,
      dist: 290,
      target: [0, 47, 8]
    };
    var introStart = performance.now();
    var introDone = reduceMotion;
    var mode = 0;
    var cut = 0;
    var cutTarget = 40.2;
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
        view.dist = 290 - 40 * e;
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

      mat4Perspective(projMat, (38 * Math.PI) / 180, cssW / cssH, 0.5, 800);
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
      gl.uniform2f(gridU.uSize, 300, 300);
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
        gl.uniform1f(glassU.uAlpha, mode === 2 ? 0.13 : 0.24);
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

      // 自发光：亮窗、灯带和标高点。关闭背面剔除，双面可见。
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
        gl.uniform1f(lineU.uAlpha, clamp((cut - 39.0) / 1.3, 0.0, 1.0) * 0.95);
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
      view.dist = 250;
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
      view.dist = clamp(view.dist * factor, 120, 420);
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
        view.dist = clamp(view.dist * Math.exp(delta * 0.0011), 120, 420);
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
