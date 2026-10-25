// The erased parser of one language, read by the table engine: a generated
// table as its only template argument, the rest handed over as data.
// Nothing here names a grammar type, so a module is the whole door onto a
// language.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/grammar/GrammarMetaData.hpp"
#include "cursiv/core/grammar/NextFeature.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/core/utils/Cancellation.hpp"
#include "cursiv/engines/lr/runtime/LrCaretTable.hpp"
#include "cursiv/engines/lr/runtime/LrDrive.hpp"
#include "cursiv/engines/lr/runtime/LrParser.hpp"
#include "cursiv/engines/lr/runtime/lr_matcher.hpp"
#include "cursiv/engines/lr/runtime/lr_read.hpp"
#include "cursiv/engines/lr/runtime/lr_states.hpp"
#include "cursiv/engines/shared/runtime/AstActions.hpp"
#include "cursiv/engines/shared/runtime/AstReducer.hpp"
#include "cursiv/engines/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/engines/shared/runtime/StampedGrammar.hpp"

namespace cursiv {

template <const auto& A>
class LrLanguageParser final : public Parser {
 public:
  LrLanguageParser(const GrammarMetaData& grammar, const AstActions& actions,
                   const LrDrive& drive, const RepairBudget& budget = {})
      : _grammar(std::make_shared<StampedGrammar>(grammar, A.stamp)),
        _actions(&actions),
        _drive(&drive),
        _budget(budget),
        _sampleReads(drive.condText == nullptr
                         ? nullptr
                         : std::make_unique<std::atomic<std::uint32_t>[]>(
                               kCaret.modeTight.size() * kCaret.terms.size())),
        _caret(caret_of(drive, _sampleReads.get())) {}

  [[nodiscard]] const Grammar& getGrammar() const noexcept override {
    return *_grammar;
  }

  // Reentrant, and it must be: a rename reparses synthetic text while its
  // caller still holds a result of this same parser.
  [[nodiscard]] ParseResult parse(
      std::string_view text,
      const CancellationToken& cancelToken = kNoCancel) const override {
    Cst cst;
    lr_parser<A>.parse(text, cst, nullptr, _drive, _budget, cancelToken);
    ParseResult out =
        AstReducer::build<A.stamp>(*_actions, std::move(cst), cancelToken);
    out.bind_grammar(_grammar);
    return out;
  }

  void nextFeaturesAt(const Cst& cst, TextOffset at, TextOffset resume,
                      std::vector<NextFeature>& out) const override {
    LexSuffixSession<LrLexTape<A>> predicateSession(
        cst.text().data(), static_cast<TextOffset>(cst.text().size()));
    lr_next_features_at(_caret, *_grammar, cst, at, resume, out);
  }

 private:
  // The caret's half of this table, borrowed once, so the answer is one
  // compiled function; the sweep is the module's and joins at construction.
  static constinit const LrCaretTable kCaret;
  static LrCaretTable caret_of(
      const LrDrive& drive, std::atomic<std::uint32_t>* sampleReads) noexcept {
    LrCaretTable t = kCaret;
    t.sampleReads = sampleReads;
    t.scan = drive.scanText;
    t.cond = drive.condText;
    t.condReach = drive.condReach;
    t.condSeen = drive.condSeen;
    return t;
  }

  std::shared_ptr<const StampedGrammar> _grammar;
  const AstActions* _actions;
  const LrDrive* _drive;
  RepairBudget _budget;
  std::unique_ptr<std::atomic<std::uint32_t>[]> _sampleReads;
  const LrCaretTable _caret;
};

template <const auto& A>
constinit const LrCaretTable LrLanguageParser<A>::kCaret{
    .lex = &lr_match_halt<A>,
    .maxCands = A.maxCands,
    .chooses = A.nconflicts != 0 || A.npeeks != 0 || A.multiCand,
    .stamp = A.stamp,
    .expectBase = A.derived.expect.base,
    .expectItems = A.derived.expect.items,
    .expectPool = A.derived.expect.pool,
    .shiftTerm = A.derived.expect.shiftTerm,
    .mode = A.mode,
    .modeMulti = A.modeMulti,
    .modeTight = A.modeTight,
    .skipSet = A.derived.skipSet,
    .errMode = A.errMode,
    .run =
        +[](std::uint16_t state, SymbolId term) {
          LrCaretTable::Run r;
          lr_action_run<A>(state, term, r.first, r.count);
          return r;
        },
    .action = &lr_shadow_action<A>,
    .follow =
        +[](std::uint16_t state) {
          return std::span<const std::uint64_t>(A.cold.follow[state].w);
        },
    .soleReduce = A.cold.soleReduce,
    .rowLhsBase = A.derived.rowLhsBase,
    .rowLhs = A.derived.rowLhs,
    .go = &lr_goto_of<A>,
    .guardAt = A.guardAt,
    .guardOps = A.guardOps,
    .held = A.held,
    .terms = A.cfg.terms,
    .prods = A.cfg.prods,
};

}  // namespace cursiv
