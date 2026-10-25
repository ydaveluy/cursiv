#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "cursiv/core/services/JsonValue.hpp"
#include "cursiv/core/text/TextRange.hpp"

// What a document reports, in the shape a client publishes it: severity, code
// and its description, tags, related locations, a machine payload. Extensions
// stay additive -- an empty vector or optional changes no consumer.

namespace cursiv {

// The values are LSP's, so publication needs no translation.
enum class DiagnosticSeverity : std::uint8_t {
  Error = 1,
  Warning = 2,
  Information = 3,
  Hint = 4,
};

// UI hints: `Unnecessary` renders greyed out, `Deprecated` struck through.
enum class DiagnosticTag : std::uint8_t { Unnecessary = 1, Deprecated = 2 };

// LSP's `integer | string`, carried as the variant it is, with the two
// comparisons every consumer writes: `*code == "..."` is one expression
// whichever alternative is held.
struct DiagnosticCode {
  std::variant<std::int64_t, std::string> value;

  // The owning spellings convert implicitly; the borrowed ones are explicit, so
  // `*code == "..."` means the comparison below and never a conversion into a
  // second code.
  explicit(false) DiagnosticCode(std::string v) noexcept
      : value(std::move(v)) {}
  explicit DiagnosticCode(std::string_view v) : value(std::string(v)) {}
  explicit DiagnosticCode(const char* v)
      : value(v == nullptr ? std::string() : std::string(v)) {}
  explicit(false) DiagnosticCode(std::int64_t v) noexcept : value(v) {}

  [[nodiscard]] bool isString() const noexcept {
    return std::holds_alternative<std::string>(value);
  }
  [[nodiscard]] bool isInteger() const noexcept {
    return std::holds_alternative<std::int64_t>(value);
  }
  // Precondition on each: the matching alternative is held.
  [[nodiscard]] const std::string& string() const {
    return std::get<std::string>(value);
  }
  [[nodiscard]] std::int64_t integer() const {
    return std::get<std::int64_t>(value);
  }

  [[nodiscard]] bool operator==(std::string_view other) const noexcept {
    return isString() && string() == other;
  }
  [[nodiscard]] bool operator==(std::int64_t other) const noexcept {
    return isInteger() && integer() == other;
  }
  [[nodiscard]] bool operator==(const DiagnosticCode&) const = default;
};

// An additional location a diagnostic points at. An empty `uri` means this same
// document, which is what most checks mean.
struct DiagnosticRelatedInformation {
  std::string uri;
  std::string message;
  TextRange range{};
};

// `ParseDiagnostic` is message + `NodeId`; this is offsets + severity + source.
// Parsing converts the first into the second, never the reverse.
//
// `data` is the machine's field: it round-trips untouched into `codeAction`
// requests, which is what lets a quick fix know what to insert without parsing
// the message's English back out.
struct Diagnostic {
  TextRange range{};
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string message;
  std::string_view source;  // the language name, static
  std::optional<DiagnosticCode> code = {};
  std::optional<std::string> codeDescription = {};
  std::vector<DiagnosticTag> tags = {};
  std::vector<DiagnosticRelatedInformation> relatedInformation = {};
  std::optional<JsonValue> data = {};
};

}  // namespace cursiv
