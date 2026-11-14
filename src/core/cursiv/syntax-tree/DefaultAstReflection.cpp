#include "cursiv/syntax-tree/DefaultAstReflection.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cursiv/syntax-tree/AstMetaData.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/ReferenceInfo.hpp"

namespace cursiv {

DefaultAstReflection::DefaultAstReflection(const detail::AstMetaData& meta)
    : _f(meta) {
  // A power of two at least twice the class count: half-full, so the linear
  // probe stops within a slot or two.
  std::size_t n = 8;
  while (n < std::size_t{meta.typeCount} * 2) n *= 2;
  _byTag.resize(n);
  _mask = n - 1;
  for (std::uint16_t i = 0; i < meta.typeCount; ++i) {
    const AstTypeTag* const tag = meta.types[i];
    std::size_t at = slot(tag);
    while (_byTag[at].tag != nullptr) at = (at + 1) & _mask;
    _byTag[at] = {tag, i};
  }
  _superTypes.resize(meta.typeCount);
  _properties.resize(meta.typeCount);
  std::vector<bool> filled(meta.typeCount);
  // A base's properties come before the class's own; a base reached twice
  // gives its properties once.
  const auto fill = [&](const auto& self, std::uint16_t i) -> void {
    if (filled[i]) return;
    filled[i] = true;
    std::vector<PropertyMetaData>& all = _properties[i];
    const std::span bases{meta.types[i]->bases, meta.types[i]->baseCount};
    for (const AstTypeTag* const base : bases) {
      // Direct: no other base stands between the class and this one.
      if (std::ranges::any_of(bases, [&](const AstTypeTag* other) {
            return other != base && other->derivesFrom(base);
          }))
        continue;
      _superTypes[i].push_back(base);
      const std::uint16_t known = typeIndexOf(base);
      if (known == kNoType) continue;
      self(self, known);
      for (const PropertyMetaData& inherited : _properties[known])
        if (std::ranges::none_of(all, [&](const PropertyMetaData& held) {
              return held.name.data() == inherited.name.data();
            }))
          all.push_back(inherited);
    }
    const detail::PropertyRow row = meta.propertyRows[i];
    all.insert(all.end(), row.at, row.at + row.count);
  };
  for (std::uint16_t i = 0; i < meta.typeCount; ++i) fill(fill, i);
}

TypeMetaData DefaultAstReflection::getTypeMetaData(
    const AstTypeTag* type) const noexcept {
  const std::uint16_t i = typeIndexOf(type);
  // An unknown class still gives its name.
  if (i == kNoType)
    return TypeMetaData{
        type != nullptr ? type->name : std::string_view{}, {}, {}};
  return TypeMetaData{type->name, _superTypes[i], _properties[i]};
}

std::span<const AstTypeTag* const> DefaultAstReflection::getAllTypes()
    const noexcept {
  return {_f.types, _f.typeCount};
}

std::span<const AstTypeTag* const> DefaultAstReflection::getAllSubTypes(
    const AstTypeTag* type) const noexcept {
  const std::uint16_t i = typeIndexOf(type);
  if (i == kNoType) return {};
  const detail::TagSet set = _f.subtypes[i];
  return {set.tags, set.count};
}

const AstTypeTag* DefaultAstReflection::typeTagOf(
    std::uint16_t type) const noexcept {
  return _f.tag_of(type);
}

std::string_view DefaultAstReflection::propertyName(
    std::uint16_t field) const noexcept {
  return field < _f.fieldCount ? _f.fields[field].name : std::string_view{};
}

std::uint16_t DefaultAstReflection::referenceIndexOf(
    const AstTypeTag* referenceType) const noexcept {
  if (referenceType != nullptr)
    for (const auto i : _f.referenceFields)
      if (_f.referenceTags[i] == referenceType) return i;
  return kNoReference;
}

std::uint16_t DefaultAstReflection::referenceIndexOf(
    const AstTypeTag* container, std::string_view feature) const noexcept {
  const std::uint16_t owner = typeIndexOf(container);
  if (owner == kNoType || feature.empty() || _f.referenceBound == 0)
    return kNoReference;
  // Inherited reference fields are already included, in declaration order.
  for (const auto f : _f.referenceRows[owner])
    if (_f.fields[f].name == feature) return f;
  return kNoReference;
}

bool DefaultAstReflection::isMultiReference(
    std::uint16_t reference) const noexcept {
  return reference < _f.referenceBound && _f.refOps[reference].multi;
}

bool DefaultAstReflection::hasMultiReference() const noexcept {
  return _f.anyMultiReference;
}

std::span<const std::uint16_t> DefaultAstReflection::referenceTargets(
    std::uint16_t reference) const noexcept {
  if (reference >= _f.fieldCount) return {};
  const detail::FieldDesc& f = _f.fields[reference];
  return {f.targets, f.target_count};
}

const AstTypeTag* DefaultAstReflection::referenceNarrowTarget(
    std::uint16_t reference, std::uint8_t narrow) const noexcept {
  if (reference >= _f.referenceBound) return nullptr;
  const detail::RefOps& ops = _f.refOps[reference];
  return narrow < ops.narrow_count ? ops.narrow_tags[narrow] : nullptr;
}

const AstTypeTag* DefaultAstReflection::referenceOwner(
    std::uint16_t reference) const noexcept {
  return reference < _f.fieldCount ? _f.tag_of(_f.fields[reference].owner)
                                   : nullptr;
}

std::string_view DefaultAstReflection::referenceProperty(
    std::uint16_t reference) const noexcept {
  return propertyName(reference);
}

AcceptSet DefaultAstReflection::referenceAccepts(
    std::uint16_t reference) const noexcept {
  return reference < _f.referenceBound
             ? AcceptSet(_f.acceptBits + std::size_t{reference} * _f.typeWords,
                         _f.typeWords)
             : AcceptSet{};
}

ReferenceInfo DefaultAstReflection::referenceInfo(
    const detail::RefHandle& handle, std::string_view text) const {
  if (handle.field >= _f.referenceBound || handle.owner == nullptr) return {};
  const detail::RefOps& ops = _f.refOps[handle.field];
  return {
      .container = handle.owner,
      .referenceText = text,
      .property = propertyName(handle.field),
      .index = _f.fields[handle.field].storage == detail::FieldStorage::vector
                   ? std::optional<std::uint32_t>(handle.index)
                   : std::nullopt,
      // The work list already names the row; nothing to look up.
      .reference = handle.field,
      .narrow = ops.narrow_count != 0
                    ? ops.narrow_of(*handle.owner, handle.index)
                    : kNoNarrow};
}

}  // namespace cursiv
