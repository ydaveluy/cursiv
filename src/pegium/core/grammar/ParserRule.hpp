#pragma once

/// Grammar contract for rules producing AST nodes.

#include <concepts>
#include <pegium/core/grammar/AbstractRule.hpp>
#include <type_traits>

namespace pegium {
class AstNode;
class CstNodeView;
}

namespace pegium::parser {
struct ParseContext;
struct TrackedParseContext;
struct RecoveryContext;
struct ExpectContext;
struct AstReflectionInitContext;
struct ValueBuildContext;
}

namespace pegium::grammar {

struct ParserRule : AbstractRule {
  constexpr ParserRule() noexcept : AbstractRule(ElementKind::ParserRule) {}
  virtual AstNode *
  getValue(const CstNodeView &, const parser::ValueBuildContext &) const = 0;
  virtual bool rule(parser::ParseContext &) const = 0;
  virtual bool rule(parser::TrackedParseContext &) const = 0;
  virtual bool recover(parser::RecoveryContext &) const = 0;
  virtual bool expect(parser::ExpectContext &) const = 0;
  virtual void init(parser::AstReflectionInitContext &) const = 0;
  virtual const AbstractElement *getElement() const noexcept = 0;

  void print(std::ostream &os) const override;
};
template <typename T>
concept IsParserRule = std::derived_from<std::remove_cvref_t<T>, ParserRule>;

} // namespace pegium::grammar
