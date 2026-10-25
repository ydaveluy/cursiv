#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/core/syntax-tree/AstMetaData.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/utils/FunctionRef.hpp"

// The reflection of one compiled grammar: what asks questions about an AST
// rather than building one, from the metadata's tables, so this is the same
// class for every grammar. Only what a per-node walk calls stays inline --
// the type probe, the member visit and the two subtype tests.

namespace cursiv {

struct DefaultAstReflection : AstReflection {
  explicit DefaultAstReflection(const AstMetaData& meta);

  // Asked of the tag, not of this grammar's table: a class's bases are a
  // fact about the class, and the closure is on the tag. A null supertype
  // accepts everything, as the interface states.
  [[nodiscard]] bool isSubtype(
      const AstTypeTag* subtype,
      const AstTypeTag* supertype) const noexcept override {
    if (supertype == nullptr) return true;
    return subtype != nullptr && subtype->derivesFrom(supertype);
  }

  void visitMembers(
      const AstNode& node,
      cursiv::function_ref<void(std::string_view, std::uint32_t, AstNode&)>
          yield) const override {
    const auto type = typeIndexOf(node.type);
    if (type != kNoType)
      _f.memberVisitors[type](node, yield);
    else
      _f.unknownMembers(node, yield);
  }

  [[nodiscard]] bool isComplete(const AstNode& node) const override;

  [[nodiscard]] TypeMetaData getTypeMetaData(
      const AstTypeTag* type) const noexcept override;

  [[nodiscard]] std::span<const AstTypeTag* const> getAllTypes()
      const noexcept override;

  [[nodiscard]] std::span<const AstTypeTag* const> getAllSubTypes(
      const AstTypeTag* type) const noexcept override;

  [[nodiscard]] bool isInstance(
      const AstNode& node, const AstTypeTag* type) const noexcept override {
    return isSubtype(node.type, type);
  }

  // One probe, not a scan: a scope computation asks this once per named
  // node, so a walk over every class of the grammar would be paid per
  // symbol and per document. The table is built once, when the language is
  // installed, and keyed by the tag's address -- one inline variable per
  // class, unique for the process.
  [[nodiscard]] std::uint16_t typeIndexOf(
      const AstTypeTag* type) const noexcept override {
    if (type == nullptr) return kNoType;
    for (std::size_t at = slot(type);; at = (at + 1) & _mask) {
      if (_byTag[at].tag == type) return _byTag[at].index;
      if (_byTag[at].tag == nullptr) return kNoType;
    }
  }

  [[nodiscard]] const AstTypeTag* typeTagOf(
      std::uint16_t type) const noexcept override;

  [[nodiscard]] std::string_view fieldName(
      std::uint16_t field) const noexcept override;

  [[nodiscard]] std::uint16_t referenceIndexOf(
      const AstTypeTag* referenceType) const noexcept override;

  [[nodiscard]] std::uint16_t referenceIndexOf(
      const AstTypeTag* container,
      std::string_view feature) const noexcept override;

  [[nodiscard]] bool isMultiReference(
      std::uint16_t reference) const noexcept override;

  [[nodiscard]] bool hasMultiReference() const noexcept override;

  [[nodiscard]] std::span<const std::uint16_t> referenceTargets(
      std::uint16_t reference) const noexcept override;

  [[nodiscard]] const AstTypeTag* referenceNarrowTarget(
      std::uint16_t reference, std::uint8_t narrow) const noexcept override;

  [[nodiscard]] const AstTypeTag* referenceOwner(
      std::uint16_t reference) const noexcept override;

  [[nodiscard]] std::string_view referenceProperty(
      std::uint16_t reference) const noexcept override;

  [[nodiscard]] AcceptSet referenceAccepts(
      std::uint16_t reference) const noexcept override;

  // One work-list entry, described. `text` is what the site spells; a caller
  // describing a site it has not read leaves it out. `document` stays null
  // -- a handle alone does not name one, and the provider climbs when it
  // needs it.
  [[nodiscard]] ReferenceInfo referenceInfo(const RefHandle& handle,
                                            std::string_view text = {}) const;

  [[nodiscard]] const AstMetaData& metaData() const noexcept { return _f; }

 private:
  struct TagSlot {
    const AstTypeTag* tag = nullptr;
    std::uint16_t index = 0;
  };

  // The address is the identity, so it is also the key; the low bits of a
  // pointer to a static are the alignment's, hence the shift.
  [[nodiscard]] std::size_t slot(const AstTypeTag* tag) const noexcept {
    return (reinterpret_cast<std::uintptr_t>(tag) >> 4) & _mask;
  }

  const AstMetaData& _f;
  std::vector<TagSlot> _byTag;
  std::size_t _mask = 0;
};

}  // namespace cursiv
