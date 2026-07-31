#include "mesh_thumbnail.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace tamias {
namespace {

struct CamBasis {
  Vec3 eye;
  Vec3 right;
  Vec3 up;
  Vec3 forward;
};

CamBasis make_camera(const Aabb& bounds) {
  const Vec3 center = bounds.valid() ? bounds.center() : Vec3{};
  const float radius =
      bounds.valid() ? (std::max)(0.25f, length(bounds.extent()) * 0.5f) : 1.f;
  // Match the default turntable three-quarter view (Y-up).
  constexpr float kYaw = 0.785398163f;
  constexpr float kPitch = 0.35f;
  const float cp = std::cos(kPitch);
  const float sp = std::sin(kPitch);
  const float cy = std::cos(kYaw);
  const float sy = std::sin(kYaw);
  const Vec3 offset{cp * sy, sp, cp * cy};
  CamBasis cam;
  cam.eye = center + offset * (radius * 2.6f);
  cam.forward = normalize(center - cam.eye);
  cam.right = normalize(cross(cam.forward, {0.f, 1.f, 0.f}));
  if (length(cam.right) < 1e-4f) {
    cam.right = {1.f, 0.f, 0.f};
  }
  cam.up = cross(cam.right, cam.forward);
  return cam;
}

Vec3 to_view(const CamBasis& cam, Vec3 p) {
  const Vec3 d = p - cam.eye;
  return {dot(d, cam.right), dot(d, cam.up), dot(d, cam.forward)};
}

void fill_triangle(std::vector<float>& zbuf, QImage& image, int w, int h, float x0, float y0,
                   float z0, float x1, float y1, float z1, float x2, float y2, float z2,
                   QRgb color) {
  const int min_x = (std::max)(0, static_cast<int>(std::floor((std::min)({x0, x1, x2}))));
  const int max_x = (std::min)(w - 1, static_cast<int>(std::ceil((std::max)({x0, x1, x2}))));
  const int min_y = (std::max)(0, static_cast<int>(std::floor((std::min)({y0, y1, y2}))));
  const int max_y = (std::min)(h - 1, static_cast<int>(std::ceil((std::max)({y0, y1, y2}))));
  if (min_x > max_x || min_y > max_y) {
    return;
  }

  const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (std::fabs(area) < 1e-6f) {
    return;
  }
  const float inv_area = 1.f / area;

  for (int y = min_y; y <= max_y; ++y) {
    const float py = static_cast<float>(y) + 0.5f;
    auto* scan = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = min_x; x <= max_x; ++x) {
      const float px = static_cast<float>(x) + 0.5f;
      const float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inv_area;
      const float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inv_area;
      const float w2 = 1.f - w0 - w1;
      if (w0 < 0.f || w1 < 0.f || w2 < 0.f) {
        continue;
      }
      const float z = w0 * z0 + w1 * z1 + w2 * z2;
      const int zi = y * w + x;
      if (z >= zbuf[static_cast<std::size_t>(zi)]) {
        continue;
      }
      zbuf[static_cast<std::size_t>(zi)] = z;
      scan[x] = color;
    }
  }
}

}  // namespace

QImage render_mesh_thumbnail(const MeshCpu& mesh, int width, int height) {
  width = (std::max)(64, width);
  height = (std::max)(64, height);

  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor(32, 32, 34));
  if (mesh.indices.size() < 3 || mesh.vertices.empty()) {
    return image;
  }

  const CamBasis cam = make_camera(mesh.bounds);
  std::vector<Vec3> view_pos(mesh.vertices.size());
  float min_x = 1e30f;
  float max_x = -1e30f;
  float min_y = 1e30f;
  float max_y = -1e30f;
  for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
    view_pos[i] = to_view(cam, mesh.vertices[i].position);
    min_x = (std::min)(min_x, view_pos[i].x);
    max_x = (std::max)(max_x, view_pos[i].x);
    min_y = (std::min)(min_y, view_pos[i].y);
    max_y = (std::max)(max_y, view_pos[i].y);
  }

  const float span_x = (std::max)(1e-4f, max_x - min_x);
  const float span_y = (std::max)(1e-4f, max_y - min_y);
  const float pad = 0.12f;
  const float scale =
      (1.f - pad * 2.f) / (std::max)(span_x / static_cast<float>(width), span_y / static_cast<float>(height));
  const float mid_x = (min_x + max_x) * 0.5f;
  const float mid_y = (min_y + max_y) * 0.5f;
  const float ox = static_cast<float>(width) * 0.5f - mid_x * scale;
  const float oy = static_cast<float>(height) * 0.5f + mid_y * scale;

  auto project = [&](const Vec3& v) {
    return Vec3{v.x * scale + ox, -v.y * scale + oy, v.z};
  };

  std::vector<float> zbuf(static_cast<std::size_t>(width * height), 1e30f);
  const Vec3 light = normalize(Vec3{-0.35f, -0.55f, 0.75f});
  constexpr std::size_t kMaxTris = 80000;
  const std::size_t tri_count = mesh.indices.size() / 3;
  const std::size_t stride = (std::max)(std::size_t{1}, (tri_count + kMaxTris - 1) / kMaxTris);

  for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3 * stride) {
    const auto i0 = mesh.indices[t];
    const auto i1 = mesh.indices[t + 1];
    const auto i2 = mesh.indices[t + 2];
    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
      continue;
    }

    const Vec3 p0 = project(view_pos[i0]);
    const Vec3 p1 = project(view_pos[i1]);
    const Vec3 p2 = project(view_pos[i2]);
    const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
    if (area <= 0.f) {
      continue;  // back-face
    }

    Vec3 n = mesh.vertices[i0].normal + mesh.vertices[i1].normal + mesh.vertices[i2].normal;
    if (length(n) < 1e-6f) {
      n = cross(mesh.vertices[i1].position - mesh.vertices[i0].position,
                mesh.vertices[i2].position - mesh.vertices[i0].position);
    }
    n = normalize(n);
    const float ndotl = (std::max)(0.f, dot(n, light));
    const float shade = 0.22f + 0.78f * ndotl;
    const Vec3 base = (mesh.vertices[i0].color + mesh.vertices[i1].color + mesh.vertices[i2].color) *
                      (1.f / 3.f);
    const int r = static_cast<int>(std::lround(std::clamp(base.x * shade, 0.f, 1.f) * 255.f));
    const int g = static_cast<int>(std::lround(std::clamp(base.y * shade, 0.f, 1.f) * 255.f));
    const int b = static_cast<int>(std::lround(std::clamp(base.z * shade, 0.f, 1.f) * 255.f));
    fill_triangle(zbuf, image, width, height, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z,
                  qRgba(r, g, b, 255));
  }

  return image;
}

QString save_mesh_thumbnail(const QString& source_path, const QImage& image) {
  if (image.isNull() || source_path.isEmpty()) {
    return {};
  }
  const QString root =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/thumbnails");
  if (!QDir().mkpath(root)) {
    return {};
  }
  const QByteArray hash =
      QCryptographicHash::hash(QFileInfo(source_path).absoluteFilePath().toUtf8(),
                               QCryptographicHash::Sha1)
          .toHex();
  const QString out = root + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".png");
  if (!image.save(out, "PNG")) {
    return {};
  }
  return out;
}

}  // namespace tamias
