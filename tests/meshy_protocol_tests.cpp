#include "ai/meshy_protocol.h"

#include <gtest/gtest.h>

#include <string>

namespace tamias::ai {
namespace {

mcp::Json parse_or_fail(const std::string& text) {
  auto parsed = mcp::Json::parse(text);
  EXPECT_TRUE(parsed.has_value()) << "fixture is not valid JSON: " << text;
  return parsed.value_or(mcp::Json::object());
}

// 服务端真实回包的形状。texture_urls 这里刻意用**数组**（一个任务一组贴图），
// 单对象那种形状在下面单独测。
const char* const kSucceededPayload =
    R"({"id":"018f2c4a-meshy-task","mode":"preview","status":"SUCCEEDED","progress":100,)"
    R"("thumbnail_url":"https://assets.meshy.ai/thumb/018f2c4a.png",)"
    R"("model_urls":{"glb":"https://assets.meshy.ai/018f2c4a/model.glb",)"
    R"("obj":"https://assets.meshy.ai/018f2c4a/model.obj",)"
    R"("fbx":"https://assets.meshy.ai/018f2c4a/model.fbx",)"
    R"("usdz":"https://assets.meshy.ai/018f2c4a/model.usdz"},)"
    R"("texture_urls":[{"base_color":"https://assets.meshy.ai/018f2c4a/base_color.png",)"
    R"("metallic":"https://assets.meshy.ai/018f2c4a/metallic.png",)"
    R"("roughness":"https://assets.meshy.ai/018f2c4a/roughness.png",)"
    R"("normal":"https://assets.meshy.ai/018f2c4a/normal.png"}],)"
    R"("credits_consumed":5})";

}  // namespace

TEST(MeshyStatus, RecognisesServerSpellingsAndNeverGuessesDone) {
  EXPECT_EQ(meshy_status_from_string("PENDING"), MeshyTaskStatus::kPending);
  EXPECT_EQ(meshy_status_from_string("IN_PROGRESS"), MeshyTaskStatus::kInProgress);
  EXPECT_EQ(meshy_status_from_string("in_progress"), MeshyTaskStatus::kInProgress);
  EXPECT_EQ(meshy_status_from_string("SUCCEEDED"), MeshyTaskStatus::kSucceeded);
  EXPECT_EQ(meshy_status_from_string("FAILED"), MeshyTaskStatus::kFailed);
  // 两种拼法都出现过，别让拼写差异变成"永远在跑"。
  EXPECT_EQ(meshy_status_from_string("CANCELED"), MeshyTaskStatus::kCanceled);
  EXPECT_EQ(meshy_status_from_string("CANCELLED"), MeshyTaskStatus::kCanceled);
  // 不认识的不许当完成。
  EXPECT_EQ(meshy_status_from_string("WEIRD_NEW_STATE"), MeshyTaskStatus::kPending);
  EXPECT_EQ(meshy_status_from_string(""), MeshyTaskStatus::kPending);

  EXPECT_FALSE(meshy_status_terminal(MeshyTaskStatus::kPending));
  EXPECT_FALSE(meshy_status_terminal(MeshyTaskStatus::kInProgress));
  EXPECT_TRUE(meshy_status_terminal(MeshyTaskStatus::kSucceeded));
  EXPECT_TRUE(meshy_status_terminal(MeshyTaskStatus::kFailed));
  EXPECT_TRUE(meshy_status_terminal(MeshyTaskStatus::kCanceled));
}

TEST(MeshyRequest, PreviewCarriesSculptOptionsAndOmitsRefineField) {
  MeshyTextTo3dRequest request;
  request.prompt = "a wooden chair";
  request.ai_model = "meshy-5";
  request.target_polycount = 12000;

  const mcp::Json body = meshy_text_to_3d_body(request);
  ASSERT_TRUE(body.is_object());
  ASSERT_NE(body.find("prompt"), nullptr);
  EXPECT_EQ(body.find("prompt")->as_string(), "a wooden chair");
  EXPECT_EQ(body.find("mode")->as_string(), "preview");
  EXPECT_EQ(body.find("ai_model")->as_string(), "meshy-5");
  EXPECT_EQ(body.find("target_polycount")->as_int(), 12000);
  EXPECT_TRUE(body.find("should_remesh")->as_bool());
  // preview 不该带 preview_task_id（那是 refine 的输入）。
  EXPECT_EQ(body.find("preview_task_id"), nullptr);
  // 没给 texture_prompt 就不要塞空串进去。
  EXPECT_EQ(body.find("texture_prompt"), nullptr);
  // 序列化出来仍然是一份合法 JSON（客户端就是把它当 body 发出去的）。
  EXPECT_TRUE(mcp::Json::parse(body.dump()).has_value());
}

TEST(MeshyRequest, RefineOnlyCarriesThePreviewLink) {
  MeshyTextTo3dRequest request;
  request.prompt = "a wooden chair, refined";
  request.mode = "refine";
  request.preview_task_id = "018f2c4a-meshy-task";

  const mcp::Json body = meshy_text_to_3d_body(request);
  EXPECT_EQ(body.find("mode")->as_string(), "refine");
  EXPECT_EQ(body.find("preview_task_id")->as_string(), "018f2c4a-meshy-task");
  // refine 那一轮造型参数已经在 preview 定死了，多塞会被服务端当成冲突参数。
  EXPECT_EQ(body.find("art_style"), nullptr);
  EXPECT_EQ(body.find("topology"), nullptr);
  EXPECT_EQ(body.find("target_polycount"), nullptr);
}

TEST(MeshyResponse, CreatedTaskIdAcceptsStringAndObjectShapes) {
  const auto from_string = meshy_parse_created_task_id(parse_or_fail(R"({"result":"abc-123"})"));
  ASSERT_TRUE(from_string.has_value());
  EXPECT_EQ(*from_string, "abc-123");

  const auto from_object =
      meshy_parse_created_task_id(parse_or_fail(R"({"result":{"id":"abc-456"}})"));
  ASSERT_TRUE(from_object.has_value());
  EXPECT_EQ(*from_object, "abc-456");

  EXPECT_FALSE(meshy_parse_created_task_id(parse_or_fail(R"({"code":400})")).has_value());
  EXPECT_FALSE(meshy_parse_created_task_id(parse_or_fail(R"({"result":""})")).has_value());
}

TEST(MeshyResponse, SucceededTaskCarriesUrlsAndCredits) {
  const auto task = meshy_parse_task(parse_or_fail(kSucceededPayload));
  ASSERT_TRUE(task.has_value()) << task.error();
  EXPECT_EQ(task->id, "018f2c4a-meshy-task");
  EXPECT_EQ(task->status, MeshyTaskStatus::kSucceeded);
  EXPECT_TRUE(meshy_status_terminal(task->status));
  EXPECT_EQ(task->progress, 100);
  EXPECT_EQ(task->thumbnail_url, "https://assets.meshy.ai/thumb/018f2c4a.png");
  EXPECT_EQ(task->credits_consumed, 5);
  EXPECT_EQ(task->model_urls.glb, "https://assets.meshy.ai/018f2c4a/model.glb");
  // texture_urls 是数组形状，也要能解出来。
  EXPECT_EQ(task->texture_urls.base_color, "https://assets.meshy.ai/018f2c4a/base_color.png");
  EXPECT_EQ(task->texture_urls.normal, "https://assets.meshy.ai/018f2c4a/normal.png");
  // Tamias 只吃 glb / obj：挑格式的策略在协议层给一处兜底。
  EXPECT_EQ(task->model_urls.best_importable(), task->model_urls.glb);
}

TEST(MeshyResponse, TextureUrlsAlsoAcceptsASingleObject) {
  const auto task = meshy_parse_task(parse_or_fail(
      R"({"id":"t0","status":"SUCCEEDED","texture_urls":{"base_color":"b.png","orm":"o.png"}})"));
  ASSERT_TRUE(task.has_value());
  EXPECT_EQ(task->texture_urls.base_color, "b.png");
  EXPECT_EQ(task->texture_urls.normal, "");
}

TEST(MeshyResponse, RunningTaskReportsProgressAndIsNotTerminal) {
  const auto task =
      meshy_parse_task(parse_or_fail(R"({"id":"t1","status":"IN_PROGRESS","progress":42})"));
  ASSERT_TRUE(task.has_value());
  EXPECT_EQ(task->status, MeshyTaskStatus::kInProgress);
  EXPECT_FALSE(meshy_status_terminal(task->status));
  EXPECT_EQ(task->progress, 42);
  EXPECT_TRUE(task->model_urls.empty());
  EXPECT_EQ(task->credits_consumed, -1) << "回包没说额度时不要编一个 0";
}

TEST(MeshyResponse, FailedTaskSurfacesTheServerReason) {
  const auto task = meshy_parse_task(parse_or_fail(
      R"({"id":"t2","status":"FAILED","task_error":{"message":"prompt rejected by moderation"}})"));
  ASSERT_TRUE(task.has_value());
  EXPECT_EQ(task->status, MeshyTaskStatus::kFailed);
  EXPECT_TRUE(meshy_status_terminal(task->status));
  EXPECT_EQ(task->error, "prompt rejected by moderation");
}

TEST(MeshyResponse, UnknownStatusStaysPendingAndProgressIsClamped) {
  const auto task = meshy_parse_task(
      parse_or_fail(R"({"id":"t3","status":"BRAND_NEW_STATE","progress":250})"));
  ASSERT_TRUE(task.has_value());
  EXPECT_EQ(task->status, MeshyTaskStatus::kPending) << "不认识的状态不许当完成";
  EXPECT_FALSE(meshy_status_terminal(task->status));
  EXPECT_EQ(task->progress, 100) << "进度要夹到 0–100，UI 不能拿到 250";
}

TEST(MeshyResponse, MissingStatusIsAnErrorNotARunningTask) {
  EXPECT_FALSE(meshy_parse_task(parse_or_fail(R"({"message":"invalid api key"})")).has_value());
  EXPECT_TRUE(meshy_parse_task(parse_or_fail(R"({"code":401})")).error().find("status") !=
              std::string::npos);
}

TEST(MeshyResponse, BalanceReadsNumber) {
  const auto balance = meshy_parse_balance(parse_or_fail(R"({"balance":1200})"));
  ASSERT_TRUE(balance.has_value());
  EXPECT_EQ(*balance, 1200);
  EXPECT_FALSE(meshy_parse_balance(parse_or_fail(R"({"result":"nope"})")).has_value());
}

TEST(MeshyEndpoints, JoinsBaseAndTaskPaths) {
  EXPECT_EQ(meshy_text_to_3d_url("https://api.meshy.ai/openapi/v2"),
            "https://api.meshy.ai/openapi/v2/text-to-3d");
  // 结尾多一个斜杠不该拼出双斜杠。
  EXPECT_EQ(meshy_text_to_3d_url("https://api.meshy.ai/openapi/v2/"),
            "https://api.meshy.ai/openapi/v2/text-to-3d");
  EXPECT_EQ(meshy_task_url("https://api.meshy.ai/openapi/v2", "abc"),
            "https://api.meshy.ai/openapi/v2/text-to-3d/abc");
  // 额度在另一个版本下：从 base 里剥掉 /openapi/<ver> 再拼。
  EXPECT_EQ(meshy_balance_url("https://api.meshy.ai/openapi/v2"),
            "https://api.meshy.ai/openapi/v1/balance");
  // 自建网关（base 里没有 /openapi）时退回简单拼接。
  EXPECT_EQ(meshy_balance_url("http://127.0.0.1:8080"), "http://127.0.0.1:8080/balance");
}

TEST(MeshyModelUrls, FallsBackWhenGlbIsMissing) {
  MeshyModelUrls urls;
  EXPECT_TRUE(urls.empty());
  EXPECT_EQ(urls.best_importable(), "");
  urls.obj = "model.obj";
  EXPECT_EQ(urls.best_importable(), "model.obj");
  urls.glb = "model.glb";
  EXPECT_EQ(urls.best_importable(), "model.glb");
  EXPECT_FALSE(urls.empty());
}

}  // namespace tamias::ai
