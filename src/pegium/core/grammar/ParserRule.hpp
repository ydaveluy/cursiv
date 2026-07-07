#pragma once

/// Grammar contract for rules producing AST nodes.

#include <array>
#include <atomic>
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
  /// Copies leave the word-char-table cache empty (the atomic flag makes the
  /// members non-copyable by default): a copied rule is a new grammar node,
  /// and its readers fall back to deriving until its own bootstrap runs.
  constexpr ParserRule(const ParserRule &other) noexcept
      : AbstractRule(other) {}
  constexpr ParserRule &operator=(const ParserRule &other) noexcept {
    AbstractRule::operator=(other);
    return *this;
  }
  virtual AstNode *
  getValue(const CstNodeView &, const parser::ValueBuildContext &) const = 0;
  virtual bool rule(parser::ParseContext &) const = 0;
  virtual bool rule(parser::TrackedParseContext &) const = 0;
  virtual bool recover(parser::RecoveryContext &) const = 0;
  virtual bool expect(parser::ExpectContext &) const = 0;
  virtual void init(parser::AstReflectionInitContext &) const = 0;
  virtual const AbstractElement *getElement() const noexcept = 0;

  void print(std::ostream &os) const override;

  /// Grammar-derived word-character table of the grammar rooted at this
  /// entry rule, cached by the reflection bootstrap
  /// (`bootstrapAstReflection`) BEFORE the language becomes discoverable, so
  /// concurrent parses only ever read it. Null until then; readers fall back
  /// to deriving per recovery search (a pure read-only grammar walk). The
  /// flag is release/acquire so a reader that observes it set also observes
  /// the table bytes; the first initialization wins and later calls (e.g. a
  /// language re-registered while parses are in flight) leave the published
  /// table untouched instead of racing its readers.
  [[nodiscard]] const std::array<bool, 256> *
  recoveryWordCharTable() const noexcept {
    return _recoveryWordCharTableInitialized.load(std::memory_order_acquire)
               ? &_recoveryWordCharTable
               : nullptr;
  }
  void initializeRecoveryWordCharTable(
      const std::array<bool, 256> &table) const noexcept {
    if (_recoveryWordCharTableInitialized.load(std::memory_order_acquire)) {
      return;
    }
    _recoveryWordCharTable = table;
    _recoveryWordCharTableInitialized.store(true, std::memory_order_release);
  }

private:
  mutable std::array<bool, 256> _recoveryWordCharTable{};
  mutable std::atomic<bool> _recoveryWordCharTableInitialized = false;
};
template <typename T>
concept IsParserRule = std::derived_from<std::remove_cvref_t<T>, ParserRule>;

} // namespace pegium::grammar
