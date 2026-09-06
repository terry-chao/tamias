#include "engine/profile/timing_scope.h"
#include "engine/profile/timing_session.h"
#include "engine/profile/timing_xml_writer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

namespace tamias {
namespace {

class TimingSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto& session = TimingSession::instance();
    session.set_category_enabled(TimingCategory::Command, true);
    session.set_category_enabled(TimingCategory::Modeling, true);
    session.set_category_enabled(TimingCategory::Render, false);
    session.set_category_enabled(TimingCategory::Ui, true);
    session.clear();
  }
  void TearDown() override { TimingSession::instance().clear(); }
};

TEST_F(TimingSessionTest, UnrecordedScopeEmitsNothing) {
  {
    TimingScope scope("create_box", TimingCategory::Command);
  }
  EXPECT_TRUE(TimingSession::instance().events().empty());
}

TEST_F(TimingSessionTest, NestedScopesLinkParent) {
  auto& session = TimingSession::instance();
  session.start();
  {
    TimingScope command("create_box", TimingCategory::Command);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    {
      TimingScope modeling("evaluate_feature_model", TimingCategory::Modeling);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  session.stop();

  const auto events = session.events();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].name, "create_box");
  EXPECT_EQ(events[0].category, TimingCategory::Command);
  EXPECT_EQ(events[0].parent, -1);
  EXPECT_EQ(events[1].name, "evaluate_feature_model");
  EXPECT_EQ(events[1].category, TimingCategory::Modeling);
  EXPECT_EQ(events[1].parent, 0);
  EXPECT_GT(events[0].duration_us, events[1].duration_us);
  EXPECT_GT(events[1].duration_us, 0u);
  EXPECT_GT(session.duration_us(), 0u);

  const auto self = exclusive_durations(events);
  ASSERT_EQ(self.size(), 2u);
  EXPECT_EQ(self[1], events[1].duration_us);
  EXPECT_EQ(self[0], events[0].duration_us - events[1].duration_us);
}

TEST_F(TimingSessionTest, DisabledCategoryIsSkipped) {
  auto& session = TimingSession::instance();
  session.set_category_enabled(TimingCategory::Render, false);
  session.start();
  {
    TimingScope command("create_box", TimingCategory::Command);
    TimingScope frame("submit_current_frame", TimingCategory::Render);
  }
  session.stop();
  const auto events = session.events();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].name, "create_box");
}

TEST_F(TimingSessionTest, XmlNestsChildrenAndEscapes) {
  auto& session = TimingSession::instance();
  session.start();
  {
    TimingScope command("create_box", TimingCategory::Command);
    TimingScope modeling("evaluate_feature_model", TimingCategory::Modeling);
  }
  session.stop();

  const std::string xml = TimingXmlWriter::to_string(session);
  EXPECT_NE(xml.find("<TamiasTimingReport version=\"1\">"), std::string::npos);
  EXPECT_NE(xml.find("name=\"create_box\""), std::string::npos);
  EXPECT_NE(xml.find("category=\"command\""), std::string::npos);
  EXPECT_NE(xml.find("name=\"evaluate_feature_model\""), std::string::npos);
  EXPECT_NE(xml.find("category=\"modeling\""), std::string::npos);
  const auto command_pos = xml.find("name=\"create_box\"");
  const auto model_pos = xml.find("name=\"evaluate_feature_model\"");
  ASSERT_NE(command_pos, std::string::npos);
  ASSERT_NE(model_pos, std::string::npos);
  EXPECT_LT(command_pos, model_pos);
  EXPECT_NE(xml.find("</event>"), std::string::npos);

  session.start();
  {
    TimingScope quoted("a&b<c>", TimingCategory::Command);
  }
  session.stop();
  const std::string escaped = TimingXmlWriter::to_string(session);
  EXPECT_NE(escaped.find("name=\"a&amp;b&lt;c&gt;\""), std::string::npos);
}

TEST_F(TimingSessionTest, WriteFileRoundTrip) {
  auto& session = TimingSession::instance();
  session.start();
  {
    TimingScope scope("create_box", TimingCategory::Command);
  }
  session.stop();

  const auto path = std::filesystem::temp_directory_path() / "tamias_timing_test.xml";
  auto written = TimingXmlWriter::write_file(session, path);
  ASSERT_TRUE(written) << written.error();
  std::string body;
  {
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in);
    body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  EXPECT_EQ(body, TimingXmlWriter::to_string(session));
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace
}  // namespace tamias
