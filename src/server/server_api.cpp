#include "server/server_api.h"

#include "server/session_service.h"

#include <cstddef>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

// 这一层只做三件事：句柄校验、错误文本落盘、以及**把 C++ 异常挡在边界内侧**。
// 业务语义全在 SessionService（session_service.cpp）。
//
// 为什么异常必须挡：引擎会抛（几何内核、std::bad_alloc、std::filesystem）。
// 让异常穿过 extern "C" 进到 .NET 是未定义行为——P/Invoke 那边看到的是进程崩溃，
// 不是异常。所有入口一律 try/catch → 错误码 + last_error()。

namespace {

using tamias::SessionService;

// 线程局部的最近错误：与 truck_last_error() 同款约定（只在失败后读，下次调用前有效）。
thread_local std::string g_last_error;

std::int32_t fail(std::int32_t code, std::string message) {
  g_last_error = std::move(message);
  return code;
}

SessionService* as_service(void* handle) {
  return static_cast<SessionService*>(handle);
}

// 与 plugin_host.cpp 的 fill_utf8 同语义：写入 + '\0'，返回写入字节数（不含结尾 0）；
// 参数不合法 -1；cap 不够就截断。
std::int32_t fill_utf8(const std::string& text, char* utf8, std::int32_t cap) {
  if (utf8 == nullptr || cap <= 0) {
    return -1;
  }
  const auto n = static_cast<std::int32_t>(text.size());
  const auto copy = n < cap - 1 ? n : cap - 1;
  if (copy > 0) {
    std::memcpy(utf8, text.data(), static_cast<std::size_t>(copy));
  }
  utf8[copy] = '\0';
  return copy;
}

// 句柄 + 出参的统一前置检查，省得每个入口重复三行。
bool require_session(void* session, const char* what, std::int32_t* out_code) {
  if (session == nullptr) {
    *out_code = fail(TAMIAS_SERVER_E_BAD_HANDLE, std::string(what) + ": null session");
    return false;
  }
  return true;
}

}  // namespace

extern "C" {

const char* tamias_server_version(void) {
  static const std::string kVersion =
      "tamias-server/0.1.0 (abi " + std::to_string(TAMIAS_SERVER_ABI_VERSION) + ")";
  return kVersion.c_str();
}

std::int32_t tamias_server_abi_version(void) {
  return TAMIAS_SERVER_ABI_VERSION;
}

const char* tamias_server_last_error(void) {
  return g_last_error.c_str();
}

std::int32_t tamias_server_init(void) {
  try {
    tamias::ensure_server_initialized();
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("init failed: ") + e.what());
  }
}

void* tamias_session_create(const char* document_name_utf8) {
  try {
    std::string name = document_name_utf8 != nullptr ? document_name_utf8 : "Untitled";
    return new SessionService(std::move(name));
  } catch (const std::exception& e) {
    g_last_error = std::string("session_create failed: ") + e.what();
    return nullptr;
  }
}

void tamias_session_destroy(void* session) {
  // delete nullptr 是合法的：销毁一个空句柄应该是 no-op，不是崩溃。
  delete as_service(session);
}

std::int32_t tamias_session_dispatch(void* session, const char* command_utf8,
                                     const char* args_utf8) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "dispatch", &code)) {
    return code;
  }
  if (command_utf8 == nullptr) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "dispatch: null command id");
  }
  try {
    auto r = as_service(session)->dispatch(command_utf8, args_utf8 != nullptr ? args_utf8 : "");
    if (!r) {
      return fail(TAMIAS_SERVER_E_FAILED, r.error());
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("dispatch threw: ") + e.what());
  }
}

std::int32_t tamias_session_begin_transaction(void* session, const char* name_utf8) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "begin_transaction", &code)) {
    return code;
  }
  try {
    auto& commands = as_service(session)->session().command_system();
    auto r = commands.begin_transaction(name_utf8 != nullptr ? name_utf8 : "");
    if (!r) {
      return fail(TAMIAS_SERVER_E_FAILED, r.error());
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("begin_transaction threw: ") + e.what());
  }
}

std::int32_t tamias_session_commit_transaction(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "commit_transaction", &code)) {
    return code;
  }
  try {
    auto r = as_service(session)->session().command_system().commit_transaction();
    if (!r) {
      return fail(TAMIAS_SERVER_E_FAILED, r.error());
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("commit_transaction threw: ") + e.what());
  }
}

std::int32_t tamias_session_abort_transaction(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "abort_transaction", &code)) {
    return code;
  }
  try {
    auto r = as_service(session)->session().command_system().abort_transaction();
    if (!r) {
      return fail(TAMIAS_SERVER_E_FAILED, r.error());
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("abort_transaction threw: ") + e.what());
  }
}

std::int32_t tamias_session_undo(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "undo", &code)) {
    return code;
  }
  try {
    auto& s = as_service(session)->session();
    if (s.can_undo()) {
      s.undo();
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("undo threw: ") + e.what());
  }
}

std::int32_t tamias_session_redo(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "redo", &code)) {
    return code;
  }
  try {
    auto& s = as_service(session)->session();
    if (s.can_redo()) {
      s.redo();
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("redo threw: ") + e.what());
  }
}

// can_* 是三态：1 / 0 是答案，负数才是错。所以**不能**用 require_session 的返回码——
// 正数错误码（E_FAILED = 1）会和「可以撤销」撞车。这里错误一律返回 -1。
std::int32_t tamias_session_can_undo(void* session) {
  if (session == nullptr) {
    fail(TAMIAS_SERVER_E_BAD_HANDLE, "can_undo: null session");
    return -1;
  }
  try {
    return as_service(session)->session().can_undo() ? 1 : 0;
  } catch (const std::exception& e) {
    fail(TAMIAS_SERVER_E_FAILED, std::string("can_undo threw: ") + e.what());
    return -1;
  }
}

std::int32_t tamias_session_can_redo(void* session) {
  if (session == nullptr) {
    fail(TAMIAS_SERVER_E_BAD_HANDLE, "can_redo: null session");
    return -1;
  }
  try {
    return as_service(session)->session().can_redo() ? 1 : 0;
  } catch (const std::exception& e) {
    fail(TAMIAS_SERVER_E_FAILED, std::string("can_redo threw: ") + e.what());
    return -1;
  }
}

std::int32_t tamias_session_document_name(void* session, char* utf8, std::int32_t cap) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "document_name", &code)) {
    return code;
  }
  try {
    return fill_utf8(as_service(session)->document_name(), utf8, cap);
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("document_name threw: ") + e.what());
  }
}

std::int32_t tamias_session_entity_count(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "entity_count", &code)) {
    return code;
  }
  try {
    return static_cast<std::int32_t>(as_service(session)->entity_ids().size());
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("entity_count threw: ") + e.what());
  }
}

std::int32_t tamias_session_entity_id_at(void* session, std::int32_t index,
                                        std::uint64_t* out_id) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "entity_id_at", &code)) {
    return code;
  }
  if (out_id == nullptr) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "entity_id_at: null out_id");
  }
  try {
    const auto ids = as_service(session)->entity_ids();
    if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
      return fail(TAMIAS_SERVER_E_BAD_ARG, "entity_id_at: index out of range");
    }
    *out_id = ids[static_cast<std::size_t>(index)];
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("entity_id_at threw: ") + e.what());
  }
}

std::int32_t tamias_session_entity_name(void* session, std::uint64_t id, char* utf8,
                                        std::int32_t cap) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "entity_name", &code)) {
    return code;
  }
  try {
    return fill_utf8(as_service(session)->entity_name(id), utf8, cap);
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("entity_name threw: ") + e.what());
  }
}

std::int32_t tamias_session_entity_kind(void* session, std::uint64_t id, char* utf8,
                                        std::int32_t cap) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "entity_kind", &code)) {
    return code;
  }
  try {
    return fill_utf8(as_service(session)->entity_kind(id), utf8, cap);
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("entity_kind threw: ") + e.what());
  }
}

std::int32_t tamias_session_selection_count(void* session) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "selection_count", &code)) {
    return code;
  }
  try {
    return static_cast<std::int32_t>(as_service(session)->session().selection().size());
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("selection_count threw: ") + e.what());
  }
}

std::int32_t tamias_session_selection_id_at(void* session, std::int32_t index,
                                           std::uint64_t* out_id) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "selection_id_at", &code)) {
    return code;
  }
  if (out_id == nullptr) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "selection_id_at: null out_id");
  }
  try {
    const auto ids = as_service(session)->session().selection();
    if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
      return fail(TAMIAS_SERVER_E_BAD_ARG, "selection_id_at: index out of range");
    }
    *out_id = ids[static_cast<std::size_t>(index)];
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("selection_id_at threw: ") + e.what());
  }
}

std::int32_t tamias_session_set_selection(void* session, const std::uint64_t* ids,
                                         std::int32_t count) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "set_selection", &code)) {
    return code;
  }
  if (count < 0 || (count > 0 && ids == nullptr)) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "set_selection: invalid ids/count");
  }
  try {
    std::vector<std::uint64_t> copy;
    if (count > 0) {
      copy.assign(ids, ids + count);
    }
    as_service(session)->session().set_selection(copy);
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("set_selection threw: ") + e.what());
  }
}

std::int32_t tamias_session_load_bytes(void* session, const char* name_utf8,
                                       const std::uint8_t* data, std::int32_t size) {
  std::int32_t code = TAMIAS_SERVER_OK;
  if (!require_session(session, "load_bytes", &code)) {
    return code;
  }
  if (name_utf8 == nullptr) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "load_bytes: null name");
  }
  if (size < 0 || (size > 0 && data == nullptr)) {
    return fail(TAMIAS_SERVER_E_BAD_ARG, "load_bytes: invalid data/size");
  }
  try {
    const std::span<const std::uint8_t> bytes(data, static_cast<std::size_t>(size));
    auto r = as_service(session)->load_bytes(name_utf8, bytes);
    if (!r) {
      // 未知扩展名是「调用方给错了」，不是「服务端坏了」——分开报，HTTP 侧才好映射成 4xx。
      if (r.error().find("unsupported type") != std::string::npos) {
        return fail(TAMIAS_SERVER_E_UNSUPPORTED, r.error());
      }
      return fail(TAMIAS_SERVER_E_FAILED, r.error());
    }
    return TAMIAS_SERVER_OK;
  } catch (const std::exception& e) {
    return fail(TAMIAS_SERVER_E_FAILED, std::string("load_bytes threw: ") + e.what());
  }
}

}  // extern "C"
