#include "engine/profile/timing_xml_writer.h"

#include "engine/core/fs_utf8.h"
#include "engine/profile/timing_session.h"

#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {
namespace {

void append_escaped(std::string& out, std::string_view text) {
  out.reserve(out.size() + text.size());
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      default:
        out += c;
        break;
    }
  }
}

void write_event(std::string& out, const std::vector<TimingEvent>& events,
                 const std::vector<std::vector<int>>& children, int index, int indent) {
  const TimingEvent& event = events[static_cast<std::size_t>(index)];
  out.append(static_cast<std::size_t>(indent), ' ');
  out += "<event name=\"";
  append_escaped(out, event.name);
  out += "\" category=\"";
  out += timing_category_name(event.category);
  out += "\" start_us=\"";
  out += std::to_string(event.start_us);
  out += "\" duration_us=\"";
  out += std::to_string(event.duration_us);
  out += "\" thread_id=\"";
  out += std::to_string(event.thread_id);
  const auto& kids = children[static_cast<std::size_t>(index)];
  if (kids.empty()) {
    out += "\"/>\n";
    return;
  }
  out += "\">\n";
  for (int child : kids) {
    write_event(out, events, children, child, indent + 4);
  }
  out.append(static_cast<std::size_t>(indent), ' ');
  out += "</event>\n";
}

}  // namespace

std::string TimingXmlWriter::to_string(const TimingSession& session) {
  const std::vector<TimingEvent> events = session.events();
  const std::string started = session.started_utc();
  const std::uint64_t duration = session.duration_us();

  std::vector<std::vector<int>> children(events.size());
  std::vector<int> roots;
  roots.reserve(events.size());
  for (int i = 0; i < static_cast<int>(events.size()); ++i) {
    const int parent = events[static_cast<std::size_t>(i)].parent;
    if (parent < 0 || parent >= static_cast<int>(events.size())) {
      roots.push_back(i);
    } else {
      children[static_cast<std::size_t>(parent)].push_back(i);
    }
  }

  std::string out;
  out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  out += "<TamiasTimingReport version=\"1\">\n";
  out += "  <session started=\"";
  append_escaped(out, started);
  out += "\" duration_us=\"";
  out += std::to_string(duration);
  if (roots.empty()) {
    out += "\"/>\n";
  } else {
    out += "\">\n";
    for (int root : roots) {
      write_event(out, events, children, root, 4);
    }
    out += "  </session>\n";
  }
  out += "</TamiasTimingReport>\n";
  return out;
}

Result<void> TimingXmlWriter::write_file(const TimingSession& session,
                                         const std::filesystem::path& path) {
  const std::string xml = to_string(session);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return Err("Failed to open file for writing: " + path_to_utf8(path));
  }
  out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
  if (!out) {
    return Err("Failed to write timing report: " + path_to_utf8(path));
  }
  return {};
}

}  // namespace tamias
