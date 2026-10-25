#pragma once

#include <lsp/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/lsp/formatting/FormattingAction.hpp"

// The engine: rules in, minimal edits out. It knows the CST and the
// document, never a grammar, so it compiles once for the tree. The CST is a
// postorder tape with no parent link; document order comes from descending
// the forest's roots through `reverse_children`.

namespace cursiv {

// The rules of one request, keyed by (record, side). A later rule of equal or
// higher priority replaces an earlier one on the same boundary, so a general
// rule declared first is refined by a specific one declared after it.
class FormattingActions final : public FormattingCollector {
 public:
  // Two slots per record, so a rule is found by indexing rather than by
  // hashing: the walk asks about every record it visits.
  explicit FormattingActions(NodeCount records)
      : _at(std::size_t{records} * 2, kNoAction) {}

  void collect(NodeId node, FormattingMode mode,
               const FormattingAction& action) override;
  void replaceHidden(NodeId node, std::string text) override;

  [[nodiscard]] const FormattingAction* find(NodeId node,
                                             FormattingMode mode) const;
  [[nodiscard]] const std::string* replacement(NodeId node) const;

 private:
  static constexpr std::uint32_t kNoAction = 0xFFFFFFFFU;

  [[nodiscard]] static constexpr std::size_t key(NodeId node,
                                                 FormattingMode mode) {
    return (std::size_t{node} << 1U) |
           static_cast<std::size_t>(mode == FormattingMode::Append);
  }

  std::vector<std::uint32_t> _at;
  std::vector<FormattingAction> _pool;
  // Sparse, and a comment is rare beside a record: this one stays a map.
  std::unordered_map<NodeId, std::string> _replacements;
};

// The whitespace prefix for `indentation` units under `options`. Shared with
// `HiddenNodeFormatter::indentation`, so an author's margin and the engine's
// agree by construction.
[[nodiscard]] std::string indentation_text(
    std::int32_t indentation, const lsp::FormattingOptions& options);

// Runs the walk and materialises the edits. `visitHidden` runs on every hidden
// record before its edits are computed, with the indentation in force and the
// newline style, so an `onHidden` callback may register a rule or a
// replacement.
[[nodiscard]] std::vector<lsp::TextEdit> run_formatting(
    const Document& document, const Cst& cst,
    const lsp::FormattingOptions& options,
    const std::optional<lsp::Range>& range, FormattingActions& actions,
    cursiv::function_ref<void(NodeId, std::int32_t, std::string_view)>
        visitHidden,
    const CancellationToken& token);

}  // namespace cursiv
