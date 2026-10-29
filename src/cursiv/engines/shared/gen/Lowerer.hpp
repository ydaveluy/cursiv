// What both lowerings do with a grammar image before they lay a single
// production: number the symbols, declare the terminals with their token
// graphs, read the conditions as postfix programs. An engine adds the walk of
// its own bodies.
#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/core/grammar/Association.hpp"
#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/core/utils/macros.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/LexReifier.hpp"
#include "cursiv/engines/shared/gen/LowerDesc.hpp"
#include "cursiv/engines/shared/gen/LowerPool.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/gen/TwinBranch.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/Symbol.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

// An expression a rule body has no place for, such as a capture outside a
// token.
inline constexpr std::string_view kNotInRule =
    "this expression cannot be used directly in a rule: `mark<>` and `same<>` "
    "work only inside a terminal or a `through`";

class Lowerer {
 public:
  virtual ~Lowerer() = default;
  Lowerer(const Lowerer&) = delete;
  Lowerer& operator=(const Lowerer&) = delete;

 protected:
  struct Config {
    std::vector<std::uint32_t> ignore;
    std::vector<std::uint32_t> hide;
    std::uint32_t section = kNoImage;
    ImageId end = kNoImage;
    std::uint32_t home = 0;
    ImageClassDesc bound{};
  };

  enum class Key : std::uint8_t {
    token,
    through,
    implied,
    phrase,
    cond,
    hasLeaf,
    isLeaf,
    andLeaf,
    cutLeaf,
    endLeaf,
    stop,
    privateToken
  };

  struct Cut {
    bool ok = false;
    std::vector<ImageId> guards;
    ImageId token = kNoImage;
  };

  Lowerer(GrammarImage& image, const LowerDesc& desc, LowerPools& pools,
          std::uint16_t& nterms, ByteSet& wordBytes, ChainPool& writes,
          GrammarFault& fault, LowerPool& pool);

  virtual std::uint16_t intern_name(std::string_view text) = 0;
  virtual ImageId ended(ImageId id) = 0;
  virtual void declare_start() = 0;
  virtual void declare_rules() = 0;
  virtual void define_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs,
                          std::uint8_t tail) = 0;
  virtual bool define_open() { return false; }

  const ImageNode& at(ImageId id) const { return _im.nodes[id]; }
  ImageId kid(const ImageNode& n, std::uint32_t i) const {
    return _im.kids[n.first + i];
  }
  std::vector<ImageId> kids_of(const ImageNode& n) const;
  // The node over the kids `rewrite` answers for, itself where none moved.
  template <class Rewrite>
  ImageId map_kids(ImageId id, Rewrite&& rewrite) {
    const ImageNode n = at(id);
    std::vector<ImageId> next;
    bool changed = false;
    for (const ImageId k : kids_of(n)) {
      next.push_back(rewrite(k));
      changed |= next.back() != k;
    }
    return changed ? _im.derive(n, next) : id;
  }
  // `&close | eof`: where a section's text ends, or the document's.
  ImageId section_end();
  std::uint64_t key(Key kind, unsigned cfg, ImageId node);
  std::uint16_t row_id(const void* row) const;
  const ImageStageDesc& stage(std::uint32_t rule) const {
    return _im.stage(rule);
  }
  bool rule_is(std::uint32_t rule, ImageRuleKind kind) const {
    const ImageStageDesc* const st = _im.rules[rule].desc->stage;
    return st != nullptr && st->kind == kind;
  }
  bool terminal_rule(std::uint32_t rule) const {
    return rule_is(rule, ImageRuleKind::terminal);
  }
  bool data_rule(std::uint32_t rule) const {
    return rule_is(rule, ImageRuleKind::data);
  }
  bool node_rule(std::uint32_t rule) const {
    return rule_is(rule, ImageRuleKind::node);
  }
  bool infix_rule(std::uint32_t rule) const {
    return rule_is(rule, ImageRuleKind::infix);
  }
  std::uint8_t role_in(unsigned cfg, std::uint32_t rule) const;
  std::uint16_t sym_of(unsigned cfg, std::uint32_t rule);
  std::uint16_t role_sym(std::size_t at) const {
    return static_cast<std::uint16_t>(1 + _d.terms.size() + at);
  }
  // `name` interned, on `row`.
  template <class Row>
  void name_row(Row& row, std::string_view name) {
    row.nameOff = intern_name(name);
    row.nameLen = static_cast<std::uint16_t>(name.size());
  }
  // Each rule's row named, from `n` on, then copied for each other
  // configuration.
  template <class Rows>
  void name_rules(Rows& rows, std::uint16_t& n) {
    for (const std::uint32_t rule : _ntRule)
      name_row(rows[n++], _im.rules[rule].name);
    for (std::size_t k = 1; k < _cfgs.size(); ++k)
      for (std::size_t i = 0; i < _ntRule.size(); ++i) {
        const auto row = rows[1 + i];
        rows[n++] = row;
      }
  }
  // Each operator of an infix rule, with its precedence, the first level
  // binding tightest, and its grouping.
  template <class Op>
  void each_op(const ImageStageDesc& st, Op&& op) {
    for (std::uint32_t i = 0; i < st.levelCount; ++i) {
      const ImageStageLevelDesc& level = st.levels[i];
      const auto prec = static_cast<std::uint8_t>(st.levelCount - i);
      const Association group = level.group == ImageGroup::right
                                    ? Association::right
                                : level.group == ImageGroup::non
                                    ? Association::non
                                    : Association::left;
      for (std::uint32_t j = 0; j < level.count; ++j)
        op(_im.node(*level.ops[j].op), level.ops[j].row, prec, group);
    }
  }
  ImageId body_in(unsigned cfg, std::uint32_t rule);
  bool has_rows(ImageId id) const;
  bool creates(ImageId id, const void* row) const;
  bool reads_under(unsigned cfg, std::uint32_t rule) const;
  unsigned region_of(unsigned cfg, const ImageNode& skip) const;
  unsigned section_of(std::uint32_t grammar, ImageId end) const;
  ImageId token_core(ImageId id) const;
  std::uint32_t op_rule_of(ImageId op) const;
  bool buries(ImageId id) const;
  ImageId optional_item(ImageId item) const;
  void index();
  void declare();
  void define();
  // One pass of `one` over each rule read under each configuration from
  // `from` on, by its number there: true where one defined anything.
  template <class One>
  bool define_pass(unsigned from, One&& one) {
    bool grew = false;
    const auto n = static_cast<std::uint16_t>(_ntRule.size());
    for (unsigned k = from; k < _cfgs.size(); ++k) {
      auto i = static_cast<std::uint16_t>(k * n);
      for (const std::uint32_t rule : _ntRule) {
        ++i;
        if (reads_under(k, rule)) grew = one(k, rule, i) || grew;
      }
    }
    return grew;
  }

  std::uint16_t terminal_sym(unsigned cfg, ImageId e);
  std::uint16_t term_of(unsigned cfg, ImageId e);
  std::uint16_t intern_spelled(std::uint64_t id, std::string_view text,
                               bool charset, bool folded,
                               const std::function<std::uint16_t()>& lex);
  void spell_literal(std::uint16_t t, const ImageNode& lit);
  bool list_words(ImageId id, std::vector<std::string_view>& words) const;
  std::uint16_t lay_laid(ImageId e, std::uint8_t laid, std::string_view name);
  void unspell(std::uint16_t t, std::uint8_t laid);
  void check_implied_token(unsigned cfg, ImageId e);
  std::uint16_t implied_term(ImageId e, std::uint16_t k);

  bool lexical(ImageId id);
  bool gap(ImageId id);
  bool phrase_gap(ImageId id);
  bool lex_phrase(ImageId id);
  Cut cut_prefix(ImageId p);
  bool cut_tokens(ImageId id) const;
  bool cut_one(ImageId id) const;
  std::vector<ImageId> gap_runs(const std::vector<ImageId>& kids);

  std::uint16_t gap_cond(unsigned cfg, ImageId e);
  void gap_ops(unsigned cfg, ImageId e);
  void gap_op(std::uint16_t v) { _c.gapOps[_c.ngapOps++] = v; }
  void gap_join(unsigned cfg, std::uint16_t op,
                const std::vector<ImageId>& es);
  std::uint16_t gap_leaf(std::uint64_t key,
                         const std::function<GapLeaf()>& make);
  std::uint16_t skip_node(unsigned cfg);
  std::uint16_t phrase_lexical(unsigned cfg, ImageId e);
  std::uint16_t phrase(unsigned cfg, ImageId e, std::uint16_t skip,
                       std::vector<std::uint32_t>& path);
  std::uint16_t phrase_skip(std::uint16_t skip) {
    return _lex.push(_c.lex[skip]);
  }
  std::uint16_t phrase_token(unsigned cfg, ImageId e);
  std::uint16_t phrase_private_token(unsigned cfg, ImageId e, ImageId body);
  std::uint16_t cut_node(unsigned cfg, ImageId token);
  void cut_terms(unsigned cfg, ImageId token, DynSet& out);

  GrammarImage& _im;
  // A rule's body is laid without the branches that repeat earlier ones.
  TwinReader _twins;
  const LowerDesc& _d;
  LowerPools& _c;
  std::uint16_t& _nterms;
  ChainPool& _writes;
  GrammarFault& _fault;
  LowerPool& _pool;
  LexReifier _lex;

  std::vector<std::uint32_t> _termRule;
  std::vector<std::uint32_t> _ntRule;
  std::unordered_map<std::uint32_t, std::uint32_t> _termIdx;
  std::unordered_map<std::uint32_t, std::uint32_t> _ntIdx;
  std::vector<std::uint32_t> _predicate;
  std::vector<std::uint32_t> _skipped;
  std::vector<std::pair<std::uint32_t, std::uint8_t>> _roles;
  std::unordered_map<const void*, std::uint16_t> _rowIdx;
  std::vector<WriteKind> _rowKind;
  std::vector<Config> _cfgs;
  std::unordered_map<ImageId, ImageId> _ended;
  std::unordered_map<std::uint64_t, ImageId> _bodies;

 private:
  void open_configs();
  void refuse_empty_items();
  bool clone_one(unsigned cfg, std::uint32_t rule, std::uint16_t lhs);
  void declare_term(std::uint32_t rule);
  void declare_role_term(std::size_t at);
  void inherit_role_term(std::size_t at);
  void declare_config(unsigned cfg);
  void delimit(ImageId spelled, std::int8_t d);
  void delimit_spelled(ImageId spelled, std::int8_t d);

  friend class LexReifier;
  std::unordered_map<ImageId, std::uint8_t> _lexicalMemo;
  std::unordered_map<ImageId, std::uint8_t> _gapMemo;
};

// What a lowering fills, made before the walk that fills it.
template <class Lowered, class Pool>
struct LowerState {
  Lowered lowered{};
  Pool pool{};
};

// The walk both lowerings lay a body with, into the tree of laid nodes `Pool`
// holds. `Self::lay` lays the nodes its engine lays apart and hands every other
// to `lay_shared`; it also lays an embed's close and the call of a grammar's
// entry.
template <class Self, class Lowered, class Pool>
class LowerWalk : private LowerState<Lowered, Pool>, public Lowerer {
 public:
  LowerWalk(GrammarImage& image, const LowerDesc& desc)
      : Lowerer(image, desc, this->lowered.cfg, this->lowered.cfg.nterms,
                this->lowered.cfg.wordBytes, this->lowered.writes,
                this->lowered.fault, this->pool),
        _o(this->lowered),
        _p(this->pool),
        _cfg(this->lowered.cfg) {}

 protected:
  using Node = typename decltype(Pool::e)::value_type;
  using Shape = decltype(Node::kind);
  using Id = typename decltype(Pool::kid)::value_type;

  Id mark(unsigned cfg, Id at) {
    _p.e[at].cfg = static_cast<std::uint8_t>(cfg);
    return at;
  }
  Id push_sym(std::uint16_t sym, unsigned cfg) {
    return mark(cfg, _p.push(Node{Shape::sym, sym}));
  }
  Id gather(unsigned cfg, Shape kind, const std::vector<Id>& kids) {
    // Kids numbered in 16 bits are refused past them.
    if constexpr (sizeof(Id) == sizeof(std::uint16_t))
      if (_p.kid.size() + kids.size() > kNoRow)
        throw LowerRefused(LowerFail::tooManyPositions);
    const auto base = static_cast<Id>(_p.kid.size());
    _p.kid.insert(_p.kid.end(), kids.begin(), kids.end());
    return mark(cfg, _p.push(Node{kind, base, static_cast<Id>(kids.size())}));
  }
  Id wrap(unsigned cfg, Id at) { return gather(cfg, Shape::seq, {at}); }
  Id lay_kids(unsigned cfg, Shape kind, const std::vector<ImageId>& es) {
    std::vector<Id> kids;
    for (const ImageId e : es) kids.push_back(self().lay(cfg, e));
    return gather(cfg, kind, kids);
  }
  Id lay_seq(unsigned cfg, ImageId e) {
    const std::vector<ImageId> runs = gap_runs(kids_of(at(e)));
    if (runs.size() == 1) return self().lay(cfg, runs.front());
    return lay_kids(cfg, Shape::seq, runs);
  }
  Id lay_ref(unsigned cfg, std::uint32_t rule) {
    if (_im.rules[rule].grammar) {
      const Id at = lay_ref(cfg, _im.ready(rule).face[0].entry);
      self().enter(cfg, at);
      return at;
    }
    // A rule read in place lays its body here, its conversion over what that
    // reads, as the rule's own definition would.
    if (stage(rule).inPlace) {
      const Id at = self().lay(cfg, body_in(cfg, rule));
      convert_over(at, row_id(stage(rule).convertRow));
      return at;
    }
    const std::uint16_t sym = sym_of(cfg, rule);
    if (cfg != 0 && !is_term(sym)) _c.cloneNeeded[nt_index(sym)] = true;
    return push_sym(sym, cfg);
  }
  Id lay_embed(unsigned cfg, ImageId e) {
    const ImageNode n = at(e);
    const ImageNode inner = at(kid(n, 0));
    if (inner.kind != ImageKind::seq || inner.count != 3)
      refuse_lowering(kNotInRule);
    const unsigned in = section_of(n.a, token_core(kid(inner, 2)));
    const Id open = self().lay(cfg, kid(inner, 0));
    const Id entry = self().lay(in, kid(inner, 1));
    const Id close = self().lay_close(cfg, in, kid(inner, 2));
    _c.cfgEnd[in] = static_cast<std::uint16_t>(_p.e[close].sym);
    return gather(cfg, Shape::seq, {open, entry, close});
  }
  // An unordered group: each optional item is read in its present form, `x`
  // for `option(x)` and one turn at least for any other repetition, and
  // marked, one bit each, as one the text may leave out.
  Id lay_unordered(unsigned cfg, ImageId e) {
    const std::vector<ImageId> items = kids_of(at(e));
    const Id group = lay_kids(cfg, Shape::unordered, items);
    std::uint32_t optional = 0;
    for (std::size_t i = 0; i < items.size() && i < 32; ++i) {
      Id& laid = _p.kid[_p.e[group].sym + i];
      Node& item = _p.e[laid];
      if (optional_item(items[i]) == kNoImage || item.kind != Shape::rep)
        continue;
      optional |= std::uint32_t{1} << i;
      if (item.max == 1)
        laid = item.sym;
      else
        item.min = 1;
    }
    _p.e[group].unorderedOptional = optional;
    return group;
  }
  // A node both engines lay alike, through `Self`'s own layings of a
  // repetition, a write, a flag, a condition and an implied token.
  CURSIV_ALWAYS_INLINE Id lay_shared(unsigned cfg, ImageId e,
                                     const ImageNode& n) {
    switch (n.kind) {
      case ImageKind::ref:
        return lay_ref(cfg, n.a);
      case ImageKind::epsilon:
        return _p.push(Node{Shape::none});
      case ImageKind::embed:
        return lay_embed(cfg, e);
      case ImageKind::seq:
        return gap(e) ? self().lay_condition(cfg, e) : lay_seq(cfg, e);
      case ImageKind::choice:
        return gap(e) ? self().lay_condition(cfg, e)
                      : lay_kids(cfg, Shape::alt, kids_of(n));
      case ImageKind::repeat:
        return self().lay_repeat(cfg, e);
      case ImageKind::assign:
      case ImageKind::append:
        return self().lay_write(cfg, e);
      case ImageKind::flag:
        return self().lay_flag(cfg, e);
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::andOf:
        return self().lay_condition(cfg, e);
      case ImageKind::implied:
        return self().lay_implied(cfg, e);
      default:  // a token, or a combinator `terminal_sym` refuses
        return push_sym(terminal_sym(cfg, e), cfg);
    }
  }
  // What a rule's body owes its rule: a data rule's conversion, unless a fold
  // reads it, and a node rule's close, where the body opens no record -- true
  // where that close was laid.
  bool close_body(const ImageStageDesc& st, Id at) {
    if (st.kind == ImageRuleKind::data && !st.folded)
      _p.e[at].made.push_back(row_id(st.convertRow));
    if (st.kind != ImageRuleKind::node) return false;
    for (const std::uint16_t r : _p.e[at].made) {
      const WriteKind k = _rowKind[r];
      if (k == WriteKind::create || k == WriteKind::climb ||
          k == WriteKind::close)
        return false;
    }
    _p.e[at].made.push_back(row_id(st.closeRow));
    return true;
  }

  Lowered& _o;
  Pool& _p;
  decltype(Lowered::cfg)& _cfg;

 private:
  Self& self() { return static_cast<Self&>(*this); }
  // Over a choice of tokens, a conversion rides the record of each token, as a
  // directive over a choice rides each alternative: none opens over the
  // choice.
  bool one_token(Id at) const {
    const Node& n = _p.e[at];
    if (n.kind == Shape::sym) return is_term(static_cast<std::uint16_t>(n.sym));
    if (n.kind != Shape::alt || !n.made.empty() || !n.row.empty()) return false;
    for (decltype(n.count) k = 0; k < n.count; ++k)
      if (!one_token(_p.kid[n.sym + k])) return false;
    return true;
  }
  void convert_over(Id at, std::uint16_t row) {
    Node& n = _p.e[at];
    if (n.kind != Shape::alt || !one_token(at)) {
      n.made.push_back(row);
      return;
    }
    for (decltype(n.count) k = 0; k < n.count; ++k)
      convert_over(_p.kid[n.sym + k], row);
  }
};

}  // namespace cursiv
