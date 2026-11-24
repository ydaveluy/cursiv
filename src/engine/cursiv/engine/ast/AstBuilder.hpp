// The tree builder: one pass over the records a parse wrote, in the order
// they closed, each record running the code of its element.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/syntax-tree/ParseResultBuilder.hpp"
#include "cursiv/utils/Cancellation.hpp"
#include "cursiv/utils/macros.hpp"

namespace cursiv::engine {

class AstBuilder;
struct GrammarFacts;

inline constexpr std::uint16_t kNoAction = 0xFFFF;

// A value waiting for the node that takes it: the record it was read at, the
// write it waits for and the number of that write's member, none for a node
// no write names yet, and the node it carries, null for text.
struct Pending {
  NodeId record;
  std::uint16_t action;
  std::uint16_t member;
  AstNode* node;
};

// A write: its action, and the number of the member it writes.
struct DeferredWrite {
  std::uint16_t action = kNoAction;
  std::uint16_t member = detail::kNoIndex;
};
// What a record does to the tree: `run`, then `write` of the node the record
// holds where `write` names an action.
struct AstRecord {
  void (*run)(AstBuilder&, NodeId, const CstNode&, DeferredWrite write) = nullptr;
  DeferredWrite write;
};
// Writes the waiting values from the one given up into a node and drops
// them.
using AstFill = void (*)(AstBuilder&, AstNode* owner, Pending* first);
// The text a record denotes through its own conversions, given the texts of
// its parts; false where a conversion failed. Where a conversion takes no
// input, `reads` is false: the record's parts are not gathered.
struct AstText {
  bool (*convert)(AstBuilder&, NodeId, std::span<const std::string_view> parts,
                  std::string_view& out) = nullptr;
  bool reads = true;
  bool joined = false;
};
// The value a record's conversion gives: `type` stands for the type by its
// address, and `read` writes an `optional` of it.
struct AstValue {
  const char* type = nullptr;
  bool (*read)(AstBuilder&, NodeId, void* out) = nullptr;
};
// Puts a waiting value into its owner; false where the owner is not of the
// member's class.
using AstWrite = bool (*)(AstBuilder&, AstNode* owner, const Pending&);

// The kinds of word a state gives a completion, in the two bits on top: a
// terminal it shifts with, above bit 16, the action that writes what its
// token is read for; a set of terminals that begin a rule called there and
// the action of the call; an action whose node begins there; and, alone,
// that a repetition led to the state.
inline constexpr std::uint32_t kCaretRule = 0x80000000u;
inline constexpr std::uint32_t kCaretNode = 0x40000000u;
inline constexpr std::uint32_t kCaretTurn = 0xC0000000u;

// Each conversion block is stored once, including the all-empty block.
template <class T> struct AstSlots {
  static constexpr std::size_t kBlock = 16;
  const T* const* blocks = nullptr;
  explicit operator bool() const { return blocks != nullptr; }
  const T& operator[](std::size_t i) const { return blocks[i / kBlock][i % kBlock]; }
};

// A language's tree builder as its generated source states it, all constant.
struct AstTables {
  // Elements below writeCount leave a member waiting. The next ones, up to
  // recordCount, run their tree action.
  const DeferredWrite* deferred = nullptr;
  const AstRecord* records = nullptr;
  std::uint32_t writeCount = 0;
  std::uint32_t recordCount = 0;
  std::uint32_t elementCount = 0;
  // Per action, its write, null where it writes no member, and which of its
  // member's narrowings a reference it writes carries, `kNoNarrow` for none.
  const AstWrite* writes = nullptr;
  const std::uint8_t* narrowOf = nullptr;
  // Per element, the conversions of its record's text and the value it
  // gives; each null for a language that has none.
  AstSlots<AstText> texts;
  AstSlots<AstValue> values;
  const detail::AstMetaData* meta = nullptr;
  // The grammar's answers, among them the words it reads in any letter case.
  const GrammarFacts* grammar = nullptr;
  // What a completion reads. Per action, the member of `meta` it writes. Per
  // state of the parser, its list of `caretWord`, 0 for none: list `k` is
  // `caretWord[caretAt[k - 1]]` up to `caretWord[caretAt[k]]`, each word of
  // one of the kinds below. Terminal sets have the parser table's width.
  const std::uint16_t* memberOf = nullptr;
  const std::uint32_t* caretOf = nullptr;
  const std::uint32_t* caretAt = nullptr;
  const std::uint32_t* caretWord = nullptr;
  const std::uint64_t* caretSets = nullptr;
};

class AstBuilder {
 public:
  // The tree of `tree`'s records, which the result keeps.
  static ParseResult build(const AstTables& tables, Cst&& tree,
                           const CancellationToken& cancelToken = kNoCancel);

  // What the code of a record or a write reads, and the result it writes.
  const AstTables& tables;
  ParseResultBuilder out;
  const Cst& cst;
  // The parse repaired the text: a token it invented denotes nothing, and a
  // write that reads one leaves its member as it was.
  const bool repaired;
  const CancellationToken& stop;
  static constexpr NodeCount kStopRecords = 64 * 1024;
  // Whether a repair invented the text of record `id`: the token itself, or
  // one of those a data rule read.
  [[nodiscard]] bool invented(NodeId id) const;

  CURSIV_ALWAYS_INLINE void push(const Pending& item) {
    if (_top == _limit) [[unlikely]]
      grow();
    *_top++ = item;
  }
  // Where the values read inside record `id` begin: records close in order,
  // so they are on top.
  [[nodiscard]] CURSIV_ALWAYS_INLINE Pending* inside(NodeId id, const CstNode& node) const {
    Pending* first = _top;
    const NodeId begin = id - (node.subtreeSize - 1);
    while (first[-1].record + 1 > begin) --first;
    return first;
  }
  [[nodiscard]] Pending* inside(NodeId id) const { return inside(id, cst[id]); }
  [[nodiscard]] const Pending* top() const { return _top; }
  // The node a nest wraps: the one among the values from `first` up to `end`
  // that no write names yet, the other values there written into it.
  AstNode* wrapped(Pending* first, Pending* end, NodeId id);
  // Forgets the values from `first` up.
  void drop(Pending* first) { _top = first; }
  // The node of class `type` among the values from `first` up that no write
  // names yet: it takes record `id`, the one its rule closes.
  AstNode* adopt(const AstTypeTag* type, Pending* first, NodeId id);
  // The first node read inside record `id` that no write names yet and
  // `takes` accepts.
  AstNode* take(bool (*takes)(const AstTypeTag*) noexcept, NodeId id, const CstNode& node);
  [[noreturn]] void broken(NodeId id) const;

  // Where a write's value is: the record's one visible child, else the record
  // itself.
  [[nodiscard]] CURSIV_ALWAYS_INLINE NodeId payload(NodeId id) const {
    return cst[id].subtreeSize == 1 ? id : payload_below(id);
  }
  // How many values in a row wait for `item`'s member from it on: what a
  // list reserves.
  [[nodiscard]] std::size_t waiting(const Pending& item) const;
  // The texts of the parts of record `id`, in order, trivia left out: its
  // tokens, and each rule read inside it whose text is converted as one part,
  // converted. Valid until the next call; false where a conversion failed.
  [[nodiscard]] bool parts(NodeId id, std::span<const std::string_view>& out);
  // Texts as one: a view of the source where they all touch, else text the
  // tree keeps.
  [[nodiscard]] std::string_view join(std::span<const std::string_view> parts) const;
  // The text record `id` denotes: its parts joined, through its own
  // conversions. A language that converts no text on the way reads a token
  // here.
  [[nodiscard]] CURSIV_ALWAYS_INLINE bool denoted(NodeId id, std::string_view& out) {
    const CstNode& top = cst[id];
    if (tables.texts && tables.texts[element(top)].convert != nullptr) [[unlikely]]
      return converted(id, out);
    if (top.subtreeSize != 1) return joined(id, out);
    out = cst.text(top);
    // A token with no text is a repair's.
    return !(out.empty() && top.isRecovered());
  }
  // The value of type `type` that record `id` gives, or the one record under
  // it that does, written to the `optional` at `out`.
  [[nodiscard]] bool value(NodeId id, const char* type, void* out);
  // Whether each token of record `id` was read as a word in any letter case.
  [[nodiscard]] bool folds(NodeId id);
  // `text` as the tree keeps it, for a view that outlives the build.
  [[nodiscard]] std::string_view keep(std::string_view text) const {
    return out.keepText({&text, 1});
  }
  // A conversion that failed, said at the record it read.
  void fail(std::string message, NodeId at);

 private:
  AstBuilder(const AstTables& tables, ParseResult& result, const CancellationToken& cancelToken);
  ~AstBuilder();
  AstBuilder(const AstBuilder&) = delete;
  AstBuilder& operator=(const AstBuilder&) = delete;

  void run(NodeId first, NodeId root);
  void grow();
  [[nodiscard]] NodeId payload_below(NodeId id) const;

  // The element of a record that has one, else the one past the last, which
  // every table holds an empty answer for: trivia and a record with no
  // action stand past every element.
  [[nodiscard]] CURSIV_ALWAYS_INLINE std::uint32_t element(const CstNode& node) const {
    const std::uint32_t e = node.element();
    return e < tables.elementCount ? e : tables.elementCount;
  }
  // The tokens of record `id`, a view of the source where they all touch;
  // false where a repair invented one.
  [[nodiscard]] bool joined(NodeId id, std::string_view& out);
  [[nodiscard]] bool converted(NodeId id, std::string_view& out);

  // A record whose parts are being gathered: how many records under it are
  // still to read, the last first, and where its parts begin.
  struct Gathering {
    NodeId record;
    NodeId begin;
    NodeCount left;
    bool grouped;
    std::size_t base;
  };

  struct Reading;
  std::unique_ptr<Reading> _reading;
  std::vector<std::string_view> _parts;
  std::vector<Gathering> _gathering;
  std::vector<NodeId> _invented;
  // The waiting values, the last read on top: in `_small` until a tree needs
  // more.
  static constexpr std::size_t kSmallStack = 64;
  // The sentinel in slot zero has record + 1 equal to zero.
  Pending _small[kSmallStack];
  Pending* _base = _small;
  Pending* _top = _small + 1;
  Pending* _limit = _small + kSmallStack;
};

}  // namespace cursiv::engine
