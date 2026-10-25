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

#include "cursiv/core/grammar/GrammarImage.hpp"
#include "cursiv/engines/shared/gen/LowerPools.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {

namespace detail {
struct SetIndex;
}

class Lowerer;

class LexReifier {
 public:
  LexReifier(Lowerer& lowerer, ByteSet& wordBytes, detail::SetIndex& sets);

  ImageId resolve(unsigned cfg, ImageId node,
                  std::span<const std::uint32_t> path = {});
  std::uint16_t reify(ImageId resolved, bool tail = true);

  ImageId terminal_body(std::uint32_t rule, ImageId body);
  ImageId canon(ImageId node);
  ImageId token_of(unsigned cfg, ImageId lit, const ImageClassDesc& bound);

  void declare_word(const ImageClassDesc& bound);
  void declare_section_word(const ImageClassDesc& bound);

  ImageId make(ImageKind kind, std::span<const ImageId> kids = {},
               std::uint8_t flags = 0, std::uint32_t a = kNoImage,
               std::uint64_t m = 0, std::uint64_t n = 0,
               std::string_view text = {});
  ImageId make_not(ImageId kid);
  ImageId make_class(const ImageClassDesc& cls, bool complement);

  std::uint16_t push(LexNode node);
  std::uint16_t push_kid(std::uint16_t kid);
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
    ImageId node = kNoImage;
    ImageId cp = kNoImage;
    std::vector<ImageId> refused{};
  };

  struct EndCloser {
    ImageId type = kNoImage;
    bool eof = false;
  };

  struct Echo {
    bool echoes = false;
    const void* tag = nullptr;
    ImageId before = kNoImage;
    ImageId pad = kNoImage;
    ImageId after = kNoImage;
  };

  using Path = std::vector<std::uint32_t>;

  const ImageNode& at(ImageId id) const { return _im.nodes[id]; }
  ImageId kid(const ImageNode& n, std::uint32_t i) const {
    return _im.kids[n.first + i];
  }
  std::vector<ImageId> kids_of(const ImageNode& n) const;
  ImageId rebuilt(ImageId id, const std::vector<ImageId>& kids);
  ImageId lit_with(const ImageNode& lit, ImageId guard);
  bool terminal_rule(std::uint32_t rule) const;

  ImageId resolve_in(unsigned cfg, ImageId id, Path& path);
  ImageId resolve_rule(unsigned cfg, std::uint32_t rule, Path& path);
  ImageId resolve_seq(unsigned cfg, ImageId id, Path& path);
  bool has_captures(ImageId id) const;
  bool consumes(ImageId id) const;
  bool eof_lit(ImageId id) const;
  ImageId without_eof(ImageId lit);

  bool nested_item(std::uint32_t rule, ImageId open, ImageId close,
                   ImageId item);
  bool nested_unambiguous(ImageId open, ImageId close);
  bool nested_literals(ImageId id, std::vector<std::string_view>& out) const;
  EndCloser end_closer(ImageId close);
  Echo echo_closer(ImageId close) const;
  bool echo_pad(ImageId id) const;

  Guard guard_of(const ImageNode& lit) const;
  Guard classify(ImageId guard) const;
  bool refusable(ImageId id) const;
  bool word_final(std::string_view spelling, bool folded) const;
  std::uint16_t word_set();
  std::uint16_t guard_set(const Guard& g, bool tail);
  std::uint16_t lit_guard(const ImageNode& lit, const Guard& g, bool tail);
  std::uint16_t class_guard(std::uint16_t guard);

  std::uint16_t refusal(std::uint16_t bound,
                        std::span<const std::uint16_t> classes,
                        std::uint32_t scalar = 0);
  std::uint16_t reify_lit(ImageId id, bool tail);
  std::uint16_t reify_not_of(ImageId id);
  std::uint16_t reify_not_lit(ImageId lit);
  std::uint16_t reify_not_class(ImageId cls);
  std::uint16_t reify_class(ImageId cls);
  std::uint16_t reify_eof();
  std::uint16_t reify_through(ImageId id, bool tail);
  std::uint16_t reify_through_lit(ImageId open, std::string_view close,
                                  bool eof);
  std::uint16_t reify_nested(ImageId id);
  std::uint16_t reify_mark(ImageId id, bool tail);
  std::uint16_t reify_ref(ImageId id);
  void refusals(const Guard& g, std::vector<std::uint16_t>& out);
  void class_refusals(ImageId cls, std::vector<std::uint16_t>& out);
  std::uint16_t refused(ImageId word, std::uint16_t node);
  std::uint16_t echo_side(ImageId side);
  bool mark_run(ImageId id) const;

  Lowerer& _lowerer;
  GrammarImage& _im;
  LowerPools& _c;
  ByteSet& _wordBytes;
  detail::SetIndex& _x;
  std::map<std::tuple<ImageKind, std::uint8_t, std::uint32_t, std::uint64_t,
                      std::uint64_t, std::string, std::string, const void*,
                      std::vector<ImageId>>,
           ImageId>
      _classes;
  std::map<ImageId, ImageId> _canon;
  std::map<ImageId, std::uint16_t> _codePoints;
};

}  // namespace cursiv
