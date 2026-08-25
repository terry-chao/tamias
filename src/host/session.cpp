#include "host/session.h"

#include <utility>

namespace tamias {

Session::Session(std::shared_ptr<Document> document) : document_(std::move(document)) {}

Session::~Session() = default;

void Session::reset_document(std::shared_ptr<Document> document) {
  document_ = std::move(document);
  command_system_.clear();
  tool_mode_ = ToolMode::None;
  notify(HostEvent::DocumentChanged);
}

Result<void> Session::dispatch(std::string_view name, const CommandArgs& args) {
  return command_system_.dispatch(*document_, std::string(name), args);
}

void Session::undo() {
  command_system_.undo();
  notify(HostEvent::DocumentChanged);
}

void Session::redo() {
  command_system_.redo();
  notify(HostEvent::DocumentChanged);
}

void Session::set_tool(ToolMode mode) {
  if (tool_mode_ == mode) {
    return;
  }
  tool_mode_ = mode;
  notify(HostEvent::ToolChanged);
}

void Session::select(std::uint64_t id) {
  document_->select(id);
  notify(HostEvent::SelectionChanged);
}

void Session::deselect(std::uint64_t id) {
  document_->deselect(id);
  notify(HostEvent::SelectionChanged);
}

void Session::clear_selection() {
  document_->clear_selection();
  notify(HostEvent::SelectionChanged);
}

void Session::set_selection(const std::vector<std::uint64_t>& ids) {
  document_->clear_selection();
  for (const std::uint64_t id : ids) {
    document_->select(id);
  }
  notify(HostEvent::SelectionChanged);
}

void Session::notify(HostEvent event, std::string_view message) {
  if (listener_) {
    listener_(event, message);
  }
}

}  // namespace tamias
