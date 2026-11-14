// Walks the declarative grammar and writes a document's tokens and the AST its
// directives build, together: bytes from `GrammarLexicon`, nodes from
// `GrammarActions`, recursion closed by a cost-guided budget.
#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/grammar/ElementId.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/DecisionSource.hpp"
#include "cursiv/ElementKind.hpp"
#include "cursiv/GrammarActions.hpp"
#include "cursiv/GrammarCost.hpp"
#include "cursiv/GrammarDerivation.hpp"
#include "cursiv/GrammarGuide.hpp"
#include "cursiv/GrammarLexicon.hpp"
#include "cursiv/GrammarModel.hpp"

namespace cursiv::fuzzer::detail {

inline constexpr unsigned kGrammarDepthCap = 64;
inline constexpr unsigned kGrammarRepCap =
    4;                                        // extra rounds a repetition takes
inline constexpr unsigned kGrammarOpCap = 3;  // operators a climb may chain
inline constexpr unsigned kSpellTries = 8;  // spellings tried before giving up

struct GrammarGenerator {
  const GrammarModel& g;
  const GrammarLexicon& lex;
  const GrammarActions& act;
  const GrammarCost& cost;
  unsigned tokenBudget = 24;
  const GrammarGuide* guide = nullptr;

  // `ar` owns the tree and the bytes its value members view: a member borrowing
  // the derivation's own text would dangle the moment it moves.
  [[nodiscard]] GrammarDerivation generate(DecisionSource& src,
                                           WalkTree& ar) const {
    Walk w{&src, &ar, {}, 0};
    bool built = false;
    const ElementId entry = g.entryRule();
    // An entry that is a climb, which `walk` never sees: the model gives it no
    // children for `rule_node` to walk.
    w.d.tree = g.kindOf(entry) == ElementKind::infix_rule
                   ? infix_chain(entry, w, built)
                   : rule_node(entry, w, built);
    w.d.complete = w.ok && build_text(w.d);
    return std::move(w.d);
  }

 private:
  struct Walk {
    DecisionSource* src;
    WalkTree* ar;
    GrammarDerivation d;
    unsigned depth = 0;
    bool ok = true;
    // The region that ignores nothing being written, and where it started:
    // every token of it but the first admits no separator in front.
    unsigned bare = 0;
    std::size_t bareFrom = 0;
    // The last token included the trivia beside it, so the next one takes none.
    bool fed = false;
    // The end of input was asserted: no token may follow.
    bool ended = false;
    // The value rule the assignment being walked descended into: its converter
    // is the payload's, and a token of its own names another element.
    ElementId valueRule = kNoElement;
    // Whether the assignment being walked reached its member, saved and
    // restored per assignment.
    bool written = false;
  };

  // What one rule's body collected: the node it built, and the node a bare
  // call handed it to pass up.
  struct Frame {
    ElementId rule = kNoElement;
    AstNode* node = nullptr;
    AstNode* adopted = nullptr;
  };

  [[nodiscard]] bool cheap(const Walk& w) const {
    return w.d.tokens.size() >= tokenBudget || w.src->exhausted();
  }

  void refuse(ElementId e, const AstNode* on, Walk& w) const {
    w.d.unwritten.push_back(AstUnwritten{on, std::string(g.featureOf(e))});
  }

  // What one rule builds: the node its directives created, the node its single
  // child handed up, or nothing at all.
  [[nodiscard]] AstNode* rule_node(ElementId rule, Walk& w, bool& built) const {
    built = false;
    if (++w.depth > kGrammarDepthCap) {
      ++w.d.capHits;
      w.ok = false;
      return nullptr;
    }
    // A rule that carries its own creation makes its node here: the model
    // fuses a rule's label and its `create` into one element when no
    // alternative builds a head of its own.
    Frame f{rule, act.make(rule, *w.ar), nullptr};
    for (const ElementId c : g.childrenOf(rule)) walk(c, kNoElement, f, w);
    --w.depth;
    AstNode* const out = f.node != nullptr ? f.node : f.adopted;
    built = out != nullptr;
    return out;
  }

  void walk(ElementId e, ElementId assign, Frame& f, Walk& w) const {
    if (!w.ok) return;
    const std::span<const ElementId> children = g.childrenOf(e);
    switch (g.kindOf(e)) {
      case ElementKind::literal:
      case ElementKind::terminal_rule: {
        Veto after;
        std::string text =
            g.kindOf(e) == ElementKind::literal
                ? lex.spell(e, SpellStyle::shortest, *w.src, after)
                : terminal_value(e, after, w);
        emit(e, std::move(text), after, w);
        return;
      }
      case ElementKind::infix_rule:
      case ElementKind::parser_rule:
      case ElementKind::data_type_rule: {
        const bool climb = g.kindOf(e) == ElementKind::infix_rule;
        if (climb ? act.primary(e) == kNoElement : children.empty()) break;
        bool built = false;
        AstNode* const sub =
            climb ? infix_chain(e, w, built) : rule_node(e, w, built);
        if (!w.ok) return;
        if (!built) {
          if (assign != kNoElement && w.valueRule == kNoElement)
            w.valueRule = e;
          return;
        }
        if (assign != kNoElement) {
          AstNode* const to = f.node;
          if (to == nullptr ||
              act.writeNode(assign, *to, sub) == Write::NotModelled)
            refuse(assign, to, w);
          else
            w.written = true;
        } else if (f.adopted == nullptr) {
          f.adopted = sub;
        }
        return;
      }
      case ElementKind::assignment:
        assign_member(e, f, w);
        return;
      case ElementKind::create:
        f.node = act.make(e, *w.ar);
        for (const ElementId c : children) walk(c, assign, f, w);
        return;
      case ElementKind::nest: {
        // The node built so far is the one this wraps; a bare call left it in
        // `adopted`.
        AstNode* const inner = f.node != nullptr ? f.node : f.adopted;
        f.adopted = nullptr;
        f.node = act.make(e, *w.ar);
        if (f.node == nullptr || inner == nullptr ||
            act.writeNode(e, *f.node, inner) == Write::NotModelled)
          refuse(e, f.node, w);
        for (const ElementId c : children) walk(c, assign, f, w);
        return;
      }
      case ElementKind::ordered_choice:
      case ElementKind::unordered_choice: {
        if (children.empty()) return;
        walk(children[pick(children, w)], assign, f, w);
        return;
      }
      case ElementKind::repetition:
        repeat(e, assign, f, w);
        return;
      case ElementKind::and_predicate:
      case ElementKind::not_predicate:
        // An assertion spells nothing. The walk imposes only the end of
        // input; any other is left unwritten below.
        if (g.kindOf(e) == ElementKind::not_predicate && children.empty()) {
          w.ended = true;
          return;
        }
        break;
      case ElementKind::skip_region: {
        const unsigned wasBare = w.bare;
        const std::size_t wasFrom = w.bareFrom;
        if (lex.ignores_nothing(e)) {
          ++w.bare;
          w.bareFrom = w.d.tokens.size();
        }
        for (const ElementId c : children) walk(c, assign, f, w);
        w.bare = wasBare;
        w.bareFrom = wasFrom;
        return;
      }
      case ElementKind::group:
      case ElementKind::unordered_group:
      case ElementKind::through:
        for (const ElementId c : children) walk(c, assign, f, w);
        return;
      default:
        break;
    }
    // What the walk cannot write: a character range in the syntax, whose
    // separators it cannot place, and an assertion it cannot impose.
    ++w.d.unrendered;
    w.ok = false;
  }

  // A precedence table, written flat and folded after. The chain is
  // `primary (op primary)*`; the tree it must build is what climbing the steps
  // builds, the same resolution the table decided at construction.
  [[nodiscard]] AstNode* infix_chain(ElementId e, Walk& w, bool& built) const {
    built = false;
    if (++w.depth > kGrammarDepthCap) {
      ++w.d.capHits;
      w.ok = false;
      return nullptr;
    }
    const ElementId prim = act.primary(e);
    const std::span<const InfixOp> ops = act.operators(e);
    std::vector<AstNode*> operands;
    std::vector<InfixOp> chain;
    std::vector<std::string> texts;
    bool whole = false;
    operands.push_back(rule_node(prim, w, whole));

    unsigned n = 0;
    if (w.ok && !cheap(w) && !ops.empty())
      n = w.src->choose(DecisionKind::byte, kGrammarOpCap + 1);
    for (unsigned i = 0; i < n && w.ok; ++i) {
      const InfixOp& o = ops[w.src->choose(DecisionKind::entry,
                                           static_cast<unsigned>(ops.size()))];
      const std::size_t before = w.d.tokens.size();
      Frame none;
      walk(o.element, kNoElement, none, w);
      if (!w.ok) break;
      if (w.d.tokens.size() == before) {
        ++w.d.unrendered;
        w.ok = false;
        break;
      }
      // An operator of several tokens is a data rule: its parts joined by what
      // each denotes, as a member's value is.
      std::string text = w.d.tokens[before].text;
      if (w.d.tokens.size() > before + 1) {
        text.clear();
        for (std::size_t i = before; i < w.d.tokens.size(); ++i) {
          const auto part = act.denote(w.d.tokens[i].elem, w.d.tokens[i].text);
          if (!part) {
            w.ok = false;  // an operator with no value: nothing to predict
            break;
          }
          text += *part;
        }
        if (!w.ok) break;
      }
      texts.push_back(std::move(text));
      bool one = false;
      operands.push_back(rule_node(prim, w, one));
      whole = whole && one;
      chain.push_back(o);
    }
    --w.depth;
    if (!w.ok) return nullptr;
    if (chain.empty()) {
      built = operands.front() != nullptr;
      return operands.front();
    }
    // A chain whose operands build no node folds nothing; the assignment above
    // names the member, which is the only place the tree can lose one.
    if (!whole) return nullptr;
    std::size_t at = 0;
    AstNode* const root = climb(e, operands, chain, texts, at, 0, w);
    built = root != nullptr;
    return root;
  }

  [[nodiscard]] AstNode* climb(ElementId e,
                               const std::vector<AstNode*>& operands,
                               const std::vector<InfixOp>& chain,
                               const std::vector<std::string>& texts,
                               std::size_t& at, unsigned floor, Walk& w) const {
    AstNode* lhs = operands[at];
    while (at < chain.size() && chain[at].prec >= floor) {
      const InfixOp o = chain[at];
      const std::string op = texts[at];
      ++at;
      AstNode* const rhs = climb(e, operands, chain, texts, at,
                                 o.right ? o.prec : o.prec + 1u, w);
      AstNode* const node = act.make(e, *w.ar);
      if (node == nullptr || lhs == nullptr || rhs == nullptr ||
          act.fold(e, o.element, *node, lhs, w.ar->stash(op), rhs) ==
              Write::NotModelled) {
        refuse(e, node, w);
        return nullptr;
      }
      lhs = node;
    }
    return lhs;
  }

  // A member is written from one node or one token. A value of several tokens,
  // which a data rule joins, is only named.
  void assign_member(ElementId e, Frame& f, Walk& w) const {
    AstNode* const to = f.node;
    const std::size_t first = w.d.tokens.size();
    const bool outer = w.written;
    const ElementId outerRule = w.valueRule;
    w.written = false;
    w.valueRule = kNoElement;
    for (const ElementId c : g.childrenOf(e)) walk(c, e, f, w);
    const bool written = w.written;
    const ElementId rule = w.valueRule;
    w.written = outer;
    w.valueRule = outerRule;
    if (!w.ok || written) return;
    const std::size_t last = w.d.tokens.size();
    if (to == nullptr || last == first ||
        (last > first + 1 && rule == kNoElement)) {
      refuse(e, to, w);
      return;
    }
    // A terminal hands over its raw text, and its own converter runs once,
    // where the member is written. A value rule hands over its parts
    // joined by what each denotes -- a text the document never spelled.
    std::string value;
    if (rule == kNoElement) {
      value = w.d.tokens[first].text;
    } else {
      for (std::size_t i = first; i < last; ++i) {
        const auto part = act.denote(w.d.tokens[i].elem, w.d.tokens[i].text);
        if (!part) return;  // a converter refused: the engine writes nothing
        value += *part;
      }
    }
    const ElementId from = rule != kNoElement ? rule : w.d.tokens[first].elem;
    if (act.writeText(e, from, *to, w.ar->stash(value)) == Write::NotModelled)
      refuse(e, to, w);
  }

  // The alternative: the cheapest once the budget is spent, so recursion
  // closes. One that spells nothing comes last: its assertion is unchecked.
  [[nodiscard]] unsigned pick(std::span<const ElementId> children,
                              Walk& w) const {
    if (guide != nullptr) {
      const int k = guide->prefer(children, w.d.tokens.size());
      if (k >= 0) return static_cast<unsigned>(k);
    }
    unsigned spelling = 0;
    for (const ElementId c : children) spelling += cost[c] != 0 ? 1u : 0u;
    if (spelling == 0) spelling = static_cast<unsigned>(children.size());
    const bool all = spelling == children.size();
    if (!cheap(w)) {
      unsigned k = w.src->choose(DecisionKind::entry, spelling);
      for (unsigned i = 0; i < children.size(); ++i)
        if (all || cost[children[i]] != 0) {
          if (k-- == 0) return i;
        }
      return 0;
    }
    unsigned best = static_cast<unsigned>(children.size());
    for (unsigned i = 0; i < children.size(); ++i) {
      if (!all && cost[children[i]] == 0) continue;
      if (best == children.size() || cost[children[i]] < cost[children[best]])
        best = i;
    }
    return best == children.size() ? 0u : best;
  }

  void repeat(ElementId e, ElementId assign, Frame& f, Walk& w) const {
    const std::span<const ElementId> children = g.childrenOf(e);
    if (children.empty()) return;
    const unsigned lo = g.minOf(e);
    const unsigned hi = g.maxOf(e);
    const unsigned extra = hi > lo ? std::min(hi - lo, kGrammarRepCap) : 0u;
    // A later round begins on the separator a bracketed repetition carries.
    const ElementId head = children.size() > 1 ? children[1] : children[0];
    const bool steered =
        guide != nullptr && guide->another(head, w.d.tokens.size()) >= 0;
    unsigned n = lo;
    if (!steered && !cheap(w) && extra != 0)
      n = lo + w.src->choose(DecisionKind::byte, extra + 1);
    for (unsigned i = 0; i < n && w.ok; ++i) round(children, i, assign, f, w);
    while (steered && w.ok && n < lo + extra &&
           guide->another(head, w.d.tokens.size()) > 0)
      round(children, n++, assign, f, w);
  }

  void round(std::span<const ElementId> children, unsigned i, ElementId assign,
             Frame& f, Walk& w) const {
    if (i != 0 && children.size() > 1) walk(children[1], assign, f, w);
    walk(children[0], assign, f, w);
  }

  void emit(ElementId e, std::string text, const Veto& after, Walk& w) const {
    if (text.empty() || w.ended) {
      ++w.d.unrendered;
      w.ok = false;
      return;
    }
    const bool readsSeparator = lex.reads_separator(e);
    const bool attached = (w.bare != 0 && w.d.tokens.size() > w.bareFrom) ||
                          readsSeparator || w.fed;
    w.fed = readsSeparator;
    w.d.tokens.push_back(
        GrammarToken{e, std::move(text), {}, attached, 0, 0, after});
  }

  // The value of a named terminal, from its own expression. A spelling the
  // grammar reserves as a literal is refused: that word is the literal's.
  [[nodiscard]] std::string terminal_value(ElementId e, Veto& after,
                                           Walk& w) const {
    for (unsigned t = 0; t < kSpellTries; ++t) {
      const auto style = cheap(w) || t + 1 == kSpellTries
                             ? SpellStyle::shortest
                             : static_cast<SpellStyle>(w.src->choose(
                                   DecisionKind::style, kSpellStyles));
      std::string s = lex.spell(e, style, *w.src, after);
      if (s.empty() || lex.reserved(s)) continue;
      w.d.styles |= 1u << static_cast<unsigned>(style);
      return s;
    }
    return {};
  }

  // The separators: one ignored word in front of every token but the first,
  // and none inside a region that ignores nothing. False where a token's last
  // assertion forbids the byte after it: the text is then no sentence.
  [[nodiscard]] bool build_text(GrammarDerivation& d) const {
    const std::string_view sep = lex.separator();
    d.text.clear();
    for (std::size_t i = 0; i < d.tokens.size(); ++i) {
      GrammarToken& t = d.tokens[i];
      t.sep = i != 0 && !t.attached ? std::string(sep) : std::string();
      d.text += t.sep;
      t.begin = static_cast<std::uint32_t>(d.text.size());
      d.text += t.text;
      t.end = static_cast<std::uint32_t>(d.text.size());
    }
    for (const GrammarToken& t : d.tokens)
      if (t.end < d.text.size() &&
          t.after.forbids(static_cast<unsigned char>(d.text[t.end]))) {
        ++d.unrendered;
        return false;
      }
    return true;
  }
};

}  // namespace cursiv::fuzzer::detail
