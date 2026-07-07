#include <pegium/core/parser/RecoveryDebug.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <pegium/core/grammar/AbstractElement.hpp>

namespace pegium::parser::detail {
namespace {

[[nodiscard]] std::int64_t json_int(TextOffset value) noexcept {
  return static_cast<std::int64_t>(value);
}

[[nodiscard]] std::int64_t json_int(std::size_t value) noexcept {
  return static_cast<std::int64_t>(value);
}

struct RecoveryAttemptEditSummary {
  TextOffset firstEditOffset = 0;
  TextOffset lastEditOffset = 0;
  TextOffset editSpan = 0;
  std::size_t insertCount = 0;
  std::size_t deleteCount = 0;
  std::size_t replaceCount = 0;
  std::size_t editCount = 0;
  std::size_t entryCount = 0;
  std::uint32_t editCost = 0;
  bool hasEdits = false;
};

[[nodiscard]] RecoveryAttemptEditSummary
summarize_recovery_attempt_edits(const RecoveryAttempt &attempt) {
  // No-edit attempts report the sentinel: with no edits there is no
  // meaningful first-edit offset. The loop below min-folds the real edit
  // offsets when edits exist.
  RecoveryAttemptEditSummary summary{
      .firstEditOffset = std::numeric_limits<TextOffset>::max(),
      .editCount = attempt.editCount,
      .entryCount = attempt.recoveryEdits.size(),
      .editCost = attempt.editCost,
  };
  using enum ParseDiagnosticKind;
  for (const auto &entry : attempt.recoveryEdits) {
    switch (entry.kind) {
    case Inserted:
      ++summary.insertCount;
      break;
    case Deleted:
      ++summary.deleteCount;
      break;
    case Replaced:
      ++summary.replaceCount;
      break;
    case Incomplete:
    case Recovered:
    case ConversionError:
      break;
    }
    summary.firstEditOffset = std::min(summary.firstEditOffset, entry.beginOffset);
    summary.lastEditOffset = std::max(summary.lastEditOffset, entry.endOffset);
  }
  summary.hasEdits = !attempt.recoveryEdits.empty();
  if (summary.hasEdits) {
    summary.editSpan = summary.lastEditOffset - summary.firstEditOffset;
  }
  return summary;
}

[[nodiscard]] std::string element_text(const grammar::AbstractElement *element) {
  if (element == nullptr) {
    return "null";
  }
  std::ostringstream stream;
  element->print(stream);
  return std::move(stream).str();
}

[[nodiscard]] pegium::JsonValue
element_to_json(const grammar::AbstractElement *element) {
  if (element == nullptr) {
    return nullptr;
  }

  return pegium::JsonValue::Object{
      {"kind", json_int(static_cast<std::uint32_t>(element->getKind()))},
      {"text", element_text(element)},
  };
}

[[nodiscard]] pegium::JsonValue
syntax_entry_to_json(const auto &entry) {
  std::ostringstream stream;
  stream << entry.kind;
  return pegium::JsonValue::Object{
      {"element", element_to_json(entry.element)},
      {"kind", stream.str()},
      {"offset", json_int(entry.offset)},
  };
}

[[nodiscard]] pegium::JsonValue
failure_leaf_to_json(const FailureLeaf &leaf) {
  return pegium::JsonValue::Object{
      {"beginOffset", json_int(leaf.beginOffset)},
      {"element", element_to_json(leaf.element)},
      {"endOffset", json_int(leaf.endOffset)},
  };
}

[[nodiscard]] pegium::JsonValue
edit_summary_to_json(const RecoveryAttemptEditSummary &summary) {
  return pegium::JsonValue::Object{
      {"deleteCount", json_int(summary.deleteCount)},
      {"entryCount", json_int(summary.entryCount)},
      {"editCost", json_int(summary.editCost)},
      {"editCount", json_int(summary.editCount)},
      {"editSpan", json_int(summary.editSpan)},
      {"firstEditOffset", json_int(summary.firstEditOffset)},
      {"hasEdits", summary.hasEdits},
      {"insertCount", json_int(summary.insertCount)},
      {"lastEditOffset", json_int(summary.lastEditOffset)},
      {"replaceCount", json_int(summary.replaceCount)},
  };
}

[[nodiscard]] pegium::JsonValue order_key_to_json(const RecoveryKey &key) {
  // Mirror every is_better_recovery_key axis in comparison order so a dump
  // fully explains a ranking: fullMatch > matched > firstEditScore >
  // faithfulness > progressAfterEdits. The raw fields (firstEditOffset,
  // editCost, editCount) are kept alongside the two DERIVED scores the
  // comparator actually orders on.
  return pegium::JsonValue::Object{
      {"fullMatch", key.fullMatch},
      {"matched", key.matched},
      {"firstEditOffset", json_int(key.firstEditOffset)},
      {"rankAnchorOffset", json_int(key.rankAnchorOffset)},
      {"firstEditScore", json_int(recovery_key_first_edit_score(key))},
      {"editCost", json_int(key.editCost)},
      {"editCount", json_int(key.editCount)},
      {"faithfulness", json_int(faithfulness(key))},
      {"progressAfterEdits", json_int(key.progressAfterEdits)},
  };
}

[[nodiscard]] pegium::JsonValue
attempt_spec_to_json(const RecoveryAttemptSpec &spec) {
  pegium::JsonValue::Object object{
      {"window", recovery_window_to_json(spec.window)},
  };
  if (!spec.committedRecoveryEdits.empty()) {
    object.try_emplace("committedRecoveryResumeFloor",
                       json_int(spec.committedRecoveryResumeFloor));
    object.try_emplace("committedRecoveryEditCount",
                       json_int(spec.committedRecoveryEdits.size()));
  }
  return object;
}

} // namespace

std::string_view
recovery_attempt_status_name(RecoveryAttemptStatus status) noexcept {
  using enum RecoveryAttemptStatus;
  switch (status) {
  case StrictFailure:
    return "StrictFailure";
  case RecoveredButNotCredible:
    return "RecoveredButNotCredible";
  case Selectable:
    return "Selectable";
  }
  return "Unknown";
}

pegium::JsonValue
strict_parse_summary_to_json(const StrictParseSummary &summary) {
  return pegium::JsonValue::Object{
      {"entryRuleMatched", summary.entryRuleMatched},
      {"fullMatch", summary.fullMatch},
      {"inputSize", json_int(summary.inputSize)},
      {"maxCursorOffset", json_int(summary.maxCursorOffset)},
      {"parsedLength", json_int(summary.parsedLength)},
  };
}

pegium::JsonValue failure_snapshot_to_json(const FailureSnapshot &snapshot) {
  pegium::JsonValue::Array leaves;
  leaves.reserve(snapshot.failureLeafHistory.size());
  for (const auto &leaf : snapshot.failureLeafHistory) {
    leaves.emplace_back(failure_leaf_to_json(leaf));
  }

  pegium::JsonValue::Object object{
      {"failureLeafHistory", std::move(leaves)},
      {"failureTokenIndex", json_int(snapshot.failureTokenIndex)},
      {"hasFailureToken", snapshot.hasFailureToken},
      {"maxCursorOffset", json_int(snapshot.maxCursorOffset)},
  };
  if (snapshot.hasFailureToken) {
    object.try_emplace(
        "failureToken",
        failure_leaf_to_json(snapshot.failureLeafHistory[snapshot.failureTokenIndex]));
  } else {
    object.try_emplace("failureToken", nullptr);
  }
  return object;
}

pegium::JsonValue recovery_window_to_json(const RecoveryWindow &window) {
  return pegium::JsonValue::Object{
      {"backwardTokenCount", json_int(window.tokenCount)},
      {"beginOffset", json_int(window.beginOffset)},
      {"editFloorOffset", json_int(window.editFloorOffset)},
      {"forwardTokenCount", json_int(window.forwardTokenCount)},
      {"hasStablePrefix", window.hasStablePrefix},
      {"maxCursorOffset", json_int(window.maxCursorOffset)},
      {"stablePrefixOffset", json_int(window.stablePrefixOffset)},
      {"visibleLeafBeginIndex", json_int(window.visibleLeafBeginIndex)},
  };
}

pegium::JsonValue
recovery_windows_to_json(std::span<const RecoveryWindow> windows) {
  pegium::JsonValue::Array array;
  array.reserve(windows.size());
  for (const auto &window : windows) {
    array.emplace_back(recovery_window_to_json(window));
  }
  return array;
}

pegium::JsonValue
recovery_attempt_to_json(const RecoveryAttempt &attempt,
                         const RecoveryAttemptSpec *spec) {
  const auto editSummary = summarize_recovery_attempt_edits(attempt);
  pegium::JsonValue::Array recoveryEdits;
  recoveryEdits.reserve(attempt.recoveryEdits.size());
  for (const auto &edit : attempt.recoveryEdits) {
    recoveryEdits.emplace_back(syntax_entry_to_json(edit));
  }

  const auto parseDiagnostics =
      materialize_syntax_diagnostics(attempt.recoveryEdits);
  pegium::JsonValue::Array diagnostics;
  diagnostics.reserve(parseDiagnostics.size());
  for (const auto &diagnostic : parseDiagnostics) {
    diagnostics.emplace_back(syntax_entry_to_json(diagnostic));
  }

  pegium::JsonValue::Object object{
      {"completedRecoveryWindows", json_int(attempt.completedRecoveryWindows)},
      {"editCost", json_int(attempt.editCost)},
      {"editCount", json_int(attempt.editCount)},
      {"editTrace", edit_summary_to_json(editSummary)},
      {"entryRuleMatched", attempt.entryRuleMatched},
      {"failureSnapshot",
       attempt.failureSnapshot.has_value()
           ? failure_snapshot_to_json(*attempt.failureSnapshot)
           : pegium::JsonValue(nullptr)},
      {"fullMatch", attempt.fullMatch},
      {"maxCursorOffset", json_int(attempt.maxCursorOffset)},
      {"lastVisibleCursorOffset", json_int(attempt.lastVisibleCursorOffset)},
      {"orderKey", order_key_to_json(recovery_attempt_key(attempt))},
      {"parseDiagnostics", std::move(diagnostics)},
      {"recoveryEdits", std::move(recoveryEdits)},
      {"parsedLength", json_int(attempt.parsedLength)},
      {"reachedRecoveryTarget", attempt.reachedRecoveryTarget},
      {"hasStablePrefix", attempt.hasStablePrefix},
      {"stableAfterRecovery", attempt.stableAfterRecovery},
      {"stablePrefixOffset", json_int(attempt.stablePrefixOffset)},
      {"status", std::string(recovery_attempt_status_name(attempt.status))},
  };
  if (attempt.replayWindow.has_value()) {
    object.try_emplace("replayWindow",
                       recovery_window_to_json(*attempt.replayWindow));
  }
  if (spec != nullptr) {
    object.try_emplace("spec", attempt_spec_to_json(*spec));
  }
  return object;
}

pegium::JsonValue
recovery_search_run_to_json(const RecoverySearchRunResult &run) {
  return pegium::JsonValue::Object{
      {"failureVisibleCursorOffset", json_int(run.failureVisibleCursorOffset)},
      {"recoveryAttemptRuns", json_int(run.recoveryAttemptRuns)},
      {"recoveryWindowsTried", json_int(run.recoveryWindowsTried)},
      {"selectedAttempt", recovery_attempt_to_json(run.selectedAttempt)},
      {"selectedWindows", recovery_windows_to_json(run.selectedWindows)},
      {"strictParseRuns", json_int(run.strictParseRuns)},
  };
}

} // namespace pegium::parser::detail
