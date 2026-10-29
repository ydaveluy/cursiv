#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/syntax-tree/AstField.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// What one compiled grammar's AST types are, as data: classes, subtyping,
// members and their metadata -- the tables `DefaultAstReflection` answers
// from, written by the generator. Nothing here builds a node: that is
// `AstActions`.

namespace cursiv {

namespace detail {

// 0xFFFF reads "no entry" in every column of the registries, `field` included
// (`kNoField` is the same value, spelled for the element table).
inline constexpr std::uint16_t kNoIndex = 0xFFFF;

// What the builder shows of one field. `targets` is every family the member
// accepts, in declaration order, and its index is the variant alternative to
// emplace; `target` is its head. The type tests live here and not in the
// writers, which is what keeps a writer templated on the member pointer alone
// while still making every `static_cast` it performs a proven one.
struct FieldDesc {
  std::string_view name;
  FieldNature nature = FieldNature::value;
  FieldStorage storage = FieldStorage::direct;
  std::uint16_t target = kNoIndex;
  std::uint16_t owner = kNoIndex;
  const std::uint16_t* targets = nullptr;
  std::uint16_t target_count = 0;
};

// Every type that is one type, its own first.
struct TagSet {
  const AstTypeTag* const* tags = nullptr;
  std::uint16_t count = 0;
};

// The members one class declares, as a run of the flattened property array.
struct PropertyRow {
  const PropertyMetaData* at = nullptr;
  std::size_t count = 0;
};

using WrittenFn = bool (*)(const AstNode&);
using YieldFn = void (*)(
    const AstNode&,
    cursiv::function_ref<void(std::string_view, std::uint32_t, AstNode&)>);

}  // namespace detail

struct AstMetaData {
  const AstTypeTag* const* types = nullptr;
  const detail::TagSet* subtypes = nullptr;
  // One row of `typeWords` per type: the types that derive from it.
  const std::uint64_t* derivesBits = nullptr;
  std::uint16_t typeCount = 0;
  std::uint16_t typeWords = 0;

  const detail::FieldDesc* fields = nullptr;
  // `refOps`, `acceptBits` and `referenceTags` have one row per member below
  // `referenceBound`; with `referenceRows`, they are null for a grammar with
  // no reference member.
  const RefOps* refOps = nullptr;
  // One row of `typeWords` per member: what a reference site accepts.
  const std::uint64_t* acceptBits = nullptr;
  const detail::PropertyRow* propertyRows = nullptr;
  const AstTypeTag* const* referenceTags = nullptr;
  // One dispatch per known type, with inherited members selected at generation.
  const detail::WrittenFn* complete = nullptr;
  const detail::YieldFn* memberVisitors = nullptr;
  detail::YieldFn unknownMembers = nullptr;
  std::span<const std::uint16_t> referenceFields;
  const std::span<const std::uint16_t>* referenceRows = nullptr;
  std::uint16_t fieldCount = 0;
  // One past the last reference member, 0 when there is none: the one bound
  // a reference row is checked against.
  std::uint16_t referenceBound = 0;

  bool anyMultiReference = false;

  // `subtypes` puts a type's own tag at rank 0, so no second array is needed.
  constexpr const AstTypeTag* tag_of(std::uint16_t i) const {
    return i < typeCount ? types[i] : nullptr;
  }

  // The pair is proved, not tested: `base` comes from these tables and `sub`
  // from a pending whose `value` was non-null, carrying the index `makes` was
  // called with. No tape can reach an out-of-range pair, so the range check is
  // an assert and not two branches on the hottest path.
  constexpr bool derives(std::uint16_t base, std::uint16_t sub) const {
    assert(base < typeCount && sub < typeCount &&
           "a pair of classes this grammar names");
    return ((derivesBits[std::size_t{base} * typeWords + sub / 64] >>
             (sub % 64)) &
            1u) != 0;
  }

  constexpr std::uint16_t field_alt_of(const detail::FieldDesc& f,
                                       std::uint16_t sub) const {
    for (std::uint16_t i = 0; i < f.target_count; ++i)
      if (derives(f.targets[i], sub)) return i;
    return detail::kNoIndex;
  }
};
}  // namespace cursiv
