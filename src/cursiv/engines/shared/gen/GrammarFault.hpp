// The one refusal a lowering records against a rule, never aborts on: a code
// and the rule that earned it. The walk carries on writing a well-formed value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

enum class GrammarFaultCode : std::uint8_t {
  none = 0,
  nullableRepetition,
  ruleWithoutProduction,
  twoEndMarks,
  optionalEndMark,
  optionalCondition,
  twinProductions,
  unitProductionCycle,
  hiddenLeftRecursion,
  leftRecursion,
  precedenceClash,
  infixOperatorNotText,
  directiveOnNoPosition,
  tooManyPrecedences,
  skippedTerminal,
  docConflict,
};

// Set once: a later refusal is a consequence, not a fault.
struct GrammarFault {
  GrammarFaultCode code = GrammarFaultCode::none;
  std::uint16_t rule = kNoRow;
  // The bound the refusal names, where `gated` says an engine's gate hid it.
  std::uint16_t count = 0;
  bool gated = false;

  constexpr explicit operator bool() const {
    return code != GrammarFaultCode::none;
  }
  constexpr void raise(GrammarFaultCode c, std::uint16_t owner = kNoRow,
                       bool byGate = false, std::uint16_t bound = 0) {
    if (code == GrammarFaultCode::none)
      code = c, rule = owner, gated = byGate, count = bound;
  }
  constexpr bool operator==(const GrammarFault&) const = default;
};

// The code, spelled: what a refusal lane pins on.
constexpr std::string_view grammar_fault_name(GrammarFaultCode c) {
  constexpr std::string_view names[] = {"none",
                                        "nullableRepetition",
                                        "ruleWithoutProduction",
                                        "twoEndMarks",
                                        "optionalEndMark",
                                        "optionalCondition",
                                        "twinProductions",
                                        "unitProductionCycle",
                                        "hiddenLeftRecursion",
                                        "leftRecursion",
                                        "precedenceClash",
                                        "infixOperatorNotText",
                                        "directiveOnNoPosition",
                                        "tooManyPrecedences",
                                        "skippedTerminal",
                                        "docConflict"};
  static_assert(std::size(names) ==
                static_cast<std::size_t>(GrammarFaultCode::docConflict) + 1);
  return names[static_cast<std::size_t>(c)];
}

// What the author is told: what the engine cannot hold and what to write
// instead, never which pass noticed.
inline std::string grammar_fault_text(const GrammarFault& f) {
  switch (f.code) {
    case GrammarFaultCode::nullableRepetition:
      return "repeats an item that may match nothing, so one text has as "
             "many readings as turns left empty -- make the item consume and "
             "let the repetition's minimum say how many may be absent: "
             "`many(option(x))` is `many(x)`, `repeat<2, 3>(option(x))` is "
             "`repeat<0, 3>(x)`";
    case GrammarFaultCode::ruleWithoutProduction:
      return "derives nothing: every reading of it turned out impossible, so "
             "whoever calls it can never return";
    case GrammarFaultCode::twoEndMarks:
      return "asserts end of input twice in one choice -- the second says "
             "nothing the first has not";
    case GrammarFaultCode::optionalEndMark:
      return "makes an end-of-input assertion optional: the reading that keeps "
             "it refuses everything past it, and the reading that drops it is "
             "that same production with no condition at all -- write `eof` "
             "where you mean it, or a choice one branch of which is `eof`";
    case GrammarFaultCode::optionalCondition:
      return "makes a condition optional: the reading that drops it reads "
             "the same text with no condition at all -- write the condition "
             "where it must hold, or make optional what it guards";
    case GrammarFaultCode::twinProductions:
      return "holds two alternatives that spell the same symbols and write "
             "different things -- no lookahead tells them apart, so the "
             "grammar has to say which it meant";
    case GrammarFaultCode::unitProductionCycle:
      return "stands on a cycle of reductions that read no text (`A -> B`, "
             "`B -> A`, or `A -> A B` where `B` may match nothing): the chain "
             "comes back to itself having read nothing, so one document has "
             "as many derivations as anyone cares to write";
    case GrammarFaultCode::hiddenLeftRecursion:
      if (f.gated)
        return "reaches itself at its left edge behind more than " +
               std::to_string(f.count) +
               " optional items: an alternative spells out the subsets of "
               "that many and reads each earlier one as an item that may "
               "match nothing, so before the first token the parser cannot "
               "know how many of them to skip -- keep " +
               std::to_string(f.count) +
               " optional items or fewer before the recursive call, group "
               "some behind one that consumes, or put the call first";
      return "reaches itself at its left edge behind an item that may match "
             "nothing (`A -> B A x` where `B` can be empty): before the first "
             "token the parser cannot know how many empty `B`s to read -- make "
             "that item consume, or move it after the recursive call";
    case GrammarFaultCode::leftRecursion:
      return "reaches itself before matching anything in a way the LL "
             "descent cannot fold -- spell a chain of binary operators as "
             "`infix<>`; otherwise give the cycle an arm that only calls its "
             "next rule, write an inner loop's turn before an outer one that "
             "starts with the same text, or make every reading consume a token "
             "first (refusals.md, leftRecursion)";
    case GrammarFaultCode::precedenceClash:
      return "names the same operator text on two levels of a precedence "
             "table, or with two groupings: nothing after its first token "
             "tells the readings apart, so the table cannot say which binds "
             "tighter -- give each text one level and one grouping, across "
             "every table that reads it";
    case GrammarFaultCode::infixOperatorNotText:
      return "names as an operator a rule that builds a node: an operator "
             "hands over text -- a literal, a terminal or a data rule, with a "
             "converter where `op` is not text";
    case GrammarFaultCode::directiveOnNoPosition:
      return "writes a member from `eof` alone, or from nothing: there is no "
             "record for the write to ride, so the member would never be "
             "written. Put the directive on the part the reading consumes, or "
             "on a choice one branch of which is `eof`";
    case GrammarFaultCode::tooManyPrecedences:
      return "declares more than 255 precedence levels";
    case GrammarFaultCode::skippedTerminal:
      return "names a terminal its reading skips as trivia: the lexer never "
             "hands it over there, so no text can match it -- drop it, or "
             "read it under a skipper that keeps it";
    case GrammarFaultCode::docConflict:
      return "documents one word twice with different prose: a token has one "
             "hover text -- keep one of them";
    case GrammarFaultCode::none:
      break;
  }
  return "";
}

// The sentence, then the rule as its author wrote it, then what the engine
// kept of it. `rule` is empty where no rule is named.
inline std::string grammar_fault_message(const GrammarFault& f,
                                         std::string_view rule,
                                         std::string_view written,
                                         std::string_view kept = {}) {
  if (!f) return "";
  const std::string who = rule.empty() ? std::string("a rule of this grammar ")
                                       : "rule `" + std::string(rule) + "` ";
  return "cursiv: " + who + grammar_fault_text(f) + std::string(written) +
         std::string(kept);
}

// The rules a cycle runs through, back to the first, on a line of its own.
inline std::string grammar_fault_cycle(
    std::span<const std::string_view> rules) {
  if (rules.empty()) return {};
  std::string out = "\n    cycle:";
  for (const auto rule : rules) out += " `" + std::string(rule) + "` ->";
  return out + " `" + std::string(rules.front()) + '`';
}

}  // namespace cursiv
