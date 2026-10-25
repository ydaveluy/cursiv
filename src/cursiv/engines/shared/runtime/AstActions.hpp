// How one compiled grammar builds its AST, as data: a node factory, a writer
// per class, a conversion and a run per entry of the tape, read by
// `AstReducer` alone.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "cursiv/core/grammar/ElementId.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"

namespace cursiv {

struct AstBuildCtx;
struct AstMetaData;
struct AstNode;
struct Made;
struct Pending;

namespace detail {

// The text an element denotes, its converters applied; null when it converts
// nothing and the raw spelling stands.
using TextFn = std::optional<std::string_view> (*)(AstBuildCtx&, NodeId);
// One conversion, as the table holds it. `value` stands where the grammar
// stated the value, and the write is then a store; `fn` answers every slot.
struct ConvSlot {
  bool (*fn)(AstBuildCtx&, NodeId, void*) = nullptr;
  const void* value = nullptr;
};
using MakeFn = AstNode* (*)(AstBuildCtx&, NodeId);

}  // namespace detail

class AstReducer;
using AstEntryFn = void (*)(AstReducer&, NodeId);
// One class's writer: the pending lands on a node of that class, or the class
// declares no such member and the answer is false.
using AstApplyFn = bool (*)(AstReducer&, const Pending&, const Made&);

struct AstActions {
  const AstMetaData* meta = nullptr;

  const detail::MakeFn* makes = nullptr;

  const AstEntryFn* entries = nullptr;
  // Per entry: the text of a payload, and the text the same record gives its
  // parent as a part -- the rule a site converts stands in the second only.
  const detail::TextFn* texts = nullptr;
  const detail::TextFn* calls = nullptr;
  // Per entry, non-zero where the record is a data rule's own, empty or not.
  const std::uint8_t* callRecords = nullptr;
  std::uint32_t entryCount = 0;
  // Per type index. Per field, `typeWords` words: the families a node member
  // takes off the region.
  const AstApplyFn* applies = nullptr;
  const std::uint64_t* takeBits = nullptr;

  bool anyNest = false;
  bool anyRef = false;
  // Whether a nest turn that reached back over its receiver says so by the
  // low bit of the state on its record, or by its row: then `turns` lists, per
  // entry, the rows that do.
  bool turnsByState = false;
  const std::uint8_t* turns = nullptr;

  // A leaf that owes nothing names no entry, and denotes its own bytes.
  detail::TextFn text_at(ElementId e) const {
    return texts != nullptr && e < entryCount ? texts[e] : nullptr;
  }
  detail::TextFn call_at(ElementId e) const {
    return calls != nullptr && e < entryCount ? calls[e] : nullptr;
  }
  bool is_call(ElementId e) const {
    return callRecords != nullptr && e < entryCount && callRecords[e] != 0;
  }
  bool turn(ElementId e) const {
    return turns != nullptr && e < entryCount && turns[e] != 0;
  }
};

}  // namespace cursiv
