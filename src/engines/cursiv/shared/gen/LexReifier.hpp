// A terminal's body, resolved against the reading it stands in and reified as
// a graph of `LexNode`: the one place the token graph is made from a grammar
// image. A node a resolution makes is told by its structure (`canon`), so two
// spellings of one token are one key.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/gen/LowerPools.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"

namespace cursiv {

class Lowerer;

class LexReifier {
 public:
  LexReifier(Lowerer& lowerer, ByteSet& wordBytes);

  detail::ImageId resolve(unsigned cfg, detail::ImageId node,
                          std::span<const std::uint32_t> path = {});
  std::uint16_t reify(detail::ImageId resolved, bool tail = true);

  detail::ImageId terminal_body(std::uint32_t rule, detail::ImageId body);
  detail::ImageId canon(detail::ImageId node);
  detail::ImageId token_of(unsigned cfg, detail::ImageId lit,
                           const detail::ImageClassDesc& bound);

  void declare_word(const detail::ImageClassDesc& bound);
  void declare_section_word(const detail::ImageClassDesc& bound);

  detail::ImageId make(detail::ImageKind kind,
                       std::span<const detail::ImageId> kids = {},
                       std::uint8_t flags = 0,
                       std::uint32_t a = detail::kNoImage, std::uint64_t m = 0,
                       std::uint64_t n = 0, std::string_view text = {});
  detail::ImageId make_not(detail::ImageId k) {
    return make(detail::ImageKind::notOf, std::span(&k, 1));
  }
  detail::ImageId make_class(const detail::ImageClassDesc& cls);

  std::uint16_t push(LexNode node) { return _c.push_lex(node); }
  std::uint16_t push_kid(std::uint16_t kid) { return _c.push_lex_kid(kid); }
  std::uint16_t group(LexKind kind, std::span<const std::uint16_t> kids,
                      std::uint32_t min = 0);
  std::uint16_t intern(const ByteSet& set, std::uint16_t decode = kNoSet);

 private:
  struct Guard {
    bool word = false;
    bool words = false;
    bool single = false;
    bool expression = false;
    ByteSet bytes{};
    detail::ImageId node = detail::kNoImage;
    detail::ImageId cp = detail::kNoImage;
    std::vector<detail::ImageId> refused{};
  };

  struct EndCloser {
    detail::ImageId type = detail::kNoImage;
    bool eof = false;
  };

  struct Echo {
    bool echoes = false;
    const void* tag = nullptr;
    detail::ImageId before = detail::kNoImage;
    detail::ImageId pad = detail::kNoImage;
    detail::ImageId after = detail::kNoImage;
  };

  using Path = std::vector<std::uint32_t>;

  const detail::ImageNode& at(detail::ImageId id) const {
    return _im.nodes[id];
  }
  detail::ImageId kid(const detail::ImageNode& n, std::uint32_t i) const {
    return _im.kids[n.first + i];
  }
  detail::ImageId rebuilt(detail::ImageId id,
                          const std::vector<detail::ImageId>& kids);
  detail::ImageId lit_with(const detail::ImageNode& lit, detail::ImageId guard);

  detail::ImageId resolve_in(unsigned cfg, detail::ImageId id, Path& path);
  detail::ImageId resolve_rule(unsigned cfg, std::uint32_t rule, Path& path);
  detail::ImageId resolve_seq(unsigned cfg, detail::ImageId id, Path& path);
  bool has_captures(detail::ImageId id) const;
  bool consumes(detail::ImageId id) const;
  bool eof_lit(detail::ImageId id) const;
  detail::ImageId without_eof(detail::ImageId lit);

  bool nested_item(std::uint32_t rule, detail::ImageId open,
                   detail::ImageId close, detail::ImageId item);
  bool nested_unambiguous(detail::ImageId open, detail::ImageId close);
  bool nested_literals(detail::ImageId id,
                       std::vector<std::string_view>& out) const;
  EndCloser end_closer(detail::ImageId close);
  Echo echo_closer(detail::ImageId close) const;
  bool echo_pad(detail::ImageId id) const;

  Guard guard_of(const detail::ImageNode& lit) const;
  Guard classify(detail::ImageId guard) const;
  bool refusable(detail::ImageId id) const;
  std::uint16_t word_set();
  std::uint16_t guard_set(const Guard& g, bool tail);
  std::uint16_t lit_guard(const detail::ImageNode& lit, const Guard& g,
                          bool tail);
  std::uint16_t class_guard(std::uint16_t guard);

  std::uint16_t refusal(std::uint16_t bound,
                        std::span<const std::uint16_t> classes,
                        std::uint32_t scalar = 0);
  std::uint16_t reify_lit(detail::ImageId id, bool tail);
  std::uint16_t reify_not_of(detail::ImageId id);
  std::uint16_t reify_not_lit(detail::ImageId lit);
  std::uint16_t reify_not_class(detail::ImageId cls);
  std::uint16_t reify_class(detail::ImageId cls);
  std::uint16_t reify_eof();
  std::uint16_t reify_through(detail::ImageId id, bool tail);
  std::uint16_t reify_through_lit(detail::ImageId open, std::string_view close,
                                  bool eof);
  std::uint16_t reify_nested(detail::ImageId id);
  std::uint16_t reify_mark(detail::ImageId id, bool tail);
  std::uint16_t reify_ref(detail::ImageId id);
  void refusals(const Guard& g, std::vector<std::uint16_t>& out);
  void class_refusals(detail::ImageId cls, std::vector<std::uint16_t>& out);
  std::uint16_t refused(detail::ImageId word, std::uint16_t node);
  std::uint16_t echo_side(detail::ImageId side) {
    return side == detail::kNoImage ? kNoLexNode : reify(side);
  }
  bool mark_run(detail::ImageId id) const;

  Lowerer& _lowerer;
  detail::GrammarImage& _im;
  LowerPools& _c;
  ByteSet& _wordBytes;
  std::map<std::tuple<detail::ImageKind, std::uint8_t, std::uint32_t,
                      std::uint64_t, std::uint64_t, std::string, std::string,
                      const void*, std::vector<detail::ImageId>>,
           detail::ImageId>
      _classes;
  std::map<detail::ImageId, detail::ImageId> _canon;
  std::map<detail::ImageId, std::uint16_t> _codePoints;
};

}  // namespace cursiv
