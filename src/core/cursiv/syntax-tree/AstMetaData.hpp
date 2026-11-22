#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/syntax-tree/AstField.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// One compiled grammar's AST types as data: classes, subtyping and
// members, written by the generator for `DefaultAstReflection`. Nothing here
// builds a node.

namespace cursiv::detail {

// 0xFFFF reads "no entry" in every column of the registries.
inline constexpr std::uint16_t kNoIndex = 0xFFFF;

// One member. `targets` lists every type the member accepts, in declaration
// order, and `target` is the first.
struct FieldDesc {
  std::string_view name;
  FieldContent content = FieldContent::value;
  FieldStorage storage = FieldStorage::direct;
  std::uint16_t target = kNoIndex;
  std::uint16_t owner = kNoIndex;
  const std::uint16_t* targets = nullptr;
  std::uint16_t target_count = 0;
};

// A row of classes.
struct TagSet {
  const AstTypeTag* const* tags = nullptr;
  std::uint16_t count = 0;
};

// A class's properties in declaration order, the inherited ones first.
struct PropertyRow {
  const PropertyMetaData* at = nullptr;
  std::size_t count = 0;
};

using NewNodeFn = AstNodePtr (*)();
using YieldFn = void (*)(
    const AstNode&,
    cursiv::FunctionRef<void(std::string_view, std::uint32_t, AstNode&)>, std::size_t limit);

struct AstMetaData {
  const AstTypeTag* const* types = nullptr;
  // Per type: every class that is one, itself first.
  const TagSet* subtypes = nullptr;
  std::uint16_t typeCount = 0;
  // The words of one row of `acceptBits`: a bit per type.
  std::uint16_t typeWords = 0;

  const FieldDesc* fields = nullptr;
  // `refOps`, `acceptBits` and `referenceTags` have one row per member below
  // `referenceBound`; with `referenceRows`, they are null for a grammar with
  // no reference member.
  const RefOps* refOps = nullptr;
  // One row of `typeWords` per member: what a reference site accepts.
  const std::uint64_t* acceptBits = nullptr;
  const PropertyRow* propertyRows = nullptr;
  const AstTypeTag* const* referenceTags = nullptr;
  // Per member, what yields the nodes it holds; null for a member that holds
  // none.
  const YieldFn* yields = nullptr;
  // Per type, the members that hold nodes, the inherited ones first.
  const std::span<const std::uint16_t>* nodeRows = nullptr;
  // One per known type, null for a class no rule builds.
  const NewNodeFn* newNodes = nullptr;
  std::span<const std::uint16_t> referenceFields;
  const std::span<const std::uint16_t>* referenceRows = nullptr;
  std::uint16_t fieldCount = 0;
  // One past the last reference member, 0 when there is none: the one bound
  // a reference row is checked against.
  std::uint16_t referenceBound = 0;

  bool anyMultiReference = false;

  // The tag of type `i`, null past the last type.
  constexpr const AstTypeTag* tag_of(std::uint16_t i) const {
    return i < typeCount ? types[i] : nullptr;
  }
};
}  // namespace cursiv::detail
