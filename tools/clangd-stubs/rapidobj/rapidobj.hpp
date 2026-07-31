// Minimal rapidobj API stub for clangd only.
// The real header (3rdparty/rapidobj.hpp) crashes some clangd builds
// while parsing MergeParallel / std::visit — keep that out of the LSP path.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace rapidobj {

template <typename T>
class Array {
 public:
  T& operator[](std::size_t i) { return data_[i]; }
  const T& operator[](std::size_t i) const { return data_[i]; }
  std::size_t size() const { return data_.size(); }
  bool empty() const { return data_.empty(); }

 private:
  std::vector<T> data_;
};

struct Attributes {
  Array<float> positions;
  Array<float> texcoords;
  Array<float> normals;
  Array<float> colors;
};

struct Index {
  int position_index = 0;
  int texcoord_index = 0;
  int normal_index = 0;
};

struct Mesh {
  Array<Index> indices;
  Array<std::uint8_t> num_face_vertices;
  Array<std::int32_t> material_ids;
  Array<std::uint32_t> smoothing_group_ids;
};

struct Shape {
  std::string name;
  Mesh mesh;
};

struct Error {
  explicit operator bool() const noexcept { return static_cast<bool>(code); }
  std::error_code code{};
  std::string line{};
  std::size_t line_num{};
};

struct Result {
  Attributes attributes;
  std::vector<Shape> shapes;
  Error error;
};

inline Result ParseFile(const std::filesystem::path&) { return {}; }
inline bool Triangulate(Result&) { return true; }

}  // namespace rapidobj
