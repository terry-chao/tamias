#pragma once

#include <QString>

namespace tamias {

// 密钥落盘的地方。
//
// Windows 走**凭据管理器**：DPAPI 按当前用户加密，存进系统凭据库，不进注册表、
// 不进 QSettings、不进项目目录，换个用户登录读不到。其它平台退化成 AppConfigLocation
// 下一个只有属主可读的文件——保护弱一档，所以 backend_name() 要照实说、UI 得展示。
//
// `key` 是槽位标识（内部用，如 "ai/api-key"），不是给人看的名字。
// 存的 secret 是原文：打开凭据管理器能直接看到和删掉，这是有意的——用户得能自己清理。
class SecretStore final {
 public:
  SecretStore() = delete;

  // 没存过、或者系统拒绝了读取，都返回空串。读失败不算错误，也不动任何东西。
  [[nodiscard]] static QString load(const QString& key);
  // secret 为空表示**删除**该槽位（不是存一个空密钥）。失败时把原因写进 error。
  static bool save(const QString& key, const QString& secret, QString* error = nullptr);
  [[nodiscard]] static bool remove(const QString& key);
  // 密钥存在哪儿的说法，直接拼进 UI 文案。
  [[nodiscard]] static QString backend_name();
};

}  // namespace tamias
