#pragma once

#include <cstdint>

// What an element is, as a closed vocabulary: the kind `GrammarModel`
// answers with for each position of a declared grammar.

namespace cursiv {

enum class ElementKind : std::uint8_t {
  // Rules. A rule call is not a kind of its own: it resolves to the rule
  // it names -- an infix operator included, which is a literal or a call to
  // the terminal or data rule that reads it.
  parser_rule,
  data_type_rule,
  terminal_rule,
  infix_rule,
  // Leaves of the text.
  literal,
  character_range,
  any_character,
  // Directives: what the match does to the AST.
  assignment,
  create,
  nest,
  // Structure.
  group,
  ordered_choice,
  unordered_group,
  repetition,
  and_predicate,
  not_predicate,
  // cursiv's own three: a choice proven order-free, a bounded scan, and a
  // local skip configuration.
  unordered_choice,
  through,
  skip_region,
  // The two reserved rows every table ends with.
  trivia,
  error,
};

}  // namespace cursiv
