#include "engine/profile/timing_scope.h"

#include "engine/profile/timing_session.h"

namespace tamias {

TimingScope::TimingScope(std::string_view name, TimingCategory category) {
  auto& session = TimingSession::instance();
  if (!session.is_recording() || !session.category_enabled(category)) {
    return;
  }
  generation_ = session.generation();
  index_ = session.begin_event(name, category);
}

TimingScope::~TimingScope() {
  if (index_ >= 0) {
    TimingSession::instance().end_event(index_, generation_);
  }
}

}  // namespace tamias
