#pragma once

#include <QString>

namespace tamias {

struct GoldenTestCase {
  enum class Status { Passed, Failed, Skipped };

  QString id;
  Status status = Status::Failed;
  int duration_ms = -1;
};

}  // namespace tamias
