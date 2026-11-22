// How a tree is written, and the only writer of a `Cst`: the run an engine
// appends its records to, the repairs it notes, and the calls that end a
// pass. Leaf and walk reads index the tree once, so they come after it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/CstRun.hpp"
#include "cursiv/syntax-tree/OwnedNodes.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/text/TextOffset.hpp"

namespace cursiv {

struct Grammar;

class CstBuilder {
 public:
  explicit CstBuilder(Cst& cst) : _cst(cst) {}

  // A repair, appended by the attempt that survived. `Cst::recovered()` reads
  // this list.
  void noteRepair(DiagnosticKind kind, ElementId element, TextOffset begin,
                   TextOffset end, std::vector<ElementId> also = {},
                   bool mixedIndent = false) {
    // Two identical repairs in a row are one fault: nested constructs closing
    // at one point expect the same closer.
    if (!_cst._diagnostics.empty()) {
      const ParseDiagnostic& last = _cst._diagnostics.back();
      if (last.kind == kind && last.element == element && last.begin == begin &&
          last.end == end && last.also == also &&
          last.mixedIndent == mixedIndent)
        return;
    }
    _cst._diagnostics.push_back(
        {kind, mixedIndent, element, std::move(also), begin, end, {}});
  }

  // Where an engine writes its records, and where every reader reads them:
  // one run, indexed by node id.
  CstRun& stream() { return _cst._stream; }
  const CstRun& stream() const { return _cst._stream; }
  void reserveRecords(std::size_t records, std::size_t later = 0) {
    _cst._stream.reserveRecords(records, later);
  }

  // Ends a pass: `root` is the record of the entry, none where the text gave
  // no tree, and `accepted` whether the whole text was read.
  void finish(NodeId root, bool accepted) noexcept {
    if (root != kNoNode && root < _cst.size())
      _cst.bindEntry(root);
    else
      _cst.bindEntry(kNoNode);
    _cst.bindAccepted(accepted);
  }
  void bindModel(const Grammar& grammar, OwnedNodes owned) noexcept {
    _cst.bindModel(grammar, owned);
  }
  void bindTranslation(std::string_view document,
                        std::unique_ptr<SourceTranslation> translation) {
    _cst.bindTranslation(document, std::move(translation));
  }

 private:
  Cst& _cst;
};

}  // namespace cursiv
