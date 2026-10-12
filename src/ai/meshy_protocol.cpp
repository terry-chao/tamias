#include "ai/meshy_protocol.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace tamias::ai {
namespace {

std::string upper_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return out;
}

// 字段访问都从这里走：缺字段、类型不符、null 一律回缺省——协议边界上这是常态。
std::string json_string(const mcp::Json& object, std::string_view key) {
  const mcp::Json* value = object.find(key);
  return (value != nullptr && value->is_string()) ? value->as_string() : std::string{};
}

int json_int(const mcp::Json& object, std::string_view key, int fallback) {
  const mcp::Json* value = object.find(key);
  if (value == nullptr || !value->is_number()) {
    return fallback;
  }
  return static_cast<int>(value->is_int() ? value->as_int() : value->as_double());
}

MeshyTextureUrls parse_texture_urls(const mcp::Json& root) {
  MeshyTextureUrls urls;
  const mcp::Json* value = root.find("texture_urls");
  if (value == nullptr) {
    return urls;
  }
  // 服务端给过两种形状：单对象，以及"一个任务一组贴图"的数组。两种都收。
  const mcp::Json* object = value;
  if (value->is_array()) {
    if (value->items().empty()) {
      return urls;
    }
    object = &value->items().front();
  }
  if (!object->is_object()) {
    return urls;
  }
  urls.base_color = json_string(*object, "base_color");
  urls.metallic = json_string(*object, "metallic");
  urls.roughness = json_string(*object, "roughness");
  urls.normal = json_string(*object, "normal");
  return urls;
}

}  // namespace

const char* meshy_status_name(MeshyTaskStatus status) {
  switch (status) {
    case MeshyTaskStatus::kPending:
      return "PENDING";
    case MeshyTaskStatus::kInProgress:
      return "IN_PROGRESS";
    case MeshyTaskStatus::kSucceeded:
      return "SUCCEEDED";
    case MeshyTaskStatus::kFailed:
      return "FAILED";
    case MeshyTaskStatus::kCanceled:
      return "CANCELED";
  }
  return "PENDING";
}

MeshyTaskStatus meshy_status_from_string(std::string_view text) {
  const std::string status = upper_ascii(text);
  if (status == "SUCCEEDED" || status == "SUCCESS") {
    return MeshyTaskStatus::kSucceeded;
  }
  if (status == "FAILED" || status == "FAILURE" || status == "ERROR") {
    return MeshyTaskStatus::kFailed;
  }
  // 两种拼法都见过，别让拼写差异变成"永远在跑"。
  if (status == "CANCELED" || status == "CANCELLED") {
    return MeshyTaskStatus::kCanceled;
  }
  if (status == "IN_PROGRESS" || status == "RUNNING" || status == "PROCESSING") {
    return MeshyTaskStatus::kInProgress;
  }
  // 不认识的按 Pending：多轮询一轮，好过把"还没好"当成"好了"。
  return MeshyTaskStatus::kPending;
}

bool meshy_status_terminal(MeshyTaskStatus status) {
  return status == MeshyTaskStatus::kSucceeded || status == MeshyTaskStatus::kFailed ||
         status == MeshyTaskStatus::kCanceled;
}

bool MeshyModelUrls::empty() const { return best_importable().empty(); }

std::string MeshyModelUrls::best_importable() const {
  const std::string* candidates[] = {&glb, &obj, &fbx, &usdz};
  for (const std::string* url : candidates) {
    if (!url->empty()) {
      return *url;
    }
  }
  return {};
}

mcp::Json meshy_text_to_3d_body(const MeshyTextTo3dRequest& request) {
  const bool refine = request.mode == "refine";
  mcp::Json body = mcp::Json::object();
  body.set("prompt", mcp::Json::string(request.prompt));
  body.set("mode", mcp::Json::string(refine ? "refine" : "preview"));
  if (refine) {
    // refine 只认 preview_task_id：其余造型参数在 preview 那轮已经定下来了。
    body.set("preview_task_id", mcp::Json::string(request.preview_task_id));
  } else {
    body.set("art_style", mcp::Json::string(request.art_style));
    body.set("should_remesh", mcp::Json::boolean(request.should_remesh));
    body.set("topology", mcp::Json::string(request.topology));
    body.set("target_polycount", mcp::Json::integer(request.target_polycount));
  }
  if (!request.ai_model.empty()) {
    body.set("ai_model", mcp::Json::string(request.ai_model));
  }
  if (!request.texture_prompt.empty()) {
    body.set("texture_prompt", mcp::Json::string(request.texture_prompt));
  }
  return body;
}

Result<std::string> meshy_parse_created_task_id(const mcp::Json& root) {
  if (!root.is_object()) {
    return Err("Meshy: create response is not a JSON object");
  }
  // 有的版本结果是裸字符串，有的是 {"result": "..."}；两种都收。
  const mcp::Json* result = root.find("result");
  if (result == nullptr) {
    return Err("Meshy: create response has no 'result'");
  }
  if (result->is_string()) {
    if (result->as_string().empty()) {
      return Err("Meshy: create response returned an empty task id");
    }
    return result->as_string();
  }
  if (result->is_object()) {
    const std::string id = json_string(*result, "id");
    if (id.empty()) {
      return Err("Meshy: create response returned an object without 'id'");
    }
    return id;
  }
  return Err("Meshy: create response 'result' is neither a string nor an object");
}

Result<MeshyTask> meshy_parse_task(const mcp::Json& root) {
  if (!root.is_object()) {
    return Err("Meshy: task response is not a JSON object");
  }
  // 失败回包可能只有 error/message，没有 status —— 那种要当失败，不能当"还在跑"。
  if (!root.contains("status")) {
    const std::string detail = json_string(root, "message") + json_string(root, "error");
    return Err(detail.empty() ? "Meshy: task response has no 'status'"
                              : "Meshy: " + detail);
  }

  MeshyTask task;
  task.id = json_string(root, "id");
  task.status = meshy_status_from_string(json_string(root, "status"));
  task.progress = std::clamp(json_int(root, "progress", 0), 0, 100);
  task.thumbnail_url = json_string(root, "thumbnail_url");
  // 失败原因有两种形状：字符串，或 {"message": "..."}。两种都收。
  if (const mcp::Json* task_error = root.find("task_error"); task_error != nullptr) {
    if (task_error->is_string()) {
      task.error = task_error->as_string();
    } else if (task_error->is_object()) {
      task.error = json_string(*task_error, "message");
    }
  }
  if (task.error.empty()) {
    task.error = json_string(root, "error");
  }
  task.credits_consumed = json_int(root, "credits_consumed", -1);
  task.texture_urls = parse_texture_urls(root);

  if (const mcp::Json* urls = root.find("model_urls"); urls != nullptr && urls->is_object()) {
    task.model_urls.glb = json_string(*urls, "glb");
    task.model_urls.obj = json_string(*urls, "obj");
    task.model_urls.fbx = json_string(*urls, "fbx");
    task.model_urls.usdz = json_string(*urls, "usdz");
  }
  return task;
}

Result<int> meshy_parse_balance(const mcp::Json& root) {
  if (!root.is_object()) {
    return Err("Meshy: balance response is not a JSON object");
  }
  const mcp::Json* balance = root.find("balance");
  if (balance == nullptr || !balance->is_number()) {
    return Err("Meshy: balance response has no numeric 'balance'");
  }
  return static_cast<int>(balance->is_int() ? balance->as_int() : balance->as_double());
}

std::string meshy_join(std::string_view base, std::string_view path) {
  std::string text(base);
  while (!text.empty() && text.back() == '/') {
    text.pop_back();
  }
  if (!path.empty() && path.front() != '/') {
    text.push_back('/');
  }
  text.append(path);
  return text;
}

std::string meshy_text_to_3d_url(std::string_view base) {
  return meshy_join(base, "/text-to-3d");
}

std::string meshy_task_url(std::string_view base, std::string_view task_id) {
  return meshy_join(meshy_join(base, "/text-to-3d"), "/" + std::string(task_id));
}

std::string meshy_balance_url(std::string_view base) {
  const std::string text(base);
  const std::size_t marker = text.find("/openapi");
  if (marker == std::string::npos) {
    return meshy_join(base, "/balance");
  }
  return text.substr(0, marker) + "/openapi/v1/balance";
}

}  // namespace tamias::ai
