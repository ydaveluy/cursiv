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

#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/engines/shared/gen/ChainPool.hpp"
#include "cursiv/engines/shared/gen/GrammarFault.hpp"
#include "cursiv/engines/shared/gen/LexReifier.hpp"
#include "cursiv/engines/shared/gen/LowerDesc.hpp"
#include "cursiv/engines/shared/gen/LowerPool.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/table/DynSet.hpp"
#include "cursiv/engines/shared/table/GapLeaf.hpp"
#include "cursiv/engines/shared/table/WriteKind.hpp"

namespace cursiv {

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
  const ImageStageDesc& stage(std::uint32_t rule) const;
  bool rule_is(std::uint32_t rule, ImageRuleKind kind) const;
  bool terminal_rule(std::uint32_t rule) const;
  bool data_rule(std::uint32_t rule) const;
  bool node_rule(std::uint32_t rule) const;
  bool infix_rule(std::uint32_t rule) const;
  bool section_cfg(unsigned cfg) const { return _cfgs[cfg].end != kNoImage; }
  std::uint8_t role_in(unsigned cfg, std::uint32_t rule) const;
  std::uint16_t sym_of(unsigned cfg, std::uint32_t rule);
  std::uint16_t role_sym(std::size_t at) const;
  std::uint16_t name_of(std::uint32_t rule, std::uint16_t& len);
  ImageId body_in(unsigned cfg, std::uint32_t rule);
  bool has_rows(ImageId id) const;
  bool creates(ImageId id, const void* row) const;
  bool reads_under(unsigned cfg, std::uint32_t rule) const;
  unsigned region_of(unsigned cfg, const ImageNode& skip) const;
  unsigned section_of(unsigned cfg, std::uint32_t grammar, ImageId end) const;
  ImageId token_core(ImageId id) const;
  std::uint32_t op_rule_of(ImageId op) const;
  void index();
  void declare();
  void define();

  std::uint16_t terminal_sym(unsigned cfg, ImageId e);
  std::uint16_t term_of(unsigned cfg, ImageId e);
  std::uint16_t intern_spelled(std::uint64_t id, std::string_view text,
                               bool charset, bool folded,
                               const std::function<std::uint16_t()>& lex);
  void spell_literal(std::uint16_t t, const ImageNode& lit);
  std::uint16_t lay_laid(ImageId e, std::uint8_t laid, std::string_view name);
  void unspell(std::uint16_t t, std::uint8_t laid);
  bool implied_token_ok(unsigned cfg, ImageId e);
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
  void gap_op(std::uint16_t v);
  void gap_join(unsigned cfg, std::uint16_t op,
                const std::vector<ImageId>& es);
  std::uint16_t gap_leaf(std::uint64_t key,
                         const std::function<GapLeaf()>& make);
  std::uint16_t skip_node(unsigned cfg);
  std::uint16_t phrase_lexical(unsigned cfg, ImageId e);
  std::uint16_t phrase(unsigned cfg, ImageId e, std::uint16_t skip,
                       std::vector<std::uint32_t>& path);
  std::uint16_t phrase_skip(std::uint16_t skip);
  std::uint16_t phrase_token(unsigned cfg, ImageId e);
  std::uint16_t phrase_private_token(unsigned cfg, ImageId e, ImageId body);
  std::uint16_t cut_node(unsigned cfg, ImageId token);
  void cut_terms(unsigned cfg, ImageId token, DynSet& out);

  GrammarImage& _im;
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

}  // namespace cursiv
