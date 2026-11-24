// The steps of a language's states over its tables. What a state does with
// a token is a cell of its row, and where a reduction leads a cell of its
// gotos; the lexers, the reductions and the tests are the language's own code.
#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "cursiv/engine/parser/Explorer.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/engine/parser/Repairer.hpp"

// The steps hand the token on by value: no address of theirs outlives a
// tail call, which GCC cannot tell unoptimised.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-musttail-local-addr"
#endif

namespace cursiv::engine {

// A step over machine `M`: what a run does at `p` with the token in hand,
// going on by a tail call. `at` is the cell an action takes or the record a
// reduction left; a step that enters the state on top reads none.
template <class M>
using State = CURSIV_PRESERVE_NONE Exit(M&, const char* p, NodeCount at, const char* tokenEnd,
                                        std::uint64_t tokenHead);

// What a lone child's element becomes under the record closed around it.
using Fuse = std::uint32_t (*)(std::uint32_t);

// The steps over the tables of language `L`. Its first `kAsks` steps, in
// order, are `newline`, `dedent_at_end` and `tie`.
template <class L>
struct Drive {
  // The cell row `row` acts on `terminal` with.
  CURSIV_ALWAYS_INLINE static std::uint32_t cell(std::uint32_t row, std::uint16_t terminal) {
    const ActionRow& r = L::rows[row];
    const std::uint32_t at = r.base + terminal;
    return L::cells[at].terminal == terminal ? at : r.otherwise;
  }
  // The operands of a cell past its `value`: its element or count, then its
  // gap or what it reads again; or both as one.
  CURSIV_ALWAYS_INLINE static std::uint16_t element(std::uint32_t at) {
    return static_cast<std::uint16_t>(L::operands[at]);
  }
  CURSIV_ALWAYS_INLINE static std::uint16_t extra(std::uint32_t at) {
    return static_cast<std::uint16_t>(L::operands[at] >> 16);
  }

  // The ends of a run, entered as the steps are.
  template <class M>
  [[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE static Exit accepted(
      M& m, const char*, NodeCount, const char*, std::uint64_t) {
    m.accept();
    return Exit::accepted;
  }
  template <class M>
  [[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE static Exit refuse(
      M& m, const char*, NodeCount, const char* tokenEnd, std::uint64_t tokenHead) {
    return m.refused({tokenEnd, tokenHead});
  }
  // Room made for a step's writes, the hidden trivia written first with
  // `Flush`; then the step is taken again.
  template <class M, bool Flush, auto Step>
  [[gnu::cold]] CURSIV_NOINLINE CURSIV_PRESERVE_NONE static Exit room_then(
      M& m, const char* p, NodeCount at, const char* tokenEnd, std::uint64_t tokenHead) {
    if constexpr (Flush) m.settle();
    m.make_room();
    CURSIV_MUSTTAIL return Step(m, p, at, tokenEnd, tokenHead);
  }

  // The token at `p` as `mode` reads it: by the reader that keeps what it
  // reads over the machine, by the mode's own over one that steps.
  template <class M>
  CURSIV_ALWAYS_INLINE static Token lexed(M& m, const char* p, std::uint16_t mode) {
    if constexpr (M::kSteps) return m.lexing().t.modes[mode].read(m.lexing(), p, mode);
    else return L::keep(m.lexing(), p, mode);
  }

  // The state on top entered with no token: it acts on the token its mode
  // reads, or on a line's indentation before the line's first token.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit read(M& m, const char* p, NodeCount, const char* tokenEnd,
                                        std::uint64_t tokenHead) {
    // The token is read into the registers it is handed on in.
    Token tok{tokenEnd, tokenHead};
    const std::uint32_t state = m.under();
    bool indented = false;
    if constexpr (L::indents) {
      if (m.indented.depth == 0 && m.lexing().line_start(p)) [[unlikely]] {
        const Token made = m.lexing().indentation(p, m.indented);
        indented = made.end != nullptr;
        if (indented) tok = made;
      }
    }
    if (!indented) tok = lexed(m, p, m.lexing().t.stateMode[state]);
    const std::uint32_t at = cell(state, tok.terminal());
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[at].step](m, p, at, tok.end, tok.head);
  }

  // State `state` acts on the token in hand.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit held(M& m, const char* p, NodeCount state,
                                        const char* tokenEnd, std::uint64_t tokenHead) {
    const std::uint32_t at = cell(state, Token{tokenEnd, tokenHead}.terminal());
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[at].step](m, p, at, tokenEnd, tokenHead);
  }

  // The token in hand shifted as the cell's element into the cell's state,
  // which `Next` enters once the `Popped` frames of its reduction are popped.
  // `Gapped` reads the cell's gap; `To` and `Element` stand for the cell's.
  template <class M, auto Next, std::uint32_t Popped, bool Gapped, Nesting Nest,
            std::uint32_t To = kNoStack, std::uint32_t Element = kNoStack>
  CURSIV_ALWAYS_INLINE CURSIV_PRESERVE_NONE static Exit shift(M& m, const char* p, NodeCount at,
                                                              const char* tokenEnd,
                                                              std::uint64_t tokenHead) {
    const Token tok{tokenEnd, tokenHead};
    if constexpr (Gapped) {
      const std::uint16_t gap = extra(at);
      if (gap != kAnyGap && p != m.lexing().src + tok.begin(m.lexing()) &&
          (gap == kNoGap || m.lexing().t.modes[gap].read(m.lexing(), p, gap).begin(m.lexing()) !=
                                tok.begin(m.lexing())))
        CURSIV_MUSTTAIL return refuse<M>(m, p, at, tokenEnd, tokenHead);
    }
    if (!m.roomy(Popped > 1 ? M::kSteps : true, true) || (L::hides && !m.flushed())) [[unlikely]]
      CURSIV_MUSTTAIL return room_then<M, L::hides, &retake<M>>(m, p, at, tokenEnd, tokenHead);
    const std::uint32_t state = To == kNoStack ? L::cells[at].value : To;
    const std::uint32_t element = Element == kNoStack ? Drive::element(at) : Element;
    NodeCount open = at;
    if constexpr (M::kSteps) m.shift(state, element, tok);
    else if constexpr (Popped != 0) open = m.template shift<Popped, L::matching>(state, element, tok);
    else m.template shift<0, L::matching>(state, element, tok);
    if constexpr (Nest == Nesting::indent) m.lexing().indent(m.indented, tok);
    if constexpr (Nest == Nesting::dedent) m.lexing().dedent(m.indented);
    if constexpr (Nest == Nesting::open) ++m.indented.depth;
    if constexpr (Nest == Nesting::close) {
      if (m.indented.depth != 0) --m.indented.depth;
    }
    if constexpr (M::kSteps)
      return m.shifted();
    else if constexpr (std::is_null_pointer_v<decltype(Next)>)
      CURSIV_MUSTTAIL return L::template steps<M>[L::enters[state]](m, tok.end, at, tok.end, tok.head);
    else
      CURSIV_MUSTTAIL return Next(m, tok.end, open, tok.end, tok.head);
  }

  // A shift into one state that several cells take, each with its own
  // element: written once.
  template <class M, auto Next, std::uint32_t Popped, bool Gapped, Nesting Nest, std::uint32_t To>
  CURSIV_NOINLINE CURSIV_PRESERVE_NONE static Exit shared(M& m, const char* p, NodeCount at,
                                                          const char* tokenEnd,
                                                          std::uint64_t tokenHead) {
    CURSIV_MUSTTAIL return shift<M, Next, Popped, Gapped, Nest, To>(m, p, at, tokenEnd, tokenHead);
  }

  // The step of cell `at`, taken again.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit retake(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                          std::uint64_t tokenHead) {
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[at].step](m, p, at, tokenEnd, tokenHead);
  }

  // A row's reduction on the tokens it has no cell for: `Next` where the
  // state acts on the token in hand.
  template <class M, auto Next>
  CURSIV_PRESERVE_NONE static Exit followed(M& m, const char* p, NodeCount at,
                                            const char* tokenEnd, std::uint64_t tokenHead) {
    const std::uint16_t terminal = Token{tokenEnd, tokenHead}.terminal();
    const Tables& t = m.lexing().t;
    if (terminal < t.terminalCount && t.inSet(t.followOf[m.under()], terminal))
      CURSIV_MUSTTAIL return Next(m, p, at, tokenEnd, tokenHead);
    CURSIV_MUSTTAIL return refuse<M>(m, p, at, tokenEnd, tokenHead);
  }

  // A token the state has several actions for: the run stops, and goes on
  // from the arm chosen.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit split(M& m, const char*, NodeCount at, const char* tokenEnd,
                                         std::uint64_t tokenHead) {
    return m.split({tokenEnd, tokenHead}, L::cells[at].value, element(at), extra(at));
  }

  // Between brackets, the end of a line written in the text is a blank: the
  // state reads the next token.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit newline(M& m, const char* p, NodeCount at,
                                           const char* tokenEnd, std::uint64_t tokenHead) {
    const Token tok{tokenEnd, tokenHead};
    if (m.indented.depth != 0 && tok.end != m.lexing().src + tok.begin(m.lexing())) [[unlikely]] {
      CURSIV_MUSTTAIL return L::template steps<M>[L::enters[m.under()]](m, tok.end, at, tokenEnd,
                                                                        tokenHead);
    }
    const std::uint32_t next = L::cells[at].value;
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tokenEnd, tokenHead);
  }

  // The end of the text closes the levels still open: the row the cell's
  // operands name acts on a dedent.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit dedent_at_end(M& m, const char* p, NodeCount at,
                                                 const char* tokenEnd, std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    std::uint32_t next = L::cells[at].value;
    if (m.indented.level != 0) {
      tok = token(tok.begin(m.lexing()), tok.end, L::dedent);
      next = cell(L::operands[at], L::dedent);
    }
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tok.end, tok.head);
  }

  // Whether the token in hand is one several terminals of the state read, as
  // `check` tells it.
  template <class M>
  CURSIV_ALWAYS_INLINE static bool ties(M& m, Token tok, const TieCheck& check) {
    switch (check.kind) {
      case TieCheck::row:
        return m.lexing().tied(tok, check.test);
      case TieCheck::alike:
        if ((tok.head & kTold) != 0) return false;
        for (std::uint32_t k = check.test; k != check.test + check.n; ++k) {
          const std::uint16_t word = m.lexing().t.tiedWords[k];
          if (m.lexing().reads_as(tok, m.lexing().t.tied[word].solo, word)) return true;
        }
        return false;
      case TieCheck::grouped:
        return tok.hasGroup();
      case TieCheck::ranked:
        return tok.hasGroup() && m.lexing().rank(tok) == check.test;
    }
    return false;
  }

  // A token several terminals of the state read: the run stops, and goes on
  // from an action of any.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit tie(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                       std::uint64_t tokenHead) {
    const Token tok{tokenEnd, tokenHead};
    const TieCheck& check = L::ties[L::operands[at]];
    if (ties(m, tok, check)) return m.split(tok, check.first, check.count, check.rereads);
    const std::uint32_t next = L::cells[at].value;
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tokenEnd, tokenHead);
  }

  // A token another lexer read where this state reads a trivia's text: the
  // text is read again here. A token with no width is never read again.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit pass(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                        std::uint64_t tokenHead) {
    const Token tok{tokenEnd, tokenHead};
    if (tok.end != m.lexing().src + tok.begin(m.lexing())) {
      m.forget(p);
      CURSIV_MUSTTAIL return L::template steps<M>[L::enters[m.under()]](m, p, at, tokenEnd, tokenHead);
    }
    const std::uint32_t next = L::cells[at].value;
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tokenEnd, tokenHead);
  }

  // The text of the token in hand read again from `p` in `mode`, and row
  // `row` acting on the token read.
  template <class M>
  CURSIV_ALWAYS_INLINE static std::uint32_t read_again(M& m, const char* p, std::uint16_t mode,
                                                       std::uint32_t row, Token& tok) {
    m.forget(p);
    tok = lexed(m, p, mode);
    return cell(row, tok.terminal());
  }

  // An arm that reads the token's text again: in the mode its first operand
  // names, for row `value`.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit reread(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                          std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    if (tok.end == m.lexing().src + tok.begin(m.lexing()))
      CURSIV_MUSTTAIL return refuse<M>(m, p, at, tokenEnd, tokenHead);
    const std::uint32_t next = read_again(m, p, element(at), L::cells[at].value, tok);
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tok.end, tok.head);
  }

  // An arm of a tie that keeps the token as the lexer read it: row `value`
  // acts on it, past its cells that ask of the token before they act.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit retry_tie(M& m, const char* p, NodeCount at,
                                             const char* tokenEnd, std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    tok = m.lexing().as_tied(tok, tok.terminal(), (tok.head & kLiteralBranch) != 0);
    std::uint32_t next = cell(L::cells[at].value, tok.terminal());
    while (L::cells[next].step < kAsks) next = L::cells[next].value;
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tok.end, tok.head);
  }

  // An arm of tied terminals: the token read as the cell's terminal where
  // its text reads so, then cell `value` acts.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit tied_arm(M& m, const char* p, NodeCount at,
                                            const char* tokenEnd, std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    const ActionCell c = L::cells[at];
    if (!m.lexing().takes_tied(tok, c.terminal)) return m.refused(tok);
    tok = m.lexing().as_tied(tok, c.terminal, m.lexing().t.tied[c.terminal].mixed);
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[c.value].step](m, p, c.value, tok.end,
                                                                        tok.head);
  }

  // An arm of a terminal the fallback lexer left out: the text read again in
  // the terminal's own mode, then cell `value` acts.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit rival_arm(M& m, const char* p, NodeCount at,
                                             const char* tokenEnd, std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    const ActionCell c = L::cells[at];
    const std::uint16_t solo = m.lexing().t.tied[c.terminal].solo;
    tok = m.lexing().t.modes[solo].read(m.lexing(), m.lexing().src + tok.begin(m.lexing()), solo);
    if (tok.terminal() != c.terminal) return m.refused({tokenEnd, tokenHead});
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[c.value].step](m, p, c.value, tok.end,
                                                                        tok.head);
  }

  // Where the tests of a reduction refuse the token in hand, the state on
  // top reads its text again without the terminals it reads only for such
  // reductions; elsewhere, or for a token with no width, it is refused.
  template <class M>
  CURSIV_PRESERVE_NONE static Exit again(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                         std::uint64_t tokenHead) {
    Token tok{tokenEnd, tokenHead};
    if (tok.end != m.lexing().src + tok.begin(m.lexing())) {
      const std::uint32_t state = m.under();
      const ReadAgain* const end = L::agains + L::againCount;
      const ReadAgain* const reads = std::lower_bound(
          L::agains, end, state, [](const ReadAgain& r, std::uint32_t s) { return r.state < s; });
      const std::uint16_t* const words = m.lexing().t.tiedWords;
      if (reads != end && reads->state == state &&
          std::find(words + reads->tested, words + reads->tested + reads->n,
                    tok.terminal()) != words + reads->tested + reads->n) {
        const std::uint32_t next = read_again(m, p, reads->mode, reads->row, tok);
        CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tok.end,
                                                                         tok.head);
      }
    }
    CURSIV_MUSTTAIL return refuse<M>(m, p, at, tokenEnd, tokenHead);
  }

  // A production reduced over a machine that steps, as `reductions` tells it
  // for the step of cell `at`, or with no token for the step that enters the
  // state on top; the step is written for its `Form` and its tests `Holds`.
  template <class M, bool Held, unsigned Form, auto Holds = nullptr>
  CURSIV_PRESERVE_NONE static Exit reduce(M& m, const char* p, NodeCount at, const char* tokenEnd,
                                          std::uint64_t tokenHead) {
    constexpr bool nothing = (Form & kReadsNothing) != 0;
    const Token tok = Held ? Token{tokenEnd, tokenHead} : Token{nullptr, 0};
    const Reduction& r = L::reductions[Held ? L::cells[at].step : L::enters[m.under()]];
    if constexpr (nothing && std::is_same_v<M, Explorer>)
      if (m.options.closure && m.visited(r.production, p, tok)) return Exit::joined;
    if constexpr (!std::is_null_pointer_v<decltype(Holds)>)
      if (!Holds(m, p, tok)) CURSIV_MUSTTAIL return again<M>(m, p, at, tokenEnd, tokenHead);
    if (!m.roomy(nothing, (r.shapes & (kRecordEmpty | kRecordSeveral)) != 0)) [[unlikely]]
      CURSIV_MUSTTAIL return room_then<M, false, &reduce<M, Held, Form, Holds>>(m, p, at, tokenEnd,
                                                                                 tokenHead);
    NodeCount open = nothing ? m.here(r.production) : m.pop(r.size, r.production);
    const GotoCell to =
        (Form & kOneSite) != 0 ? r.to : L::gotos[L::gotoBase[m.under()] + r.lhs];
    // Its record as the call site below reads it, if any.
    const std::uint32_t record =
        L::records[r.records + (r.bySite ? to.site & ~GotoCell::kRereads : 0)];
    if (static_cast<ElementId>(record) != kNoElement)
      open = close(m, open, r.shapes, static_cast<ElementId>(record),
                   nothing && Held ? m.lexing().src + tok.begin(m.lexing()) : p,
                   L::fuses[record >> 16]);
    // A token its tests name, or one the state reached reads again, goes
    // back to the text with the trivia before it; a repaired one does not.
    if (!Held || ((tok.head & kReplaced) == 0 &&
                  ((Form & kGivesBack) != 0 ||
                   ((to.site & GotoCell::kRereads) != 0 && tok.terminal() != kEndOfInput)))) {
      m.push(to.target, open);
      if constexpr (Held) m.forget(p);
      CURSIV_MUSTTAIL return L::template steps<M>[to.enter](m, p, open, tok.end, tok.head);
    }
    m.push(to.target, open, L::splits ? tok : Token{nullptr, 0});
    const std::uint32_t next =
        (Form & kThenReduces) != 0 ? r.next : cell(to.target, tok.terminal());
    CURSIV_MUSTTAIL return L::template steps<M>[L::cells[next].step](m, p, next, tok.end, tok.head);
  }
  // The record a reduction closes with `element` over what it wrote since
  // `open`, by the shapes of what that can be.
  template <class M>
  CURSIV_ALWAYS_INLINE static NodeCount close(M& m, NodeCount open, unsigned shapes,
                                              std::uint32_t element, const char* at, Fuse fuse) {
    switch (shapes) {
      case 1: return m.template close<1>(open, element, at, fuse);
      case 2: return m.template close<2>(open, element, at, fuse);
      case 3: return m.template close<3>(open, element, at, fuse);
      case 4: return m.template close<4>(open, element, at, fuse);
      case 5: return m.template close<5>(open, element, at, fuse);
      case 6: return m.template close<6>(open, element, at, fuse);
      case 7: return m.template close<7>(open, element, at, fuse);
      default: return m.template close<0>(open, element, at, fuse);
    }
  }

  // A run goes on from where the machine stopped: an arm, a state with the
  // token in hand, or a state that reads its token.
  template <class M>
  CURSIV_ALWAYS_INLINE static Exit run(M& m) {
    const std::uint32_t resume = m.resume;
    const Token tok = m.tok;
    if constexpr (L::splits) {
      if (resume >= 2 * L::states) {
        const std::uint32_t at = L::arms + (resume - 2 * L::states);
        return L::template steps<M>[L::cells[at].step](m, m.at, at, tok.end, tok.head);
      }
    }
    const std::uint32_t state = resume >> 1;
    if ((resume & 1) != 0) return held<M>(m, m.at, state, tok.end, tok.head);
    return L::template steps<M>[L::enters[state]](m, m.at, 0, tok.end, tok.head);
  }
};

}  // namespace cursiv::engine

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
