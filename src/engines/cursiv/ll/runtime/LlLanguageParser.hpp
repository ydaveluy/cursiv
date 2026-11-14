// The type-erased parser of one language for the predictive engine: a generated
// table is its only template argument, the rest is data.
#pragma once

#include <memory>
#include <string_view>
#include <utility>

#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/grammar/GrammarMetaData.hpp"
#include "cursiv/grammar/NextFeature.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstBuilder.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/ll/runtime/LlContext.hpp"
#include "cursiv/ll/runtime/LlPredictionSession.hpp"
#include "cursiv/ll/runtime/LlPredictionWork.hpp"
#include "cursiv/ll/runtime/ll_caret.hpp"
#include "cursiv/ll/runtime/ll_repair.hpp"
#include "cursiv/shared/runtime/AstActions.hpp"
#include "cursiv/shared/runtime/AstReducer.hpp"
#include "cursiv/shared/runtime/LexSuffixSession.hpp"
#include "cursiv/shared/runtime/StampedGrammar.hpp"

namespace cursiv::engine {

// The document ends where the entry rule does, and what stands between the
// last token and the end is trivia the tree keeps.
template <const auto& T>
inline void ll_finish(LlContext& c) {
  ll_span_region_end(c);
  // Text no rule wanted still goes on the tape, as a deletion the diagnostics
  // report.
  ll_tail<T>(c);
  ll_note_run<T>(c);
  if (c.search) LlSearchWork::current().work += c.search->work;
  ll_flush_trivia<T>(c);
  // Always accepted: every refusal was repaired where it occurred, and the
  // diagnostics report the repairs. A reader checks those, not this flag.
  c.tape.bind_accepted(true);
}

template <const auto& T>
class LlLanguageParser final : public Parser {
 public:
  LlLanguageParser(const GrammarMetaData& grammar, const AstActions& actions,
                   void (*drive)(LlContext&), const RepairBudget& budget = {})
      : _grammar(std::make_shared<StampedGrammar>(grammar, T.stamp)),
        _actions(&actions),
        _drive(drive),
        _budget(budget) {}

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
    throw_if_cancelled(cancelToken);
    const ThreadStop threadStop(cancelToken);
    LlPredictionSession<T> predictionSession;
    LexSuffixSession<LlLexTape<T>> predicateSession(
        text.data(), static_cast<TextOffset>(text.size()));
    Cst cst(text);
    CstBuilder builder(cst);
    builder.reserve_stream(text.size());
    // The descent polls nothing: the stream reads the stop as it fills.
    CstRun::Watch watch(builder.stream(), cancelToken);
    builder.bind_stream();
    LlContext c(text, builder, _budget);
    c.stop = CancellationPoll(cancelToken);
    if constexpr (kTermsRecorded<T>) c.pending.reserve(8);
    _drive(c);
    ll_finish<T>(c);
    // What the predictions read goes to the thread's count, as the searches'.
    const LlPredictionWork predicted = predictionSession.work();
    LlPredictionWork::current().tokens += predicted.tokens;
    LlPredictionWork::current().planVisits += predicted.planVisits;
    builder.bind_entry(0, static_cast<NodeId>(cst.size()), c.last);
    // Converting the tree is another phase; answer a stop requested after
    // the descent's last periodic poll before materialising the AST.
    throw_if_cancelled(cancelToken);
    ParseResult out = AstReducer::build<T.stamp>(*_actions, std::move(cst),
                                                 cancelToken, _grammar.get());
    out.bindGrammar(_grammar);
    return out;
  }

  void nextFeaturesAt(
      const Cst& cst, TextOffset at, TextOffset resume,
      std::vector<NextFeature>& out,
      const CancellationToken& cancelToken = kNoCancel) const override {
    LexSuffixSession<LlLexTape<T>> predicateSession(
        cst.text().data(), static_cast<TextOffset>(cst.text().size()));
    ll_next_features_at<T>(*_grammar, cst, at, resume, out, cancelToken);
  }

 private:
  std::shared_ptr<const StampedGrammar> _grammar;
  const AstActions* _actions;
  void (*_drive)(LlContext&);
  RepairBudget _budget;
};

}  // namespace cursiv::engine
