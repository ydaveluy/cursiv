#include "cursiv/observability/ObservationFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace cursiv {
namespace {

// Every control character becomes a space: a line break would split the
// line, a NUL end it where a C string stops, an escape steer a terminal.
void append_flat(std::string& out, std::string_view text) {
  const std::size_t at = out.size();
  out.append(text);
  std::ranges::replace_if(
      out.begin() + static_cast<std::ptrdiff_t>(at), out.end(),
      [](char c) { return static_cast<unsigned char>(c) < 0x20; }, ' ');
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
    case ValidationPreparationThrew:
      return "ValidationPreparationThrew";
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

std::string detail::format_observation(const Observation& observation) {
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
