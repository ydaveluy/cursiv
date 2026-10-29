// The one refusal a lowering records against a rule, never aborts on: a code,
// the rule that earned it and, where one part of the rule earned it, that
// part. The walk carries on writing a well-formed value.
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
  nullableUnorderedItem,
  ruleWithoutProduction,
  twoEndMarks,
  optionalEndMark,
  optionalCondition,
  unitProductionCycle,
  hiddenLeftRecursion,
  leftRecursion,
  precedenceClash,
  infixOperatorNotText,
  directiveOnNoPosition,
  tooManyPrecedences,
  skippedTerminal,
  docConflict,
  postfixBinaryClash,
};

// Set once: a later refusal is a consequence, not a fault.
struct GrammarFault {
  GrammarFaultCode code = GrammarFaultCode::none;
  std::uint16_t rule = kNoRow;
  // The bound the refusal names, where `gated` says an engine's gate or the
  // bound of a check hid the answer.
  std::uint16_t count = 0;
  bool gated = false;
  // The part of the rule the refusal names, as the dump spells it.
  std::string part{};

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
                                        "nullableUnorderedItem",
                                        "ruleWithoutProduction",
                                        "twoEndMarks",
                                        "optionalEndMark",
                                        "optionalCondition",
                                        "unitProductionCycle",
                                        "hiddenLeftRecursion",
                                        "leftRecursion",
                                        "precedenceClash",
                                        "infixOperatorNotText",
                                        "directiveOnNoPosition",
                                        "tooManyPrecedences",
                                        "skippedTerminal",
                                        "docConflict",
                                        "postfixBinaryClash"};
  static_assert(std::size(names) ==
                static_cast<std::size_t>(GrammarFaultCode::postfixBinaryClash) +
                    1);
  return names[static_cast<std::size_t>(c)];
}

// Two operators of a table that may share a text, one of them spelled past
// what the comparison lists.
inline std::string unlisted_operators(const GrammarFault& f) {
  return "has two operators whose texts cannot be compared, " + f.part +
         " of its precedence table: one of them is written with a "
         "repetition, an `option` or a region, or has more than " +
         std::to_string(f.count) +
         " spellings, so both may match the same text -- write each operator "
         "with literals, terminals and data rules of fixed text";
}

// What the author is told: what the engine cannot hold and what to write
// instead, never which pass noticed.
inline std::string grammar_fault_text(const GrammarFault& f) {
  switch (f.code) {
    case GrammarFaultCode::nullableRepetition:
      return "repeats an item that may match nothing, so the same text could "
             "be read with any number of empty turns -- make the item consume "
             "input and use the minimum of the repetition for what may be "
             "absent: write `many(x)` for `many(option(x))`, and "
             "`repeat<0, 3>(x)` for `repeat<2, 3>(option(x))`";
    case GrammarFaultCode::nullableUnorderedItem:
      // `gated`: the group spells every order in full, and so reads each
      // item.
      if (f.gated)
        return "has an optional item, `" + f.part +
               "`, in an unordered group whose items hold a `nest`: in such a "
               "group every item must consume input -- make the item consume, "
               "or write the group with and without it: `unordered(a, b) | a`";
      return "has an unordered item that may match nothing, `" + f.part +
             "`: an item that matches nothing could stand anywhere in the "
             "order -- to make an item optional, write `option(x)`, "
             "`many(x)` or `repeat<0, n>(x)` where `x` consumes input";
    case GrammarFaultCode::ruleWithoutProduction:
      return "can never match any text: each of its alternatives is "
             "impossible, such as one that needs a token after `eof` or a "
             "recursion that never ends -- give the rule an alternative that "
             "can match";
    case GrammarFaultCode::twoEndMarks:
      return "asserts end of input twice in one choice: the second adds "
             "nothing -- remove it";
    case GrammarFaultCode::optionalEndMark:
      return "makes an end-of-input assertion optional: `option(eof)` matches "
             "with or without the end of input, so it tests nothing -- write "
             "`eof` where the input must end, or a choice with an `eof` "
             "branch";
    case GrammarFaultCode::optionalCondition:
      return "makes a condition optional: an optional condition always holds, "
             "so it tests nothing -- write the condition where it must hold, "
             "or make optional the part it guards";
    case GrammarFaultCode::unitProductionCycle:
      return "is part of a cycle of rules that call each other without "
             "matching any text (`A -> B` and `B -> A`, or `A -> A B` where "
             "`B` may match nothing): the same text could be read through "
             "the cycle any number of times -- make the cycle match some text, "
             "or remove the call that closes it";
    case GrammarFaultCode::hiddenLeftRecursion:
      if (f.gated)
        return "reaches itself before matching anything, behind more than " +
               std::to_string(f.count) +
               " optional items: before the first token, the parser cannot "
               "know how many of them to skip -- keep " +
               std::to_string(f.count) +
               " optional items or fewer before the recursive call, put some "
               "behind an item that consumes input, or put the call first";
      return "reaches itself before matching anything, behind an item that "
             "may match nothing (`A -> B A x` where `B` may be empty): before "
             "the first token, the parser cannot know how many empty `B`s to "
             "read -- make that item consume input, or move it after the "
             "recursive call";
    case GrammarFaultCode::leftRecursion:
      if (f.gated)
        return "reaches itself before matching anything inside a repetition "
               "or behind optional items, beyond what the LL parser supports: "
               "at most " +
               std::to_string(f.count) +
               " optional items before the call, in a rule of limited size -- "
               "write the alternatives out, and put the optional items before "
               "the call behind one that consumes input: `X? Y? A z` becomes "
               "`(X Y? | Y) A z | A z` (see refusals.md, leftRecursion)";
      return "reaches itself before matching anything, in a way the LL parser "
             "cannot handle -- write a chain of binary operators with "
             "`infix<>`; otherwise give one rule of the cycle an alternative "
             "that only calls the next rule, put first the alternative where "
             "a rule calls itself directly, or make every alternative match a "
             "token first (see refusals.md, leftRecursion)";
    case GrammarFaultCode::precedenceClash:
      if (f.gated) return unlisted_operators(f);
      return "names the same operator text on two levels of a precedence "
             "table, or with two groupings: the next tokens cannot tell the "
             "two operators apart, so the table cannot say which binds "
             "tighter -- give each operator text one level and one grouping "
             "in the table";
    case GrammarFaultCode::postfixBinaryClash:
      if (f.gated) return unlisted_operators(f);
      return "uses " + f.part +
             " of its precedence table: after an operand, the parser cannot "
             "tell which of the two it is -- give the postfix and the binary "
             "operator different texts";
    case GrammarFaultCode::infixOperatorNotText:
      return "names as an operator a rule that builds a node: an operator "
             "gives a value, not a node -- use a literal, a terminal or a data "
             "rule, with a converter if the operator member is not text";
    case GrammarFaultCode::directiveOnNoPosition:
      return "writes a member from `eof` alone, or from nothing: such a "
             "directive matches no text, so the member would never be "
             "written -- put the directive on a part that consumes input, or "
             "on a choice with an `eof` branch";
    case GrammarFaultCode::tooManyPrecedences:
      return "declares more than 255 precedence levels -- use at most 255";
    case GrammarFaultCode::skippedTerminal:
      return "names a terminal that is skipped as trivia at that point, so it "
             "can never match there -- remove it, or read it inside `tight(e)` "
             "or a `skip()` that does not skip it";
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
