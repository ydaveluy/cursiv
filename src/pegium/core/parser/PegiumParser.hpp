#pragma once

#include <pegium/core/parser/AndPredicate.hpp>
#include <pegium/core/parser/AnyCharacter.hpp>
#include <pegium/core/parser/Assignment.hpp>
#include <pegium/core/parser/CharacterRange.hpp>
#include <pegium/core/parser/Create.hpp>
#include <pegium/core/parser/DataTypeRule.hpp>
#include <pegium/core/parser/ExpectContext.hpp>
#include <pegium/core/parser/Group.hpp>
#include <pegium/core/parser/GroupExpect.hpp>
#include <pegium/core/parser/GroupRecovery.hpp>
#include <pegium/core/parser/Parser.hpp>
#include <pegium/core/parser/InfixRule.hpp>
#include <pegium/core/parser/InfixRuleExpect.hpp>
#include <pegium/core/parser/InfixRuleRecovery.hpp>
#include <pegium/core/parser/Literal.hpp>
#include <pegium/core/parser/LiteralRecovery.hpp>
#include <pegium/core/parser/Nest.hpp>
#include <pegium/core/parser/NotPredicate.hpp>
#include <pegium/core/parser/OrderedChoice.hpp>
#include <pegium/core/parser/OrderedChoiceExpect.hpp>
#include <pegium/core/parser/OrderedChoiceRecovery.hpp>
#include <pegium/core/parser/ParserRule.hpp>
#include <pegium/core/parser/Repetition.hpp>
#include <pegium/core/parser/RepetitionExpect.hpp>
#include <pegium/core/parser/RepetitionRecovery.hpp>
#include <pegium/core/parser/SkipperBuilder.hpp>
#include <pegium/core/parser/TerminalRule.hpp>
#include <pegium/core/parser/TerminalRuleRecovery.hpp>
#include <pegium/core/parser/UnorderedGroup.hpp>
#include <pegium/core/services/DefaultCoreService.hpp>
#include <pegium/core/syntax-tree/AstNode.hpp>
#include <pegium/core/workspace/Document.hpp>
#include <type_traits>

namespace pegium::parser {

/// any character equivalent to regex `.`
static constexpr AnyCharacter dot;

/// Create a CharacterRange
template <utils::char_array_builder builder> consteval auto operator""_cr() {
  static_assert(!builder.value.empty(), "A CharacterRange cannot be empty.");

  if constexpr (builder.value[0] == '^') {
    constexpr auto without_caret = utils::array_substr<1>(builder.value);
    return !CharacterRange<without_caret>{} + dot;
  } else {
    return CharacterRange<builder.value>{};
  }
}

/// Create a Keyword
template <utils::char_array_builder builder> consteval auto operator""_kw() {
  static_assert(!builder.value.empty(), "A keyword cannot be empty.");
  return Literal<builder.value>{};
}

/// The end of file
static constexpr auto eof = !dot;
/// The end of line
static constexpr auto eol = "\n"_kw | "\r\n"_kw | "\r"_kw;
/// a space character equivalent to regex `\s`
static constexpr auto s = " \t\r\n\f\v"_cr;
/// a non space character equivalent to regex `\S`
static constexpr auto S = !s + dot;
/// a word character equivalent to regex `\w`
static constexpr auto w = "a-zA-Z0-9_"_cr;
/// a non word character equivalent to regex `\W`
static constexpr auto W = !w + dot;
/// a digit character equivalent to regex `\d`
static constexpr auto d = "0-9"_cr;
/// a non-digit character equivalent to regex `\D`
static constexpr auto D = !d + dot;

/// An until operation that starts from element `from` and ends to element
/// `to`. e.g `"from"_kw <=> "to"_kw`.
/// This operation is non-greedy, it will stop as soon as it finds the `to`
/// element.
/// @param from the starting element
/// @param to the ending element
/// @return the until element
template <Expression StartExpr, Expression EndExpr>
constexpr auto operator<=>(StartExpr &&startExpr, EndExpr &&endExpr) {
  return std::forward<StartExpr>(startExpr) +
         many(!std::forward<EndExpr>(endExpr) + dot) +
         std::forward<EndExpr>(endExpr);
}
} // namespace pegium::parser

namespace pegium::parser {
struct ParseContext;
struct RecoveryContext;
}

namespace pegium {
struct CoreServices;
}

namespace pegium::parser {

class PegiumParser : public Parser, protected pegium::DefaultCoreService {
public:
  PegiumParser() noexcept;
  explicit PegiumParser(const pegium::CoreServices &services) noexcept
      : pegium::DefaultCoreService(services) {}
  ~PegiumParser() noexcept override = default;

  PegiumParser(const PegiumParser &) = delete;
  PegiumParser &operator=(const PegiumParser &) = delete;
  PegiumParser(PegiumParser &&) = delete;
  PegiumParser &operator=(PegiumParser &&) = delete;

  using Parser::parse;

  [[nodiscard]] ParseResult
  parse(text::TextSnapshot,
        const utils::CancellationToken & = {}) const override;
  [[nodiscard]] ExpectResult expect(
      std::string_view text, TextOffset offset,
      const utils::CancellationToken &cancelToken = {}) const override;

private:
  template <typename ValueType, bool Nullable> struct RuleTypeSelector {
    static_assert(sizeof(ValueType) == 0, "Unsupported type for Rule");
  };

  template <typename ValueType, bool Nullable>
    requires(!std::derived_from<ValueType, AstNode>)
  struct RuleTypeSelector<ValueType, Nullable> {
    using type = DataTypeRule<ValueType, Nullable>;
  };

  // Pegium currently builds parser-produced AST nodes as mutable shells and
  // populates them afterwards through assignments. Until standard reflection
  // can simplify hierarchy discovery generically, AST-producing rules and
  // reflection bootstrap intentionally share the same default-constructible
  // constraint.
  template <typename ValueType, bool Nullable>
    requires DefaultConstructibleAstNode<ValueType>
  struct RuleTypeSelector<ValueType, Nullable> {
    using type = ParserRule<ValueType, Nullable>;
  };

protected:
  virtual const Skipper &getSkipper() const noexcept { return NoOpSkipper(); }
  virtual ParseOptions getParseOptions() const noexcept { return {}; }

  template <typename ValueType = std::string_view>
  using Terminal = TerminalRule<ValueType>;
  // Default rule: body must be non-nullable. Use `NullableRule<T>` when the
  // body is nullable (e.g. an entry rule of the form `many(...)`).
  template <typename ValueType = std::string>
  using Rule = typename RuleTypeSelector<ValueType, false>::type;
  template <typename ValueType = std::string>
  using NullableRule = typename RuleTypeSelector<ValueType, true>::type;
  // Infix AST nodes are created through the same shell-and-assign model as
  // regular parser rules, so they intentionally share the same constraint.
  template <typename T, auto Left, auto Op, auto Right>
    requires DefaultConstructibleAstNode<T>
  using Infix = InfixRule<T, Left, Op, Right>;
};

} // namespace pegium::parser
