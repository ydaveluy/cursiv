// A terminal's body, resolved against its reading and reified as a graph of
// `LexNode`. Nodes are keyed by structure (`canon`), so two spellings of one
// token are one key.
#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "cursiv/grammar/GrammarImage.hpp"
#include "cursiv/shared/gen/LowerPools.hpp"
#include "cursiv/shared/table/ByteSet.hpp"
#include "cursiv/shared/table/LexNode.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"

namespace cursiv::engine {

class Lowerer;

class LexReifier {
 public:
  LexReifier(Lowerer& lowerer, ByteSet& wordBytes);

  ImageId resolve(unsigned cfg, ImageId node,
                  std::span<const std::uint32_t> path = {});
  std::uint16_t reify(ImageId resolved, bool tail = true);

  ImageId terminal_body(std::uint32_t rule, ImageId body);
  ImageId canon(ImageId node);
  ImageId token_of(unsigned cfg, ImageId lit, const ImageClassDesc& bound);

  void declare_word(const ImageClassDesc& bound);
  void declare_section_word(const ImageClassDesc& bound);

  ImageId make(ImageKind kind, std::span<const ImageId> children = {},
               std::uint8_t flags = 0, std::uint32_t a = kNoImage,
               std::uint64_t m = 0, std::uint64_t n = 0,
               std::string_view text = {});

  std::uint16_t push(LexNode node) { return _c.push_lex(node); }
  std::uint16_t group(LexKind kind, std::span<const std::uint16_t> children,
                      std::uint32_t min = 0);

 private:
  ImageId make_not(ImageId k) {
    return make(ImageKind::notOf, std::span(&k, 1));
  }
  ImageId make_class(const ImageClassDesc& cls);
  std::uint16_t push_child(std::uint16_t child) {
    return _c.push_lex_child(child);
  }
  std::uint16_t intern(const ByteSet& set, std::uint16_t decode = kNoSet);

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

  const ImageNode& at(ImageId id) const { return _im.nodes[id]; }
  ImageId child_at(const ImageNode& n, std::uint32_t i) const {
    return _im.children[n.first + i];
  }
  ImageId rebuilt(ImageId id, const std::vector<ImageId>& children);
  ImageId lit_with(const ImageNode& lit, ImageId guard);

  Descent<ImageId> canon_walk(ImageId id);
  Descent<ImageId> resolve_in(unsigned cfg, ImageId id);
  Descent<ImageId> resolve_rule(unsigned cfg, std::uint32_t rule);
  Descent<ImageId> resolve_seq(unsigned cfg, ImageId id);
  Descent<bool> has_captures(ImageId id) const;
  Descent<bool> consumes(ImageId id) const;
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
  std::uint16_t word_set();
  std::uint16_t guard_set(const Guard& g, bool tail);
  std::uint16_t lit_guard(const ImageNode& lit, const Guard& g, bool tail);
  std::uint16_t class_guard(std::uint16_t guard);

  std::uint16_t refusal(std::uint16_t bound,
                        std::span<const std::uint16_t> classes,
                        std::uint32_t scalar = 0);
  Descent<std::uint16_t> reify_walk(ImageId id, bool tail);
  Descent<std::uint16_t> reify_lit(ImageId id, bool tail);
  Descent<std::uint16_t> reify_not_of(ImageId id);
  Descent<std::uint16_t> reify_not_lit(ImageId lit);
  std::uint16_t reify_not_class(ImageId cls);
  std::uint16_t reify_class(ImageId cls);
  std::uint16_t reify_eof();
  Descent<std::uint16_t> reify_through(ImageId id, bool tail);
  Descent<std::uint16_t> reify_through_lit(ImageId open, std::string_view close,
                                           bool eof);
  Descent<std::uint16_t> reify_nested(ImageId id);
  Descent<std::uint16_t> reify_mark(ImageId id, bool tail);
  std::uint16_t reify_ref(ImageId id);
  Descent<void> refusals(const Guard& g, std::vector<std::uint16_t>& out);
  void class_refusals(ImageId cls, std::vector<std::uint16_t>& out);
  std::uint16_t refused(ImageId word, std::uint16_t node);
  Descent<std::uint16_t> echo_side(ImageId side);
  bool mark_run(ImageId id) const;

  Lowerer& _lowerer;
  GrammarImage& _im;
  LowerPools& _c;
  ByteSet& _wordBytes;
  std::map<std::tuple<ImageKind, std::uint8_t, std::uint32_t, std::uint64_t,
                      std::uint64_t, std::string, std::string, const void*,
                      std::vector<ImageId>>,
           ImageId>
      _classes;
  std::map<ImageId, ImageId> _canon;
  // The rules being resolved: a call back into one stays a call.
  std::unordered_set<std::uint32_t> _resolving;
  std::map<ImageId, std::uint16_t> _codePoints;
};

}  // namespace cursiv::engine
