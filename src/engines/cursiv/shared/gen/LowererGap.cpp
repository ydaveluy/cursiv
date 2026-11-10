#include <functional>

#include "cursiv/grammar/expr.hpp"
#include "cursiv/shared/gen/LexRecursion.hpp"
#include "cursiv/shared/gen/LowerVerdicts.hpp"
#include "cursiv/shared/gen/Lowerer.hpp"
#include "cursiv/shared/gen/UnboundedCondition.hpp"
#include "cursiv/shared/table/DynSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/table/Symbol.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

namespace {

constexpr std::string_view kUnreadable =
    "a condition (`&e` or `!e`) cannot test this expression: `e` may hold "
    "terminals, data rules that do not call themselves, classes, literals "
    "and `any`, their sequences, choices and repetitions, nested `&` and "
    "`!`, gap conditions (`gap::has`, `gap::is`, `eof`) and `skip()`, "
    "combined with `!`, `|` and `+`. A condition cannot test a rule that "
    "builds a node -- test the terminals it starts with";

bool gap_readable(const LowerPools& c, std::uint16_t n,
                  bool recognition = false) {
  for (std::vector todo{std::pair{n, recognition}}; !todo.empty();) {
    const auto [at, recognizes] = todo.back();
    todo.pop_back();
    const LexNode& e = c.lex[at];
    switch (e.kind) {
      case LexKind::token:
        if (!recognizes || c.terms[e.a].synthetic != 0) return false;
        break;
      case LexKind::positive:
      case LexKind::negative:
      case LexKind::gap_has:
      case LexKind::gap_is:
        if (!recognizes) return false;
        todo.emplace_back(
            e.a, e.kind == LexKind::positive || e.kind == LexKind::negative);
        break;
      case LexKind::end_mark:
        if (!recognizes) return false;
        break;
      case LexKind::rep:
        if (e.b != kNoLexNode) todo.emplace_back(e.b, recognizes);
        todo.emplace_back(e.a, recognizes);
        break;
      case LexKind::chars:
      case LexKind::nguard:
        break;
      case LexKind::skip:
        if (!recognizes) return false;
        [[fallthrough]];
      case LexKind::seq:
      case LexKind::alt:
        for (unsigned i = 0; i < e.b; ++i)
          todo.emplace_back(c.lexChild[e.a + i], recognizes);
        break;
      default:
        return false;
    }
  }
  return true;
}

}  // namespace

std::uint16_t Lowerer::gap_leaf(std::uint64_t id,
                                const std::function<GapLeaf()>& make) {
  const auto [at_, fresh] = _pool.gapLeaf.try_emplace(id, _c.ngapLeaves);
  if (!fresh) return at_->second;
  if (_c.ngapLeaves >= kGapAnd) {
    _pool.gapLeaf.erase(at_);
    lower_refuse(_c, LowerFail::gapLeafPool);
    return 0;
  }
  const GapLeaf leaf = make();
  if (leaf.kind != GapLeafKind::end && leaf.kind != GapLeafKind::cut &&
      (!gap_readable(_c, leaf.node, leaf.reads_tokens()) ||
       (leaf.skip != kNoLexNode && !gap_readable(_c, leaf.skip, true))))
    lower_refuse(_c, LowerFail::gapShape);
  _c.gapLeaves[_c.ngapLeaves] = leaf;
  return _c.ngapLeaves++;
}

Descent<bool> Lowerer::lexical(ImageId id) {
  if (const auto it = _lexicalMemo.find(id); it != _lexicalMemo.end())
    co_return it->second != 0;
  const ImageNode n = at(id);
  bool out = false;
  switch (n.kind) {
    case ImageKind::cpset:
    case ImageKind::lit:
    case ImageKind::byte:
    case ImageKind::eof:
    case ImageKind::through:
    case ImageKind::sectionClose:
    case ImageKind::gapHas:
    case ImageKind::gapIs:
      out = true;
      break;
    case ImageKind::andOf:
    case ImageKind::skip:
    case ImageKind::doc:
    case ImageKind::trigger:
    case ImageKind::notOf:
    case ImageKind::repeat:
      out = co_await lexical(child_at(n, 0));
      break;
    case ImageKind::seq:
    case ImageKind::choice:
      out = true;
      for (const ImageId k : children_of(n))
        if (!co_await lexical(k)) {
          out = false;
          break;
        }
      break;
    case ImageKind::ref:
      out = terminal_rule(n.a) || data_rule(n.a);
      break;
    default:
      break;
  }
  _lexicalMemo.emplace(id, out ? 1 : 0);
  co_return out;
}

Descent<bool> Lowerer::gap(ImageId id) {
  if (const auto it = _gapMemo.find(id); it != _gapMemo.end())
    co_return it->second != 0;
  const ImageNode n = at(id);
  bool out = false;
  switch (n.kind) {
    case ImageKind::gapHas:
    case ImageKind::gapIs:
      out = true;
      break;
    case ImageKind::andOf:
      out = co_await lexical(child_at(n, 0));
      break;
    case ImageKind::notOf: {
      const ImageId k = child_at(n, 0);
      if (const ImageNode kn = at(k); kn.kind == ImageKind::trigger) {
        const ImageId p = child_at(kn, 0);
        out = co_await lexical(p);
        if (out) out = !(co_await cut_prefix(p)).ok;
      } else {
        out = co_await gap(k);
        if (!out) out = co_await lexical(k);
      }
      break;
    }
    case ImageKind::seq:
    case ImageKind::choice: {
      bool parts = true;
      bool any = false;
      for (const ImageId k : children_of(n)) {
        const bool g = co_await gap(k);
        parts = parts && (g || at(k).kind == ImageKind::eof);
        any = any || g;
      }
      out = parts && any;
      break;
    }
    default:
      break;
  }
  _gapMemo.emplace(id, out ? 1 : 0);
  co_return out;
}

Descent<bool> Lowerer::phrase_gap(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::andOf:
      co_return co_await lexical(child_at(n, 0));
    case ImageKind::notOf:
      if (co_await phrase_gap(child_at(n, 0))) co_return true;
      co_return co_await lexical(child_at(n, 0));
    case ImageKind::choice:
    case ImageKind::seq:
      for (const ImageId k : children_of(n))
        if (co_await phrase_gap(k)) co_return true;
      co_return false;
    default:
      co_return false;
  }
}

Descent<bool> Lowerer::lex_phrase(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::seq:
    case ImageKind::andOf:
    case ImageKind::notOf:
    case ImageKind::eof:
    case ImageKind::skip:
      co_return true;
    case ImageKind::choice:
      for (const ImageId k : children_of(n))
        if (co_await lex_phrase(k)) co_return true;
      co_return false;
    case ImageKind::repeat:
      if (n.n > 1) co_return true;
      co_return co_await lex_phrase(child_at(n, 0));
    case ImageKind::ref:
      co_return data_rule(n.a);
    case ImageKind::doc:
    case ImageKind::trigger:
      co_return co_await lex_phrase(child_at(n, 0));
    default:
      co_return false;
  }
}

bool Lowerer::cut_one(ImageId id) const {
  const ImageNode& n = at(id);
  switch (n.kind) {
    case ImageKind::lit:
    case ImageKind::cpset:
      return true;
    case ImageKind::ref:
      return terminal_rule(n.a);
    case ImageKind::doc:
      return cut_one(child_at(n, 0));
    default:
      return false;
  }
}

bool Lowerer::cut_tokens(ImageId id) const {
  const ImageNode& n = at(id);
  return n.kind != ImageKind::choice
             ? cut_one(id)
             : std::ranges::all_of(children_of(n),
                                   [&](ImageId k) { return cut_one(k); });
}

Descent<Lowerer::Cut> Lowerer::cut_prefix(ImageId p) {
  const ImageNode n = at(p);
  const std::vector<ImageId> es =
      n.kind == ImageKind::seq ? children_of(n) : std::vector<ImageId>{p};
  Cut out;
  if (es.empty()) co_return out;
  for (std::size_t i = 0; i + 1 < es.size(); ++i) {
    if (!co_await gap(es[i])) co_return {};
    out.guards.push_back(es[i]);
  }
  out.token = es.back();
  out.ok = cut_tokens(out.token);
  co_return out;
}

std::uint16_t Lowerer::skip_node(unsigned cfg) {
  if (_pool.skipNode.size() <= cfg) _pool.skipNode.resize(cfg + 1u, kNoLexNode);
  if (_pool.skipNode[cfg] != kNoLexNode) return _pool.skipNode[cfg];
  std::vector<std::uint16_t> bodies;
  for (std::uint32_t i = _c.cfgTriviaAt[cfg]; i < _c.cfgTriviaAt[cfg + 1u];
       ++i)
    bodies.push_back(_lex.push(LexNode{LexKind::token, _c.cfgTrivia[i]}));
  return _pool.skipNode[cfg] = _lex.group(LexKind::skip, bodies);
}

std::uint16_t Lowerer::phrase_lexical(unsigned cfg, ImageId e) {
  if (at(e).kind == ImageKind::epsilon)
    return _lex.push(LexNode{LexKind::seq, _c.nlexChild, 0});
  return _lex.reify(_lex.resolve(cfg, e));
}

std::uint16_t Lowerer::phrase_private_token(unsigned cfg, ImageId e,
                                            ImageId body) {
  const ImageNode n = at(e);
  const auto [at_, fresh] = _pool.termIndex.try_emplace(
      key(Key::privateToken, _cfgs[cfg].sectionCfg, e), _nterms);
  const std::uint16_t t = at_->second;
  if (fresh) {
    if (_nterms >= kNtBase) {
      _pool.termIndex.erase(at_);
      lower_refuse(_c, LowerFail::tooManyTerminals);
      return 0;
    }
    ++_nterms;
    _c.terms[t] = {};
    if (n.kind == ImageKind::ref) name_row(_c.terms[t], _im.rules[n.a].name);
    _c.termBuild[t].lex = phrase_lexical(cfg, e);
    _c.termBuild[t].predicateOnly = true;
    const ImageNode b = at(body);
    if (b.kind == ImageKind::lit) spell_literal(t, b);
  }
  return _lex.push(LexNode{LexKind::token, t});
}

std::uint16_t Lowerer::phrase_token(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  if (n.kind != ImageKind::ref) return phrase_private_token(cfg, e, e);
  if (_termIdx.contains(n.a))
    return _lex.push(LexNode{LexKind::token, sym_of(cfg, n.a)});
  return phrase_private_token(cfg, e, body_in(cfg, n.a));
}

Descent<std::uint16_t> Lowerer::phrase(unsigned cfg, ImageId e,
                                       std::uint16_t skip,
                                       std::vector<std::uint32_t>& path) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::seq: {
      std::vector<std::uint16_t> parts, row;
      for (const ImageId k : children_of(n))
        parts.push_back(co_await phrase(cfg, k, skip, path));
      const bool spaced = _c.lex[skip].b != 0;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (spaced && i != 0) row.push_back(phrase_skip(skip));
        row.push_back(parts[i]);
      }
      co_return _lex.group(LexKind::seq, row);
    }
    case ImageKind::choice: {
      std::vector<std::uint16_t> children;
      for (const ImageId k : children_of(n))
        children.push_back(co_await phrase(cfg, k, skip, path));
      co_return _lex.group(LexKind::alt, children);
    }
    case ImageKind::repeat: {
      if (n.m >= kEndless || (n.n != kUnbounded && n.n >= kEndless))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const auto one = co_await phrase(cfg, child_at(n, 0), skip, path);
      if (n.m == 1 && n.n == 1) co_return one;
      const auto between = _c.lex[skip].b == 0 || n.n <= 1
                               ? kNoLexNode
                               : phrase_skip(skip);
      co_return _lex.push(LexNode{
          LexKind::rep, one, between, static_cast<std::uint32_t>(n.m),
          n.n == kUnbounded ? kEndless : static_cast<std::uint32_t>(n.n)});
    }
    case ImageKind::trigger: {
      const auto body = co_await phrase(cfg, child_at(n, 0), skip, path);
      if (lex_empty_nodes(_c)[body] != 0)
        lower_refuse_rule(_c, LowerFail::commitPrefixEmpty, _pool.owner);
      co_return body;
    }
    case ImageKind::sectionClose: {
      if (_cfgs[cfg].end == kNoImage)
        refuse_lowering("cursiv: a section's close is read outside any section");
      co_return co_await phrase(cfg, _cfgs[cfg].end, skip, path);
    }
    case ImageKind::andOf:
    case ImageKind::notOf: {
      const auto body = co_await phrase(cfg, child_at(n, 0), skip, path);
      co_return _lex.push(LexNode{
          n.kind == ImageKind::andOf ? LexKind::positive : LexKind::negative,
          body});
    }
    case ImageKind::eof:
      co_return _lex.push(LexNode{LexKind::end_mark});
    case ImageKind::epsilon:
      co_return _lex.push(LexNode{LexKind::seq, _c.nlexChild, 0});
    case ImageKind::gapHas:
    case ImageKind::gapIs:
      co_return _lex.push(LexNode{
          n.kind == ImageKind::gapHas ? LexKind::gap_has : LexKind::gap_is,
          phrase_lexical(cfg, child_at(n, 0))});
    case ImageKind::doc:
      co_return co_await phrase(cfg, child_at(n, 0), skip, path);
    case ImageKind::skip: {
      std::vector<std::uint16_t> trivia;
      for (std::uint64_t i = 0; i < n.m + n.n; ++i)
        trivia.push_back(phrase_token(
            cfg, _lex.make(ImageKind::ref, {}, 0, _im.ruleLists[n.a + i])));
      co_return co_await phrase(cfg, child_at(n, 0),
                                _lex.group(LexKind::skip, trivia), path);
    }
    case ImageKind::ref: {
      if (terminal_rule(n.a)) co_return phrase_token(cfg, e);
      if (!data_rule(n.a))
        refuse_lowering(
            "a condition cannot test a rule that builds a node -- test the "
            "terminals it starts with");
      if (std::ranges::find(path, n.a) != path.end())
        refuse_lowering(
            "a condition cannot test a data rule that calls itself, directly "
            "or through other rules -- test the terminals it starts with");
      path.push_back(n.a);
      const std::uint16_t out =
          co_await phrase(cfg, body_in(cfg, n.a), skip, path);
      path.pop_back();
      co_return out;
    }
    default:
      co_return phrase_token(cfg, e);
  }
}

Descent<void> Lowerer::cut_terms(unsigned cfg, ImageId token, DynSet& out) {
  if (const ImageNode n = at(token); n.kind == ImageKind::choice) {
    for (const ImageId k : children_of(n)) co_await cut_terms(cfg, k, out);
    co_return;
  }
  out.set(term_of(cfg, token));
}

std::uint16_t Lowerer::cut_node(unsigned cfg, ImageId token) {
  DynSet terms;
  descend(cut_terms(cfg, token, terms));
  std::vector<std::uint16_t> children;
  for (std::uint16_t t = 0; t < _nterms; ++t)
    if (terms.test(t))
      children.push_back(_lex.push(LexNode{LexKind::token, t}));
  return children.size() == 1 ? children.front()
                              : _lex.group(LexKind::alt, children);
}

Descent<void> Lowerer::gap_join(unsigned cfg, std::uint16_t op,
                                const std::vector<ImageId>& es) {
  for (const ImageId e : es) co_await gap_ops(cfg, e);
  gap_op(op);
  gap_op(static_cast<std::uint16_t>(es.size()));
}

Descent<void> Lowerer::gap_ops(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::gapHas:
    case ImageKind::gapIs: {
      const bool has = n.kind == ImageKind::gapHas;
      gap_op(gap_leaf(key(has ? Key::hasLeaf : Key::isLeaf, 0, e), [&] {
        return GapLeaf{.kind = has ? GapLeafKind::has : GapLeafKind::is,
                       .node = phrase_lexical(cfg, child_at(n, 0))};
      }));
      co_return;
    }
    case ImageKind::andOf: {
      const ImageId k = child_at(n, 0);
      const ImageNode kn = at(k);
      if (kn.kind == ImageKind::eof || kn.kind == ImageKind::notOf) {
        co_await gap_ops(cfg, k);
        co_return;
      }
      if (kn.kind == ImageKind::trigger) {
        const Cut cut = co_await cut_prefix(child_at(kn, 0));
        if (cut.ok) {
          const ImageId trigger =
              _lex.make(ImageKind::trigger, std::span(&cut.token, 1));
          gap_op(gap_leaf(key(Key::cutLeaf, cfg, trigger), [&] {
            return GapLeaf{.kind = GapLeafKind::cut,
                           .node = cut_node(cfg, cut.token)};
          }));
          for (const ImageId g : cut.guards) co_await gap_ops(cfg, g);
          if (!cut.guards.empty()) {
            gap_op(kGapAnd);
            gap_op(static_cast<std::uint16_t>(cut.guards.size() + 1u));
          }
          co_return;
        }
      }
      if (!co_await lexical(k)) refuse_lowering(kUnreadable);
      gap_op(gap_leaf(key(Key::andLeaf, cfg, e), [&] {
        const std::uint16_t skip = skip_node(cfg);
        std::vector<std::uint32_t> path;
        const std::uint16_t body = descend(phrase(cfg, k, skip, path));
        return GapLeaf{
            .kind = descend(lex_phrase(k)) ? GapLeafKind::phrase
                                           : GapLeafKind::head,
            .node = body,
            .skip = _c.lex[skip].b == 0 ? kNoLexNode : phrase_skip(skip),
            .cfg = kn.kind == ImageKind::sectionClose
                       ? std::uint8_t{0}
                       : static_cast<std::uint8_t>(cfg),
            .trigger = kn.kind == ImageKind::trigger};
      }));
      co_return;
    }
    case ImageKind::eof:
      gap_op(gap_leaf(key(Key::endLeaf, 0, e),
                      [] { return GapLeaf{.kind = GapLeafKind::end}; }));
      co_return;
    case ImageKind::notOf: {
      ImageId inner = child_at(n, 0);
      if (at(inner).kind == ImageKind::andOf) inner = child_at(at(inner), 0);
      if (co_await gap(inner) || at(inner).kind == ImageKind::eof)
        co_await gap_ops(cfg, inner);
      else
        co_await gap_ops(cfg,
                         _lex.make(ImageKind::andOf, std::span(&inner, 1)));
      gap_op(kGapNot);
      gap_op(1);
      co_return;
    }
    case ImageKind::choice:
      co_await gap_join(cfg, kGapOr, children_of(n));
      co_return;
    case ImageKind::seq:
      co_await gap_join(cfg, kGapAnd, children_of(n));
      co_return;
    default:
      refuse_lowering(kUnreadable);
  }
}

std::uint16_t Lowerer::gap_cond(unsigned cfg, ImageId e) {
  const std::uint64_t k =
      descend(phrase_gap(e)) ? key(Key::phrase, cfg, e) : key(Key::cond, 0, e);
  const auto [at_, fresh] = _pool.gapCond.try_emplace(k, _c.ngapConds);
  const std::uint16_t out = at_->second;
  if (!fresh) return out;
  if (_c.ngapConds >= kGapConds) {
    _pool.gapCond.erase(at_);
    lower_refuse(_c, LowerFail::gapTooMany);
    return 0;
  }
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  descend(gap_ops(cfg, e));
  ++_c.ngapConds;
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  find_unbounded_runs(_c, out, _pool.owner, _c.unbounded);
  return out;
}

void Lowerer::check_implied_token(unsigned cfg, ImageId e) {
  for (ImageNode n = at(e); n.kind == ImageKind::doc ||
                            (n.kind == ImageKind::ref && terminal_rule(n.a));
       n = at(e))
    e = n.kind == ImageKind::doc ? child_at(n, 0) : body_in(cfg, n.a);
  const ImageNode n = at(e);
  if (n.kind != ImageKind::lit || (n.flags & (kLitGuardWord | kLitGuardExpr)))
    refuse_lowering(
        "in `implied(tok, cond)`, `tok` must be a literal with no guard, or "
        "a terminal whose body is one: the parser inserts it with zero width "
        "when it is missing -- imply a plain `lit<>`, or write `tok | eof` "
        "where the token may be absent");
  _impliedToks.insert(_lex.canon(e));
}

std::uint16_t Lowerer::implied_term(ImageId e, std::uint16_t k) {
  const std::uint16_t before = _nterms;
  // One terminal per condition: a phrase's differs with the trivia around it.
  const std::uint16_t t =
      intern_spelled(key(Key::implied, k, e), "implied", false, false, [&] {
        return _lex.push(LexNode{LexKind::implied, 0, 0, 0, 0});
      });
  if (t == before) {
    unspell(t, kSyntheticImplied);
    _c.terms[t].cond = k;
  }
  return t;
}

}  // namespace cursiv::engine
