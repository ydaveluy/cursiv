#include "cursiv/core/observability/ObservationFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace cursiv {
namespace {

void append_flat(std::string& out, std::string_view text) {
  const std::size_t at = out.size();
  out.append(text);
  std::ranges::replace_if(
      out.begin() + static_cast<std::ptrdiff_t>(at), out.end(),
      [](char c) { return c == '\n' || c == '\r'; }, ' ');
}

void append_field(std::string& out, std::string_view key,
                  std::string_view value) {
  if (value.empty()) return;
  out += ' ';
  out += key;
  out += '=';
  append_flat(out, value);
}

}  // namespace

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
  }
  return "?";
}

std::string format_observation(const Observation& observation) {
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
