#include "engine/drawing/zip_archive.h"

#if defined(TAMIAS_HAVE_ZLIB)
#include <zlib.h>
#endif

#include <cstring>

namespace tamias {
namespace {

constexpr std::uint32_t kLocalHeader = 0x04034b50u;
constexpr std::uint32_t kCentralHeader = 0x02014b50u;
constexpr std::uint32_t kEndOfCentralDir = 0x06054b50u;
constexpr std::size_t kEndOfCentralDirSize = 22;
constexpr std::size_t kMaxComment = 65535;

std::uint16_t read_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                    (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool has_bytes(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t count) {
  return offset <= bytes.size() && count <= bytes.size() - offset;
}

Result<std::vector<std::uint8_t>> inflate_raw(std::span<const std::uint8_t> in,
                                              std::size_t out_size) {
  std::vector<std::uint8_t> out(out_size);
  if (out_size == 0) {
    return out;
  }
#if defined(TAMIAS_HAVE_ZLIB)
  z_stream stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
  stream.avail_in = static_cast<uInt>(in.size());
  stream.next_out = reinterpret_cast<Bytef*>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
    return Err("zip: inflateInit failed");
  }
  const int code = inflate(&stream, Z_FINISH);
  const std::size_t produced = static_cast<std::size_t>(stream.total_out);
  inflateEnd(&stream);
  if (code != Z_STREAM_END || produced != out_size) {
    return Err("zip: deflate stream is corrupt");
  }
  return out;
#else
  (void)in;
  return Err("zip: this build has no zlib, so compressed zip entries cannot be read");
#endif
}

}  // namespace

bool ZipArchive::looks_like_zip(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 4) {
    return false;
  }
  const std::uint32_t signature = read_u32(bytes.data());
  return signature == kLocalHeader || signature == 0x08074b50u /* 空包/分卷 */;
}

Result<ZipArchive> ZipArchive::open(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kEndOfCentralDirSize) {
    return Err("zip: file is too small to be a zip archive");
  }
  // 从尾部往前找 EOCD（后面可能跟最多 64K 的注释）。
  const std::size_t search_from =
      bytes.size() > kEndOfCentralDirSize + kMaxComment
          ? bytes.size() - (kEndOfCentralDirSize + kMaxComment)
          : 0;
  std::size_t eocd = bytes.size();
  for (std::size_t i = bytes.size() - kEndOfCentralDirSize + 1; i-- > search_from;) {
    if (read_u32(bytes.data() + i) == kEndOfCentralDir) {
      eocd = i;
      break;
    }
  }
  if (eocd == bytes.size()) {
    return Err("zip: no end-of-central-directory record (not a zip archive)");
  }
  const std::uint16_t entry_count = read_u16(bytes.data() + eocd + 10);
  const std::uint32_t dir_offset = read_u32(bytes.data() + eocd + 16);
  if (entry_count == 0xFFFFu || dir_offset == 0xFFFFFFFFu) {
    return Err("zip: zip64 archives are not supported");
  }
  if (!has_bytes(bytes, dir_offset, 1)) {
    return Err("zip: central directory is outside the file");
  }

  ZipArchive archive;
  archive.bytes_ = bytes;
  std::size_t cursor = dir_offset;
  for (std::uint16_t i = 0; i < entry_count; ++i) {
    if (!has_bytes(bytes, cursor, 46) || read_u32(bytes.data() + cursor) != kCentralHeader) {
      return Err("zip: malformed central directory entry");
    }
    const std::uint16_t method = read_u16(bytes.data() + cursor + 10);
    const std::uint32_t compressed_size = read_u32(bytes.data() + cursor + 20);
    const std::uint32_t size = read_u32(bytes.data() + cursor + 24);
    const std::uint16_t name_len = read_u16(bytes.data() + cursor + 28);
    const std::uint16_t extra_len = read_u16(bytes.data() + cursor + 30);
    const std::uint16_t comment_len = read_u16(bytes.data() + cursor + 32);
    const std::uint32_t local_header = read_u32(bytes.data() + cursor + 42);
    if (compressed_size == 0xFFFFFFFFu || size == 0xFFFFFFFFu ||
        local_header == 0xFFFFFFFFu) {
      return Err("zip: zip64 entries are not supported");
    }
    if (!has_bytes(bytes, cursor + 46, name_len)) {
      return Err("zip: truncated entry name");
    }
    const std::string name(reinterpret_cast<const char*>(bytes.data() + cursor + 46), name_len);
    Entry entry;
    entry.local_header = local_header;
    entry.compressed_size = compressed_size;
    entry.size = size;
    entry.method = method;
    archive.entries_.emplace(name, entry);
    archive.names_.push_back(name);
    cursor += 46u + name_len + extra_len + comment_len;
  }
  return archive;
}

bool ZipArchive::contains(std::string_view name) const {
  return entries_.find(std::string(name)) != entries_.end();
}

Result<std::vector<std::uint8_t>> ZipArchive::extract(std::string_view name) const {
  const auto it = entries_.find(std::string(name));
  if (it == entries_.end()) {
    return Err("zip: no such entry: " + std::string(name));
  }
  const Entry& entry = it->second;
  if (!has_bytes(bytes_, entry.local_header, 30) ||
      read_u32(bytes_.data() + entry.local_header) != kLocalHeader) {
    return Err("zip: malformed local file header");
  }
  const std::uint16_t name_len = read_u16(bytes_.data() + entry.local_header + 26);
  const std::uint16_t extra_len = read_u16(bytes_.data() + entry.local_header + 28);
  const std::size_t data_offset =
      static_cast<std::size_t>(entry.local_header) + 30u + name_len + extra_len;
  if (!has_bytes(bytes_, data_offset, entry.compressed_size)) {
    return Err("zip: entry data is outside the file");
  }
  const std::span<const std::uint8_t> compressed = bytes_.subspan(data_offset, entry.compressed_size);
  if (entry.method == 0) {  // store
    return std::vector<std::uint8_t>(compressed.begin(), compressed.end());
  }
  if (entry.method == 8) {  // deflate
    return inflate_raw(compressed, static_cast<std::size_t>(entry.size));
  }
  return Err("zip: unsupported compression method " + std::to_string(entry.method));
}

}  // namespace tamias
