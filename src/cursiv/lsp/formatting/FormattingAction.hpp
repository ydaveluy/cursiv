#pragma once

#include <lsp/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

// The vocabulary a rule is written in: where (a selection of CST records)
// and what (a whitespace instruction), never what the text becomes. Nothing
// here knows the grammar, so this header compiles once for every language;
// the grammar-dependent half lives in `AbstractFormatter.hpp`.

namespace cursiv {

enum class FormattingMode : std::uint8_t { Prepend, Append };

struct FormattingActionOptions {
  // Higher priority wins when two rules target the same gap.
  std::int32_t priority = 0;
  // Keep at least what is already there.
  bool allowMore = false;
  // Keep at most what is already there.
  bool allowLess = false;
};

// One low-level whitespace instruction: `characters` is inline spacing in
// spaces, `lines` is a line-break count followed by the current
// indentation, `tabs` is an indentation delta in indentation units. Authors
// write `oneSpace`/`newLine`/`indent`, never this directly.
struct FormattingMove {
  std::optional<std::int32_t> characters = std::nullopt;
  std::optional<std::int32_t> tabs = std::nullopt;
  std::optional<std::int32_t> lines = std::nullopt;
};

// A rule attached to one whitespace boundary. Several candidate moves make a
// `fit(...)`: the engine picks the one closest to the shape already written.
struct FormattingAction {
  FormattingActionOptions options;
  std::vector<FormattingMove> moves;
};

class FormattingCollector {
 public:
  virtual ~FormattingCollector() noexcept = default;
  virtual void collect(NodeId node, FormattingMode mode,
                       const FormattingAction& action) = 0;
  // The full replacement text of one hidden record -- a reformatted comment.
  virtual void replaceHidden(NodeId node, std::string text) = 0;
};

// A selection of CST records whitespace rules attach to, obtained from
// `NodeFormatter`. A plain value: two named regions -- an opening and a closing
// brace -- are two variables, not two types.
class FormattingRegion {
 public:
  FormattingRegion() = default;
  FormattingRegion(const Cst* cst, std::vector<NodeId> nodes,
                   FormattingCollector* collector)
      : _cst(cst), _nodes(std::move(nodes)), _collector(collector) {}

  [[nodiscard]] bool empty() const noexcept { return _nodes.empty(); }
  // The selected records, in document order.
  [[nodiscard]] std::span<const NodeId> nodes() const noexcept {
    return _nodes;
  }
  [[nodiscard]] const Cst* cst() const noexcept { return _cst; }

  // The whitespace before each selected record.
  FormattingRegion& prepend(const FormattingAction& action);
  // The whitespace after each selected record.
  FormattingRegion& append(const FormattingAction& action);
  FormattingRegion& surround(const FormattingAction& action);

  // A sub-selection. Negative indices count from the end.
  [[nodiscard]] FormattingRegion slice(std::ptrdiff_t start) const;
  [[nodiscard]] FormattingRegion slice(std::ptrdiff_t start,
                                       std::ptrdiff_t end) const;

 private:
  [[nodiscard]] std::size_t normalizeIndex(std::ptrdiff_t index) const noexcept;

  const Cst* _cst = nullptr;
  std::vector<NodeId> _nodes;
  FormattingCollector* _collector = nullptr;
};

// The formatter of one hidden record -- a comment. Hidden records are
// CST-only: no AST node is built for them, so they are reached by the
// terminal rule that produced them, not a node callback. `ruleName()` is
// the name the author wrote in the grammar -- no second registry.
class HiddenNodeFormatter {
 public:
  HiddenNodeFormatter(const Cst& cst, NodeId node, std::int32_t baseIndentation,
                      const lsp::FormattingOptions& options,
                      std::string_view newlineStyle,
                      FormattingCollector& collector) noexcept
      : _cst(&cst),
        _node(node),
        _baseIndentation(baseIndentation),
        _options(&options),
        _newlineStyle(newlineStyle),
        _collector(&collector) {}

  [[nodiscard]] NodeId node() const noexcept { return _node; }
  [[nodiscard]] std::string_view ruleName() const noexcept {
    const Grammar* const grammar = _cst->grammar();
    return grammar != nullptr ? grammar->recordRule((*_cst)[_node])
                              : std::string_view{};
  }
  [[nodiscard]] std::string_view text() const noexcept {
    return _cst->text(_node);
  }
  // The document's line ending, not this record's: a rewritten comment that
  // held none of its own would otherwise mix styles into the file.
  [[nodiscard]] std::string_view newlineStyle() const noexcept {
    return _newlineStyle;
  }
  // The indentation this record sits at, in indentation units.
  [[nodiscard]] std::int32_t baseIndentation() const noexcept {
    return _baseIndentation;
  }
  // The whitespace prefix for `baseIndentation() + delta`.
  [[nodiscard]] std::string indentation(std::int32_t delta = 0) const;

  // Replace the record's text wholesale. The engine re-indents the replacement
  // to the record's own column, so a rewriter writes the comment and not its
  // margin.
  void replace(std::string text) const;

  HiddenNodeFormatter& prepend(const FormattingAction& action);
  HiddenNodeFormatter& append(const FormattingAction& action);
  HiddenNodeFormatter& surround(const FormattingAction& action);

 private:
  const Cst* _cst;
  NodeId _node;
  std::int32_t _baseIndentation;
  const lsp::FormattingOptions* _options;
  std::string_view _newlineStyle;
  FormattingCollector* _collector;
};

}  // namespace cursiv
