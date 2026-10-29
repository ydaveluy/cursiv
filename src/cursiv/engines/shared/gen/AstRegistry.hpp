// The AST registries of one grammar as plain data: the one place the emitters
// read a grammar's types, members and rows, so that everything past it is
// compiled once.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/engines/shared/gen/AstRowGen.hpp"

namespace cursiv {

struct AstRegistry {
  AstRegistry() = default;
  AstRegistry(AstRegistry&&) = default;
  AstRegistry& operator=(AstRegistry&&) = default;

  // Every spelling here, the rows' rule aliases included, is qualified from
  // global scope when the registry is built: the emitters write it as is.
  struct Type {
    bool built = false;
    std::string source;
  };
  // One member: what the reflection table shows of it and what the emitters
  // need to write it.
  struct Field {
    std::string_view name;
    detail::FieldNature nature = detail::FieldNature::value;
    detail::FieldStorage storage = detail::FieldStorage::direct;
    std::uint16_t owner = detail::kNoIndex;
    // The family types the member accepts, in declaration order: the run of
    // `targets` that starts at `firstTarget`.
    std::uint32_t firstTarget = 0;
    std::uint32_t targetCount = 0;
    std::vector<std::string> narrows;
    // The member's offset in its class.
    std::ptrdiff_t offset = 0;
  };

  std::vector<AstRowGen> rows;
  std::vector<Type> types;
  std::vector<Field> fields;
  std::vector<std::uint16_t> targets;
  std::vector<std::uint64_t> derives;
  // What a reference site accepts: one row of `typeWords` per member up to
  // the last reference member, none for a grammar without one.
  std::vector<std::uint64_t> accepts;
  std::size_t typeWords = 0;
  std::size_t convCount = 0;
  // Emitted rule spellings borrowed by rows; deque growth preserves their views.
  std::deque<std::string> sources;
  bool anyNest = false;

  std::span<const std::uint16_t> targets_of(const Field& f) const {
    return std::span(targets).subspan(f.firstTarget, f.targetCount);
  }
  // One past the last reference member: the reference tables stop there, and
  // a grammar with no reference member keeps none.
  std::size_t reference_bound() const {
    return typeWords == 0 ? 0 : accepts.size() / typeWords;
  }
  // The head of a member's targets, none for a member that names no family.
  std::uint16_t head_of(const Field& f) const {
    return f.targetCount == 0 ? detail::kNoIndex : targets[f.firstTarget];
  }
};

}  // namespace cursiv
