// The type-erased parser of one language for the table engine: a generated
// table is its only template argument, the rest is data.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/GrammarMetaData.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/text/TextOffset.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/lr/runtime/LrCaretTable.hpp"
#include "cursiv/lr/runtime/LrDrive.hpp"
#include "cursiv/lr/runtime/LrParser.hpp"
#include "cursiv/lr/runtime/lr_matcher.hpp"
#include "cursiv/lr/runtime/lr_read.hpp"
#include "cursiv/lr/runtime/lr_states.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstReducer.hpp"
#include "cursiv/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/shared/runtime/StampedGrammar.hpp"

namespace cursiv::engine {

template <const auto& A>
class LrLanguageParser final : public Parser {
 public:
  LrLanguageParser(const GrammarMetaData& grammar, const AstActions& actions,
                   const LrDrive& drive, const RepairBudget& budget = {})
      : _grammar(std::make_shared<StampedGrammar>(grammar, A.stamp)),
        _actions(&actions),
        _drive(&drive),
        _budget(budget),
        _caret(caret_of(drive,
                        drive.condText == nullptr ? nullptr : &_sampleReads)) {}

  [[nodiscard]] const Grammar& getGrammar() const noexcept override {
    return *_grammar;
  }

  // A temporary that owns its text is refused here as through `Parser`.
  using Parser::parse;
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

  void nextFeaturesAt(
      const Cst& cst, TextOffset at, TextOffset resume,
      std::vector<NextFeature>& out,
      const CancellationToken& cancelToken = kNoCancel) const override {
    LexSuffixSession<LrLexTape<A>> predicateSession(
        cst.text().data(), static_cast<TextOffset>(cst.text().size()));
    lr_next_features_at(_caret, *_grammar, cst, at, resume, out, cancelToken);
  }

 private:
  // The caret's half of this table, borrowed once, so the answer is one
  // compiled function; the sweep is the module's and joins at construction.
  static constinit const LrCaretTable kCaret;
  // The walk without a flagged winner, bound only where a mode flags one.
  static constexpr auto without_of() {
    decltype(LrCaretTable::lexWithout) f = nullptr;
    if constexpr (A.has_flags()) f = &lr_match_without<A>;
    return f;
  }
  static LrCaretTable caret_of(const LrDrive& drive,
                               const LrSampleReads* sampleReads) noexcept {
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
  LrSampleReads _sampleReads;
  const LrCaretTable _caret;
};

template <const auto& A>
constinit const LrCaretTable LrLanguageParser<A>::kCaret{
    .lex = &lr_match_halt<A>,
    .flagged =
        A.has_flags()
            ? +[](std::uint16_t mode,
                  SymbolId
                      sym) { return sym < A.cfg.nterms && A.flagged(mode, sym); }
            : nullptr,
    .lexWithout = without_of(),
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
    .skipStop = A.derived.skipStop,
    .errMode = A.errMode,
    .errModeOf = &lr_err_mode<A>,
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
    .names = A.cfg.names.data(),
    .prods = A.cfg.prods,
    .reductions = A.derived.reductions,
    .preds = A.derived.preds,
};

}  // namespace cursiv::engine
