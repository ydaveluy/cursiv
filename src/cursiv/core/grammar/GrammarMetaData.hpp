#pragma once

#include <cstdint>
#include <string_view>

// What one compiled grammar answers about its records and its tokens, as data:
// a question per entry of the built table, and a row per terminal -- the tables
// an implementation of `Grammar` answers from, written by the generator.

namespace cursiv {

struct AstTypeTag;

// A literal is spelled by the grammar and can be handed over whole; every
// other terminal is told, so exactly one of the two is filled.
struct GrammarTerminal {
  std::string_view text;
  std::string_view name;
  std::string_view doc;
  bool folded = false;
};

struct GrammarMetaData {
  using AskFn = bool (*)(std::string_view);
  using SayFn = std::string_view (*)();

  const AskFn* writes = nullptr;
  const AskFn* fills = nullptr;
  const SayFn* features = nullptr;
  const SayFn* rules = nullptr;
  // The class each entry's member belongs to, which no tree holds before the
  // record is open.
  const AstTypeTag* const* types = nullptr;
  // The narrowing of the site each record writes; null for a grammar that
  // narrows nowhere.
  const std::uint8_t* narrows = nullptr;
  std::uint32_t recordCount = 0;

  const GrammarTerminal* terminals = nullptr;
  std::uint16_t terminalCount = 0;
};
}  // namespace cursiv
