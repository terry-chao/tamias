#pragma once

// 服务端 C ABI（实现在 server_api.cpp + session_service.cpp）。
//
// 为什么要有这一层：服务端是 `Session` 的**第四个宿主**。桌面宿主是 Qt（src/app），
// 浏览器宿主是 embind（src/wasm），MCP 宿主是应用内 loopback HTTP；服务端宿主是
// 「C#/.NET 服务壳 P/Invoke 进这个扁平 ABI」。决策与边界见
// docs/DECISION-SERVER-LANGUAGE.md。
//
// 改这里之前先读那份文档。三条硬约定：
//
//  1. **只追加**：不改既有函数的签名，不改枚举的既有值。加函数 / 改布局要同步抬
//     TAMIAS_SERVER_ABI_VERSION（与 src/plugin/host_api.h 的 kHostApiVersion 同一套
//     规矩）；服务壳启动时握手，对不上就拒绝启动——宁可起不来，也不要按错位的表调用。
//  2. **边界粗粒度**：一次调用做完一整件事。几何（网格 / Brep / 内核对象）永远不过
//     边界，过边界的只有命令名、参数文本和只读字符串。
//  3. **句柄不是线程安全的**：一个会话句柄同一时刻只能被一个线程使用。并行请求由
//     服务壳按会话分派。
//
// 这一层**不是**第二套编辑 API：写操作仍然只有 `dispatch` 一条路，走的是桌面 / 插件 /
// AI 同一条命令注册表（`command_registry()`）。新加的只是「传输层的扁平 C ABI」。

#include <cstdint>

#if defined(_WIN32)
#  if defined(TAMIAS_SERVER_BUILD)
#    define TAMIAS_SERVER_API __declspec(dllexport)
#  else
#    define TAMIAS_SERVER_API __declspec(dllimport)
#  endif
#else
#  define TAMIAS_SERVER_API __attribute__((visibility("default")))
#endif

extern "C" {

// 版本与错误码。错误码前四个与 Truck 桥同构（truck_bridge.h），两边壳代码可以共用。
enum {
  TAMIAS_SERVER_ABI_VERSION = 1,

  TAMIAS_SERVER_OK = 0,
  TAMIAS_SERVER_E_FAILED = 1,
  TAMIAS_SERVER_E_BAD_ARG = 2,
  TAMIAS_SERVER_E_BAD_HANDLE = 3,
  TAMIAS_SERVER_E_UNSUPPORTED = 4,
};

TAMIAS_SERVER_API const char* tamias_server_version(void);

// 服务壳启动时握手的**唯一**依据：返回 TAMIAS_SERVER_ABI_VERSION。
// 不要靠解析 version() 那个字符串——那是给人看的，格式不是契约。
TAMIAS_SERVER_API std::int32_t tamias_server_abi_version(void);

// 最近一次失败的原因（UTF-8）。**线程局部**，只在同一线程上、下一次调用之前有效。
// 成功时不清空它——只在返回非 OK 时读。
TAMIAS_SERVER_API const char* tamias_server_last_error(void);

// 进程级初始化：注册建模内核与命令注册表。幂等，可重复调用；
// tamias_session_create 也会自动调它，所以服务壳不必显式调。
TAMIAS_SERVER_API std::int32_t tamias_server_init(void);

// ── 会话 ──────────────────────────────────────────────────────────────────────
// 一个会话 = 一份 Session（文档 + 命令栈 + 相机 + 选择），即「一个打开的文档」。
// 名称可以为 NULL（退化成 "Untitled"）。失败返回 NULL，原因见 last_error()。
TAMIAS_SERVER_API void* tamias_session_create(const char* document_name_utf8);
TAMIAS_SERVER_API void tamias_session_destroy(void* session);

// ── 窄写：只有这一条路 ────────────────────────────────────────────────────────
// args_utf8 走与桌面控制台同一条解析器（host/command_arg_text.h），
// 例如 "s:sub_type=rect;d:width=0.4;d:height=3.0;p:points=0,0,0"。可以为 NULL / 空。
TAMIAS_SERVER_API std::int32_t tamias_session_dispatch(void* session, const char* command_utf8,
                                                       const char* args_utf8);

// 事务：begin 与 commit 之间成功执行的命令合成**一条**撤销记录；abort 逆序回滚，
// 栈里不留记录。不能嵌套（嵌套返回失败，原因见 last_error）。
TAMIAS_SERVER_API std::int32_t tamias_session_begin_transaction(void* session,
                                                                const char* name_utf8);
TAMIAS_SERVER_API std::int32_t tamias_session_commit_transaction(void* session);
TAMIAS_SERVER_API std::int32_t tamias_session_abort_transaction(void* session);

// 栈空时是**无操作**并返回 OK（幂等，便于 HTTP 侧重试）。要判断有没有得撤，用 can_undo。
TAMIAS_SERVER_API std::int32_t tamias_session_undo(void* session);
TAMIAS_SERVER_API std::int32_t tamias_session_redo(void* session);
// 1 = 可以，0 = 不可以，负数 = 出错。
TAMIAS_SERVER_API std::int32_t tamias_session_can_undo(void* session);
TAMIAS_SERVER_API std::int32_t tamias_session_can_redo(void* session);

// ── 宽读：只读快照，不暴露内核对象 ────────────────────────────────────────────
// 取字符串：写入 UTF-8 + '\0'，返回写入的字节数（不含结尾的 0）；参数不合法 -1。
// cap 不够时**截断**而不是报错（与 plugin_host.cpp 的 fill_utf8 同语义）。
TAMIAS_SERVER_API std::int32_t tamias_session_document_name(void* session, char* utf8,
                                                            std::int32_t cap);
TAMIAS_SERVER_API std::int32_t tamias_session_entity_count(void* session);
TAMIAS_SERVER_API std::int32_t tamias_session_entity_id_at(void* session, std::int32_t index,
                                                           std::uint64_t* out_id);
TAMIAS_SERVER_API std::int32_t tamias_session_entity_name(void* session, std::uint64_t id,
                                                          char* utf8, std::int32_t cap);
TAMIAS_SERVER_API std::int32_t tamias_session_entity_kind(void* session, std::uint64_t id,
                                                          char* utf8, std::int32_t cap);

TAMIAS_SERVER_API std::int32_t tamias_session_selection_count(void* session);
TAMIAS_SERVER_API std::int32_t tamias_session_selection_id_at(void* session, std::int32_t index,
                                                              std::uint64_t* out_id);
TAMIAS_SERVER_API std::int32_t tamias_session_set_selection(void* session,
                                                            const std::uint64_t* ids,
                                                            std::int32_t count);

// ── 文档 IO（内存）─────────────────────────────────────────────────────────────
// 按扩展名分派：.tdoc / .trscn / .obj。data 由调用方拥有，调用期间有效即可；
// 载入成功后会话换成新文档（选择与命令栈清空）。
TAMIAS_SERVER_API std::int32_t tamias_session_load_bytes(void* session, const char* name_utf8,
                                                         const std::uint8_t* data,
                                                         std::int32_t size);

}  // extern "C"
