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
    w.d.tree = rule_node(g.entryRule(), w, built);
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
    // The innermost trivia region being written, and the scope whose trivia go
    // in front of the next token: a region's first token, and the token after
    // the region, take the caller's. `kNoElement` is the grammar's own.
    ElementId region = kNoElement;
    ElementId gap = kNoElement;
    // The end of input was asserted: no token may follow.
    bool ended = false;
    bool adjacent = false;
    // The value rule the assignment being walked descended into: its converter
    // is the payload's, and a token of its own names another element.
    ElementId valueRule = kNoElement;
    std::vector<LineRole> brackets{};
  };

  [[nodiscard]] bool cheap(const Walk& w) const {
    return w.d.tokens.size() >= tokenBudget || w.src->exhausted();
  }

  void refuse(ElementId e, const AstNode* on, Walk& w) const {
    w.d.unwritten.push_back(AstUnwritten{on, std::string(g.featureOf(e))});
  }

  // What one rule builds: a climb's operator chain, the node its directives
  // created, the node its single child handed up, or nothing at all.
  [[nodiscard]] AstNode* rule_node(ElementId rule, Walk& w, bool& built) const {
    if (g.kindOf(rule) == ElementKind::infix_rule)
      return infix_chain(rule, w, built);
    built = false;
    if (++w.depth > kGrammarDepthCap) {
      ++w.d.capHits;
      w.ok = false;
      return nullptr;
    }
    // A rule that carries its own creation makes its node here: the model
    // fuses a rule's label and its `create` into one element when no
    // alternative builds a head of its own.
    AstNode* current = act.make(rule, *w.ar);
    for (const ElementId c : g.childrenOf(rule)) walk(c, kNoElement, current, w);
    --w.depth;
    built = current != nullptr;
    return current;
  }

  void walk(ElementId e, ElementId assign, AstNode*& current, Walk& w) const {
    if (!w.ok) return;
    const std::span<const ElementId> children = g.childrenOf(e);
    switch (g.kindOf(e)) {
      case ElementKind::literal:
      case ElementKind::character_range:
      case ElementKind::any_character:
      case ElementKind::terminal_rule:
      case ElementKind::through: {
        Veto after;
        std::string text =
            g.kindOf(e) == ElementKind::literal || g.kindOf(e) == ElementKind::through
                ? lex.spell(e, SpellStyle::shortest, *w.src, after)
                : terminal_value(e, after, w);
        emit(e, std::move(text), after, w);
        return;
      }
      case ElementKind::infix_rule:
      case ElementKind::parser_rule:
      case ElementKind::data_type_rule: {
        if (g.kindOf(e) == ElementKind::infix_rule
                ? act.primary(e) == kNoElement
                : children.empty())
          break;
        bool built = false;
        AstNode* const sub = rule_node(e, w, built);
        if (!w.ok) return;
        if (!built) {
          if (assign != kNoElement && w.valueRule == kNoElement)
            w.valueRule = e;
          return;
        }
        current = sub;
        return;
      }
      case ElementKind::assignment:
        assign_member(e, current, w);
        return;
      case ElementKind::flag:
        for (const ElementId c : children) walk(c, assign, current, w);
        if (w.ok && (current == nullptr ||
                     act.writeText(e, kNoElement, *w.ar, *current, {}) == Write::NotModelled))
          refuse(e, current, w);
        return;
      case ElementKind::create:
        current = act.make(e, *w.ar);
        for (const ElementId c : children) walk(c, assign, current, w);
        return;
      case ElementKind::nest: {
        AstNode* const inner = current;
        current = act.make(e, *w.ar);
        if (current == nullptr || inner == nullptr ||
            act.writeNode(e, *current, inner) == Write::NotModelled)
          refuse(e, current, w);
        for (const ElementId c : children) walk(c, assign, current, w);
        return;
      }
      case ElementKind::ordered_choice:
      case ElementKind::unordered_choice: {
        if (children.empty()) return;
        walk(children[pick(children, w)], assign, current, w);
        return;
      }
      case ElementKind::repetition:
        repeat(e, assign, current, w);
        return;
      case ElementKind::empty_gap:
        w.adjacent = true;
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
        const ElementId caller = w.region;
        const std::size_t first = w.d.tokens.size();
        w.region = e;
        for (const ElementId c : children) walk(c, assign, current, w);
        w.region = caller;
        if (w.d.tokens.size() != first) w.gap = caller;
        return;
      }
      case ElementKind::group:
      case ElementKind::unordered_group:
        for (const ElementId c : children) walk(c, assign, current, w);
        return;
      default:
        break;
    }
    // A layout token or an unenforced assertion leaves the derivation incomplete.
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
    std::vector<const InfixOp*> next;
    for (unsigned i = 0; i < n && w.ok; ++i) {
      next.clear();
      for (const InfixOp& o : ops)
        if (may_follow(chain, o)) next.push_back(&o);
      if (next.empty()) break;
      const InfixOp& o = *next[w.src->choose(
          DecisionKind::entry, static_cast<unsigned>(next.size()))];
      const std::size_t before = w.d.tokens.size();
      AstNode* none = nullptr;
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

  // Whether `o` may follow `chain`: a non-associative level never groups two
  // of its operators, and only a looser one between them keeps them apart.
  [[nodiscard]] static bool may_follow(const std::vector<InfixOp>& chain,
                                       const InfixOp& o) {
    if (o.group != Association::Non) return true;
    for (auto it = chain.rbegin(); it != chain.rend() && it->prec >= o.prec;
         ++it)
      if (it->prec == o.prec) return false;
    return true;
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
      AstNode* const rhs =
          climb(e, operands, chain, texts, at,
                o.group == Association::Right ? o.prec : o.prec + 1u, w);
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
  // which a data rule joins, is only named. The operand builds in a frame of
  // its own: the node it ends with, created, called or nested, is the value.
  void assign_member(ElementId e, AstNode* to, Walk& w) const {
    const std::size_t first = w.d.tokens.size();
    const ElementId outerRule = w.valueRule;
    w.valueRule = kNoElement;
    AstNode* child = nullptr;
    for (const ElementId c : g.childrenOf(e)) walk(c, e, child, w);
    const ElementId rule = w.valueRule;
    w.valueRule = outerRule;
    if (!w.ok) return;
    if (child != nullptr) {
      if (to == nullptr || act.writeNode(e, *to, child) == Write::NotModelled)
        refuse(e, to, w);
      return;
    }
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
    if (act.writeText(e, from, *w.ar, *to, value) == Write::NotModelled)
      refuse(e, to, w);
  }

  // Prefer alternatives that spell tokens while exploring. At the budget,
  // a nullable alternative can terminate a recursive rule.
  [[nodiscard]] unsigned pick(std::span<const ElementId> children,
                              Walk& w) const {
    if (guide != nullptr) {
      const int k = guide->prefer(children, w.d.tokens.size());
      if (k >= 0) return static_cast<unsigned>(k);
    }
    unsigned spelling = 0;
    const bool closing = cheap(w);
    const auto eligible = [&](ElementId c) {
      const auto min = cost[c];
      return (min.tokens != 0 || (closing && !min.conditional)) &&
             (w.brackets.empty() || w.brackets.back() != LineRole::open ||
              lex.lineRole(c) != LineRole::newline);
    };
    for (const ElementId c : children) spelling += eligible(c) ? 1u : 0u;
    if (spelling == 0) spelling = static_cast<unsigned>(children.size());
    const bool all = spelling == children.size();
    if (!closing) {
      unsigned k = w.src->choose(DecisionKind::entry, spelling);
      for (unsigned i = 0; i < children.size(); ++i)
        if (all || eligible(children[i])) {
          if (k-- == 0) return i;
        }
      return 0;
    }
    unsigned best = static_cast<unsigned>(children.size());
    for (unsigned i = 0; i < children.size(); ++i) {
      if (!all && !eligible(children[i])) continue;
      if (best == children.size() || cost[children[i]] < cost[children[best]])
        best = i;
    }
    return best == children.size() ? 0u : best;
  }

  void repeat(ElementId e, ElementId assign, AstNode*& current, Walk& w) const {
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
    for (unsigned i = 0; i < n && w.ok; ++i) round(children, i, assign, current, w);
    while (steered && w.ok && n < lo + extra &&
           guide->another(head, w.d.tokens.size()) > 0)
      round(children, n++, assign, current, w);
  }

  void round(std::span<const ElementId> children, unsigned i, ElementId assign,
             AstNode*& current, Walk& w) const {
    if (i != 0 && children.size() > 1) walk(children[1], assign, current, w);
    walk(children[0], assign, current, w);
  }

  void emit(ElementId e, std::string text, const Veto& after, Walk& w) const {
    const LineRole role = lex.lineRole(e);
    if (text.empty() || w.ended || (role == LineRole::newline &&
        !w.brackets.empty() && w.brackets.back() == LineRole::open)) {
      ++w.d.unrendered;
      w.ok = false;
      return;
    }
    std::string_view sep;
    if (!std::exchange(w.adjacent, false) && !w.d.tokens.empty()) {
      const auto& last = w.d.tokens.back();
      const auto separator = [&](ElementId scope) {
        return lex.separator(scope, last.elem, e, last.text, text.front());
      };
      sep = separator(w.gap);
      // A separator before the region may also be its first visible token.
      if (w.gap != w.region && !sep.empty() && separator(w.region) != sep) sep = {};
    }
    if (role == LineRole::open || role == LineRole::block) w.brackets.push_back(role);
    else if (role == LineRole::close && !w.brackets.empty()) w.brackets.pop_back();
    w.gap = w.region;
    w.d.tokens.push_back(
        GrammarToken{e, std::move(text), std::string(sep), 0, 0, after});
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
      if (s.empty()) continue;
      w.d.styles |= 1u << static_cast<unsigned>(style);
      return s;
    }
    return {};
  }

  // The tokens, each behind the trivia `emit` chose for it. False where a
  // token's last assertion forbids the byte after it: the text is then no
  // sentence.
  [[nodiscard]] static bool build_text(GrammarDerivation& d) {
    d.text.clear();
    for (GrammarToken& t : d.tokens) {
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
