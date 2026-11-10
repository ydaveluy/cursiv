#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// Type-erased questions about a language's AST types, for services.
// Implemented by `DefaultAstReflection`.

namespace cursiv {

// What a reference site accepts: one bit per type index of its language,
// subtypes included. A view of the grammar's own table, alive for the
// process.
using AcceptSet = std::span<const std::uint64_t>;

// A type this language does not know.
inline constexpr std::uint16_t kNoType = 0xFFFFu;

struct PropertyMetaData {
  std::string_view name;
  // The class a reference member accepts. Null for a member that is not one.
  const AstTypeTag* referenceType = nullptr;
};

// What a class declares. `superTypes` holds every base, transitively, not only
// the direct ones.
struct TypeMetaData {
  std::string_view name;
  std::span<const AstTypeTag* const> superTypes;
  std::span<const PropertyMetaData> properties;
};

struct AstReflection {
  constexpr virtual ~AstReflection() = default;

  // Over AST classes, in the reference's own argument order -- (subtype,
  // supertype): `subtype` is `supertype`, or derives from it. A null
  // `supertype` accepts everything.
  [[nodiscard]] virtual bool isSubtype(
      const AstTypeTag* subtype,
      const AstTypeTag* supertype) const noexcept = 0;

  // A new node of `type`, its members at their defaults, its parent null. Empty
  // for a class this grammar's rules do not build. A completion stands such a
  // node where a reference is about to be written.
  [[nodiscard]] virtual AstNodePtr createNode(const AstTypeTag* type) const = 0;

  // An unknown tag gives its name and nothing else; it never throws.
  [[nodiscard]] virtual TypeMetaData getTypeMetaData(
      const AstTypeTag* type) const noexcept = 0;

  // Every AST class this grammar names, in the order it indexes them.
  [[nodiscard]] virtual std::span<const AstTypeTag* const> getAllTypes()
      const noexcept = 0;

  // Every type that is this one, itself included.
  [[nodiscard]] virtual std::span<const AstTypeTag* const> getAllSubTypes(
      const AstTypeTag* type) const noexcept = 0;

  [[nodiscard]] virtual bool isInstance(
      const AstNode& node, const AstTypeTag* type) const noexcept = 0;

  // Every node `node` directly contains, with the member that holds it and,
  // for a list member, its index; `kNoIndex` for a member that holds one.
  static constexpr std::uint32_t kNoIndex = ~std::uint32_t{0};
  virtual void visitMembers(
      const AstNode& node,
      cursiv::function_ref<void(std::string_view property, std::uint32_t index,
                                AstNode& child)>
          yield) const = 0;

  // The type index of a tag in this grammar, and back. A tag is process-wide;
  // an index belongs to the language.
  [[nodiscard]] virtual std::uint16_t typeIndexOf(
      const AstTypeTag* type) const noexcept = 0;
  [[nodiscard]] virtual const AstTypeTag* typeTagOf(
      std::uint16_t type) const noexcept = 0;

  // The member a field index names -- the key a `RefHandle` and the builder
  // carry, where `getTypeMetaData` keys the same members by class. Empty for
  // a row this grammar does not have.
  [[nodiscard]] virtual std::string_view fieldName(
      std::uint16_t field) const noexcept = 0;

  // The reference row a `reference<Ts...>` type is indexed by, `kNoReference`
  // when this grammar declares no such member. For a caller that holds a type
  // and not a site.
  [[nodiscard]] virtual std::uint16_t referenceIndexOf(
      const AstTypeTag* referenceType) const noexcept = 0;

  // The reference index of the member `feature` of class `container`, as a
  // completion has it at a caret. `kNoReference` when it is not a reference.
  [[nodiscard]] virtual std::uint16_t referenceIndexOf(
      const AstTypeTag* container, std::string_view feature) const noexcept = 0;

  // Whether the member holds several targets -- a `multi_reference` against
  // a `reference`. False for `kNoReference`.
  [[nodiscard]] virtual bool isMultiReference(
      std::uint16_t reference) const noexcept = 0;

  // Whether any member of this grammar does. It says nothing of the groups
  // its declarations join: another grammar's references can form them.
  [[nodiscard]] virtual bool hasMultiReference() const noexcept = 0;

  // The types a reference declares, by index -- a set, because a member may
  // name several. Subtypes are not listed: `referenceAccepts` below resolves
  // those.
  [[nodiscard]] virtual std::span<const std::uint16_t> referenceTargets(
      std::uint16_t reference) const noexcept = 0;

  // The type one site of that member narrows to, by the narrowing index the
  // site records: what a failed lookup names as expected. Null for
  // `kNoNarrow` or a row this grammar does not have.
  [[nodiscard]] virtual const AstTypeTag* referenceNarrowTarget(
      std::uint16_t reference, std::uint8_t narrow) const noexcept = 0;

  // The class that declares the member, and the member's own name -- what a
  // linking diagnostic carries so a code action knows which reference broke.
  // Null and empty for a row this grammar does not have.
  [[nodiscard]] virtual const AstTypeTag* referenceOwner(
      std::uint16_t reference) const noexcept = 0;
  [[nodiscard]] virtual std::string_view referenceProperty(
      std::uint16_t reference) const noexcept = 0;

  // What a reference accepts, one bit per type index: read once per query,
  // then `accepts` is a single bit test. Empty, which accepts everything, for
  // `kNoReference` or an index past the last reference.
  [[nodiscard]] virtual AcceptSet referenceAccepts(
      std::uint16_t reference) const noexcept = 0;

  // Whether `bits` accepts `tag`, through this language's type index; a tag it
  // does not know gets `kNoType`, which no set accepts.
  [[nodiscard]] bool acceptsTag(AcceptSet bits,
                                const AstTypeTag* tag) const noexcept {
    if (bits.empty()) return true;
    return accepts(bits, typeIndexOf(tag));
  }

  [[nodiscard]] static bool accepts(AcceptSet bits,
                                    std::uint16_t typeIndex) noexcept {
    if (bits.empty()) return true;
    const std::size_t word = typeIndex / 64u;
    return word < bits.size() && ((bits[word] >> (typeIndex % 64u)) & 1u) != 0;
  }
};

}  // namespace cursiv
