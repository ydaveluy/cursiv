#include <functional>

#include "cursiv/core/grammar/expr.hpp"

#include "cursiv/engines/shared/gen/LexRecursion.hpp"
#include "cursiv/engines/shared/gen/Lowerer.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/gen/UnboundedCondition.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"

namespace cursiv {

namespace {

constexpr std::string_view kUnreadable =
    "cursiv: `&e` and `!e` read `e` as the parse would: "
    "terminals, nonrecursive data rules, classes, literals and "
    "`any`, their sequences, choices and repetitions, nested `&` "
    "and `!`, gap conditions (`gap::has`, `gap::is`, `eof`) and "
    "`skip()`; conditions compose with !, | and +. A rule that "
    "builds a node is parsed, not read: no condition asks one -- "
    "ask the terminals it starts with";

bool gap_readable(const LowerPools& c, std::uint16_t n,
                  bool recognition = false) {
  const LexNode& e = c.lex[n];
  switch (e.kind) {
    case LexKind::token:
      return recognition && c.terms[e.a].laid == 0;
    case LexKind::positive:
    case LexKind::negative:
      return recognition && gap_readable(c, e.a, true);
    case LexKind::gap_has:
    case LexKind::gap_is:
      return recognition && gap_readable(c, e.a);
    case LexKind::end_mark:
      return recognition;
    case LexKind::rep:
      return gap_readable(c, e.a, recognition) &&
             (e.b == kNoLexNode || gap_readable(c, e.b, recognition));
    case LexKind::chars:
    case LexKind::nguard:
      return true;
    case LexKind::skip:
      if (!recognition) return false;
      [[fallthrough]];
    case LexKind::seq:
    case LexKind::alt:
      for (unsigned i = 0; i < e.b; ++i)
        if (!gap_readable(c, c.lexKid[e.a + i], recognition)) return false;
      return true;
    default:
      return false;
  }
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
  return static_cast<std::uint16_t>(_c.ngapLeaves++);
}

bool Lowerer::lexical(ImageId id) {
  if (const auto it = _lexicalMemo.find(id); it != _lexicalMemo.end())
    return it->second != 0;
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
      out = lexical(kid(n, 0));
      break;
    case ImageKind::seq:
    case ImageKind::choice:
      out = std::ranges::all_of(kids_of(n),
                                [&](ImageId k) { return lexical(k); });
      break;
    case ImageKind::ref:
      out = terminal_rule(n.a) || data_rule(n.a);
      break;
    default:
      break;
  }
  _lexicalMemo.emplace(id, out ? 1 : 0);
  return out;
}

bool Lowerer::gap(ImageId id) {
  if (const auto it = _gapMemo.find(id); it != _gapMemo.end())
    return it->second != 0;
  const ImageNode n = at(id);
  bool out = false;
  switch (n.kind) {
    case ImageKind::gapHas:
    case ImageKind::gapIs:
      out = true;
      break;
    case ImageKind::andOf:
      out = lexical(kid(n, 0));
      break;
    case ImageKind::notOf: {
      const ImageId k = kid(n, 0);
      const ImageNode kn = at(k);
      if (kn.kind == ImageKind::trigger) {
        const ImageId p = kid(kn, 0);
        out = lexical(p) && !cut_prefix(p).ok;
      } else {
        out = gap(k) || lexical(k);
      }
      break;
    }
    case ImageKind::seq:
    case ImageKind::choice: {
      bool parts = true;
      bool any = false;
      for (const ImageId k : kids_of(n)) {
        parts = parts && (gap(k) || at(k).kind == ImageKind::eof);
        any = any || gap(k);
      }
      out = parts && any;
      break;
    }
    default:
      break;
  }
  _gapMemo.emplace(id, out ? 1 : 0);
  return out;
}

bool Lowerer::phrase_gap(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::andOf:
      return lexical(kid(n, 0));
    case ImageKind::notOf:
      return phrase_gap(kid(n, 0)) || lexical(kid(n, 0));
    case ImageKind::choice:
    case ImageKind::seq:
      return std::ranges::any_of(kids_of(n),
                                 [&](ImageId k) { return phrase_gap(k); });
    default:
      return false;
  }
}

bool Lowerer::lex_phrase(ImageId id) {
  const ImageNode n = at(id);
  switch (n.kind) {
    case ImageKind::seq:
    case ImageKind::andOf:
    case ImageKind::notOf:
    case ImageKind::eof:
    case ImageKind::skip:
      return true;
    case ImageKind::choice:
      return std::ranges::any_of(kids_of(n),
                                 [&](ImageId k) { return lex_phrase(k); });
    case ImageKind::repeat:
      return n.n > 1 || lex_phrase(kid(n, 0));
    case ImageKind::ref:
      return data_rule(n.a);
    case ImageKind::doc:
    case ImageKind::trigger:
      return lex_phrase(kid(n, 0));
    default:
      return false;
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
      return cut_one(kid(n, 0));
    default:
      return false;
  }
}

bool Lowerer::cut_tokens(ImageId id) const {
  const ImageNode& n = at(id);
  return n.kind != ImageKind::choice
             ? cut_one(id)
             : std::ranges::all_of(kids_of(n),
                                   [&](ImageId k) { return cut_one(k); });
}

Lowerer::Cut Lowerer::cut_prefix(ImageId p) {
  const ImageNode n = at(p);
  const std::vector<ImageId> es =
      n.kind == ImageKind::seq ? kids_of(n) : std::vector<ImageId>{p};
  Cut out;
  if (es.empty()) return out;
  for (std::size_t i = 0; i + 1 < es.size(); ++i) {
    if (!gap(es[i])) return {};
    out.guards.push_back(es[i]);
  }
  out.token = es.back();
  out.ok = cut_tokens(out.token);
  return out;
}

void Lowerer::gap_op(std::uint16_t v) { _c.gapOps[_c.ngapOps++] = v; }

std::uint16_t Lowerer::phrase_skip(std::uint16_t skip) {
  return _lex.push(_c.lex[skip]);
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
    return _lex.push(LexNode{LexKind::seq, _c.nlexKid, 0});
  return _lex.reify(_lex.resolve(cfg, e));
}

std::uint16_t Lowerer::phrase_private_token(unsigned cfg, ImageId e,
                                              ImageId body) {
  const ImageNode n = at(e);
  const auto [at_, fresh] = _pool.termIndex.try_emplace(
      key(Key::privateToken, _cfgs[cfg].home, e), _nterms);
  const std::uint16_t t = at_->second;
  if (fresh) {
    if (_nterms >= kNtBase) {
      _pool.termIndex.erase(at_);
      lower_refuse(_c, LowerFail::tooManyTerminals);
      return 0;
    }
    ++_nterms;
    _c.terms[t] = {};
    if (n.kind == ImageKind::ref) {
      std::uint16_t len = 0;
      const std::uint16_t off = name_of(n.a, len);
      _c.terms[t].nameOff = off;
      _c.terms[t].nameLen = len;
    }
    const std::uint16_t lex = phrase_lexical(cfg, e);
    _c.termBuild[t].lex = lex;
    _c.termBuild[t].predicateOnly = true;
    const ImageNode b = at(body);
    if (b.kind == ImageKind::lit) spell_literal(t, b);
  }
  return _lex.push(LexNode{LexKind::token, t});
}

std::uint16_t Lowerer::phrase_token(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  if (n.kind != ImageKind::ref) return phrase_private_token(cfg, e, e);
  if (_termIdx.contains(n.a)) {
    const std::uint16_t t = sym_of(cfg, n.a);
    return _lex.push(LexNode{LexKind::token, t});
  }
  return phrase_private_token(cfg, e, body_in(cfg, n.a));
}

std::uint16_t Lowerer::phrase(unsigned cfg, ImageId e, std::uint16_t skip,
                                std::vector<std::uint32_t>& path) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::seq: {
      std::vector<std::uint16_t> parts, row;
      for (const ImageId k : kids_of(n)) parts.push_back(phrase(cfg, k, skip, path));
      const bool spaced = _c.lex[skip].b != 0;
      for (std::size_t i = 0; i < parts.size(); ++i) {
        if (spaced && i != 0) row.push_back(phrase_skip(skip));
        row.push_back(parts[i]);
      }
      return _lex.group(LexKind::seq, row);
    }
    case ImageKind::choice: {
      std::vector<std::uint16_t> kids;
      for (const ImageId k : kids_of(n)) kids.push_back(phrase(cfg, k, skip, path));
      return _lex.group(LexKind::alt, kids);
    }
    case ImageKind::repeat: {
      if (n.m >= 0xffffffffu || (n.n != kUnbounded && n.n >= 0xffffffffu))
        throw LowerRefused(LowerFail::tokenGraphTooLarge);
      const auto one = phrase(cfg, kid(n, 0), skip, path);
      if (n.m == 1 && n.n == 1) return one;
      const auto between = _c.lex[skip].b == 0 || n.n <= 1
                               ? kNoLexNode
                               : phrase_skip(skip);
      return _lex.push(LexNode{LexKind::rep, one, between,
                               static_cast<std::uint32_t>(n.m),
                               n.n == kUnbounded
                                   ? 0xffffffffu
                                   : static_cast<std::uint32_t>(n.n)});
    }
    case ImageKind::trigger: {
      const auto body = phrase(cfg, kid(n, 0), skip, path);
      if (lex_may_be_empty(_c, body))
        lower_refuse_rule(_c, LowerFail::commitPrefixEmpty, _pool.owner);
      return body;
    }
    case ImageKind::sectionClose: {
      if (_cfgs[cfg].end == kNoImage)
        refuse_lowering("cursiv: a section's close is read outside any section");
      return phrase(cfg, _cfgs[cfg].end, skip, path);
    }
    case ImageKind::andOf:
    case ImageKind::notOf: {
      const auto body = phrase(cfg, kid(n, 0), skip, path);
      return _lex.push(LexNode{
          n.kind == ImageKind::andOf ? LexKind::positive : LexKind::negative,
          body});
    }
    case ImageKind::eof:
      return _lex.push(LexNode{LexKind::end_mark});
    case ImageKind::epsilon:
      return _lex.push(LexNode{LexKind::seq, _c.nlexKid, 0});
    case ImageKind::gapHas:
    case ImageKind::gapIs:
      return _lex.push(LexNode{
          n.kind == ImageKind::gapHas ? LexKind::gap_has : LexKind::gap_is,
          phrase_lexical(cfg, kid(n, 0))});
    case ImageKind::doc:
      return phrase(cfg, kid(n, 0), skip, path);
    case ImageKind::skip: {
      std::vector<std::uint16_t> trivia;
      for (std::uint64_t i = 0; i < n.m + n.n; ++i)
        trivia.push_back(phrase_token(
            cfg, _lex.make(ImageKind::ref, {}, 0, _im.ruleLists[n.a + i])));
      return phrase(cfg, kid(n, 0), _lex.group(LexKind::skip, trivia), path);
    }
    case ImageKind::ref: {
      if (terminal_rule(n.a)) return phrase_token(cfg, e);
      if (!data_rule(n.a))
        refuse_lowering(
            "cursiv: A rule that builds a node is parsed, not read: no "
            "condition asks one");
      if (std::ranges::find(path, n.a) != path.end())
        refuse_lowering("cursiv: a condition reads a data rule that reaches itself");
      path.push_back(n.a);
      const std::uint16_t out =
          phrase(cfg, body_in(cfg, n.a), skip, path);
      path.pop_back();
      return out;
    }
    default:
      return phrase_token(cfg, e);
  }
}

void Lowerer::cut_terms(unsigned cfg, ImageId token, DynSet& out) {
  const ImageNode n = at(token);
  if (n.kind == ImageKind::choice) {
    for (const ImageId k : kids_of(n)) cut_terms(cfg, k, out);
    return;
  }
  out.set(term_of(cfg, token));
}

std::uint16_t Lowerer::cut_node(unsigned cfg, ImageId token) {
  DynSet terms;
  cut_terms(cfg, token, terms);
  std::vector<std::uint16_t> kids;
  for (std::uint16_t t = 0; t < _nterms; ++t)
    if (terms.test(t))
      kids.push_back(_lex.push(LexNode{LexKind::token, t}));
  return kids.size() == 1 ? kids.front() : _lex.group(LexKind::alt, kids);
}

void Lowerer::gap_join(unsigned cfg, std::uint16_t op,
                         const std::vector<ImageId>& es) {
  for (const ImageId e : es) gap_ops(cfg, e);
  gap_op(op);
  gap_op(static_cast<std::uint16_t>(es.size()));
}

void Lowerer::gap_ops(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::gapHas:
    case ImageKind::gapIs: {
      const bool has = n.kind == ImageKind::gapHas;
      gap_op(gap_leaf(key(has ? Key::hasLeaf : Key::isLeaf, 0, e), [&] {
        return GapLeaf{.kind = has ? GapLeafKind::has : GapLeafKind::is,
                       .node = phrase_lexical(cfg, kid(n, 0))};
      }));
      return;
    }
    case ImageKind::andOf: {
      const ImageId k = kid(n, 0);
      const ImageNode kn = at(k);
      if (kn.kind == ImageKind::eof || kn.kind == ImageKind::notOf) {
        gap_ops(cfg, k);
        return;
      }
      if (kn.kind == ImageKind::trigger) {
        const Cut cut = cut_prefix(kid(kn, 0));
        if (cut.ok) {
          const ImageId trigger = _lex.make(
              ImageKind::trigger, std::span(&cut.token, 1));
          gap_op(gap_leaf(key(Key::cutLeaf, cfg, trigger), [&] {
            return GapLeaf{.kind = GapLeafKind::cut,
                           .node = cut_node(cfg, cut.token)};
          }));
          for (const ImageId g : cut.guards) gap_ops(cfg, g);
          if (!cut.guards.empty()) {
            gap_op(kGapAnd);
            gap_op(static_cast<std::uint16_t>(cut.guards.size() + 1u));
          }
          return;
        }
      }
      if (!lexical(k)) refuse_lowering(kUnreadable);
      gap_op(gap_leaf(key(Key::andLeaf, cfg, e), [&] {
        const std::uint16_t skip = skip_node(cfg);
        std::vector<std::uint32_t> path;
        const std::uint16_t body = phrase(cfg, k, skip, path);
        return GapLeaf{
            .kind = lex_phrase(k) ? GapLeafKind::phrase : GapLeafKind::head,
            .node = body,
            .skip = _c.lex[skip].b == 0 ? kNoLexNode : phrase_skip(skip),
            .cfg = kn.kind == ImageKind::sectionClose
                       ? std::uint8_t{0}
                       : static_cast<std::uint8_t>(cfg),
            .trigger = kn.kind == ImageKind::trigger};
      }));
      return;
    }
    case ImageKind::eof:
      gap_op(gap_leaf(key(Key::endLeaf, 0, e),
                      [] { return GapLeaf{.kind = GapLeafKind::end}; }));
      return;
    case ImageKind::notOf: {
      ImageId inner = kid(n, 0);
      if (at(inner).kind == ImageKind::andOf) inner = kid(at(inner), 0);
      if (gap(inner) || at(inner).kind == ImageKind::eof)
        gap_ops(cfg, inner);
      else
        gap_ops(cfg, _lex.make(ImageKind::andOf, std::span(&inner, 1)));
      gap_op(kGapNot);
      gap_op(1);
      return;
    }
    case ImageKind::choice:
      gap_join(cfg, kGapOr, kids_of(n));
      return;
    case ImageKind::seq:
      gap_join(cfg, kGapAnd, kids_of(n));
      return;
    default:
      refuse_lowering(kUnreadable);
  }
}

std::uint16_t Lowerer::gap_cond(unsigned cfg, ImageId e) {
  const std::uint64_t k = phrase_gap(e) ? key(Key::phrase, cfg, e)
                                        : key(Key::cond, 0, e);
  const auto [at_, fresh] = _pool.gapCond.try_emplace(k, _c.ngapConds);
  const std::uint16_t out = at_->second;
  if (!fresh) return out;
  if (_c.ngapConds >= kGapConds) {
    _pool.gapCond.erase(at_);
    lower_refuse(_c, LowerFail::gapTooMany);
    return 0;
  }
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  gap_ops(cfg, e);
  ++_c.ngapConds;
  _c.gapCondAt[_c.ngapConds] = _c.ngapOps;
  find_unbounded_runs(_c, out, _pool.owner, _c.unbounded);
  return out;
}

bool Lowerer::implied_token_ok(unsigned cfg, ImageId e) {
  const ImageNode n = at(e);
  switch (n.kind) {
    case ImageKind::lit:
      return (n.flags & (kLitGuardWord | kLitGuardExpr)) == 0;
    case ImageKind::doc:
      return implied_token_ok(cfg, kid(n, 0));
    case ImageKind::ref:
      return terminal_rule(n.a) && implied_token_ok(cfg, body_in(cfg, n.a));
    default:
      return false;
  }
}

std::uint16_t Lowerer::implied_term(ImageId e, std::uint16_t k) {
  const std::uint16_t before = _nterms;
  const std::uint16_t t =
      intern_spelled(key(Key::implied, 0, e), "implied", false, false, [&] {
        return _lex.push(LexNode{LexKind::implied, 0, 0, 0, 0});
      });
  if (t == before) {
    unspell(t, kLaidImplied);
    _c.terms[t].cond = k;
  }
  return t;
}

}  // namespace cursiv
