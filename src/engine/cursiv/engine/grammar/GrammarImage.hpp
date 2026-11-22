#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cursiv/grammar/Association.hpp"
#include "cursiv/utils/Descent.hpp"

// A grammar as flat data: its types become constant descriptors
// (`ImageOf.hpp`) interned into vectors that ordinary code reads. Two
// spellings of one type are one node.

namespace cursiv::engine::gen {

using ImageId = std::uint32_t;
inline constexpr ImageId kNoImage = ~ImageId{0};

enum class ImageKind : std::uint8_t {
  epsilon,
  lit,
  cpset,
  byte,
  eof,
  indent,
  dedent,
  gapHas,
  gapIs,
  implied,
  through,
  mark,
  same,
  seq,
  choice,
  unordered,
  repeat,
  notOf,
  andOf,
  trigger,
  ref,
  embed,
  create,
  nest,
  assign,
  append,
  flag,
  skip,
  doc,
  commit,
  scan,
};

inline constexpr std::uint8_t kLitFolded = 1;
inline constexpr std::uint8_t kLitGuardWord = 2;
inline constexpr std::uint8_t kLitGuardExpr = 4;
inline constexpr std::uint8_t kLitKeyword = 8;
inline constexpr std::uint8_t kCpNot = 1;
inline constexpr std::uint8_t kGapLine = 1;

enum class ImageRuleKind : std::uint8_t {
  terminal,
  data,
  node,
  infix
};

struct ImageRuleDesc;
struct ImageStageDesc;
struct ImageGrammarDesc;

using ImageRules = std::span<const ImageRuleDesc* const>;
struct ImageTrivia {
  ImageRules ignored, hidden;
};

struct ImageClassDesc {
  std::uint64_t props = 0;
  bool complement = false;
  std::string_view scalars{};
  bool operator==(const ImageClassDesc&) const = default;
};

struct ImageDesc {
  ImageKind kind{};
  std::uint8_t flags = 0;
  std::uint32_t count = 0;
  const ImageDesc* const* children = nullptr;
  // A call's rule, an embedded grammar, or a skip's trivia.
  const ImageRuleDesc* rule = nullptr;
  const ImageTrivia* trivia = nullptr;
  std::uint64_t m = 0;
  std::uint64_t n = 0;
  std::string_view text{};
  std::string_view text2{};
  // The directive's action, as the engine describes it; opaque here.
  const void* row = nullptr;
};

// A rule from the stage, its body and its actions.
struct ImageOpDesc {
  const ImageDesc* op = nullptr;
  const void* row = nullptr;
};

struct ImageStageLevelDesc {
  Association group = Association::Left;
  const ImageOpDesc* ops = nullptr;
  std::uint32_t count = 0;
};

struct ImageStageDesc {
  ImageRuleKind kind = ImageRuleKind::terminal;
  // The type a rule returns, where it is not plain text; an infix rule's node.
  std::string_view returns{};
  const ImageDesc* body = nullptr;
  const void* convertRow = nullptr;
  const void* closeRow = nullptr;
  const ImageRuleDesc* primary = nullptr;
  const void* climbRow = nullptr;
  const void* leftRow = nullptr;
  const void* rightRow = nullptr;
  // An infix rule's members: left operand, operator, right operand.
  std::string_view members[3]{};
  const ImageStageLevelDesc* levels = nullptr;
  std::uint32_t levelCount = 0;
};

struct ImageRuleDesc {
  std::string_view name{};
  std::string_view qualified{};
  bool anonymous = false;
  // The grammar a rule of a section belongs to.
  const ImageRuleDesc* ownerGrammar = nullptr;
  const ImageStageDesc* stage = nullptr;
  bool caseInsensitive = false;
  // For a grammar a section embeds, what the section reads of it.
  const ImageGrammarDesc* section = nullptr;
};

struct ImageGrammarDesc {
  std::string_view name{};
  const ImageRuleDesc* entry = nullptr;
  ImageTrivia trivia{};
  ImageClassDesc bound{};
  // The word class a grammar that names none has.
  ImageClassDesc plain{};
  bool caseInsensitive = false;
  // The brackets between which indentation says nothing, and the terminal
  // that ends a line, trivia between them; none where not named.
  const ImageDesc* indentOpen = nullptr;
  const ImageDesc* indentClose = nullptr;
  const ImageRuleDesc* indentNewline = nullptr;
};

struct ImageNode {
  ImageKind kind{};
  std::uint8_t flags = 0;
  ImageId first = 0;
  std::uint32_t count = 0;
  std::uint32_t a = kNoImage;
  std::uint64_t m = 0;
  std::uint64_t n = 0;
  std::string_view text{};
  std::string_view text2{};
  union {
    const void* row = nullptr;
    const ImageTrivia* trivia;
  };

  ImageClassDesc characterClass() const { return {m, (flags & kCpNot) != 0, text}; }
};

// An image interns nodes and rules as they are read. It belongs to one reader
// at a time.
struct GrammarImage {
  std::vector<ImageNode> nodes;
  std::vector<ImageId> children;
  std::vector<const ImageRuleDesc*> rules;
  std::string_view name;
  ImageTrivia trivia;
  ImageClassDesc bound;
  ImageClassDesc plain;
  bool caseInsensitive = false;
  ImageId indentOpen = kNoImage;
  ImageId indentClose = kNoImage;
  std::uint32_t indentNewline = kNoImage;

  // The rule read by a call, through any embedded grammar entries.
  std::uint32_t entry_rule(std::uint32_t id) {
    while (rules[id]->section != nullptr) id = rule(*rules[id]->section->entry);
    return id;
  }
  const ImageStageDesc& stage(std::uint32_t rule) const {
    return *rules[rule]->stage;
  }

  void open(const ImageGrammarDesc& grammar);
  // `folded` folds the case of every literal under the node that has a
  // letter: the reading of a case-insensitive grammar.
  ImageId node(const ImageDesc& desc, bool folded = false);
  // A sequence or a choice of the nodes `parts`, made anew.
  ImageId group(ImageKind kind, std::span<const ImageId> parts);
  std::uint32_t rule(const ImageRuleDesc& desc);
  // Whether the grammar a rule belongs to is case-insensitive: the embedded
  // one for a section's rule, else this one.
  bool folds(std::uint32_t rule) const;

 private:
  detail::Descent<ImageId> make_node(const ImageDesc& desc, bool folded);

  std::unordered_map<const ImageDesc*, ImageId> _nodes;
  std::unordered_map<const ImageDesc*, ImageId> _foldedNodes;
  std::unordered_map<const ImageRuleDesc*, std::uint32_t> _rules;
};

}  // namespace cursiv::engine::gen
