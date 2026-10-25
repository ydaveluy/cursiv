#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// What an AST's types answer, with the grammar erased: a type tag is unique
// per type in the whole process, but the type table is per compiled grammar,
// so a service that cannot be templated holds this instead. One
// implementation, `DefaultAstReflection`, per compiled grammar's metadata.

namespace cursiv {

// What a reference site accepts, as one bit per type index of its language --
// subtypes already folded in, so a candidate is one test and no walk.
// Resolved once per site and carried down the scope path, which is why it
// has a name: it travels through the provider, the index and back.
//
// A view, not a container: the bits are the grammar's own table, computed at
// compile time and alive for the process.
using AcceptSet = std::span<const std::uint64_t>;

// A type this language does not know.
inline constexpr std::uint16_t kNoType = 0xFFFFu;

struct PropertyMetaData {
  std::string_view name;
  // The class a reference member accepts. Null for a member that is not one.
  const AstTypeTag* referenceType = nullptr;
};

// What a class declares, without an instance in hand. The identity is a tag
// rather than a string, and `superTypes` is the closure the tag already
// carries -- every base, transitively, not the direct ones alone.
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

  // Whether every member that can be absent was written. A scalar has no
  // absent state, so this does not pretend to check one; a reference is
  // judged on its text and never on its target -- an unresolved name is a
  // linking verdict about a member that was written.
  [[nodiscard]] virtual bool isComplete(const AstNode& node) const = 0;

  // An unknown tag answers its name and nothing else, never a throw: asking
  // about a class this grammar does not name is a question, not a fault, and
  // the class knows what it is called.
  [[nodiscard]] virtual TypeMetaData getTypeMetaData(
      const AstTypeTag* type) const noexcept = 0;

  // Every AST class this grammar names, in the order it indexes them. A tag
  // and not a string, which is what a type is here.
  [[nodiscard]] virtual std::span<const AstTypeTag* const> getAllTypes()
      const noexcept = 0;

  // Every type that is this one, itself included.
  [[nodiscard]] virtual std::span<const AstTypeTag* const> getAllSubTypes(
      const AstTypeTag* type) const noexcept = 0;

  [[nodiscard]] virtual bool isInstance(
      const AstNode& node, const AstTypeTag* type) const noexcept = 0;

  // Every node `node` directly contains, with the member that holds it and,
  // for a list member, where in it. The pair could be two fields on every
  // node; the grammar already knows it, so the question is asked of the
  // grammar and `AstNode` keeps its size. `kNoIndex` for a member that holds
  // one.
  static constexpr std::uint32_t kNoIndex = ~std::uint32_t{0};
  virtual void visitMembers(
      const AstNode& node,
      cursiv::function_ref<void(std::string_view property, std::uint32_t index,
                                AstNode& child)>
          yield) const = 0;

  // The type index this grammar knows a tag by, and back. Asked once per
  // description, never per candidate -- a tag is process-wide, an index is the
  // language's.
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

  // The reference a member is, by the class that declares it and the
  // member's name. What a completion has at a caret: the grammar names the
  // member (`Grammar::recordFeature`), the tree names the container.
  // `kNoReference` when that class has no such member, or when it is not a
  // reference.
  [[nodiscard]] virtual std::uint16_t referenceIndexOf(
      const AstTypeTag* container, std::string_view feature) const noexcept = 0;

  // Whether the member holds several targets -- a `multi_reference` against
  // a `reference`. False for `kNoReference`.
  [[nodiscard]] virtual bool isMultiReference(
      std::uint16_t reference) const noexcept = 0;

  // Whether any member of this grammar does: the early-out that spares a
  // find-references the walk reconstructing a group no grammar declares.
  [[nodiscard]] virtual bool hasMultiReference() const noexcept = 0;

  // The types a reference declares, by index -- a set, because a member may
  // name several. Subtypes are not listed: `referenceAccepts` below resolves
  // those.
  [[nodiscard]] virtual std::span<const std::uint16_t> referenceTargets(
      std::uint16_t reference) const noexcept = 0;

  // The type one site of that member narrows to, by the index the site
  // stamps -- what a failed lookup names as expected. Null for
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

  // What a reference accepts, one bit per type index, computed with the
  // grammar. Read once per query -- what the site accepts does not change
  // across a walk; then `accepts` below is a single bit test whatever the
  // grammar's size. Empty for `kNoReference`, which accepts everything.
  [[nodiscard]] virtual AcceptSet referenceAccepts(
      std::uint16_t reference) const noexcept = 0;

  // A description carries a tag, a reference site a bitset over this
  // grammar's indices: the conversion is the asking language's, and a tag it
  // does not know answers `kNoType`, which no set admits.
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
