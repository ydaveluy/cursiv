#pragma once

#include <algorithm>
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

  void visitProperties(
      const AstNode& node,
      cursiv::FunctionRef<void(std::string_view, std::uint32_t, AstNode&)>
          yield) const override {
    const auto type = typeIndexOf(node.type);
    if (type != kNoType) {
      for (const std::uint16_t member : _f.nodeRows[type])
        _f.yields[member](node, yield, std::dynamic_extent);
      return;
    }
    // A class this grammar does not index shows the members of its nearest
    // indexed bases, in their order, each member once.
    if (node.type == nullptr) return;
    const std::span bases(node.type->bases, node.type->baseCount);
    const auto nearest = [&](const AstTypeTag* base) {
      return typeIndexOf(base) != kNoType &&
             std::ranges::none_of(bases, [&](const AstTypeTag* other) {
               return other != base && other->derivesFrom(base) &&
                      typeIndexOf(other) != kNoType;
             });
    };
    for (std::size_t at = 0; at < bases.size(); ++at) {
      if (!nearest(bases[at])) continue;
      for (const std::uint16_t member : _f.nodeRows[typeIndexOf(bases[at])]) {
        const AstTypeTag* const owner = _f.types[_f.fields[member].owner];
        if (std::ranges::none_of(bases.first(at), [&](const AstTypeTag* seen) {
              return seen->derivesFrom(owner) && nearest(seen);
            }))
          _f.yields[member](node, yield, std::dynamic_extent);
      }
    }
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

  [[nodiscard]] std::uint16_t referenceIndexOf(
      const AstTypeTag* container,
      std::string_view property) const noexcept override;

  // The index protocol of the generated tables, which no service reads
  // through the interface. `propertyName` maps a property index to its name;
  // the one-argument `referenceIndexOf` gives the row of a reference type.
  [[nodiscard]] std::string_view propertyName(
      std::uint16_t propertyIndex) const noexcept;
  [[nodiscard]] std::uint16_t referenceIndexOf(
      const AstTypeTag* referenceType) const noexcept;
  [[nodiscard]] bool isMultiReference(std::uint16_t reference) const noexcept;
  [[nodiscard]] bool hasMultiReference() const noexcept;

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
