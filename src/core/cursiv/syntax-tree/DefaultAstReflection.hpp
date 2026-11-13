#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/AstReflection.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/utils/FunctionRef.hpp"

// The reflection of one compiled grammar, read from its metadata tables.
// Only what a per-node walk calls stays inline.

namespace cursiv {

struct DefaultAstReflection : AstReflection {
  explicit DefaultAstReflection(const detail::AstMetaData& meta);

  // Read from the tag, which holds the closure of its bases.
  [[nodiscard]] bool isSubtype(
      const AstTypeTag* subtype,
      const AstTypeTag* supertype) const noexcept override {
    if (supertype == nullptr) return true;
    return subtype != nullptr && subtype->derivesFrom(supertype);
  }

  [[nodiscard]] AstNodePtr createNode(const AstTypeTag* type) const override {
    const auto index = typeIndexOf(type);
    return index != kNoType && _f.newNodes[index] != nullptr
               ? _f.newNodes[index]()
               : AstNodePtr{};
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

  // A hash probe keyed by the tag's address: a scope computation asks this
  // once per named node.
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

  // One work-list entry, described. `text` is what the site spells, if read;
  // `document` stays null.
  [[nodiscard]] ReferenceInfo referenceInfo(const detail::RefHandle& handle,
                                            std::string_view text = {}) const;

  [[nodiscard]] const detail::AstMetaData& metaData() const noexcept {
    return _f;
  }

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

  const detail::AstMetaData& _f;
  std::vector<TagSlot> _byTag;
  std::size_t _mask = 0;
};

}  // namespace cursiv
