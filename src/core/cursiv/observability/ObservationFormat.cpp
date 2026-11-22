#include "cursiv/observability/ObservationFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

#include "cursiv/text/scalar_at.hpp"

namespace cursiv {
namespace {

// Every control character, C0, DEL or C1, becomes a space: a line break would
// split the line, a NUL end it where a C string stops, an escape control a
// terminal. Bytes that encode no character are copied as they are.
void append_flat(std::string& out, std::string_view text) {
  for (std::size_t at = 0; at < text.size();) {
    std::size_t len = 0;
    const char32_t scalar = scalarAt(text.data() + at, text.size() - at, len);
    len = std::max<std::size_t>(len, 1);
    if (scalar < 0x20 || (scalar >= 0x7F && scalar <= 0x9F))
      out += ' ';
    else
      out.append(text.substr(at, len));
    at += len;
  }
}

void append_field(std::string& out, std::string_view key,
                  std::string_view value) {
  if (value.empty()) return;
  out += ' ';
  out += key;
  out += '=';
  append_flat(out, value);
}

std::string_view to_string(ObservationSeverity severity) noexcept {
  using enum ObservationSeverity;
  switch (severity) {
    case Trace:
      return "trace";
    case Info:
      return "info";
    case Warning:
      return "warning";
    case Error:
      return "error";
  }
  return "?";
}

std::string_view to_string(ObservationCode code) noexcept {
  using enum ObservationCode;
  switch (code) {
    case WorkspaceDirectoryReadFailed:
      return "WorkspaceDirectoryReadFailed";
    case WorkspaceBootstrapFailed:
      return "WorkspaceBootstrapFailed";
    case LanguageMappingCollision:
      return "LanguageMappingCollision";
    case ScopeReadBeforeComputed:
      return "ScopeReadBeforeComputed";
    case ValidationCheckThrew:
      return "ValidationCheckThrew";
    case DocumentCheckThrew:
      return "DocumentCheckThrew";
    case WorkspaceDocumentReadFailed:
      return "WorkspaceDocumentReadFailed";
    case ReferenceResolutionThrew:
      return "ReferenceResolutionThrew";
    case NotificationThrew:
      return "NotificationThrew";
  }
  return "?";
}

}  // namespace

std::string formatObservation(const Observation& observation) {
  std::string formatted;
  formatted.reserve(observation.message.size() + 64);
  formatted.append(to_string(observation.severity));
  formatted += ' ';
  formatted.append(to_string(observation.code));
  formatted += ": ";
  append_flat(formatted, observation.message);
  append_field(formatted, "uri", observation.uri);
  append_field(formatted, "languageId", observation.languageId);
  return formatted;
}

}  // namespace cursiv
