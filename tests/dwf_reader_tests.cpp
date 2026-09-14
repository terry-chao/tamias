#include "engine/drawing/dwf_reader.h"
#include "engine/drawing/zip_archive.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace tamias {
namespace {

// 造一个最小 ZIP（store，不压缩）：测试不依赖 zlib，也方便手写期望值。
struct ZipBuilder {
  struct Entry {
    std::string name;
    std::string data;
  };
  std::vector<Entry> entries;

  void add(std::string name, std::string data) {
    entries.push_back({std::move(name), std::move(data)});
  }

  [[nodiscard]] std::vector<std::uint8_t> build() const {
    std::vector<std::uint8_t> out;
    std::vector<std::uint32_t> offsets;
    const auto u16 = [&](std::uint16_t v) {
      out.push_back(static_cast<std::uint8_t>(v & 0xFF));
      out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    };
    const auto u32 = [&](std::uint32_t v) {
      for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
      }
    };
    for (const Entry& entry : entries) {
      offsets.push_back(static_cast<std::uint32_t>(out.size()));
      u32(0x04034b50u);                        // local file header
      u16(20);                                 // version needed
      u16(0);                                  // flags
      u16(0);                                  // method: store
      u16(0); u16(0);                          // time / date
      u32(0);                                  // crc32（读取器不校验）
      u32(static_cast<std::uint32_t>(entry.data.size()));
      u32(static_cast<std::uint32_t>(entry.data.size()));
      u16(static_cast<std::uint16_t>(entry.name.size()));
      u16(0);                                  // extra len
      out.insert(out.end(), entry.name.begin(), entry.name.end());
      out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    const std::uint32_t dir_offset = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const Entry& entry = entries[i];
      u32(0x02014b50u);                        // central directory header
      u16(20); u16(20); u16(0); u16(0); u16(0); u16(0);
      u32(0);
      u32(static_cast<std::uint32_t>(entry.data.size()));
      u32(static_cast<std::uint32_t>(entry.data.size()));
      u16(static_cast<std::uint16_t>(entry.name.size()));
      u16(0); u16(0); u16(0); u16(0);
      u32(0);
      u32(offsets[i]);
      out.insert(out.end(), entry.name.begin(), entry.name.end());
    }
    const std::uint32_t dir_size = static_cast<std::uint32_t>(out.size()) - dir_offset;
    u32(0x06054b50u);                          // end of central directory
    u16(0); u16(0);
    u16(static_cast<std::uint16_t>(entries.size()));
    u16(static_cast<std::uint16_t>(entries.size()));
    u32(dir_size);
    u32(dir_offset);
    u16(0);
    return out;
  }
};

std::string fixed_page(double width, double height, const std::string& body) {
  return "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<FixedPage xmlns=\"http://schemas.microsoft.com/xps/2005/06\" Width=\"" +
         std::to_string(width) + "\" Height=\"" + std::to_string(height) + "\">" + body +
         "</FixedPage>";
}

TEST(ZipArchive, ReadsStoredEntries) {
  ZipBuilder builder;
  builder.add("manifest.xml", "<dwf/>");
  builder.add("section/0.w2d", std::string("\x00\x01\x02", 3));
  const std::vector<std::uint8_t> bytes = builder.build();

  auto archive = ZipArchive::open(bytes);
  ASSERT_TRUE(archive) << archive.error();
  EXPECT_EQ(archive->names().size(), 2u);
  EXPECT_TRUE(archive->contains("manifest.xml"));
  EXPECT_FALSE(archive->contains("missing.xml"));

  auto manifest = archive->extract("manifest.xml");
  ASSERT_TRUE(manifest) << manifest.error();
  EXPECT_EQ(std::string(manifest->begin(), manifest->end()), "<dwf/>");
}

TEST(ZipArchive, ReadsDeflatedEntries) {
  // 手写的 raw deflate 流（"hello"，fixed Huffman）——顺带确认 zlib 真的在链上。
  ZipBuilder builder;
  builder.add("stored.txt", "hello");
  std::vector<std::uint8_t> bytes = builder.build();
  // 把第一个条目的 method 改成 8，并把数据换成压缩后的字节：直接改字节太脆，另造一个包。
  const std::uint8_t deflated[] = {0xCB, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00};
  std::vector<std::uint8_t> out;
  const auto u16 = [&](std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  };
  const auto u32 = [&](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    }
  };
  const std::string name = "deflated.txt";
  u32(0x04034b50u); u16(20); u16(0); u16(8); u16(0); u16(0);
  u32(0); u32(sizeof(deflated)); u32(5);
  u16(static_cast<std::uint16_t>(name.size())); u16(0);
  out.insert(out.end(), name.begin(), name.end());
  out.insert(out.end(), std::begin(deflated), std::end(deflated));
  const std::uint32_t dir_offset = static_cast<std::uint32_t>(out.size());
  u32(0x02014b50u); u16(20); u16(20); u16(0); u16(8); u16(0); u16(0);
  u32(0); u32(sizeof(deflated)); u32(5);
  u16(static_cast<std::uint16_t>(name.size())); u16(0); u16(0); u16(0); u16(0); u32(0);
  u32(0);  // 本地头偏移
  out.insert(out.end(), name.begin(), name.end());
  const std::uint32_t dir_size = static_cast<std::uint32_t>(out.size()) - dir_offset;
  u32(0x06054b50u); u16(0); u16(0); u16(1); u16(1); u32(dir_size); u32(dir_offset); u16(0);

  auto archive = ZipArchive::open(out);
  ASSERT_TRUE(archive) << archive.error();
  auto text = archive->extract("deflated.txt");
  ASSERT_TRUE(text) << text.error();
  EXPECT_EQ(std::string(text->begin(), text->end()), "hello");
  (void)bytes;
}

TEST(DwfReader, RejectsNonZip) {
  const std::vector<std::uint8_t> junk{1, 2, 3, 4, 5, 6, 7, 8};
  auto content = read_dwf(junk);
  EXPECT_FALSE(content);
}

// DWFx：一个 FixedPage，两条路径 + 一段文字。检查坐标翻转、闭合、颜色、文字。
TEST(DwfReader, ReadsDwfxFixedPage) {
  const std::string page = fixed_page(
      100.0, 50.0,
      "<Path Data=\"M 0,0 L 100,0\" Stroke=\"#FF102030\"/>"
      "<Path Data=\"M 10,10 L 40,10 L 40,40 Z\" Fill=\"#00FF00\"/>"
      "<Glyphs OriginX=\"5\" OriginY=\"45\" FontRenderingEmSize=\"10\" "
      "UnicodeString=\"3600\" Fill=\"#FF000000\"/>");
  ZipBuilder builder;
  builder.add("Documents/1/Pages/1.fpage", page);
  const std::vector<std::uint8_t> bytes = builder.build();

  auto content = read_dwf(bytes);
  ASSERT_TRUE(content) << content.error();
  EXPECT_TRUE(content->xps);
  EXPECT_TRUE(content->has_vector());
  EXPECT_EQ(content->pages.size(), 1u);
  ASSERT_EQ(content->drawing.paths().size(), 2u);
  ASSERT_EQ(content->drawing.texts().size(), 1u);

  const DrawingPath& line = content->drawing.paths()[0];
  ASSERT_EQ(line.points.size(), 2u);
  EXPECT_FLOAT_EQ(line.points[0].x, 0.f);
  EXPECT_FLOAT_EQ(line.points[0].y, 50.f);   // XPS y=0（页顶）→ 图纸坐标 y=50
  EXPECT_FLOAT_EQ(line.points[1].y, 50.f);
  EXPECT_FLOAT_EQ(line.color.x, 16.f / 255.f);  // #102030（AARRGGBB 的 alpha 丢掉）
  EXPECT_FALSE(line.closed);

  const DrawingPath& box = content->drawing.paths()[1];
  EXPECT_TRUE(box.closed);
  EXPECT_FLOAT_EQ(box.color.y, 1.f);
  EXPECT_GE(box.points.size(), 4u);

  EXPECT_EQ(content->drawing.texts()[0].text, "3600");
  EXPECT_NEAR(content->drawing.texts()[0].position.y, 5.0, 1e-4);  // 45 → 50-45
  EXPECT_NEAR(content->drawing.texts()[0].height, 7.0, 1e-4);

  const DwfPage& first = content->pages[0];
  EXPECT_FLOAT_EQ(first.bounds.width(), 100.f);
  EXPECT_FLOAT_EQ(first.bounds.height(), 50.f);
  EXPECT_EQ(first.path_begin, 0u);
  EXPECT_EQ(first.path_end, 2u);
  EXPECT_EQ(first.text_end, 1u);
}

TEST(DwfReader, KeepsPagesApart) {
  ZipBuilder builder;
  builder.add("Documents/1/Pages/1.fpage",
              fixed_page(10.0, 10.0, "<Path Data=\"M 0,0 L 10,10\" Stroke=\"#000000\"/>"));
  builder.add("Documents/1/Pages/2.fpage",
              fixed_page(20.0, 20.0,
                         "<Path Data=\"M 0,0 L 20,0\" Stroke=\"#000000\"/>"
                         "<Path Data=\"M 0,0 L 0,20\" Stroke=\"#000000\"/>"));
  const std::vector<std::uint8_t> bytes = builder.build();

  auto content = read_dwf(bytes);
  ASSERT_TRUE(content) << content.error();
  ASSERT_EQ(content->pages.size(), 2u);
  ASSERT_EQ(content->drawing.paths().size(), 3u);
  EXPECT_EQ(content->pages[0].path_begin, 0u);
  EXPECT_EQ(content->pages[0].path_end, 1u);
  EXPECT_EQ(content->pages[1].path_begin, 1u);
  EXPECT_EQ(content->pages[1].path_end, 3u);
  EXPECT_FLOAT_EQ(content->pages[1].bounds.width(), 20.f);
}

// 二进制 DWF：W2D 还没解，退回包里带的预览图。
TEST(DwfReader, FallsBackToPreviewImage) {
  ZipBuilder builder;
  builder.add("manifest.xml", "<DWF><Section/></DWF>");
  builder.add("_sections/0.w2d", std::string("\x00\x01\x02\x03", 4));
  builder.add("preview/thumbnail.png", std::string("\x89PNG\r\n\x1a\n", 8));
  const std::vector<std::uint8_t> bytes = builder.build();

  auto content = read_dwf(bytes);
  ASSERT_TRUE(content) << content.error();
  EXPECT_FALSE(content->xps);
  EXPECT_FALSE(content->has_vector());
  ASSERT_TRUE(content->has_preview());
  EXPECT_EQ(content->preview.size(), 8u);
  EXPECT_EQ(static_cast<char>(content->preview[1]), 'P');
}

// 既没有矢量、又没有预览图：明确报出来，别静默给一张空图。
TEST(DwfReader, ReportsBinaryDwfWithoutPreview) {
  ZipBuilder builder;
  builder.add("manifest.xml", "<DWF/>");
  builder.add("_sections/0.w2d", std::string("\x00\x01\x02\x03", 4));
  const std::vector<std::uint8_t> bytes = builder.build();

  auto content = read_dwf(bytes);
  ASSERT_FALSE(content);
  EXPECT_NE(content.error().find("W2D"), std::string::npos);
}

}  // namespace
}  // namespace tamias
