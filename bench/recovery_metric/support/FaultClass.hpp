// The classes a faulted corpus is drawn in, each reported on its own: a sum
// over classes would let one class hide another.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cursiv::recovery_metric {

enum class FaultClass : std::uint8_t {
  byte,        // one byte inserted, deleted or replaced
  spread1,     // three token faults, one token between each
  spread3,
  spread10,
  separators,  // a run of separators gone, one fault each
  eof1,        // the text cut where 1 closer is owed: one fault
  eof3,        // 2-3 closers
  eof7,        // 4-7 closers
  eof20,       // 8-20 closers
  quote,       // a string's closing quote gone
  escape,      // a string's closing quote escaped
  comment,     // a block comment's closer gone, none after it
  hole,        // a run of stray tokens: one fault
};

inline constexpr std::size_t kFaultClasses = 13;

[[nodiscard]] constexpr std::string_view fault_class_name(FaultClass k) {
  constexpr std::string_view kName[kFaultClasses] = {
      "byte",   "multi:1", "multi:3",   "multi:10",  "sep-series",
      "eof:1",  "eof:2-3", "eof:4-7",   "eof:8-20",  "lex:quote",
      "lex:esc", "lex:comment", "hole"};
  return kName[static_cast<std::size_t>(k)];
}

}  // namespace cursiv::recovery_metric
