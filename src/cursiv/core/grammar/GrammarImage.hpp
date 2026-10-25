#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

// A grammar as flat data. The types of a grammar become constant descriptors,
// one per distinct expression type and per rule (`ImageOf.hpp`), and the image
// interns them into vectors that ordinary code, compiled once, reads. A node
// is told by the address of its descriptor, so two spellings of one type are
// one node.

namespace cursiv {

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
  // Nodes a rewrite makes: a read of a terminal's body by its symbol, the
  // token that closes the section a body is read in, a run closed at its
  // depth zero, and the base or step of a fold with its turns.
  call,
  sectionClose,
  throughNested,
  closed,
  turns,
};

inline constexpr std::uint8_t kLitFolded = 1;
inline constexpr std::uint8_t kLitGuardWord = 2;
inline constexpr std::uint8_t kLitGuardExpr = 4;
inline constexpr std::uint8_t kCpNot = 1;
inline constexpr std::uint8_t kGapLine = 1;

enum class ImageRuleKind : std::uint8_t {
  terminal,
  data,
  node,
  infix,
  grammar
};
enum class ImageGroup : std::uint8_t { left, right, non };

struct ImageRuleDesc;
struct ImageStageDesc;

struct ImageClassDesc {
  std::uint64_t props = 0;
  bool complement = false;
  std::string_view scalars{};
};

struct ImageDesc {
  ImageKind kind{};
  std::uint8_t flags = 0;
  std::uint32_t count = 0;
  const ImageDesc* const* kids = nullptr;
  // A call's rule, an embed's grammar; a skip's lists, ignored then hidden.
  const ImageRuleDesc* rule = nullptr;
  const ImageRuleDesc* const* rules = nullptr;
  std::uint64_t m = 0;
  std::uint64_t n = 0;
  std::string_view text{};
  std::string_view text2{};
  // The action a directive publishes, as the engine that lowers it describes
  // one: opaque here.
  const void* row = nullptr;
  // The identity of a mark or its echo.
  const void* tag = nullptr;
};

struct ImageLevelDesc {
  ImageGroup group = ImageGroup::left;
  const ImageDesc* op = nullptr;
};

struct ImageFaceDesc {
  ImageRuleKind kind = ImageRuleKind::terminal;
  std::string_view returns{};
  const ImageDesc* body = nullptr;
  const ImageRuleDesc* primary = nullptr;
  std::string_view members[3]{};
  const ImageLevelDesc* levels = nullptr;
  std::uint32_t levelCount = 0;
  const ImageRuleDesc* const* ignore = nullptr;
  std::uint32_t ignoreCount = 0;
  const ImageRuleDesc* const* hidden = nullptr;
  std::uint32_t hiddenCount = 0;
  const ImageRuleDesc* entry = nullptr;
  // A grammar's word boundary, as a section reads it.
  ImageClassDesc bound{};
};

// What a lowering reads of a rule besides the faces: the rule the stage hands
// it, its body ready to lay, and the actions it owes.
struct ImageOpDesc {
  const ImageDesc* op = nullptr;
  const void* row = nullptr;
};

struct ImageStageLevelDesc {
  ImageGroup group = ImageGroup::left;
  const ImageOpDesc* ops = nullptr;
  std::uint32_t count = 0;
};

struct ImageStageDesc {
  ImageRuleKind kind = ImageRuleKind::terminal;
  const ImageDesc* body = nullptr;
  // The rule stands for a loop folded from its head.
  bool folded = false;
  const void* convertRow = nullptr;
  const void* closeRow = nullptr;
  const ImageRuleDesc* primary = nullptr;
  const void* climbRow = nullptr;
  const void* leftRow = nullptr;
  const void* rightRow = nullptr;
  const ImageStageLevelDesc* levels = nullptr;
  std::uint32_t levelCount = 0;
};

struct ImageRuleDesc {
  std::string_view name{};
  std::string_view qualified{};
  bool anonymous = false;
  bool foreign = false;
  bool inlineTerminal = false;
  bool grammar = false;
  // The grammar a rule of a section belongs to.
  const ImageRuleDesc* home = nullptr;
  const ImageStageDesc* stage = nullptr;
  // The rule as written, then as the pipeline resolved it: one face when the
  // two agree.
  const ImageFaceDesc* written = nullptr;
  const ImageFaceDesc* resolved = nullptr;
};

struct ImageGrammarDesc {
  std::string_view name{};
  const ImageRuleDesc* entry = nullptr;
  const ImageRuleDesc* const* ignore = nullptr;
  std::uint32_t ignoreCount = 0;
  const ImageRuleDesc* const* hidden = nullptr;
  std::uint32_t hiddenCount = 0;
  ImageClassDesc bound{};
  ImageClassDesc plain{};
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
  const void* row = nullptr;
  const void* tag = nullptr;
};

struct ImageLevel {
  ImageGroup group = ImageGroup::left;
  ImageId op = kNoImage;
};

struct ImageFace {
  ImageRuleKind kind = ImageRuleKind::terminal;
  std::string_view returns;
  ImageId body = kNoImage;
  std::uint32_t primary = kNoImage;
  std::string_view members[3];
  std::uint32_t levelsAt = 0;
  std::uint32_t levelCount = 0;
  std::uint32_t ignoreAt = 0;
  std::uint32_t ignoreCount = 0;
  std::uint32_t hiddenAt = 0;
  std::uint32_t hiddenCount = 0;
  std::uint32_t entry = kNoImage;
  ImageClassDesc bound{};
};

struct ImageRule {
  const ImageRuleDesc* desc = nullptr;
  std::string_view name;
  std::string_view qualified;
  bool anonymous = false;
  bool foreign = false;
  bool inlineTerminal = false;
  bool grammar = false;
  bool completed = false;
  // As written, then as the pipeline resolved it.
  ImageFace face[2];
};

struct ImageClass {
  std::uint64_t props = 0;
  bool complement = false;
  std::string_view scalars;
  bool known = false;
};

struct GrammarImage {
  std::vector<ImageNode> nodes;
  std::vector<ImageId> kids;
  std::vector<ImageRule> rules;
  std::vector<std::uint32_t> ruleLists;
  std::vector<ImageLevel> levels;

  std::string_view name;
  std::uint32_t entry = kNoImage;
  std::uint32_t ignoreAt = 0;
  std::uint32_t ignoreCount = 0;
  std::uint32_t hiddenAt = 0;
  std::uint32_t hiddenCount = 0;
  ImageClass bound;
  ImageClass plain;

  const ImageStageDesc& stage(std::uint32_t rule) const {
    return *rules[rule].desc->stage;
  }

  // A literal's guard and the operands of a gap condition or of `implied` are
  // read by their own readers, not walked.
  std::span<const ImageId> shape_kids(ImageId node) const;

  void open(const ImageGrammarDesc& grammar);
  // A node a rewrite makes, once for the same fields and kids.
  ImageId derive(const ImageNode& shape, std::span<const ImageId> kids);
  ImageId node(const ImageDesc& desc);
  std::uint32_t rule(const ImageRuleDesc& desc);
  const ImageRule& ready(std::uint32_t rule);

 private:
  std::uint32_t list(const ImageRuleDesc* const* rules, std::size_t count);
  ImageFace face(const ImageFaceDesc& desc);

  struct DerivedKey {
    ImageNode shape;
    std::vector<ImageId> kids;
    bool operator==(const DerivedKey& o) const;
  };
  struct DerivedHash {
    std::size_t operator()(const DerivedKey& k) const;
  };

  std::unordered_map<const ImageDesc*, ImageId> _nodes;
  std::unordered_map<DerivedKey, ImageId, DerivedHash> _derived;
  std::unordered_map<const ImageRuleDesc*, std::uint32_t> _rules;
};

}  // namespace cursiv
