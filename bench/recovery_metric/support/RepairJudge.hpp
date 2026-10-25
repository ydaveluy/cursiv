// One faulted document weighed against its twin: which faults a diagnostic
// answers one to one, how much of the author's text is still read in place,
// how much of the twin's tree survives where no fault touched it, and how
// many diagnostics were said. It reads only what any engine's parse leaves
// behind -- records, spans, the two flags, the diagnostics and the AST.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstLeaf.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"
#include "recovery_metric/support/AstOverlap.hpp"
#include "recovery_metric/support/FaultPairing.hpp"
#include "recovery_metric/support/FaultWindow.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairTwin.hpp"
#include "recovery_metric/support/RepairVerdict.hpp"
#include "recovery_metric/support/TextKept.hpp"

namespace cursiv {

struct RepairJudge {
  // Null: the tree is not weighed.
  const AstReflection* reflection = nullptr;
  const fuzzer::GrammarActions* actions = nullptr;

  [[nodiscard]] RepairVerdict judge(const RepairTwin& twin, const RepairCase& c,
                                    const ParseResult& got) const {
    RepairVerdict v;
    const Cst& cst = got.cst();
    std::vector<ParseDiagnostic> said;
    for (const ParseDiagnostic& d : cst.diagnostics())
      if (d.isSyntax()) said.push_back(d);
    const std::vector<FaultWindow> windows =
        FaultWindow::of(twin.tokens, twin.hidden, c);
    const FaultPairing p = FaultPairing::pair(windows, said);
    v.faults = static_cast<std::uint32_t>(c.faults.size());
    v.diagnostics = static_cast<std::uint32_t>(said.size());
    v.found = p.found;
    v.missed = p.missed;
    v.parasites = p.parasites;
    v.accepted = cst.accepted();
    v.silent = v.accepted && said.empty();
    v.tree = got.value != nullptr;

    std::vector<CstLeaf> leaves;
    cst.visit_leaves([&](const CstLeaf& l) { leaves.push_back(l); });
    const Cst& twinCst = twin.result.cst();
    const Grammar* grammar = twinCst.grammar();
    const auto restores = [&](std::size_t token, ElementId owed) {
      if (grammar == nullptr || owed == kNoElement) return false;
      const std::string_view text = grammar->terminalText(owed);
      if (!text.empty()) return text == twin.spelling(token);
      return !twin.literal[token] &&
             grammar->terminalName(owed) ==
                 grammar->recordRule(twinCst[twin.tokens[token].id]);
    };
    v.text = TextKept::of(twin.tokens, c, windows, leaves, said, restores);

    if (reflection != nullptr && twin.result.value != nullptr) {
      const auto touched = [&](const AstNode& n) {
        if (n.cstNodeId == kNoNode || n.cstNodeId >= twinCst.size()) return true;
        const CstNode& s = twinCst[n.cstNodeId];
        for (const FaultWindow& w : windows)
          if (w.from < w.to ? s.begin < w.to && w.from < s.end
                            : s.begin < w.from && w.from < s.end)
            return true;
        return false;
      };
      const AstOverlap overlap{*reflection, actions};
      v.ast = overlap.run(*twin.result.value, got.value, touched);
      // Silent, nothing repaired, and the twin's own tree: the fault did not
      // reach the language, so it is no fault to answer.
      v.noop = v.silent && got.value != nullptr &&
               std::none_of(leaves.begin(), leaves.end(),
                            [](const CstLeaf& l) { return l.recovered; }) &&
               overlap.same(*twin.result.value, *got.value);
    }
    return v;
  }
};

}  // namespace cursiv
