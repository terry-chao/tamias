#pragma once

#include "core/result.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace tamias {

class BinaryWriter {
 public:
  [[nodiscard]] const std::vector<std::uint8_t>& data() const { return buf_; }
  [[nodiscard]] std::vector<std::uint8_t> release() { return std::move(buf_); }
  [[nodiscard]] std::size_t size() const { return buf_.size(); }
  void clear() { buf_.clear(); }

  Result<void> write_bytes(const void* data, std::size_t n) {
    if (n == 0) {
      return {};
    }
    if (data == nullptr) {
      return Err("BinaryWriter: null data");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    buf_.insert(buf_.end(), bytes, bytes + n);
    return {};
  }

  Result<void> write_u8(std::uint8_t v) { return write_bytes(&v, 1); }

  Result<void> write_u16(std::uint16_t v) {
    std::uint8_t b[2] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
    return write_bytes(b, 2);
  }

  Result<void> write_u32(std::uint32_t v) {
    std::uint8_t b[4] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                         static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
    return write_bytes(b, 4);
  }

  Result<void> write_u64(std::uint64_t v) {
    std::uint8_t b[8];
    for (int i = 0; i < 8; ++i) {
      b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
    return write_bytes(b, 8);
  }

  Result<void> write_f32(float v) {
    static_assert(sizeof(float) == 4);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return write_u32(bits);
  }

  Result<void> write_bool(bool v) { return write_u8(v ? 1u : 0u); }

  Result<void> write_string(const std::string& s) {
    if (auto r = write_u64(static_cast<std::uint64_t>(s.size())); !r) {
      return r;
    }
    if (s.empty()) {
      return {};
    }
    return write_bytes(s.data(), s.size());
  }

 private:
  std::vector<std::uint8_t> buf_;
};

class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::uint8_t> data)
      : data_(data.data()), size_(data.size()) {}

  [[nodiscard]] std::size_t position() const { return pos_; }
  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] std::size_t remaining() const { return size_ - pos_; }

  Result<void> skip(std::size_t n) {
    if (n > remaining()) {
      return Err("BinaryReader: skip past end");
    }
    pos_ += n;
    return {};
  }

  Result<void> read_bytes(void* out, std::size_t n) {
    if (n == 0) {
      return {};
    }
    if (out == nullptr) {
      return Err("BinaryReader: null out");
    }
    if (n > remaining()) {
      return Err("BinaryReader: unexpected end of data");
    }
    std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return {};
  }

  Result<std::uint8_t> read_u8() {
    std::uint8_t v = 0;
    if (auto r = read_bytes(&v, 1); !r) {
      return Err(r.error());
    }
    return v;
  }

  Result<std::uint16_t> read_u16() {
    std::uint8_t b[2];
    if (auto r = read_bytes(b, 2); !r) {
      return Err(r.error());
    }
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
  }

  Result<std::uint32_t> read_u32() {
    std::uint8_t b[4];
    if (auto r = read_bytes(b, 4); !r) {
      return Err(r.error());
    }
    return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
  }

  Result<std::uint64_t> read_u64() {
    std::uint8_t b[8];
    if (auto r = read_bytes(b, 8); !r) {
      return Err(r.error());
    }
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
      v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
    }
    return v;
  }

  Result<float> read_f32() {
    auto bits = read_u32();
    if (!bits) {
      return Err(bits.error());
    }
    float v = 0.f;
    std::memcpy(&v, &*bits, sizeof(v));
    return v;
  }

  Result<bool> read_bool() {
    auto v = read_u8();
    if (!v) {
      return Err(v.error());
    }
    return *v != 0;
  }

  Result<std::string> read_string() {
    auto len = read_u64();
    if (!len) {
      return Err(len.error());
    }
    if (*len > remaining()) {
      return Err("BinaryReader: string length exceeds remaining data");
    }
    if (*len > static_cast<std::uint64_t>(SIZE_MAX)) {
      return Err("BinaryReader: string too large");
    }
    std::string s(static_cast<std::size_t>(*len), '\0');
    if (*len > 0) {
      if (auto r = read_bytes(s.data(), static_cast<std::size_t>(*len)); !r) {
        return Err(r.error());
      }
    }
    return s;
  }

 private:
  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t pos_ = 0;
};

}  // namespace tamias
