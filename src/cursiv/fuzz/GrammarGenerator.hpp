// The only generator: walks the declarative grammar and writes the
// tokens a document is made of and the typed AST its directives build,
// together. Reads no table -- bytes come from `GrammarLexicon`, nodes
// from `GrammarActions` -- recursion closes on a cost-guided budget.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/AstArena.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/fuzz/DecisionSource.hpp"
#include "cursiv/fuzz/ElementKind.hpp"
#include "cursiv/fuzz/GrammarActions.hpp"
#include "cursiv/fuzz/GrammarCost.hpp"
#include "cursiv/fuzz/GrammarDerivation.hpp"
#include "cursiv/fuzz/GrammarGuide.hpp"
#include "cursiv/fuzz/GrammarLexicon.hpp"
#include "cursiv/fuzz/GrammarModel.hpp"

namespace cursiv::fuzzer {

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

  // The arena owns the tree and the bytes its value members view: a
  // member borrowing the derivation's own text would dangle the moment it
  // moves.
  [[nodiscard]] GrammarDerivation generate(DecisionSource& src,
                                           AstArena& ar) const {
    Walk w{&src, &ar, {}, 0};
    bool built = false;
    const ElementId entry = g.entryRule();
    // A climb holds no children in the model (`GrammarCost`'s own note),
    // so `rule_node` would walk none and write nothing. Every other climb
    // is reached through `walk`'s `infix_rule` case; the entry is the one
    // `walk` never sees.
    w.d.tree = g.kindOf(entry) == ElementKind::infix_rule
                   ? infix_chain(entry, w, built)
                   : rule_node(entry, w, built);
    w.d.complete = w.ok;
    if (w.d.complete) lay_out(w.d);
    return std::move(w.d);
  }

 private:
  struct Walk {
    DecisionSource* src;
    AstArena* ar;
    GrammarDerivation d;
    unsigned depth = 0;
    bool ok = true;
    // The adjacency island being written, and where it started: every token of
    // it but the first admits no separator in front.
    unsigned glue = 0;
    std::size_t glueFrom = 0;
    // The last token ate the trivia beside it, so the next one takes none.
    bool fed = false;
    // The value rule the assignment being walked descended into: its converter
    // is the payload's, and a token of its own names another element.
    ElementId valueRule = kNoElement;
    // Whether the assignment being walked reached its member. Saved and
    // restored per assignment: a write inside the payload is another
    // assignment's, and a shared counter let the outer one pass for
    // written.
    bool landed = false;
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
    w.d.unwritten.push_back(AstExcused{on, std::string(g.featureOf(e))});
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
    const std::span<const ElementId> kids = g.childrenOf(e);
    switch (g.kindOf(e)) {
      case ElementKind::literal:
        emit(e, std::string(g.textOf(e)), w);
        return;
      case ElementKind::terminal_rule:
        emit(e, terminal_value(e, w), w);
        return;
      case ElementKind::infix_rule:
      case ElementKind::parser_rule:
      case ElementKind::data_type_rule: {
        const bool climb = g.kindOf(e) == ElementKind::infix_rule;
        if (climb ? act.primary(e) == kNoElement : kids.empty()) break;
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
          if (to == nullptr || !act.write_node(assign, *to, sub, *w.ar))
            refuse(assign, to, w);
          else
            w.landed = true;
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
        for (const ElementId c : kids) walk(c, assign, f, w);
        return;
      case ElementKind::nest: {
        // The node built so far is the one this wraps -- a bare call left it
        // in `adopted`, and taking only `node` dropped the wrap in silence.
        AstNode* const inner = f.node != nullptr ? f.node : f.adopted;
        f.adopted = nullptr;
        f.node = act.make(e, *w.ar);
        if (f.node == nullptr || inner == nullptr ||
            !act.write_node(e, *f.node, inner, *w.ar))
          refuse(e, f.node, w);
        for (const ElementId c : kids) walk(c, assign, f, w);
        return;
      }
      case ElementKind::ordered_choice:
      case ElementKind::unordered_choice: {
        if (kids.empty()) return;
        walk(kids[pick(kids, w)], assign, f, w);
        return;
      }
      case ElementKind::repetition:
        repeat(e, assign, f, w);
        return;
      case ElementKind::and_predicate:
      case ElementKind::not_predicate:
        return;  // an assertion spells nothing
      case ElementKind::skip_region: {
        const unsigned wasGlue = w.glue;
        const std::size_t wasFrom = w.glueFrom;
        if (lex.glues(e)) {
          ++w.glue;
          w.glueFrom = w.d.tokens.size();
        }
        for (const ElementId c : kids) walk(c, assign, f, w);
        w.glue = wasGlue;
        w.glueFrom = wasFrom;
        return;
      }
      case ElementKind::group:
      case ElementKind::unordered_group:
      case ElementKind::through:
        for (const ElementId c : kids) walk(c, assign, f, w);
        return;
      default:
        break;
    }
    // What the walk cannot write: a character range standing in the
    // syntax, where spelling one byte would leave it unable to say where
    // a separator goes.
    ++w.d.unrendered;
    w.ok = false;
  }

  // A precedence table, written flat and folded after. The chain is
  // `primary (op primary)*`; the tree it owes is what climbing the steps
  // builds, the same resolution the table settled at construction.
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
        for (std::size_t i = before; i < w.d.tokens.size(); ++i)
          text += act.denote(w.d.tokens[i].elem, w.d.tokens[i].text);
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
          !act.fold(e, o.element, *node, lhs, stash(op, *w.ar), rhs)) {
        refuse(e, node, w);
        return nullptr;
      }
      lhs = node;
    }
    return lhs;
  }

  // A member is written from one node or from one token. A value spelled
  // by several tokens is what a data rule joins through each part's own
  // converter; the walk holds no such reading, so it names the member
  // instead.
  void assign_member(ElementId e, Frame& f, Walk& w) const {
    AstNode* const to = f.node;
    const std::size_t first = w.d.tokens.size();
    const bool outer = w.landed;
    const ElementId outerRule = w.valueRule;
    w.landed = false;
    w.valueRule = kNoElement;
    for (const ElementId c : g.childrenOf(e)) walk(c, e, f, w);
    const bool landed = w.landed;
    const ElementId rule = w.valueRule;
    w.landed = outer;
    w.valueRule = outerRule;
    if (!w.ok || landed) return;
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
      for (std::size_t i = first; i < last; ++i)
        value += act.denote(w.d.tokens[i].elem, w.d.tokens[i].text);
    }
    const ElementId from = rule != kNoElement ? rule : w.d.tokens[first].elem;
    if (!act.write_text(e, from, *to, stash(value, *w.ar), *w.ar))
      refuse(e, to, w);
  }

  // The bytes a value member views, copied where the tree lives.
  [[nodiscard]] static std::string_view stash(std::string_view t,
                                              AstArena& ar) {
    if (t.empty()) return {};
    char* const p = static_cast<char*>(ar.carve_bytes(t.size(), alignof(char)));
    std::memcpy(p, t.data(), t.size());
    return {p, t.size()};
  }

  // The alternative: the cheapest once the budget is spent, so a
  // recursive grammar always closes. One that spells nothing is the last
  // resort: the walk cannot certify such a branch's assertion about the
  // input, so taking it could contradict the very next token written.
  [[nodiscard]] unsigned pick(std::span<const ElementId> kids, Walk& w) const {
    if (guide != nullptr) {
      const int k = guide->prefer(kids, w.d.tokens.size());
      if (k >= 0) return static_cast<unsigned>(k);
    }
    unsigned spelling = 0;
    for (const ElementId c : kids) spelling += cost[c] != 0 ? 1u : 0u;
    if (spelling == 0) spelling = static_cast<unsigned>(kids.size());
    const bool all = spelling == kids.size();
    if (!cheap(w)) {
      unsigned k = w.src->choose(DecisionKind::entry, spelling);
      for (unsigned i = 0; i < kids.size(); ++i)
        if (all || cost[kids[i]] != 0) {
          if (k-- == 0) return i;
        }
      return 0;
    }
    unsigned best = static_cast<unsigned>(kids.size());
    for (unsigned i = 0; i < kids.size(); ++i) {
      if (!all && cost[kids[i]] == 0) continue;
      if (best == kids.size() || cost[kids[i]] < cost[kids[best]]) best = i;
    }
    return best == kids.size() ? 0u : best;
  }

  void repeat(ElementId e, ElementId assign, Frame& f, Walk& w) const {
    const std::span<const ElementId> kids = g.childrenOf(e);
    if (kids.empty()) return;
    const unsigned lo = g.minOf(e);
    const unsigned hi = g.maxOf(e);
    const unsigned extra = hi > lo ? std::min(hi - lo, kGrammarRepCap) : 0u;
    // A later round begins on the separator a bracketed repetition carries.
    const ElementId head = kids.size() > 1 ? kids[1] : kids[0];
    const bool steered =
        guide != nullptr && guide->another(head, w.d.tokens.size()) >= 0;
    unsigned n = lo;
    if (!steered && !cheap(w) && extra != 0)
      n = lo + w.src->choose(DecisionKind::byte, extra + 1);
    for (unsigned i = 0; i < n && w.ok; ++i) round(kids, i, assign, f, w);
    while (steered && w.ok && n < lo + extra &&
           guide->another(head, w.d.tokens.size()) > 0)
      round(kids, n++, assign, f, w);
  }

  void round(std::span<const ElementId> kids, unsigned i, ElementId assign,
             Frame& f, Walk& w) const {
    if (i != 0 && kids.size() > 1) walk(kids[1], assign, f, w);
    walk(kids[0], assign, f, w);
  }

  void emit(ElementId e, std::string text, Walk& w) const {
    if (text.empty()) {
      ++w.d.unrendered;
      w.ok = false;
      return;
    }
    const bool hungry = lex.devours(e);
    const bool glued =
        (w.glue != 0 && w.d.tokens.size() > w.glueFrom) || hungry || w.fed;
    w.fed = hungry;
    w.d.tokens.push_back(GrammarToken{e, std::move(text), {}, glued, 0, 0});
  }

  // The value of a named terminal, from its own expression. A spelling the
  // grammar reserves as a literal is refused: that word is the literal's.
  [[nodiscard]] std::string terminal_value(ElementId e, Walk& w) const {
    for (unsigned t = 0; t < kSpellTries; ++t) {
      const auto style = cheap(w) || t + 1 == kSpellTries
                             ? SpellStyle::shortest
                             : static_cast<SpellStyle>(w.src->choose(
                                   DecisionKind::style, kSpellStyles));
      std::string s = lex.spell(e, style, *w.src);
      if (s.empty() || lex.reserved(s)) continue;
      w.d.styles |= 1u << static_cast<unsigned>(style);
      return s;
    }
    return {};
  }

  // The separators: one ignored word in front of every token but the first,
  // and none at all inside an adjacency island. The document is assembled once
  // -- no fixpoint, because nothing here reads the text back.
  void lay_out(GrammarDerivation& d) const {
    const std::string_view sep = lex.separator();
    d.text.clear();
    for (std::size_t i = 0; i < d.tokens.size(); ++i) {
      GrammarToken& t = d.tokens[i];
      t.sep = i != 0 && !t.glued ? std::string(sep) : std::string();
      d.text += t.sep;
      t.begin = static_cast<std::uint32_t>(d.text.size());
      d.text += t.text;
      t.end = static_cast<std::uint32_t>(d.text.size());
    }
  }
};

}  // namespace cursiv::fuzzer
