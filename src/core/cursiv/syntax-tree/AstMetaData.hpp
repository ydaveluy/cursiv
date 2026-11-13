#pragma once

#include <cassert>
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
// builds a node: that is `AstActions`.

namespace cursiv::detail {

// 0xFFFF reads "no entry" in every column of the registries, `field` included
// (`kNoField` is the same value, spelled for the element table).
inline constexpr std::uint16_t kNoIndex = 0xFFFF;

// What the builder reads of one field. `targets` lists every type the member
// accepts, in declaration order; its index is the variant alternative to
// emplace, and `target` is the first. The writers' casts rely on these tests.
struct FieldDesc {
  std::string_view name;
  FieldContent content = FieldContent::value;
  FieldStorage storage = FieldStorage::direct;
  std::uint16_t target = kNoIndex;
  std::uint16_t owner = kNoIndex;
  const std::uint16_t* targets = nullptr;
  std::uint16_t target_count = 0;
};

// Every subtype of one type, itself first.
struct TagSet {
  const AstTypeTag* const* tags = nullptr;
  std::uint16_t count = 0;
};

// The members one class declares, as a run of the flattened property array.
struct PropertyRow {
  const PropertyMetaData* at = nullptr;
  std::size_t count = 0;
};

using NewNodeFn = AstNodePtr (*)();
using YieldFn = void (*)(
    const AstNode&,
    cursiv::function_ref<void(std::string_view, std::uint32_t, AstNode&)>);

struct AstMetaData {
  const AstTypeTag* const* types = nullptr;
  const TagSet* subtypes = nullptr;
  // One row of `typeWords` per type: the types that derive from it.
  const std::uint64_t* derivesBits = nullptr;
  std::uint16_t typeCount = 0;
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
  // One dispatch per known type, with inherited members selected at generation.
  const YieldFn* memberVisitors = nullptr;
  // One per known type, null for a class no rule builds.
  const NewNodeFn* newNodes = nullptr;
  YieldFn unknownMembers = nullptr;
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

  // Both indexes come from this grammar's tables, so the range is asserted,
  // not tested.
  constexpr bool derives(std::uint16_t base, std::uint16_t sub) const {
    assert(base < typeCount && sub < typeCount &&
           "a pair of classes this grammar names");
    return ((derivesBits[std::size_t{base} * typeWords + sub / 64] >>
             (sub % 64)) &
            1u) != 0;
  }

  constexpr std::uint16_t field_alt_of(const FieldDesc& f,
                                       std::uint16_t sub) const {
    for (std::uint16_t i = 0; i < f.target_count; ++i)
      if (derives(f.targets[i], sub)) return i;
    return kNoIndex;
  }
};
}  // namespace cursiv::detail
